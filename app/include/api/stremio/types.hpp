/*
    GMCA — Stremio addon protocol: manifest/catalog/meta shapes, transport
    helpers, and mappers Stremio JSON -> neutral media:: model.

    A Stremio addon is a stateless HTTP REST service described by a manifest.
    transportUrl ends with `/manifest.json`; base = transportUrl without it.
    Resource URLs (id and extra values percent-encoded with encodeURIComponent):
      catalog   {base}/catalog/{type}/{encId}.json
                {base}/catalog/{type}/{encId}/{extra}.json   (extra = k=v&k=v…)
      meta      {base}/meta/{type}/{encId}.json
      stream    {base}/stream/{type}/{encId}.json
      subtitles {base}/subtitles/{type}/{encId}.json
    type ∈ {movie, series, channel, tv}.

    Units: durations in MILLISECONDS (Stremio "142 min" / "2h 22min" parsed),
    years from `releaseInfo`. No auth (HTTP::get without headers).

    Identity schema (Item::ratingKey is OPAQUE, Stremio-specific): Stremio needs
    the type alongside the id to route requests, so we fold it into ratingKey:
      movie    "movie:tt0111161"
      series   "series:tt0903747"
      episode  "series:tt0903747:1:1"      (stremioId is the video id)
      season   "season:tt0903747:1"        (synthetic, {showId}:{n})
    Item::guid = raw stremioId (the IMDB `tt…` id; cross-source identity).
    Item::key  = ratingKey.
    parseId() splits on the FIRST ':' -> {stremioType, stremioId}.

    LIMITATION (documented, not handled): kitsu ids (`kitsu:ID`, episodes
    `kitsu:ID:ep`) already contain a ':' in their prefix, which collides with the
    "{type}:{id}" ratingKey scheme. Only IMDB (`tt`) ids are supported for now.
*/

#pragma once

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include "api/http.hpp"
#include "api/media/types.hpp"
#include "utils/thread.hpp"
#include <borealis/core/logger.hpp>

