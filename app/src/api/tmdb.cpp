#include "api/tmdb.hpp"

#include "api/http.hpp"
#include "utils/config.hpp"
#include "utils/thread.hpp"

#include <borealis.hpp>

#include <algorithm>
#include <cctype>
#include <mutex>
#include <unordered_map>

namespace tmdb {

namespace {

constexpr const char* kApi = "https://api.themoviedb.org/3";
constexpr const char* kImg = "https://image.tmdb.org/t/p/";

/// DEFAULT_LANGUAGE_REGIONS. The reference carries two, for the languages whose
/// artwork is commonly filed under the other region.
const std::unordered_map<std::string, std::string>& defaultRegions() {
    static const std::unordered_map<std::string, std::string> kRegions = {{"pt", "PT"}, {"es", "ES"}};
    return kRegions;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}
std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
    return s;
}
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

/// The first four characters of a date, when they are all digits. yearPart.
std::string yearPart(const std::string& date) {
    std::string v = trim(date);
    if (v.size() < 4) return "";
    v = v.substr(0, 4);
    for (char c : v)
        if (!std::isdigit((unsigned char)c)) return "";
    return v;
}

std::mutex g_mutex;
std::unordered_map<std::string, Enrichment> g_cache;
/// imdb id + type -> TMDB id. Separate from the enrichment cache because the
/// same title is looked up again under a different language.
std::unordered_map<std::string, std::string> g_ids;

std::string jstr(const nlohmann::json& j, const char* key) {
    if (!j.contains(key) || j[key].is_null()) return "";
    if (j[key].is_string()) return j[key].get<std::string>();
    if (j[key].is_number_integer()) return std::to_string(j[key].get<int64_t>());
    return "";
}
int64_t jint(const nlohmann::json& j, const char* key) {
    if (!j.contains(key) || !j[key].is_number()) return 0;
    return j[key].get<int64_t>();
}
double jnum(const nlohmann::json& j, const char* key) {
    if (!j.contains(key) || !j[key].is_number()) return 0.0;
    return j[key].get<double>();
}

std::vector<Company> companies(const nlohmann::json& j, const char* field) {
    std::vector<Company> out;
    if (!j.contains(field) || !j[field].is_array()) return out;
    for (const auto& e : j[field]) {
        std::string name = trim(jstr(e, "name"));
        if (name.empty()) continue;
        out.push_back({name, imageUrl(jstr(e, "logo_path"), "w300"), jint(e, "id")});
    }
    return out;
}

/// GET with a short budget: enrichment decorates a screen that is already
/// drawn, so nothing waits on it.
nlohmann::json get(const std::string& path) {
    std::string body = HTTP::get(path, HTTP::Timeout{6000, 3000});
    return nlohmann::json::parse(body);
}

}  // namespace

std::string normalizeLanguage(const std::string& language) {
    std::string raw = trim(language);
    if (raw.empty()) return "en";
    std::replace(raw.begin(), raw.end(), '_', '-');
    size_t dash = raw.find('-');
    if (dash == std::string::npos) return lower(raw);
    // "pt-br" -> "pt-BR": the region half is upper-cased, the language half
    // lower-cased, which is the form TMDB's language= parameter expects.
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
    // A Stremio series id is "tt1234567:season:episode", and a lookup wants the
    // base external id only.
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
    return std::string(kImg) + size + clean;
}

