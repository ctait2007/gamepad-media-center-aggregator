#include "api/parental_guide.hpp"

#include "api/http.hpp"
#include "api/introdb.hpp"
#include "utils/thread.hpp"
#include "view/mpv_core.hpp"

#include <borealis.hpp>
#include <borealis/core/i18n.hpp>

#include <algorithm>
#include <mutex>
#include <unordered_map>

using namespace brls::literals;

namespace parental {

namespace {

constexpr const char* kBase = "https://api.tiffara.com/titles";

std::mutex g_mutex;
std::unordered_map<std::string, std::vector<Warning>> g_cache;

/// The five the reference reads, in the order it lists them, with the i18n key
/// for each. Its own category names, which are not the display ones.
struct Category {
    const char* api;
    const char* i18n;
};
constexpr Category kCategories[] = {
    {"SEXUAL_CONTENT", "main/player/parental/nudity"},
    {"VIOLENCE", "main/player/parental/violence"},
    {"PROFANITY", "main/player/parental/profanity"},
    {"ALCOHOL_DRUGS", "main/player/parental/alcohol"},
    {"FRIGHTENING_INTENSE_SCENES", "main/player/parental/frightening"},
};

std::string lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

/// resolveSeverity: the level with the most votes, "none" excluded — and "" if
/// "none" won, or drew, or there was nothing to count.
std::string dominantSeverity(const nlohmann::json& category) {
    auto breakdowns = category.find("severityBreakdowns");
    if (breakdowns == category.end() || !breakdowns->is_array()) return "";

    std::string best;
    int64_t bestVotes = -1, noneVotes = 0;
    for (const auto& b : *breakdowns) {
        if (!b.is_object()) continue;
        std::string level = lower(media::jstr(b, "severityLevel"));
        int64_t votes = media::jint(b, "voteCount");
        if (level == "none") {
            noneVotes = votes;
            continue;
        }
        if (votes > bestVotes) {
            bestVotes = votes;
            best = level;
        }
    }
    if (best.empty() || bestVotes <= noneVotes) return "";
    return best;
}

/// severe < moderate < mild, and anything else last — the reference's own
/// severityOrder, with its `?: 3` for a level it does not recognise.
int severityRank(const std::string& level) {
    if (level == "severe") return 0;
    if (level == "moderate") return 1;
    if (level == "mild") return 2;
    return 3;
}

std::string severityLabel(const std::string& level) {
    if (level == "severe") return "main/player/parental/severe"_i18n;
    if (level == "moderate") return "main/player/parental/moderate"_i18n;
    if (level == "mild") return "main/player/parental/mild"_i18n;
    return level;
}

}  // namespace

void fetch(const media::Item& item, std::function<void(std::vector<Warning>)> then) {
    if (!MPVCore::PARENTAL_GUIDE) {
        then({});
        return;
    }
    // The guide is per TITLE, so an episode is looked up under its show's id —
    // which is the first field of its own, the same shape introdb reads.
    std::string imdb = introdb::imdbIdOf(item);
    if (imdb.empty()) {
        then({});
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto hit = g_cache.find(imdb);
        if (hit != g_cache.end()) {
            then(hit->second);
            return;
        }
    }

    std::string url = fmt::format("{}/{}/parentsGuide", kBase, imdb);
    ThreadPool::instance().submit([url, imdb, then](HTTP&) {
        std::vector<Warning> out;
        try {
            // Short budget: this decorates the first seconds of playback and
            // must never hold it up.
            std::string body = HTTP::get(url, HTTP::Timeout{4000, 2000});
            nlohmann::json j = nlohmann::json::parse(body);
            auto guide = j.find("parentsGuide");
            if (guide != j.end() && guide->is_array()) {
                std::vector<std::pair<int, Warning>> ranked;
                for (const Category& cat : kCategories) {
                    for (const auto& entry : *guide) {
                        if (!entry.is_object()) continue;
                        std::string name = media::jstr(entry, "category");
                        for (char& c : name) c = (char)toupper((unsigned char)c);
                        if (name != cat.api) continue;
                        std::string level = dominantSeverity(entry);
                        if (level.empty()) break;
                        ranked.push_back({severityRank(level),
                            Warning{brls::getStr(cat.i18n), severityLabel(level)}});
                        break;
                    }
                }
                // Worst first, ties keeping the reference's own category order.
                std::stable_sort(ranked.begin(), ranked.end(),
                    [](const auto& a, const auto& b) { return a.first < b.first; });
                for (auto& [rank, w] : ranked) {
                    (void)rank;
                    if (out.size() >= 5) break;  // its own take(5)
                    out.push_back(std::move(w));
                }
            }
            brls::Logger::info("parental guide: {} -> {} warning(s)", imdb, out.size());
        } catch (const std::exception& ex) {
            // Nobody has rated this title: the ordinary case, not a fault.
            brls::Logger::debug("parental guide: no data for {} ({})", imdb, ex.what());
        }

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_cache[imdb] = out;
        }
        brls::sync([then, out]() { then(out); });
    });
}

void clearCache() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_cache.clear();
}

}  // namespace parental
