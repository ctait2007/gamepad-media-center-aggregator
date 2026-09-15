/*
    GMCA — The Intro Database (theintrodb.org), the source NuvioTV's skip
    markers come from.

    NuvioTV reaches it through SkipIntroRepository/IntroDbApi, whose base url is
    a build-time key (INTRODB_API_URL) it reads from a private localProperties.
    The KEY is private; the SERVICE is not. theintrodb.org's v3 read endpoint
    takes no authentication at all — only POST /v3/submit needs an api key — so
    this goes at the public API directly rather than reproducing their client.

    Their client is also written against an older shape (GET segments?imdb_id=…,
    returning single intro/recap/outro objects with start_sec), which the public
    v3 endpoint has since replaced with:

        GET https://api.theintrodb.org/v3/media
            ?imdb_id=tt0903747&season=1&episode=1&duration_ms=3480000

        {"tmdb_id":1396,"type":"tv","season":1,"episode":1,
         "intro":  [{"start_ms":228650,"end_ms":246025}],
         "credits":[{"start_ms":3431000,"end_ms":null}]}

    Four arrays — intro, recap, credits, preview — each a list of spans, either
    end of which may be null for "runs to the boundary of the file". The arrays
    are why PlayerNextEpisodeRules reduces with maxOf/minOf rather than reading
    one span.
*/

#pragma once

#include <api/media/types.hpp>

#include <functional>
#include <string>
#include <vector>

namespace introdb {

/// One span, in SECONDS and in SkipIntroRepository's own vocabulary, so that
/// PlayerNextEpisodeRules' OUTRO_SEGMENT_TYPES membership test ports verbatim.
struct SkipInterval {
    double startTime = 0;
    /// -1 = runs to the end of the file. Only the player knows how long that
    /// is, so the caller closes it — see VideoView::skipEnd.
    double endTime = 0;
    std::string type;  // "intro" | "recap" | "outro" | "preview"
};

/// The IMDb id an item can be looked up by, or "" when it has none.
///
/// Stremio/Nuvio content is IMDb-keyed throughout — a show's ratingKey is
/// "tt1234567" and an episode's guid is "tt1234567:1:1" — so this is free for
/// the backends that matter. Plex guids (plex://…) and Jellyfin ProviderIds
/// are not, and those items simply do not get looked up.
std::string imdbIdOf(const media::Item& item);

/// GET /v3/media for `item`. `then` runs on the UI thread with whatever came
/// back, which is an empty list for anything the database has no entry for —
/// far and away the common case, and not an error.
///
/// `durationSec` is only passed on to the query, which takes it as a hint for
/// picking between cuts; 0 omits it. It does NOT close the open-ended spans —
/// the metadata duration is not the file's, and for a Stremio episode there is
/// no metadata duration at all. Those spans arrive with endTime -1 and the
/// player closes them against what mpv reports.
void fetch(const media::Item& item, double durationSec, std::function<void(std::vector<SkipInterval>)> then);

/// Forget every cached lookup. Only for tests and a settings toggle.
void clearCache();

}  // namespace introdb