std::string showYearRange(const std::string& firstAirDate, const std::string& lastAirDate,
    const std::string& status) {
    std::string start = yearPart(firstAirDate);
    if (start.empty()) return "";
    std::string end = yearPart(lastAirDate);
    // isEnded: anything that is not still going. The reference tests the two
    // running statuses rather than listing the many finished ones.
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

    // The reference's five sort keys, highest first. Scored rather than sorted
    // so the order is stable for equal candidates, which a stable_sort on five
    // separate comparators also gives but less legibly.
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

std::string apiKey() {
    return AppConfig::instance().getItem(AppConfig::TMDB_API_KEY, std::string(""));
}

bool enabled() {
    return AppConfig::instance().getItem(AppConfig::TMDB_ENABLED, false) && !apiKey().empty();
}

void fetch(const media::Item& item, std::function<void(Enrichment)> then) {
    if (!enabled()) {
        then({});
        return;
    }
    // An episode is enriched through its show: TMDB has no "episode" endpoint
    // that takes an IMDb id, and the reference looks an episode up the same way.
    const std::string source = !item.grandparentRatingKey.empty() ? item.grandparentRatingKey
        : !item.guid.empty()                                      ? item.guid
                                                                  : item.ratingKey;
    const std::string external = externalIdOf(source);
    if (external.empty()) {
        then({});
        return;
    }
    const std::string type = tmdbType(item.type == media::mediaTypeMovie ? "movie" : "tv");
    const std::string lang = normalizeLanguage(
        AppConfig::instance().getItem(AppConfig::TMDB_LANGUAGE, std::string("en")));
    const std::string key = external + ":" + type + ":" + lang;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto hit = g_cache.find(key);
        if (hit != g_cache.end()) {
            then(hit->second);
            return;
        }
    }

    ThreadPool::instance().submit([external, type, lang, key, then](HTTP&) {
        Enrichment e;
        try {
            const std::string api = apiKey();

            // --- ensureTmdbId ---------------------------------------------
            std::string id;
            if (external.rfind("tt", 0) != 0) {
                id = external;  // already a TMDB id
            } else {
                std::string idKey = external + ":" + type;
                {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    auto hit = g_ids.find(idKey);
                    if (hit != g_ids.end()) id = hit->second;
                }
                if (id.empty()) {
                    nlohmann::json f = get(fmt::format("{}/find/{}?api_key={}&external_source=imdb_id", kApi,
                        external, api));
                    const char* field = type == "tv" ? "tv_results" : "movie_results";
                    if (f.contains(field) && f[field].is_array() && !f[field].empty())
                        id = std::to_string(jint(f[field][0], "id"));
                    if (!id.empty()) {
                        std::lock_guard<std::mutex> lock(g_mutex);
                        g_ids[idKey] = id;
                    }
                }
            }
            if (id.empty()) throw std::runtime_error("no tmdb id");

            // --- details --------------------------------------------------
            nlohmann::json d = get(fmt::format("{}/{}/{}?api_key={}&language={}", kApi, type, id, api, lang));

            e.localizedTitle = trim(jstr(d, type == "tv" ? "name" : "title"));
            e.description    = trim(jstr(d, "overview"));
            e.poster         = imageUrl(jstr(d, "poster_path"), "w500");
            e.backdrop       = imageUrl(jstr(d, "backdrop_path"), "w1280");
            e.rating         = jnum(d, "vote_average");
            e.status         = trim(jstr(d, "status"));
            e.language       = upper(trim(jstr(d, "original_language")));

            if (d.contains("genres") && d["genres"].is_array())
                for (const auto& g : d["genres"]) {
                    std::string n = trim(jstr(g, "name"));
                    if (!n.empty()) e.genres.push_back(n);
                }
            if (d.contains("production_countries") && d["production_countries"].is_array())
                for (const auto& c : d["production_countries"]) {
                    std::string n = trim(jstr(c, "name"));
                    if (!n.empty()) e.countries.push_back(n);
                }
            e.productionCompanies = companies(d, "production_companies");
            e.networks            = companies(d, "networks");

            if (type == "tv") {
                e.releaseInfo = showYearRange(jstr(d, "first_air_date"), jstr(d, "last_air_date"), e.status);
                if (d.contains("episode_run_time") && d["episode_run_time"].is_array() &&
                    !d["episode_run_time"].empty() && d["episode_run_time"][0].is_number())
                    e.runtimeMinutes = d["episode_run_time"][0].get<int64_t>();
            } else {
                e.releaseInfo   = yearPart(jstr(d, "release_date"));
                e.runtimeMinutes = jint(d, "runtime");
            }

            // --- images: the logo, language-matched -----------------------
            // include_image_language is the reference's "<lang>,<lang-region>,
            // en,null", which is what makes an English logo the fallback and a
            // textless one the last resort.
            std::string langOnly = lang.substr(0, lang.find('-'));
            nlohmann::json im = get(fmt::format("{}/{}/{}/images?api_key={}&include_image_language={},{},en,null",
                kApi, type, id, api, langOnly, lang));
            if (im.contains("logos") && im["logos"].is_array()) {
                std::vector<LocalizedImage> logos;
                for (const auto& l : im["logos"])
                    logos.push_back({jstr(l, "file_path"), jstr(l, "iso_639_1"), jstr(l, "iso_3166_1")});
                e.logo = imageUrl(selectBest(logos, lang), "w500");
            }

            // --- credits --------------------------------------------------
            nlohmann::json cr = get(fmt::format("{}/{}/{}/credits?api_key={}&language={}", kApi, type, id, api,
                lang));
            if (cr.contains("cast") && cr["cast"].is_array())
                for (const auto& m : cr["cast"]) {
                    std::string n = trim(jstr(m, "name"));
                    if (n.empty()) continue;
                    e.cast.push_back(
                        {n, trim(jstr(m, "character")), imageUrl(jstr(m, "profile_path"), "w500"), jint(m, "id")});
                }
            if (cr.contains("crew") && cr["crew"].is_array())
                for (const auto& m : cr["crew"]) {
                    std::string n = trim(jstr(m, "name"));
                    std::string job = trim(jstr(m, "job"));
                    if (n.empty()) continue;
                    Person p{n, job, imageUrl(jstr(m, "profile_path"), "w500"), jint(m, "id")};
                    if (job == "Director")
                        e.directors.push_back(p);
                    else if (job == "Writer" || job == "Screenplay")
                        e.writers.push_back(p);
                }

            // --- the age rating -------------------------------------------
            // Two different endpoints and two different shapes, which is why
            // the reference fetches it separately from the details.
            if (type == "tv") {
                nlohmann::json r = get(fmt::format("{}/tv/{}/content_ratings?api_key={}", kApi, id, api));
                if (r.contains("results") && r["results"].is_array())
                    for (const auto& c : r["results"])
                        if (jstr(c, "iso_3166_1") == "US") e.ageRating = trim(jstr(c, "rating"));
            } else {
                nlohmann::json r = get(fmt::format("{}/movie/{}/release_dates?api_key={}", kApi, id, api));
                if (r.contains("results") && r["results"].is_array())
                    for (const auto& c : r["results"]) {
                        if (jstr(c, "iso_3166_1") != "US") continue;
                        if (!c.contains("release_dates") || !c["release_dates"].is_array()) continue;
                        for (const auto& rd : c["release_dates"]) {
                            std::string cert = trim(jstr(rd, "certification"));
                            if (!cert.empty()) e.ageRating = cert;
                        }
                    }
            }

            e.valid = true;
            brls::Logger::info("tmdb: {} -> {} ({}, {} cast)", external, id, e.localizedTitle, e.cast.size());
        } catch (const std::exception& ex) {
            // A title TMDB does not carry, a rate limit, a key the user typed
            // wrong: none of these are worth a dialog. The screen keeps the
            // addon's own metadata, which is what it was already showing.
            brls::Logger::debug("tmdb: no enrichment for {} ({})", external, ex.what());
        }

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_cache[key] = e;
        }
        brls::sync([then, e]() { then(e); });
    });
}

