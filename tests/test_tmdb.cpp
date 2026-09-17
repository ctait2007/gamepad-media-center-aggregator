// Standalone tests for the pure halves of the TMDB client — the parsing and
// selection rules ported from NuvioTV's TmdbMetadataService/TmdbService. The
// networked half needs a key and is not exercised here.
//
// Built by tests/run.sh. The helpers are compiled from a copy of the source
// rather than linked, because api/tmdb.cpp also pulls in borealis, AppConfig
// and the thread pool for the part that talks to TMDB.

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace tmdb {

struct LocalizedImage {
    std::string filePath;
    std::string iso6391;
    std::string iso31661;
};

// ---- verbatim from app/src/api/tmdb.cpp -----------------------------------

static const std::unordered_map<std::string, std::string>& defaultRegions() {
    static const std::unordered_map<std::string, std::string> kRegions = {{"pt", "PT"}, {"es", "ES"}};
    return kRegions;
}
static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}
static std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
    return s;
}
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
static std::string yearPart(const std::string& date) {
    std::string v = trim(date);
    if (v.size() < 4) return "";
    v = v.substr(0, 4);
    for (char c : v)
        if (!std::isdigit((unsigned char)c)) return "";
    return v;
}

std::string normalizeLanguage(const std::string& language) {
    std::string raw = trim(language);
    if (raw.empty()) return "en";
    std::replace(raw.begin(), raw.end(), '_', '-');
    size_t dash = raw.find('-');
    if (dash == std::string::npos) return lower(raw);
    return lower(raw.substr(0, dash)) + "-" + upper(raw.substr(dash + 1));
}

std::string externalIdOf(const std::string& videoId) {
    std::string id = trim(videoId);
    for (const char* prefix : {"tmdb:", "movie:", "series:"}) {
        std::string p = prefix;
        if (id.rfind(p, 0) == 0) {
            id = id.substr(p.size());
            break;
        }
    }
    id = id.substr(0, id.find(':'));
    id = id.substr(0, id.find('/'));
    id = trim(id);
    if (id.rfind("tt", 0) == 0 && id.size() > 2) {
        bool digits = true;
        for (size_t i = 2; i < id.size(); i++)
            if (!std::isdigit((unsigned char)id[i])) digits = false;
        if (digits) return id;
    }
    if (!id.empty()) {
        bool digits = true;
        for (char c : id)
            if (!std::isdigit((unsigned char)c)) digits = false;
        if (digits) return id;
    }
    return "";
}

std::string tmdbType(const std::string& mediaType) {
    std::string t = lower(trim(mediaType));
    return t == "movie" ? "movie" : "tv";
}

std::string imageUrl(const std::string& path, const std::string& size) {
    std::string clean = trim(path);
    if (clean.empty()) return "";
    return std::string("https://image.tmdb.org/t/p/") + size + clean;
}

std::string showYearRange(const std::string& firstAirDate, const std::string& lastAirDate,
    const std::string& status) {
    std::string start = yearPart(firstAirDate);
    if (start.empty()) return "";
    std::string end = yearPart(lastAirDate);
    bool ended = !status.empty() && status != "Returning Series" && status != "In Production";
    if (ended && !end.empty() && end != start) return start + "-" + end;
    if (ended) return start;
    return start + "-";
}

std::string selectBest(const std::vector<LocalizedImage>& images, const std::string& normalizedLanguage) {
    if (images.empty()) return "";
    size_t dash = normalizedLanguage.find('-');
    std::string lang = dash == std::string::npos ? normalizedLanguage : normalizedLanguage.substr(0, dash);
    std::string region = dash == std::string::npos ? "" : normalizedLanguage.substr(dash + 1);
    if (region.size() != 2) {
        auto it = defaultRegions().find(lang);
        region = it == defaultRegions().end() ? "" : it->second;
    }
    auto score = [&](const LocalizedImage& i) {
        if (i.iso6391 == lang && !region.empty() && i.iso31661 == region) return 5;
        if (i.iso6391 == lang && i.iso31661.empty()) return 4;
        if (i.iso6391 == lang) return 3;
        if (i.iso6391 == "en") return 2;
        if (i.iso6391.empty()) return 1;
        return 0;
    };
    const LocalizedImage* best = nullptr;
    int bestScore = -1;
    for (const auto& i : images) {
        int s = score(i);
        if (s > bestScore) {
            bestScore = s;
            best = &i;
        }
    }
    return best ? best->filePath : "";
}

}  // namespace tmdb

// ---------------------------------------------------------------------------

