#include "api/introdb.hpp"

#include "api/http.hpp"
#include "utils/thread.hpp"
#include "view/mpv_core.hpp"

#include <borealis.hpp>

#include <algorithm>
#include <cctype>
#include <mutex>
#include <unordered_map>

namespace introdb {

namespace {

constexpr const char* kBase = "https://api.theintrodb.org/v3/media";

/// Looked up once per episode per run. The database changes on the scale of
/// community submissions, not of an evening's viewing, and the player re-enters
/// startPlayback on every quality and track switch — without this a single
/// episode would be fetched four or five times.
std::mutex g_mutex;
std::unordered_map<std::string, std::vector<SkipInterval>> g_cache;

/// The first tt-number in `s`, or "". Written as a scan rather than a prefix
/// test because an episode carries its id as "tt1234567:1:1".
std::string findImdb(const std::string& s) {
    for (size_t i = 0; i + 2 < s.size(); i++) {
        if (s[i] != 't' || s[i + 1] != 't' || !isdigit((unsigned char)s[i + 2])) continue;
        // Not mid-word: "nott123" is not an id.
        if (i > 0 && (isalnum((unsigned char)s[i - 1]) || s[i - 1] == '_')) continue;
        size_t end = i + 2;
        while (end < s.size() && isdigit((unsigned char)s[end])) end++;
        return s.substr(i, end - i);
    }
    return "";
}

/// One of the response's four arrays -> spans of `type`.
///
/// A null start is the beginning of the file; a null end is the end of it, and
/// is carried through as -1 rather than resolved here. The metadata's own
/// duration is NOT the file's — a Stremio episode has none at all, its videos[]
/// entries carrying only a name, a still and an air date — so the only number
/// that can close these spans is mpv's, and only once the file is open. Closing
/// them here against a zero dropped every credits span, which is the shape
/// theintrodb.org records most of them in.
void collect(const nlohmann::json& body, const char* field, const char* type, std::vector<SkipInterval>& out) {
    auto it = body.find(field);
    if (it == body.end() || !it->is_array()) return;
    for (const auto& seg : *it) {
        if (!seg.is_object()) continue;
        bool openEnd = !seg.contains("end_ms") || seg["end_ms"].is_null();

        SkipInterval iv;
        iv.startTime = (!seg.contains("start_ms") || seg["start_ms"].is_null())
                           ? 0.0
                           : (double)media::jint(seg, "start_ms") / 1000.0;
        iv.endTime = openEnd ? -1.0 : (double)media::jint(seg, "end_ms") / 1000.0;
        iv.type = type;
        if (iv.endTime >= 0 && iv.endTime <= iv.startTime) continue;
        out.push_back(std::move(iv));
    }
}

}  // namespace

std::string imdbIdOf(const media::Item& item) {
    // An episode's own guid is "tt<show>:<season>:<episode>", so it answers
    // first and the grandparent is only needed for the backends that leave the
    // guid empty. The lookup is by SHOW id plus season/episode either way.
    for (const std::string* s : {&item.guid, &item.grandparentRatingKey, &item.ratingKey}) {
        std::string id = findImdb(*s);
        if (!id.empty()) return id;
    }
    return "";
}

void fetch(const media::Item& item, double durationSec, std::function<void(std::vector<SkipInterval>)> then) {
    if (!MPVCore::INTRODB) {
        then({});
        return;
    }
    std::string imdb = imdbIdOf(item);
    if (imdb.empty()) {
        then({});
        return;
    }

    bool episode = item.type == media::mediaTypeEpisode;
    // A season 0 episode is a special, and the database keys those the same way
    // the metadata does, so it is passed through rather than special-cased.
    int64_t season = episode ? item.parentIndex : 0;
    int64_t number = episode ? item.index : 0;
    if (episode && number <= 0) {
        then({});
        return;
    }

    std::string key = episode ? fmt::format("{}:{}:{}", imdb, season, number) : imdb;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto hit = g_cache.find(key);
        if (hit != g_cache.end()) {
            brls::Logger::debug("introdb: {} already looked up ({} segment(s))", key, hit->second.size());
            then(hit->second);
            return;
        }
    }

    std::string url = fmt::format("{}?imdb_id={}", kBase, imdb);
    if (episode) url += fmt::format("&season={}&episode={}", season, number);
    if (durationSec > 0) url += fmt::format("&duration_ms={}", (int64_t)(durationSec * 1000));

    ThreadPool::instance().submit([url, key, durationSec, then](HTTP&) {
        std::vector<SkipInterval> out;
        try {
            // Short budget on purpose: this decorates playback rather than
            // gating it, and the player must not wait on it.
            std::string body = HTTP::get(url, HTTP::Timeout{4000, 2000});
            nlohmann::json j = nlohmann::json::parse(body);

            // "credits" is v3's name for what the reference calls an outro, and
            // PlayerNextEpisodeRules keys its whole outro branch off that word
            // (OUTRO_SEGMENT_TYPES), so it is normalised here rather than at
            // each use.
            collect(j, "intro", "intro", out);
            collect(j, "recap", "recap", out);
            collect(j, "credits", "outro", out);
            collect(j, "preview", "preview", out);

            std::sort(out.begin(), out.end(),
                [](const SkipInterval& a, const SkipInterval& b) { return a.startTime < b.startTime; });
            brls::Logger::info("introdb: {} -> {} segment(s)", key, out.size());
        } catch (const std::exception& ex) {
            // 404 for an episode nobody has submitted yet is the ordinary case,
            // not a fault: the database is not complete and is not meant to be.
            brls::Logger::debug("introdb: no data for {} ({})", key, ex.what());
        }

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_cache[key] = out;
        }
        brls::sync([then, out]() { then(out); });
    });
}

void clearCache() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_cache.clear();
}

}  // namespace introdb
