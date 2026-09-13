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
            // Resolved concurrently, in input order. Each row needs its own
            // addon /meta round trip, and brls::async is a SINGLE serial
            // thread: doing these one after another held that thread — and so
            // every other backend call queued behind it, the Home hub rows
            // included — for the sum of every lookup.
            auto resolved = stremio::parallelMap<WatchProgressRow, media::Item>(
                rows, [this](const WatchProgressRow& row) -> media::Item {
                    std::string ratingKey =
                        ratingKeyFor(row.contentId, row.contentType, row.season, row.episode);
                    media::Item item = resolveMeta(delegate.addonEngine(), ratingKey);
                    // Unresolvable id (addon gone, wrong prefix, transient
                    // error): skip this row rather than failing the whole list.
                    if (item.ratingKey.empty()) return media::Item{};
                    item.viewOffset = row.positionMs;
                    if (row.durationMs > 0) item.duration = row.durationMs;
                    return item;
                });
            for (auto& item : resolved) {
                if (item.ratingKey.empty()) continue;
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

/// A WHOLE SHOW used to be a no-op here: the id carries no season/episode, so
/// there was nothing to write a watched_items row for, and "Mark as watched" on
/// a series poster did nothing at all. Marking a show watched means marking its
/// EPISODES watched — that is what makes the show read as finished, what Next
/// Up reads, and what NuvioTV shows — so the episode list is resolved first and
/// every episode is pushed, plus a row for the show itself.
void NuvioBackend::markWatched(const std::string& id) {
    stremio::ParsedId pid = stremio::parseId(id);
    if (pid.stremioType != "movie" && pid.stremioType != "series") return;
    std::string rk = id, contentId = pid.baseId, contentType = pid.stremioType;
    int64_t season = pid.season, episode = pid.episode;
    bool wholeShow = contentType == "series" && (season < 0 || episode < 0);

    brls::async([this, rk, contentId, contentType, season, episode, wholeShow]() {
        try {
            if (!wholeShow) {
                progressStore.pushWatched(contentId, contentType, season, episode);
                return;
            }
            for (const media::Item& ep : resolveEpisodes(delegate.addonEngine(), rk))
                progressStore.pushWatched(contentId, "series", ep.parentIndex, ep.index);
            // …and the show itself, so anything that asks about the bare id
            // (the poster sheet's own label, for one) agrees.
            progressStore.pushWatched(contentId, "series", -1, -1);
        } catch (const std::exception& ex) {
            brls::Logger::warning("nuvio markWatched: {}", ex.what());
        }
    });
}

void NuvioBackend::markUnwatched(const std::string& id) {
    stremio::ParsedId pid = stremio::parseId(id);
    if (pid.stremioType != "movie" && pid.stremioType != "series") return;
    std::string rk = id, contentId = pid.baseId, contentType = pid.stremioType;
    int64_t season = pid.season, episode = pid.episode;
    bool wholeShow = contentType == "series" && (season < 0 || episode < 0);

    brls::async([this, rk, contentId, season, episode, wholeShow]() {
        try {
            if (!wholeShow) {
                progressStore.clearWatched(contentId, season, episode);
                return;
            }
            for (const media::Item& ep : resolveEpisodes(delegate.addonEngine(), rk))
                progressStore.clearWatched(contentId, ep.parentIndex, ep.index);
            progressStore.clearWatched(contentId, -1, -1);
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

// ---- watched state -------------------------------------------------------------

/// An addon's episode list knows nothing about this ACCOUNT, so every item it
/// returns comes back unwatched and without a resume position. Stamping the
/// ProgressStore's answer on afterwards is what makes marking an episode
/// watched visible at all — before this, the push reached Nuvio and the screen
/// looked identical either way.
void NuvioBackend::applyWatchState(media::Container<media::Item>& c) {
    progressStore.ensureLoaded();
    for (media::Item& it : c.Items) {
        stremio::ParsedId pid = stremio::parseId(it.ratingKey);
        if (pid.stremioType != "movie" && pid.stremioType != "series") continue;
        if (progressStore.isWatched(pid.baseId, pid.season, pid.episode)) {
            it.viewCount = 1;
            it.viewOffset = 0;
            continue;
        }
        if (auto prog = progressStore.progressFor(pid.baseId, pid.season, pid.episode)) {
            it.viewOffset = prog->positionMs;
            if (prog->durationMs > 0) it.duration = prog->durationMs;
        }
    }
}

void NuvioBackend::getAllEpisodes(
    const std::string& showId, bool includeStreams, media::Then<media::Container<media::Item>> then, media::OnError error) {
    delegate.getAllEpisodes(showId, includeStreams,
        [this, then](media::Container<media::Item> c) {
            // ensureLoaded may pull; keep it off the UI thread
            brls::async([this, c, then]() mutable {
                try {
                    this->applyWatchState(c);
                } catch (const std::exception& ex) {
                    brls::Logger::warning("nuvio getAllEpisodes watch state: {}", ex.what());
                }
                brls::sync(std::bind(then, std::move(c)));
            });
        },
        error);
}

void NuvioBackend::getChildren(
    const std::string& id, media::Then<media::Container<media::Item>> then, media::OnError error) {
    delegate.getChildren(id,
        [this, then](media::Container<media::Item> c) {
            brls::async([this, c, then]() mutable {
                try {
                    this->applyWatchState(c);
                } catch (const std::exception& ex) {
                    brls::Logger::warning("nuvio getChildren watch state: {}", ex.what());
                }
                brls::sync(std::bind(then, std::move(c)));
            });
        },
        error);
}

// ---- library -------------------------------------------------------------------

bool NuvioBackend::canList(const media::Item& item) const {
    // Whole titles only — Nuvio's library rows are keyed on content_id +
    // content_type, which a season or an episode has no distinct value for.
    return item.type == media::mediaTypeMovie || item.type == media::mediaTypeShow;
}

void NuvioBackend::listWatchlist(const std::string&, media::MediaKind kind, size_t start, size_t size,
    media::Then<media::Container<media::Item>> then, media::OnError error) {
    size_t s = start, n = size;
    media::MediaKind want = kind;
    brls::async([this, s, n, want, then, error]() {
        try {
            libraryStore.ensureLoaded();
            std::vector<media::Item> all;
            for (const LibraryRow& row : libraryStore.rows()) {
                if (want == media::MediaKind::Movie && row.contentType != "movie") continue;
                if (want == media::MediaKind::Show && row.contentType != "series") continue;
                all.push_back(itemFromLibraryRow(row));
            }
            media::Container<media::Item> c;
            for (size_t i = s; i < all.size() && i < s + n; i++) c.Items.push_back(std::move(all[i]));
            c.StartIndex = (long)s;
            c.TotalRecordCount = (long)all.size();
            // The Library screen's Watched filter reads viewCount, and a
            // library row carries no watch state of its own.
            this->applyWatchState(c);
            brls::sync(std::bind(then, std::move(c)));
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
}

void NuvioBackend::getWatchlistState(const media::Item& item, media::Then<bool> then, media::OnError error) {
    stremio::ParsedId pid = stremio::parseId(item.ratingKey);
    std::string contentId = pid.baseId.empty() ? item.ratingKey : pid.baseId;
    std::string contentType = item.type == media::mediaTypeShow ? "series" : "movie";
    brls::async([this, contentId, contentType, then, error]() {
        try {
            libraryStore.ensureLoaded();
            bool in = libraryStore.contains(contentId, contentType);
            brls::sync([then, in]() {
                if (then) then(in);
            });
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
}

void NuvioBackend::setWatchlisted(
    const media::Item& item, bool add, std::function<void()> then, media::OnError error) {
    LibraryRow row = libraryRowFromItem(item);
    bool adding = add;
    brls::async([this, row, adding, then, error]() {
        try {
            libraryStore.ensureLoaded();
            if (adding)
                libraryStore.add(row);
            else
                libraryStore.remove(row.contentId, row.contentType);
            if (then) brls::sync(then);
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
}

}  // namespace nuvio