static int failures = 0;
#define CHECK(expr, msg)                                     \
    do {                                                     \
        if (!(expr)) {                                       \
            std::printf("  FAIL: %s (%s)\n", msg, #expr);    \
            failures++;                                      \
        }                                                    \
    } while (0)

int main() {
    using namespace tmdb;

    // normalizeTmdbLanguage
    CHECK(normalizeLanguage("") == "en", "empty defaults to en");
    CHECK(normalizeLanguage("  ") == "en", "blank defaults to en");
    CHECK(normalizeLanguage("EN") == "en", "language half lower-cases");
    CHECK(normalizeLanguage("pt_br") == "pt-BR", "underscore becomes a dash, region upper-cases");
    CHECK(normalizeLanguage("pt-br") == "pt-BR", "region upper-cases");
    CHECK(normalizeLanguage("ZH-hant") == "zh-HANT", "both halves normalise");

    // ensureTmdbId's parsing half
    CHECK(externalIdOf("tt0898266") == "tt0898266", "a bare imdb id passes through");
    CHECK(externalIdOf("tt0898266:1:4") == "tt0898266", "a stremio episode id loses its season/episode");
    CHECK(externalIdOf("tmdb:693134") == "693134", "a tmdb: prefix is stripped");
    CHECK(externalIdOf("series:tt0898266") == "tt0898266", "a series: prefix is stripped");
    CHECK(externalIdOf("movie:1396") == "1396", "a movie: prefix is stripped");
    CHECK(externalIdOf("1396") == "1396", "a numeric id is already a tmdb id");
    CHECK(externalIdOf("kitsu:12345").empty(), "an unknown scheme has no external id");
    CHECK(externalIdOf("").empty(), "empty has no external id");
    CHECK(externalIdOf("ttNOTANID").empty(), "tt followed by letters is not an id");

    // normalizeMediaType
    CHECK(tmdbType("movie") == "movie", "movie");
    CHECK(tmdbType("Movie") == "movie", "movie, any case");
    CHECK(tmdbType("series") == "tv", "series is tv");
    CHECK(tmdbType("show") == "tv", "show is tv");
    CHECK(tmdbType("") == "tv", "anything else is tv");

    // buildImageUrl
    CHECK(imageUrl("/abc.png", "w500") == "https://image.tmdb.org/t/p/w500/abc.png", "image url");
    CHECK(imageUrl("", "w500").empty(), "a blank path has no url");
    CHECK(imageUrl("   ", "w500").empty(), "a whitespace path has no url");

    // buildShowYearRange
    CHECK(showYearRange("2007-09-24", "2019-05-16", "Ended") == "2007-2019", "an ended show is a range");
    CHECK(showYearRange("2016-07-15", "", "Returning Series") == "2016-", "a running show is open-ended");
    CHECK(showYearRange("2016-07-15", "2016-08-01", "Returning Series") == "2016-",
        "a running show stays open-ended even with a last air date");
    CHECK(showYearRange("2021-01-01", "2021-03-01", "Ended") == "2021",
        "an ended show that ran one year is a single year");
    CHECK(showYearRange("2024-01-01", "", "In Production") == "2024-", "in production is still running");
    CHECK(showYearRange("", "2019-05-16", "Ended").empty(), "no first air date, no range");

    // selectBestLocalizedImagePath
    {
        std::vector<LocalizedImage> images = {
            {"/textless.png", "", ""},
            {"/en.png", "en", ""},
            {"/pt-pt.png", "pt", "PT"},
            {"/pt-br.png", "pt", "BR"},
        };
        CHECK(selectBest(images, "pt-BR") == "/pt-br.png", "an exact region match wins");
        CHECK(selectBest(images, "pt") == "/pt-pt.png", "a bare pt infers PT as its region");
        CHECK(selectBest(images, "en") == "/en.png", "english picks the english logo");
        CHECK(selectBest(images, "de") == "/en.png", "an absent language falls back to english");
    }
    {
        // Same language, no region on the candidate: ranks above another region.
        std::vector<LocalizedImage> images = {
            {"/fr-ca.png", "fr", "CA"},
            {"/fr.png", "fr", ""},
        };
        CHECK(selectBest(images, "fr-FR") == "/fr.png",
            "with no exact region, a region-less match beats another region");
    }
    {
        std::vector<LocalizedImage> images = {{"/textless.png", "", ""}, {"/ja.png", "ja", ""}};
        CHECK(selectBest(images, "de") == "/textless.png",
            "with no english either, a textless logo is the last resort");
    }
    CHECK(selectBest({}, "en").empty(), "no images, no path");

    if (failures == 0) std::printf("test_tmdb: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
