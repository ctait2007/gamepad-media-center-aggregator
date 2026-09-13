/*
    GMCA — Nuvio implementation of media::Backend (see nuvio/backend.hpp).
*/

#include "api/nuvio/backend.hpp"
#include "api/nuvio/sync.hpp"
#include "api/stremio/types.hpp"
#include <borealis/core/i18n.hpp>
#include <borealis/core/logger.hpp>
#include <borealis/core/thread.hpp>
#include <algorithm>
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
    // required to sign in at all, and the library rides on the same session.
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
    // Library ("My List") lives on the account, in the same `library` rows
    // NuvioTV writes: canList/listWatchlist/getWatchlistState/setWatchlisted
    // below drive LibraryStore. This flag is what utils/local_library.hpp
    // reads to decide account-vs-on-device, so leaving it None (as it was)
    // silently sent every add, every removal and every read to library.json
    // instead — the account's own library never changed and never appeared.
    caps_.listKind = media::ListKind::Library;
    caps_.ratings = true;
    caps_.skipIntro = false;
    caps_.transcode = false;
    caps_.serverProgress = true;
    caps_.downloadOriginal = false;
    caps_.multiProfile = true;  // Nuvio profile picker, see tab/nuvio_add.cpp
}

std::string NuvioBackend::subtitleMenuHint() const { return "main/nuvio/subtitle/none"_i18n; }

/// The episode after the one this row points at, in (season, episode) order,
/// skipping anything already watched — finishing three in a row should offer
/// the fourth, not the second. Empty when the show has nothing left.
media::Item NuvioBackend::nextEpisodeAfter(const WatchProgressRow& row) {
    std::string showKey = "series:" + row.contentId;
    std::vector<media::Item> eps = resolveEpisodes(delegate.addonEngine(), showKey);
    std::sort(eps.begin(), eps.end(), [](const media::Item& a, const media::Item& b) {
        if (a.parentIndex != b.parentIndex) return a.parentIndex < b.parentIndex;
        return a.index < b.index;
    });
    for (const media::Item& e : eps) {
        bool after = e.parentIndex > row.season || (e.parentIndex == row.season && e.index > row.episode);
        if (!after) continue;
        if (progressStore.isWatched(row.contentId, e.parentIndex, e.index)) continue;
        media::Item out = e;
        // Whatever progress that episode already has, if it was started and
        // abandoned earlier in the run.
        if (auto p = progressStore.progressFor(row.contentId, e.parentIndex, e.index)) {
            out.viewOffset = p->positionMs;
            if (p->durationMs > 0) out.duration = p->durationMs;
        }
        return out;
    }
    return media::Item{};
}

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
                    // Finished: offer what comes NEXT, at zero progress, so a
                    // show you are working through stays on the row instead of
                    // vanishing the moment you finish an episode. A finished
                    // MOVIE, and a series with nothing after it, do leave.
                    if (row.finished) {
                        if (row.contentType != "series") return media::Item{};
                        media::Item next = nextEpisodeAfter(row);
                        if (next.ratingKey.empty()) return media::Item{};
                        next.viewOffset = 0;
                        return next;
                    }
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

void NuvioBackend::applyWatchState(media::Item& it) {
    media::Container<media::Item> one;
    one.Items.push_back(std::move(it));
    this->applyWatchState(one);
    it = std::move(one.Items.front());
}

void NuvioBackend::applyWatchState(media::Container<media::Hub>& c) {
    progressStore.ensureLoaded();
    for (media::Hub& h : c.Items) {
        media::Container<media::Item> items;
        items.Items = std::move(h.items);
        this->applyWatchState(items);
        h.items = std::move(items.Items);
    }
}

namespace {

/// The common shape of the three wrappers below: do the stamping on a worker
/// (ensureLoaded may pull), then hand the result to the caller on the UI
/// thread, and never let a sync failure swallow the payload — an unstamped
/// list is worth far more than none.
template <typename T, typename Apply>
media::Then<T> stampThen(media::Then<T> then, Apply apply) {
    return [then, apply](T payload) {
        brls::async([then, apply, payload]() mutable {
            try {
                apply(payload);
            } catch (const std::exception& ex) {
                brls::Logger::warning("nuvio watch state: {}", ex.what());
            }
            brls::sync(std::bind(then, std::move(payload)));
        });
    };
}

}  // namespace

media::Then<media::Container<media::Hub>> NuvioBackend::stamped(media::Then<media::Container<media::Hub>> then) {
    return stampThen<media::Container<media::Hub>>(
        then, [this](media::Container<media::Hub>& c) { this->applyWatchState(c); });
}

media::Then<media::Container<media::Item>> NuvioBackend::stamped(media::Then<media::Container<media::Item>> then) {
    return stampThen<media::Container<media::Item>>(
        then, [this](media::Container<media::Item>& c) { this->applyWatchState(c); });
}

media::Then<media::Item> NuvioBackend::stampedItem(media::Then<media::Item> then) {
    return stampThen<media::Item>(then, [this](media::Item& it) { this->applyWatchState(it); });
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