namespace stremio {

using media::jbool;
using media::jint;
using media::jnum;
using media::jstr;

/// ---- URL helpers -----------------------------------------------------------

/// JS encodeURIComponent: encodes everything except A-Za-z0-9 and `- _ . ! ~ * ' ( )`.
/// `:` becomes %3A, so episode/kitsu ids survive the path segment unambiguously.
inline std::string encodeURIComponent(const std::string& s) {
    static const std::string unreserved = "-_.!~*'()";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (std::isalnum(c) || unreserved.find((char)c) != std::string::npos) {
            out += (char)c;
        } else {
            static const char* hex = "0123456789ABCDEF";
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

/// transportUrl -> base (strips a trailing `/manifest.json`).
/// Splits an addon url once at '?' into {path, query} (query keeps its '?',
/// or is empty). Configurable addons carry their settings either in the path
/// (torrentio style) or in a query string, and the /manifest.json suffix sits
/// on the PATH — so the two must be handled separately.
inline std::pair<std::string, std::string> splitAddonQuery(const std::string& url) {
    std::string s = url;
    const std::string blank = " \t\r\n";
    s.erase(0, s.find_first_not_of(blank) == std::string::npos ? 0 : s.find_first_not_of(blank));
    size_t end = s.find_last_not_of(blank);
    if (end != std::string::npos) s.erase(end + 1);

    size_t q = s.find('?');
    std::string path = q == std::string::npos ? s : s.substr(0, q);
    std::string query = q == std::string::npos ? std::string() : s.substr(q);
    while (!path.empty() && path.back() == '/') path.pop_back();
    return {path, query};
}

/// True if `path` ends with "/manifest.json" (case-insensitive).
inline bool endsWithManifest(const std::string& path) {
    static const std::string suffix = "/manifest.json";
    if (path.size() < suffix.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), path.rbegin(),
        [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
}

inline std::string baseFromTransport(const std::string& transportUrl) {
    static const std::string suffix = "/manifest.json";
    auto [path, query] = splitAddonQuery(transportUrl);
    // Tolerate a base already passed without /manifest.json, or a trailing slash.
    if (endsWithManifest(path)) path.erase(path.size() - suffix.size());
    return path + query;
}

/// The url to GET the manifest from, for a stored addon url in EITHER form.
/// Stremio's account API hands back a transportUrl that already ends in
/// /manifest.json; Nuvio's `addons` table stores the BASE url with that suffix
/// stripped (see NuvioTV's AddonSyncService, which strips it on write and
/// re-appends it on read). Fetching a base url verbatim gets the addon's
/// landing page instead of its manifest — a 404, or HTML that fails to parse
/// as JSON — leaving the engine with no addons at all.
inline std::string manifestFromTransport(const std::string& transportUrl) {
    auto [path, query] = splitAddonQuery(transportUrl);
    if (endsWithManifest(path)) return path + query;
    return path + "/manifest.json" + query;
}

/// Parse a Stremio `runtime` string into milliseconds. Accepts the common
/// flavors: "142 min" / "142min" / "120m", "2h 22min" / "2h22m", "2h".
/// Returns 0 when unparseable.
inline int64_t parseRuntimeMs(const std::string& runtime) {
    int64_t hours = 0, minutes = 0;
    int64_t num = 0;
    bool hasNum = false, sawHour = false;
    for (size_t i = 0; i < runtime.size(); ++i) {
        char c = runtime[i];
        if (std::isdigit((unsigned char)c)) {
            num = num * 10 + (c - '0');
            hasNum = true;
        } else if (c == 'h' || c == 'H') {
            hours = num;
            num = 0;
            hasNum = false;
            sawHour = true;
        } else if ((c == 'm' || c == 'M') && hasNum) {
            // 'm' / 'min' — consume the whole word, the digits are the minutes.
            minutes = num;
            num = 0;
            hasNum = false;
            // skip the rest of the unit word (in/inutes)
            while (i + 1 < runtime.size() && std::isalpha((unsigned char)runtime[i + 1])) ++i;
        }
    }
    // A bare trailing number with no unit, and no hour seen, is minutes ("142").
    if (hasNum && minutes == 0 && !sawHour) minutes = num;
    return (hours * 60 + minutes) * 60 * 1000;
}

/// releaseInfo -> year (first 4 consecutive digits, e.g. "1994" or "2010-2015").
inline int64_t parseYear(const std::string& releaseInfo) {
    for (size_t i = 0; i + 4 <= releaseInfo.size(); ++i) {
        if (std::isdigit((unsigned char)releaseInfo[i]) && std::isdigit((unsigned char)releaseInfo[i + 1]) &&
            std::isdigit((unsigned char)releaseInfo[i + 2]) && std::isdigit((unsigned char)releaseInfo[i + 3])) {
            try {
                return std::stoll(releaseInfo.substr(i, 4));
            } catch (...) {
            }
        }
    }
    return 0;
}

/// ---- Type mapping ----------------------------------------------------------

/// Stremio catalog/meta `type` -> neutral media:: type string. Episodes and
/// seasons are SYNTHESIZED by the backend (no native Stremio type), so they are
/// not produced here.
inline std::string mapType(const std::string& t) {
    if (t == "movie") return media::mediaTypeMovie;
    if (t == "series") return media::mediaTypeShow;
    if (t == "channel" || t == "tv") return media::mediaTypeClip;
    return t;
}

/// Neutral media:: item -> the Stremio resource type used to route requests.
/// Seasons/episodes belong to a series; clips map to a channel.
inline std::string stremioType(const media::Item& item) {
    if (item.type == media::mediaTypeMovie) return "movie";
    if (item.type == media::mediaTypeShow || item.type == media::mediaTypeSeason ||
        item.type == media::mediaTypeEpisode)
        return "series";
    if (item.type == media::mediaTypeClip) return "channel";
    return item.type;
}

/// ---- Identity (ratingKey codec) --------------------------------------------

struct ParsedId {
    std::string stremioType;  // movie | series | season | channel | tv | …
    std::string stremioId;    // raw Stremio id (tt…, tt…:S:E, {showId}:{n})
    // episode breakdown (only when the id has the "{base}:{season}:{episode}" shape)
    std::string baseId;
    int64_t season = -1;
    int64_t episode = -1;
};

/// Split ratingKey on the FIRST ':' -> {stremioType, stremioId}. For an episode
/// (type "series" with a "{base}:{s}:{e}" id) or a season ("season" with a
/// "{showId}:{n}" id), the trailing numeric components are filled in too.
inline ParsedId parseId(const std::string& ratingKey) {
    ParsedId p;
    auto colon = ratingKey.find(':');
    if (colon == std::string::npos) {
        // No type prefix: assume a bare movie id (defensive; we always emit a prefix).
        p.stremioType = "movie";
        p.stremioId = ratingKey;
        p.baseId = ratingKey;
        return p;
    }
    p.stremioType = ratingKey.substr(0, colon);
    p.stremioId = ratingKey.substr(colon + 1);
    p.baseId = p.stremioId;

    if (p.stremioType == "season") {
        // "season:{showId}:{n}" -> stremioId == "{showId}:{n}"
        auto last = p.stremioId.rfind(':');
        if (last != std::string::npos) {
            p.baseId = p.stremioId.substr(0, last);
            try {
                p.season = std::stoll(p.stremioId.substr(last + 1));
            } catch (...) {
            }
        }
    } else if (p.stremioType == "series") {
        // An episode id is "{base}:{season}:{episode}"; a bare show id has no extra ':'.
        // (IMDB base ids carry no ':', so re-splitting on ':' is unambiguous here;
        //  kitsu ids would break this — see LIMITATION at the top of the file.)
        std::vector<std::string> parts;
        size_t pos = 0, next;
        while ((next = p.stremioId.find(':', pos)) != std::string::npos) {
            parts.push_back(p.stremioId.substr(pos, next - pos));
            pos = next + 1;
        }
        parts.push_back(p.stremioId.substr(pos));
        if (parts.size() == 3) {
            p.baseId = parts[0];
            try {
                p.season = std::stoll(parts[1]);
                p.episode = std::stoll(parts[2]);
            } catch (...) {
            }
        }
    }
    return p;
}

/// Build the synthetic ratingKey for a season row of a show.
inline std::string seasonId(const std::string& showId, int64_t n) {
    return "season:" + showId + ":" + std::to_string(n);
}

/// Build the ratingKey for an episode from its Stremio video id ("{base}:{s}:{e}").
inline std::string episodeId(const std::string& videoId) { return "series:" + videoId; }

/// ---- Manifest / catalog descriptors ----------------------------------------

struct Catalog {
    std::string type;  // movie | series | channel | tv
    std::string id;
    std::string name;
    std::vector<std::string> extraSupported;  // flattened from extra[].name + extraSupported[]
    std::vector<std::string> genres;          // declared genre options (if any)
    // false when the catalog REQUIRES an extra we cannot supply (e.g. Cinemeta's
    // last-videos / calendar-videos need lastVideosIds / calendarVideosIds): it
    // is not browsable as a plain grid and must be hidden from the UI.
    bool browsable = true;
    // When browsable == false: the name of the required extra that made it so.
    // Diagnostics only (logged at manifest-load time) — a catalog vanishing from
    // the UI with no stated reason is exactly the kind of silent skip that makes
    // "my addon's rows don't show up" unbreakable from a log.
    std::string blockedBy;

    bool hasSearch() const {
        return std::find(extraSupported.begin(), extraSupported.end(), "search") != extraSupported.end();
    }
    bool hasSkip() const {
        return std::find(extraSupported.begin(), extraSupported.end(), "skip") != extraSupported.end();
    }
    bool hasGenre() const {
        return std::find(extraSupported.begin(), extraSupported.end(), "genre") != extraSupported.end();
    }
};

struct Manifest {
    std::string id;
    std::string name;
    std::string version;
    /// Optional square brand mark. NuvioTV's stream cards print it above the
    /// addon's name, which is the only thing it is used for here.
    std::string logo;
    std::set<std::string> resources;  // catalog | meta | stream | subtitles (flattened)
    std::set<std::string> types;      // movie | series | …
    std::vector<std::string> idPrefixes;
    std::vector<Catalog> catalogs;
};

struct Addon {
    std::string transportUrl;
    std::string base;
    Manifest manifest;

    /// Does this addon serve `resource` for the given Stremio `type` (and `id`)?
    /// - resource must be advertised
    /// - type must match (empty types[] = "all types")
    /// - id (when given) must match an idPrefix (empty idPrefixes[] = "all ids")
    bool supports(const std::string& resource, const std::string& type, const std::string& id = "") const {
        if (manifest.resources.count(resource) == 0) return false;
        if (!manifest.types.empty() && manifest.types.count(type) == 0) return false;
        if (!id.empty() && !manifest.idPrefixes.empty()) {
            bool ok = false;
            for (const auto& pfx : manifest.idPrefixes) {
                if (id.rfind(pfx, 0) == 0) {
                    ok = true;
                    break;
                }
            }
            if (!ok) return false;
        }
        return true;
    }
};

/// ---- Manifest / catalog parsers --------------------------------------------

inline Catalog parseCatalogDescriptor(const nlohmann::json& j) {
    Catalog c;
    c.type = jstr(j, "type");
    c.id = jstr(j, "id");
    c.name = jstr(j, "name", c.id);
    // extraSupported can come either as a flat string[] (legacy) or be derived
    // from extra[{name, options?}] objects (current). Merge both.
    if (j.contains("extraSupported") && j["extraSupported"].is_array()) {
        for (auto& e : j["extraSupported"])
            if (e.is_string()) c.extraSupported.push_back(e.get<std::string>());
    }
    if (j.contains("extra") && j["extra"].is_array()) {
        for (auto& e : j["extra"]) {
            std::string name = jstr(e, "name");
            if (!name.empty() &&
                std::find(c.extraSupported.begin(), c.extraSupported.end(), name) == c.extraSupported.end())
                c.extraSupported.push_back(name);
            // collect genre options for the "genre" extra (used by filtering UI later)
            if (name == "genre" && e.contains("options") && e["options"].is_array())
                for (auto& o : e["options"])
                    if (o.is_string()) c.genres.push_back(o.get<std::string>());
            // A REQUIRED extra we cannot supply means this is not a grid.
            //
            // `search` counts. A catalog with {"name":"search","isRequired":true}
            // is a SEARCH ENDPOINT, not a browsable list — AIOMetadata's
            // search.movie/search.series/people_search.* are declared exactly
            // that way, and fetching them without a term returns an empty
            // `metas`, so they showed up as permanently dead Home rows and as
            // empty "Movies"/"People (Movies)" sub-tabs in the sidebar. They
            // are still reachable from the Search tab, which goes through
            // allCatalogs() + hasSearch() and does not consult `browsable`.
            //
            // `skip` is paging and `genre` carries options/a default, so both
            // stay browsable — a bare fetch of those still returns a page.
            if (jbool(e, "isRequired") && name != "skip" && name != "genre") {
                c.browsable = false;
                if (c.blockedBy.empty()) c.blockedBy = name;
            }
        }
    }
    if (j.contains("genres") && j["genres"].is_array())
        for (auto& g : j["genres"])
            if (g.is_string()) c.genres.push_back(g.get<std::string>());
    return c;
}

inline Manifest parseManifest(const nlohmann::json& j) {
    Manifest m;
    m.id = jstr(j, "id");
    m.name = jstr(j, "name", m.id);
    m.version = jstr(j, "version");
    m.logo = jstr(j, "logo");
    // `resources` may be a list of STRINGS (["catalog","meta"]) OR a list of
    // OBJECTS ([{"name":"stream","types":[…],"idPrefixes":[…]}]). Flatten to a
    // set of names; per-resource type/id overrides are ignored for now.
    if (j.contains("resources") && j["resources"].is_array()) {
        for (auto& r : j["resources"]) {
            if (r.is_string())
                m.resources.insert(r.get<std::string>());
            else if (r.is_object())
                m.resources.insert(jstr(r, "name"));
        }
    }
    if (j.contains("types") && j["types"].is_array())
        for (auto& t : j["types"])
            if (t.is_string()) m.types.insert(t.get<std::string>());
    if (j.contains("idPrefixes") && j["idPrefixes"].is_array())
        for (auto& p : j["idPrefixes"])
            if (p.is_string()) m.idPrefixes.push_back(p.get<std::string>());
    if (j.contains("catalogs") && j["catalogs"].is_array())
        for (auto& c : j["catalogs"]) m.catalogs.push_back(parseCatalogDescriptor(c));
    return m;
}

/// ---- Item mappers (Stremio JSON -> media::*) -------------------------------

/// Pull `genres` / `cast` / `director` either from the dedicated array fields or,
/// when absent, from the `links[]` array grouped by `category`.
inline std::vector<std::string> stringArray(const nlohmann::json& j, const char* key) {
    std::vector<std::string> out;
    auto it = j.find(key);
    if (it != j.end() && it->is_array())
        for (auto& e : *it)
            if (e.is_string()) out.push_back(e.get<std::string>());
    return out;
}

inline std::vector<std::string> linksByCategory(const nlohmann::json& j, const std::string& category) {
    std::vector<std::string> out;
    auto it = j.find("links");
    if (it == j.end() || !it->is_array()) return out;
    for (auto& l : *it)
        if (jstr(l, "category") == category) out.push_back(jstr(l, "name"));
    return out;
}

/// Apply the shared fields of a Stremio meta/metaPreview object onto an Item.
/// `isPreview` skips the heavy fields (cast/director/videos) absent from previews.
inline void applyMetaCommon(const nlohmann::json& j, media::Item& it) {
    it.title = jstr(j, "name");
    it.summary = jstr(j, "description", jstr(j, "overview"));
    it.year = parseYear(jstr(j, "releaseInfo"));
    it.releaseInfo = jstr(j, "releaseInfo");
    // Stremio images are ABSOLUTE URLs; imageUrl() passes them through unchanged.
    it.thumb = jstr(j, "poster");
    it.art = jstr(j, "background");
    it.clearLogo = jstr(j, "logo");
    it.duration = parseRuntimeMs(jstr(j, "runtime"));
    it.originallyAvailableAt = jstr(j, "released");
    // "United States of America" — the reference's third secondary-meta item
    it.country = jstr(j, "country");
    // MetaResponseDto/CatalogResponseDto carry both of these, and the reference
    // reads them straight off the addon: statusText and languageText on its
    // HeroPreview come from here when TMDB enrichment is off. Language is
    // upper-cased there ("item.language?.uppercase()"), so it is here too.
    it.status = jstr(j, "status");
    it.language = jstr(j, "language");
    for (auto& ch : it.language) ch = (char)std::toupper((unsigned char)ch);
    // imdbRating is a 0-10 string ("8.7"); expose as the critic rating.
    it.rating = jnum(j, "imdbRating");
    if (it.rating > 0) it.ratingImage = "imdb://image.rating";
    // genres: dedicated array, else derived from links[category=Genres]
    it.genres = stringArray(j, "genres");
    if (it.genres.empty()) it.genres = linksByCategory(j, "Genres");
}

/// Catalog `metas[]` entry -> Item (poster row). type comes from the metaPreview
/// (or the catalog type passed by the caller via the json itself).
inline media::Item parseMetaPreview(const nlohmann::json& j) {
    media::Item it;
    std::string sType = jstr(j, "type");
    std::string sId = jstr(j, "id");
    it.type = mapType(sType);
    it.ratingKey = sType + ":" + sId;
    it.key = it.ratingKey;
    it.guid = sId;
    applyMetaCommon(j, it);
    return it;
}

/// Full `meta` object -> Item. For a series, leafCount = number of videos.
inline media::Item parseMeta(const nlohmann::json& j) {
    media::Item it = parseMetaPreview(j);

    // `app_extras` is where the TMDB-backed addons put what the bare Stremio
    // schema has no room for: a cast with character names AND portraits (the
    // top-level `cast` is a list of bare strings), the age rating, and the
    // full credits. NuvioTV shows faces on its cast row because this is here.
    auto extras = j.find("app_extras");
    if (extras != j.end() && extras->is_object()) {
        auto castArr = extras->find("cast");
        if (castArr != extras->end() && castArr->is_array()) {
            for (auto& c : *castArr) {
                if (!c.is_object()) continue;
                media::Role r;
                r.tag = jstr(c, "name");
                r.role = jstr(c, "character");
                r.thumb = jstr(c, "photo");
                if (!r.tag.empty()) it.roles.push_back(std::move(r));
            }
        }
        // Same shape for the crew: the top-level `director` is a list of bare
        // names, so a director's portrait never arrived and the cast row's
        // first tile sat empty.
        auto dirArr = extras->find("directors");
        if (dirArr != extras->end() && dirArr->is_array()) {
            for (auto& d : *dirArr) {
                if (!d.is_object()) continue;
                media::Role r;
                r.tag = jstr(d, "name");
                r.thumb = jstr(d, "photo");
                if (!r.tag.empty()) it.directors.push_back(std::move(r));
            }
        }
        // "R", "TV-MA", ... — the badge the reference prints beside the runtime
        it.contentRating = jstr(*extras, "certification", jstr(*extras, "certificationLocal"));
    }

    // cast / director (string arrays, else links by category). Only when
    // app_extras gave us nothing better.
    std::vector<std::string> cast = it.roles.empty() ? stringArray(j, "cast") : std::vector<std::string>{};
    if (cast.empty() && it.roles.empty()) cast = linksByCategory(j, "Cast");
    for (auto& name : cast) {
        media::Role r;
        r.tag = name;
        it.roles.push_back(r);
    }
    std::vector<std::string> directors =
        it.directors.empty() ? stringArray(j, "director") : std::vector<std::string>{};
    if (directors.empty() && it.directors.empty()) directors = linksByCategory(j, "Directors");
    for (auto& name : directors) {
        media::Role r;
        r.tag = name;
        it.directors.push_back(r);
    }
    if (j.contains("videos") && j["videos"].is_array()) it.leafCount = (int64_t)j["videos"].size();
    return it;
}

/// Series `meta.videos[]` -> episode Items, sorted by (season, episode).
/// Each video: { id:"tt…:1:1", name|title, season, episode, released, overview, thumbnail }.
/// ratingKey = "series:{video.id}", type = episode, index = episode,
/// parentIndex = season, grandparent* = the show.
inline std::vector<media::Item> parseEpisodes(const nlohmann::json& metaJson, const media::Item& show) {
    std::vector<media::Item> out;
    auto vids = metaJson.find("videos");
    if (vids == metaJson.end() || !vids->is_array()) return out;
    for (auto& v : *vids) {
        media::Item e;
        std::string vid = jstr(v, "id");
        e.ratingKey = episodeId(vid);
        e.key = e.ratingKey;
        e.guid = vid;
        e.type = media::mediaTypeEpisode;
        e.title = jstr(v, "name", jstr(v, "title"));
        e.summary = jstr(v, "overview", jstr(v, "description"));
        e.index = jint(v, "episode");
        e.parentIndex = jint(v, "season");
        e.thumb = jstr(v, "thumbnail");
        e.originallyAvailableAt = jstr(v, "released");
        e.grandparentRatingKey = show.ratingKey;
        e.grandparentTitle = show.title;
        e.grandparentThumb = show.thumb;
        e.grandparentArt = show.art;
        // Show-level facts an episode has no opinion of its own about. The
        // videos[] entries carry a name, a still and an overview and nothing
        // else, so without this an episode on the Continue Watching row had no
        // logo, no genre, no year and no rating to present.
        e.clearLogo = show.clearLogo;
        e.genres = show.genres;
        e.year = show.year;
        e.rating = show.rating;
        e.ratingImage = show.ratingImage;
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(), [](const media::Item& a, const media::Item& b) {
        if (a.parentIndex != b.parentIndex) return a.parentIndex < b.parentIndex;
        return a.index < b.index;
    });
    return out;
}

/// ---- Catalog envelope ------------------------------------------------------

struct CatalogResult {
    std::vector<media::Item> items;
    bool hasMore = false;
};

inline CatalogResult parseCatalog(const nlohmann::json& j) {
    CatalogResult r;
    auto metas = j.find("metas");
    if (metas != j.end() && metas->is_array())
        for (auto& m : *metas) r.items.push_back(parseMetaPreview(m));
    r.hasMore = jbool(j, "hasMore");
    return r;
}

/// ---- Streams (parsed now, consumed in étape 2) -----------------------------

struct StreamOption {
    std::string name;
    std::string title;
    std::string url;          // direct HTTP(S) URL
    std::string ytId;         // YouTube id
    std::string externalUrl;  // open in browser / external app
    std::string infoHash;     // torrent
    int fileIdx = -1;         // torrent file index
    bool notWebReady = false;
    std::string bingeGroup;
};

inline std::vector<StreamOption> parseStreams(const nlohmann::json& j) {
    std::vector<StreamOption> out;
    auto streams = j.find("streams");
    if (streams == j.end() || !streams->is_array()) return out;
    for (auto& s : *streams) {
        StreamOption so;
        so.name = jstr(s, "name");
        so.title = jstr(s, "title", jstr(s, "description"));  // title renamed to description in the SDK
        so.url = jstr(s, "url");
        so.ytId = jstr(s, "ytId");
        so.externalUrl = jstr(s, "externalUrl");
        so.infoHash = jstr(s, "infoHash");
        so.fileIdx = (int)jint(s, "fileIdx", -1);
        auto bh = s.find("behaviorHints");
        if (bh != s.end() && bh->is_object()) {
            so.notWebReady = jbool(*bh, "notWebReady");
            so.bingeGroup = jstr(*bh, "bingeGroup");
        }
        out.push_back(std::move(so));
    }
    return out;
}

/// ---- Stream classification (StreamOption -> media::Media source row) --------
//
// Addons format their stream `name`/`title` freely with inconsistent emoji
// (👤 vs 👥 for seeders, ⚙️/🔎/🔗 for source) — so we PARSE the strings into
// structured fields and re-render with our own consistent badges, rather than
// passing the addon's text through verbatim. (Research: every client does this.)

/// Resolution label from a stream's name+title ("4K"/"1440p"/"1080p"/"720p"/
/// "480p"/"CAM"/"SD"). CAM-class (cam/ts/telesync/screener) ranks below any
/// resolution. 1440p gets its own label so the PSV filter can drop it: an
/// unlabelled 1440p used to fall through to "SD" and rank ABOVE 1080p on Vita.
inline std::string qualityLabel(const std::string& text) {
    std::string t = text;
    for (auto& c : t) c = (char)std::tolower((unsigned char)c);
    if (t.find("2160") != std::string::npos || t.find("4k") != std::string::npos || t.find("uhd") != std::string::npos)
        return "4K";
    if (t.find("1440") != std::string::npos || t.find("2k") != std::string::npos) return "1440p";
    if (t.find("1080") != std::string::npos) return "1080p";
    if (t.find("720") != std::string::npos) return "720p";
    if (t.find("480") != std::string::npos) return "480p";
    if (t.find("cam") != std::string::npos || t.find("telesync") != std::string::npos ||
        t.find(" ts ") != std::string::npos || t.find("screener") != std::string::npos)
        return "CAM";
    return "SD";
}

/// PS Vita sort rank. The hardware H.264 decoder tops out at 1080p and 4K
/// hard-crashes the GPU (blue light), while 1080p remux bitrates stutter on the
/// Vita's limited CPU/IO. So every <=720p source outranks 1080p, which stays
/// only as a last-resort fallback (4K is filtered out before the sort, but is
/// ranked lowest here as a safety net). Field-tested guidance from Vita users:
/// keep Stremio playback at <=720p. (default pick = highest-ranked = index 0.)
inline int qualityRankVita(const std::string& label) {
    if (label == "720p") return 5;
    if (label == "480p" || label == "SD") return 4;
    if (label == "1080p") return 3;  // decodable but heavy -> fallback only
    if (label == "4K" || label == "1440p") return 1;  // exceed the decoder; excluded upstream
    return 2;                        // CAM
}

/// PS Vita video-codec rank. The Vita ffmpeg build ships NO hevc/av1/vp9/xvid
/// decoder at all (scripts/vita/ffmpeg/VITABUILD compiles --disable-decoders
/// plus an H.264-centric allowlist) — such streams cannot play, ever, not even
/// slowly. An empty label (no codec token in the addon text) is most often
/// H.264 in the wild, so it stays playable-by-default, below explicit H.264.
inline int codecRankVita(const std::string& label) {
    if (label == "H.264") return 2;
    if (label.empty()) return 1;  // unknown: best effort
    return 0;                     // HEVC / AV1 / XviD: no decoder on Vita
}

/// PS Vita audio-codec rank, for DEPRIORITIZATION only (never exclusion: a
/// stream whose audio the Vita build can't decode — no eac3/dca/truehd/opus in
/// VITABUILD — still plays video, just silent, and the tokens are less reliable
/// than video ones). Decodable (AAC/AC3/FLAC/MP3) > unknown > undecodable.
inline int audioRankVita(const std::string& label) {
    if (label == "AAC" || label == "AC3" || label == "FLAC" || label == "MP3") return 2;
    if (label.empty()) return 1;
    return 0;  // DDP / DTS / TrueHD / Atmos / Opus
}

/// Normalized video codec ("HEVC"/"AV1"/"H.264"/"XviD") from name+title; empty
/// if none. H.264 is tested before XviD: "MPEG-4 AVC" names hit "avc" first.
inline std::string parseCodecLabel(const std::string& text) {
    std::string t = text;
    for (auto& c : t) c = (char)std::tolower((unsigned char)c);
    if (t.find("x265") != std::string::npos || t.find("h265") != std::string::npos ||
        t.find("h.265") != std::string::npos || t.find("hevc") != std::string::npos)
        return "HEVC";
    if (t.find("av1") != std::string::npos) return "AV1";
    if (t.find("x264") != std::string::npos || t.find("h264") != std::string::npos ||
        t.find("h.264") != std::string::npos || t.find("avc") != std::string::npos)
        return "H.264";
    if (t.find("xvid") != std::string::npos || t.find("divx") != std::string::npos) return "XviD";
    return "";
}

/// Normalized audio codec from name+title; empty if none. Ordered so the
/// Dolby Digital Plus spellings match before plain AC3 ("eac3" contains "ac3").
inline std::string parseAudioLabel(const std::string& text) {
    std::string t = text;
    for (auto& c : t) c = (char)std::tolower((unsigned char)c);
    if (t.find("truehd") != std::string::npos) return "TrueHD";
    if (t.find("atmos") != std::string::npos) return "Atmos";
    if (t.find("eac3") != std::string::npos || t.find("e-ac3") != std::string::npos ||
        t.find("eac-3") != std::string::npos || t.find("ddp") != std::string::npos ||
        t.find("dd+") != std::string::npos || t.find("digital plus") != std::string::npos)
        return "DDP";
    if (t.find("dts") != std::string::npos) return "DTS";
    if (t.find("opus") != std::string::npos) return "Opus";
    if (t.find("flac") != std::string::npos) return "FLAC";
    if (t.find("aac") != std::string::npos) return "AAC";
    if (t.find("ac3") != std::string::npos || t.find("ac-3") != std::string::npos ||
        t.find("dd5.1") != std::string::npos)
        return "AC3";
    if (t.find("mp3") != std::string::npos) return "MP3";
    return "";
}

/// Detect a debrid-served stream from its `name` (e.g. "[RD+]"/"[RD download]"/
/// "[AD+]"/"[RD⚡]"). Sets `cached` (best-effort: '+'/⚡ = cached, "download"/⬇/⏳ =
/// not). Debrid-ness is conveyed only by this label convention — there is no
/// behaviorHint for it. Returns true when a known debrid code is present.
inline bool detectDebrid(const std::string& name, bool& cached) {
    std::string up = name;
    for (auto& c : up) c = (char)std::toupper((unsigned char)c);
    static const char* codes[] = {"[RD", "[AD", "[PM", "[TB", "[DL", "[OC", "[ED"};
    bool found = false;
    for (auto code : codes)
        if (up.find(code) != std::string::npos) {
            found = true;
            break;
        }
    // some addons also spell it out
    if (!found && (up.find("REAL-DEBRID") != std::string::npos || up.find("REALDEBRID") != std::string::npos ||
                      up.find("ALLDEBRID") != std::string::npos || up.find("DEBRID") != std::string::npos))
        found = true;
    if (!found) return false;
    // cached unless an "uncached" marker is present
    cached = !(up.find("DOWNLOAD") != std::string::npos || name.find("\xE2\xAC\x87") != std::string::npos /*⬇*/ ||
               name.find("\xE2\x8F\xB3") != std::string::npos /*⏳*/);
    return true;
}

/// Addons format name/title as several lines (provider tag, quality, codec,
/// size, seeders, language flags, …) meant for a UI that stacks them; GMCA's
/// source row is one line per field, so a raw multi-line blob renders as an
/// ugly, unpredictably-tall wall of text that varies wildly addon to addon.
/// Collapsing each field onto one line, joined by a plain separator, gives
/// every addon's text the same normalized shape (and the Label ellipsizes it
/// if still too wide, rather than wrapping into a tall block).
inline std::string flattenAddonText(const std::string& text) {
    std::string out;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find_first_of("\r\n", pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        size_t start = line.find_first_not_of(' ');
        if (start != std::string::npos) {
            size_t end = line.find_last_not_of(' ');
            if (!out.empty()) out += "  ·  ";
            out += line.substr(start, end - start + 1);
        }
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    return out;
}

/// Map one Stremio stream to a neutral source row. `label`/`detail` are the
/// addon's own `name`/`title`, flattened to one normalized line each
/// (flattenAddonText), falling back to the manifest name when a stream
/// carries no `name`. Only `url` streams are playable here; infoHash/ytId/
/// externalUrl are classified as non-playable (no torrent engine / no
/// browser on console).
inline media::Media streamToMedia(
    const StreamOption& s, const std::string& addonName, const std::string& addonLogo = "") {
    media::Media m;
    std::string blob = s.name + " " + s.title;
    m.videoResolution = qualityLabel(blob);
    m.label = flattenAddonText(!s.name.empty() ? s.name : addonName);
    m.detail = flattenAddonText(s.title);
    m.labelRaw = !s.name.empty() ? s.name : addonName;
    m.detailRaw = s.title;
    m.addonName = addonName;
    m.addonLogo = addonLogo;
    m.bingeGroup = s.bingeGroup;

    if (!s.url.empty()) {
        bool cached = true;
        bool debrid = detectDebrid(s.name, cached);
        m.kind = debrid ? media::SourceKind::Debrid : media::SourceKind::Direct;
        m.cached = cached;
        media::Part p;
        p.key = s.url;
        p.accessible = true;
        p.exists = true;
        m.parts.push_back(std::move(p));
        // structured fields, not just display: the PSV stream filter/sort in
        // resolveAllStreams reads them (codecRankVita / audioRankVita)
        m.videoCodec = parseCodecLabel(blob);
        m.audioCodec = parseAudioLabel(blob);
    } else if (!s.infoHash.empty()) {
        m.kind = media::SourceKind::Torrent;
    } else if (!s.ytId.empty()) {
        m.kind = media::SourceKind::Youtube;
    } else {
        m.kind = media::SourceKind::External;  // externalUrl or unknown: non-playable here
    }
    return m;
}

/// ---- Subtitles (subtitles resource) ----------------------------------------
//
// `{base}/subtitles/{type}/{encId}.json` returns { subtitles: [{ id, url, lang }] }
// (SDK: docs/api/responses/subtitles.md). `url` is an ABSOLUTE http(s) link to a
// SRT/VTT file, `lang` an ISO 639-2 code (or free text when no valid code). We
// query WITHOUT the optional videoHash/videoSize extras (OpenSubtitles-style hash
// matching): that would need range reads of the remote/debrid file per playback —
// too costly on console. Id-based matching (imdbId / episode id) is enough; any
// residual desync is handled by the player's existing sub-delay (subsync) control.

struct SubtitleOption {
    std::string id;
    std::string url;   // absolute http(s) URL to the subtitle file (SRT/VTT)
    std::string lang;  // ISO 639-2 code, or free text (SDK fallback)
    /// OpenSubtitles-style extras, both optional. One language can carry a
    /// dozen entries, and the release they were timed against is the only
    /// thing that tells them apart — so the picker shows this, not "English"
    /// twelve times over.
    std::string fileName;     // subtitleFileName
    std::string releaseName;  // movieReleaseName
};

inline std::vector<SubtitleOption> parseSubtitles(const nlohmann::json& j) {
    std::vector<SubtitleOption> out;
    auto subs = j.find("subtitles");
    if (subs == j.end() || !subs->is_array()) return out;
    for (auto& s : *subs) {
        SubtitleOption so;
        so.id = jstr(s, "id");
        so.url = jstr(s, "url");
        so.lang = jstr(s, "lang");
        so.fileName = jstr(s, "subtitleFileName");
        so.releaseName = jstr(s, "movieReleaseName");
        if (!so.url.empty()) out.push_back(std::move(so));  // a url is the only usable field
    }
    return out;
}

/// ---- Transport helper ------------------------------------------------------

/// GET + parse JSON. Stremio addons are unauthenticated, so no headers. Returns
/// an empty object on an empty body (rather than throwing on parse).
inline nlohmann::json getSync(const std::string& url, long timeout = HTTP::TIMEOUT) {
    std::string resp = HTTP::get(url, HTTP::Timeout{timeout});
    if (resp.empty()) return nlohmann::json::object();
    return nlohmann::json::parse(resp);
}

/// Runs `worker(items[i])` for every item on the shared ThreadPool
/// concurrently and returns the results in the SAME order as `items`,
/// blocking until every one has finished. Each catalog/addon fetch is its
/// own HTTP round trip; doing them one after another in a loop meant a
/// single slow addon held up every other one behind it — this was the
/// single biggest source of Home/Suggestions load latency in a many-addon
/// setup. `worker` should not let an exception escape past logging it: a
/// thrown one here is swallowed and that slot is left default-constructed
/// (== "no result"), same as returning one from a plain sequential loop.
template <typename In, typename Out>
inline std::vector<Out> parallelMap(const std::vector<In>& items, std::function<Out(const In&)> worker) {
    if (items.empty()) return {};
    if (items.size() == 1) {
        try {
            return {worker(items[0])};
        } catch (const std::exception& ex) {
            brls::Logger::warning("parallelMap: {}", ex.what());
            return {Out{}};
        }
    }

    struct JoinState {
        std::mutex mutex;
        std::condition_variable cond;
        std::vector<Out> results;
        size_t remaining;
    };
    auto state = std::make_shared<JoinState>();
    state->results.resize(items.size());
    state->remaining = items.size();

    for (size_t i = 0; i < items.size(); i++) {
        In item = items[i];
        ThreadPool::instance().submit([state, worker, item, i](HTTP&) {
            Out result{};
            try {
                result = worker(item);
            } catch (const std::exception& ex) {
                brls::Logger::warning("parallelMap[{}]: {}", i, ex.what());
            } catch (...) {
            }
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->results[i] = std::move(result);
                state->remaining--;
            }
            state->cond.notify_all();
        });
    }

    std::unique_lock<std::mutex> lock(state->mutex);
    state->cond.wait(lock, [&state]() { return state->remaining == 0; });
    return std::move(state->results);
}

}  // namespace stremio
