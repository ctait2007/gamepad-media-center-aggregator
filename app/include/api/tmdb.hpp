/*
    GMCA — TMDB enrichment, NuvioTV's TmdbMetadataService/TmdbService.

    WHY THERE IS A KEY FIELD IN SETTINGS AND THERE IS NOT ONE IN NuvioTV.
    NuvioTV ships the developer's own key: app/build.gradle.kts declares

        buildConfigField("String", "TMDB_API_KEY",
                         "\"${localProperties.getProperty("TMDB_API_KEY", "")}\"")

    and TmdbMetadataService reads BuildConfig.TMDB_API_KEY, so the key is baked
    into the APK from a private localProperties and the user never sees one.
    That key is theirs and is not in the repository; a key committed to a public
    one gets scraped and revoked. So the SERVICE is replicated exactly and the
    key comes from the user, who makes one for free at themoviedb.org. With no
    key, enrichment is simply off — the same state as NuvioTV's default, where
    `enabled` starts false.

    Everything else here is their pipeline:

      * ensureTmdbId  — strip a "tmdb:"/"movie:"/"series:" prefix and the
        ":season:episode" tail, then GET find/{imdb}?external_source=imdb_id for
        a tt-number. A bare numeric id is assumed to be a TMDB id already.
      * fetchEnrichment — details, credits, images and content ratings for one
        title, keyed "{id}:{type}:{language}" and shared between concurrent
        callers rather than fetched twice.
      * the artwork sizes: poster w500, backdrop w1280, logo w500, person w500,
        company/network logo w300, episode still w500.
      * selectBestLocalizedImagePath — exact region, then the bare language,
        then the language in any region, then English, then language-less.
*/

#pragma once

#include <api/media/types.hpp>

#include <functional>
#include <string>
#include <vector>

namespace tmdb {

/// One name from the credits, in MetaCastMember's shape.
struct Person {
    std::string name;
    std::string character;  // "" for a crew member; "Creator" for a tv creator
    std::string photo;      // w500, or "" when TMDB has no portrait
    int64_t tmdbId = 0;
};

/// A production company or a network (MetaCompany).
struct Company {
    std::string name;
    std::string logo;  // w300
    int64_t tmdbId = 0;
};

/// TmdbEnrichment, less the fields whose consumers we have not built yet
/// (collections, more-like-this, trailers, alternative titles).
struct Enrichment {
    std::string localizedTitle;
    std::string description;
    std::vector<std::string> genres;
    std::string backdrop;  // w1280
    std::string logo;      // w500
    std::string poster;    // w500
    std::vector<Person> cast;
    std::vector<Person> directors;
    std::vector<Person> writers;
    /// buildShowYearRange: "2007-2019" for an ended show, "2016-" for a running
    /// one, a bare year for a movie. This is what the home hero's first meta row
    /// wants and what a Stremio addon's own releaseInfo already carries.
    std::string releaseInfo;
    double rating = 0.0;
    int64_t runtimeMinutes = 0;
    std::vector<Company> productionCompanies;
    std::vector<Company> networks;
    std::string ageRating;  // "TV-14", "R"...
    std::string status;     // "Ended", "Returning Series"...
    std::vector<std::string> countries;
    std::string language;  // ISO 639-1, upper-cased as the reference stores it
    bool valid = false;
};

/// Per-episode enrichment (TmdbEpisodeEnrichment).
struct EpisodeEnrichment {
    std::string title;
    std::string overview;
    std::string thumbnail;  // w500 still
    std::string airDate;
    int64_t runtimeMinutes = 0;
};

// --- pure helpers, unit-tested in tests/test_tmdb.cpp ----------------------

/// normalizeTmdbLanguage: "" -> "en", "pt_br" -> "pt-BR", "ZH-hans" -> "zh-HANS".
std::string normalizeLanguage(const std::string& language);

/// ensureTmdbId's parsing half: the external id inside a Stremio video id.
/// "tt1234567:1:4" -> "tt1234567", "tmdb:693134" -> "693134", "" for anything
/// that is neither a tt-number nor all digits.
std::string externalIdOf(const std::string& videoId);

/// "movie" for a film, "tv" for everything else — normalizeMediaType.
std::string tmdbType(const std::string& mediaType);

/// https://image.tmdb.org/t/p/{size}{path}, or "" for a blank path.
std::string imageUrl(const std::string& path, const std::string& size);

/// buildShowYearRange, over TMDB's first_air_date/last_air_date/status.
std::string showYearRange(const std::string& firstAirDate, const std::string& lastAirDate,
    const std::string& status);

/// One entry of TMDB's images.logos/backdrops, for selectBest below.
struct LocalizedImage {
    std::string filePath;
    std::string iso6391;  // "" = language-less (a textless logo)
    std::string iso31661;
};

/// selectBestLocalizedImagePath. Returns the file path, or "".
std::string selectBest(const std::vector<LocalizedImage>& images, const std::string& normalizedLanguage);

// --- the service ----------------------------------------------------------

/// Whether enrichment should run at all: the setting is on AND a key is set.
bool enabled();

/// The user's key, or "" — AppConfig::TMDB_API_KEY.
std::string apiKey();

/// Resolve `item` to a TMDB id, then fetch its enrichment. `then` runs on the
/// UI thread. An item with no usable id, a missing key, a disabled setting or a
/// failed lookup all arrive as an Enrichment with `valid` false, which is not an
/// error — the caller keeps whatever the addon gave it.
void fetch(const media::Item& item, std::function<void(Enrichment)> then);

/// Apply `e` onto `item` in place, honouring the per-field settings
/// (TMDB_USE_ARTWORK, TMDB_USE_BASIC_INFO, ...). Only fields the user has
/// switched on are touched, and only where the enrichment actually has a value:
/// TMDB is a second source, not a replacement.
void apply(media::Item& item, const Enrichment& e);

/// Forget every cached lookup. For the settings toggle and for tests.
void clearCache();

}  // namespace tmdb
