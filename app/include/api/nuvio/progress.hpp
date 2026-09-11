/*
    GMCA — Nuvio watch progress + watched-flag cache and addon-meta resolution
    for progress rows. See progress.cpp.

    Nuvio's `watch_progress`/`watched_items` rows carry only bare identity
    (content_id/content_type/season/episode) — unlike Stremio's own datastore
    LibraryItem, which snapshots full metadata inline. So turning a row into a
    displayable media::Item means resolving it through the addon protocol
    (resolveMeta/resolveEpisodes below), exactly like StremioBackend's own
    getNextUp does — reusing the same already-public header helpers
    (api/stremio/types.hpp: parseId/parseMeta/parseEpisodes/getSync) rather
    than duplicating them.
*/

#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "api/media/types.hpp"
#include "api/stremio/addons.hpp"

namespace nuvio {

struct WatchProgressRow {
    std::string contentId;
    std::string contentType;  // "movie" | "series"
    std::string videoId;
    int64_t season = -1;
    int64_t episode = -1;
    int64_t positionMs = 0;
    int64_t durationMs = 0;
    int64_t lastWatched = 0;  // epoch ms
};

struct WatchedRow {
    std::string contentId;
    std::string contentType;
    int64_t season = -1;
    int64_t episode = -1;
    int64_t watchedAt = 0;  // epoch ms
};

/// Builds the neutral media:: ratingKey ("movie:{id}" or "series:{id}:{s}:{e}")
/// from a Nuvio row's content_id/content_type/season/episode — the same codec
/// stremio::parseId decodes.
std::string ratingKeyFor(const std::string& contentId, const std::string& contentType, int64_t season, int64_t episode);

/// Resolves a ratingKey to a media::Item by fanning out to every addon whose
/// manifest idPrefixes match (AddonEngine::addonsFor), trying each until one
/// succeeds — same machinery StremioBackend's getNextUp/getItemDetail use.
/// Returns an Item with an empty ratingKey on failure (no matching addon,
/// HTTP/parse error, or — for an episode — that season/episode absent from
/// every addon's meta): the caller skips the row rather than failing the
/// whole list. Synchronous — call from brls::async.
media::Item resolveMeta(stremio::AddonEngine& engine, const std::string& ratingKey);

/// Resolves a bare show ratingKey ("series:{id}") to its full episode list
/// (sorted season, episode), via the same addon fan-out. Empty on failure.
std::vector<media::Item> resolveEpisodes(stremio::AddonEngine& engine, const std::string& showRatingKey);

/// Per-profile watch-progress + watched-flag cache: lazily loaded (like
/// AddonEngine::ensureLoaded) and updated locally on every push, so Continue
/// Watching / Next Up / the watched flag reflect a change immediately without
/// waiting for the next full resync.
class ProgressStore {
public:
    /// Pulls sync_pull_watch_progress + sync_pull_watched_items exactly once.
    /// Thread-safe; called from inside a backend verb's brls::async body.
    void ensureLoaded();
    /// Forces a reload on the next ensureLoaded().
    void invalidate();

    /// In-progress rows (position > 0, not watched), most recent first.
    std::vector<WatchProgressRow> continueWatching(size_t limit);
    std::optional<WatchProgressRow> progressFor(const std::string& contentId, int64_t season, int64_t episode);
    bool isWatched(const std::string& contentId, int64_t season, int64_t episode);

    /// True if a progress push for this content/episode happened less than
    /// `withinSec` ago — throttles the player's 10 s "playing" ticks down to
    /// roughly once every `withinSec`. Pause/stop/exit bypass this (the caller
    /// decides, see NuvioBackend::reportProgress).
    bool recentlyPushed(const std::string& contentId, int64_t season, int64_t episode, int64_t withinSec);

    /// Pushes one progress row (sync_push_watch_progress: the server itself
    /// auto-derives the watched flag at >=90% of a >=60s duration) and updates
    /// the local cache + throttle timestamp. Synchronous — call from brls::async.
    void pushProgress(const WatchProgressRow& row);
    /// Marks watched (sync_push_watched_items) and updates the local cache.
    void pushWatched(const std::string& contentId, const std::string& contentType, int64_t season, int64_t episode);
    /// Clears the watched flag (sync_delete_watched_items) and updates the
    /// local cache.
    void clearWatched(const std::string& contentId, int64_t season, int64_t episode);

private:
    std::mutex mtx;
    bool loaded = false;
    std::vector<WatchProgressRow> progress;
    std::vector<WatchedRow> watched;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastPushed;
};

}  // namespace nuvio