void apply(media::Item& item, const Enrichment& e) {
    if (!e.valid) return;
    AppConfig& c = AppConfig::instance();

    if (c.getItem(AppConfig::TMDB_USE_ARTWORK, true)) {
        if (!e.logo.empty()) item.clearLogo = e.logo;
        if (!e.backdrop.empty()) item.art = e.backdrop;
    }
    if (c.getItem(AppConfig::TMDB_USE_BASIC_INFO, true)) {
        if (!e.localizedTitle.empty()) item.title = e.localizedTitle;
        if (!e.description.empty()) item.summary = e.description;
        if (!e.genres.empty()) item.genres = e.genres;
        if (e.rating > 0) {
            item.rating = e.rating;
            item.ratingImage = "themoviedb://image.rating";
        }
    }
    if (c.getItem(AppConfig::TMDB_USE_DETAILS, true)) {
        if (e.runtimeMinutes > 0) item.duration = e.runtimeMinutes * 60000;
        if (!e.countries.empty()) item.country = e.countries.front();
        if (!e.ageRating.empty()) item.contentRating = e.ageRating;
        if (!e.status.empty()) item.status = e.status;
        if (!e.language.empty()) item.language = e.language;
    }
    if (c.getItem(AppConfig::TMDB_USE_RELEASE_DATES, false)) {
        if (!e.releaseInfo.empty()) item.releaseInfo = e.releaseInfo;
    }
    if (c.getItem(AppConfig::TMDB_USE_CREDITS, true)) {
        auto toRoles = [](const std::vector<Person>& in) {
            std::vector<media::Role> out;
            for (const auto& p : in) out.push_back({std::to_string(p.tmdbId), p.name, p.character, p.photo});
            return out;
        };
        if (!e.cast.empty()) item.roles = toRoles(e.cast);
        if (!e.directors.empty()) item.directors = toRoles(e.directors);
    }
}

void clearCache() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_cache.clear();
    g_ids.clear();
}

}  // namespace tmdb
