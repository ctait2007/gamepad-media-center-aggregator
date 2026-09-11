/*
    GMCA — Nuvio implementation of media::Backend (see nuvio/backend.hpp).
*/

#include "api/nuvio/backend.hpp"
#include "api/nuvio/sync.hpp"
#include "api/stremio/types.hpp"
#include <borealis/core/i18n.hpp>
#include <borealis/core/logger.hpp>
#include <borealis/core/thread.hpp>
#include <chrono>

using namespace brls::literals;

namespace nuvio {

namespace {
// Throttle window for "playing" ticks (player_view.cpp reports every 10 s):
// push at most this often mid-playback. Pause/stop/exit always bypass it.
constexpr int64_t kProgressThrottleSec = 30;
}  // namespace

NuvioBackend::NuvioBackend() {
    // Replace the delegate's default (Stremio account) addon resync with
    // Nuvio's own (the `addons` table) — see AddonEngine::resyncAddons.
    delegate.addonEngine().resyncAddons = []() { nuvio::resyncAddons(); };

    // Browsable catalogs + composed home rows + ratings, always on (same as
    // Stremio's addon-protocol navigation). Watch progress/history (Continue
    // Watching, Next Up, mark watched) is always on too — an account is
    // required to sign in at all. Library/watchlist sync is not done yet.
    caps_.sections = true;
    caps_.homeHubs = true;
    caps_.continueWatching = true;
    caps_.serverSort = false;
    caps_.serverFilter = false;
    caps_.genres = true;
    caps_.collections = false;
    caps_.playlists = false;
    caps_.related = false;
    caps_.personPages = false;
    caps_.globalSearch = false;
    caps_.recentlyAdded = false;
    caps_.markWatched = true;
    // Library/watchlist ("My List") sync is a separate, not-yet-done piece —
    // canList/listWatchlist/etc. stay unoverridden (base no-ops) until then.
    caps_.listKind = media::ListKind::None;
    caps_.ratings = true;
    caps_.skipIntro = false;
    caps_.transcode = false;
    caps_.serverProgress = true;
    caps_.downloadOriginal = false;
    caps_.multiProfile = true;  // Nuvio profile picker, see tab/nuvio_add.cpp
}

std::string NuvioBackend::subtitleMenuHint() const { return "main/nuvio/subtitle/none"_i18n; }

void NuvioBackend::getContinueWatching(
    int count, media::Then<media::Container<media::Hub>> then, media::OnError error) {
    int cnt = count;
    std::string title = "main/home/resume"_i18n;  // resolved on the UI thread
    brls::async([this, cnt, title, then, error]() {
        try {
            progressStore.ensureLoaded();
            auto rows = progressStore.continueWatching((size_t)cnt);
            media::Container<media::Hub> out;
            media::Hub h;
            h.title = title;
            h.hubIdentifier = "home.continue";
            for (auto& row : rows) {
                std::string ratingKey = ratingKeyFor(row.contentId, row.contentType, row.season, row.episode);
                media::Item item = resolveMeta(delegate.addonEngine(), ratingKey);
                // Unresolvable id (addon gone, wrong prefix, transient error):
                // skip this row rather than failing the whole list.
                if (item.ratingKey.empty()) continue;
                item.viewOffset = row.positionMs;
                if (row.durationMs > 0) item.duration = row.durationMs;
                h.items.push_back(std::move(item));
            }
            if (!h.items.empty()) out.Items.push_back(std::move(h));
            out.TotalRecordCount = (long)out.Items.size();
            brls::sync(std::bind(then, std::move(out)));
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
}

void NuvioBackend::getNextUp(
    const std::string& showId, std::function<void(media::Item, bool)> then, media::OnError) {
    std::string sid = showId;
    brls::async([this, sid, then]() {
        media::Item result;
        bool fromStart = false;
        try {
            progressStore.ensureLoaded();
            stremio::ParsedId pid = stremio::parseId(sid);
            auto eps = resolveEpisodes(delegate.addonEngine(), sid);
            if (!eps.empty()) {
                // Last episode (in order) with any signal (watched or in
                // progress); -1 if the show has never been touched.
                int lastIdx = -1;
                for (size_t i = 0; i < eps.size(); i++) {
                    bool watched = progressStore.isWatched(pid.stremioId, eps[i].parentIndex, eps[i].index);
                    auto prog = progressStore.progressFor(pid.stremioId, eps[i].parentIndex, eps[i].index);
                    if (watched || (prog && prog->positionMs > 0)) lastIdx = (int)i;
                }
                if (lastIdx < 0) {
                    // Never started: offer episode 1, "Play" (not "Replay").
                    result = eps.front();
                } else if (!progressStore.isWatched(pid.stremioId, eps[lastIdx].parentIndex, eps[lastIdx].index)) {
                    // In progress: resume that same episode.
                    result = eps[lastIdx];
                    if (auto prog = progressStore.progressFor(pid.stremioId, result.parentIndex, result.index))
                        result.viewOffset = prog->positionMs;
                } else if (lastIdx + 1 < (int)eps.size()) {
                    // Finished that episode: advance to the next fresh one.
                    result = eps[lastIdx + 1];
                } else {
                    // Finished the whole show: offer episode 1 again, "Replay".
                    result = eps.front();
                    fromStart = true;
                }
            }
        } catch (const std::exception& ex) {
            brls::Logger::warning("nuvio getNextUp: {}", ex.what());
        }
        // Contract: never surface a hard error; then(Item{}, false) on no episode.
        brls::sync([result, fromStart, then]() { then(result, fromStart); });
    });
}

void NuvioBackend::markWatched(const std::string& id) {
    stremio::ParsedId pid = stremio::parseId(id);
    if (pid.stremioType != "movie" && pid.stremioType != "series") return;
    if (pid.stremioType == "series" && (pid.season < 0 || pid.episode < 0)) return;  // whole-show id: nothing to do
    std::string contentId = pid.baseId, contentType = pid.stremioType;
    int64_t season = pid.season, episode = pid.episode;
    brls::async([this, contentId, contentType, season, episode]() {
        try {
            progressStore.pushWatched(contentId, contentType, season, episode);
        } catch (const std::exception& ex) {
            brls::Logger::warning("nuvio markWatched: {}", ex.what());
        }
    });
}

void NuvioBackend::markUnwatched(const std::string& id) {
    stremio::ParsedId pid = stremio::parseId(id);
    if (pid.stremioType != "movie" && pid.stremioType != "series") return;
    if (pid.stremioType == "series" && (pid.season < 0 || pid.episode < 0)) return;
    std::string contentId = pid.baseId;
    int64_t season = pid.season, episode = pid.episode;
    brls::async([this, contentId, season, episode]() {
        try {
            progressStore.clearWatched(contentId, season, episode);
        } catch (const std::exception& ex) {
            brls::Logger::warning("nuvio markUnwatched: {}", ex.what());
        }
    });
}

void NuvioBackend::reportProgress(
    const std::string& id, media::PlayState state, int64_t posMs, int64_t durMs, const std::string&) {
    if (durMs <= 0) return;  // nothing meaningful to report without a duration
    stremio::ParsedId pid = stremio::parseId(id);
    if (pid.stremioType != "movie" && pid.stremioType != "series") return;
    if (pid.stremioType == "series" && (pid.season < 0 || pid.episode < 0)) return;

    WatchProgressRow row;
    row.contentId = pid.baseId;
    row.contentType = pid.stremioType;
    row.season = pid.season;
    row.episode = pid.episode;
    row.videoId = (pid.season >= 0 && pid.episode >= 0) ? pid.stremioId : pid.baseId;
    row.positionMs = posMs;
    row.durationMs = durMs;
    row.lastWatched =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    // Paused/stopped always push now; a "playing" tick (reported every 10 s by
    // player_view.cpp) is throttled down to roughly once every 30 s.
    bool bypassThrottle = state != media::PlayState::Playing;

    brls::async([this, row, bypassThrottle]() {
        try {
            if (!bypassThrottle &&
                progressStore.recentlyPushed(row.contentId, row.season, row.episode, kProgressThrottleSec))
                return;
            progressStore.pushProgress(row);
        } catch (const std::exception& ex) {
            brls::Logger::warning("nuvio reportProgress: {}", ex.what());
        }
    });
}

}  // namespace nuvio
