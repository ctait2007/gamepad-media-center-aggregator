/*
    GMCA — Nuvio watch progress + watched-flag cache (see progress.hpp).
*/

#include "api/nuvio/progress.hpp"
#include "api/nuvio/sync.hpp"
#include "api/stremio/types.hpp"
#include "utils/config.hpp"
#include <algorithm>
#include <borealis/core/logger.hpp>
#include <fmt/format.h>

namespace nuvio {

using media::jint;
using media::jstr;

std::string ratingKeyFor(const std::string& contentId, const std::string& contentType, int64_t season, int64_t episode) {
    if (contentType == "series" && season >= 0 && episode >= 0)
        return "series:" + contentId + ":" + std::to_string(season) + ":" + std::to_string(episode);
    return "movie:" + contentId;
}

namespace {

/// Internal cache/throttle key — identity only, unlike ratingKeyFor (which
/// also encodes the addon-protocol type prefix used for meta resolution).
std::string cacheKey(const std::string& contentId, int64_t season, int64_t episode) {
    return contentId + "#" + std::to_string(season) + "#" + std::to_string(episode);
}

media::Item resolveOne(
    const stremio::Addon& addon, stremio::AddonEngine& engine, const stremio::ParsedId& pid, const std::string& fetchId) {
    std::string url = engine.resourceUrl(addon, "meta", pid.stremioType, fetchId);
    nlohmann::json j;
    try {
        j = stremio::getSync(url);
    } catch (const std::exception& ex) {
        brls::Logger::warning("nuvio resolveMeta {}: {}", url, ex.what());
        return media::Item{};
    }
    auto meta = j.find("meta");
    if (meta == j.end() || !meta->is_object()) return media::Item{};
    media::Item show = stremio::parseMeta(*meta);
    if (pid.season < 0 || pid.episode < 0) return show;
    for (auto& e : stremio::parseEpisodes(*meta, show)) {
        if (e.parentIndex == pid.season && e.index == pid.episode) return e;
    }
    return media::Item{};
}

}  // namespace

media::Item resolveMeta(stremio::AddonEngine& engine, const std::string& ratingKey) {
    stremio::ParsedId pid = stremio::parseId(ratingKey);
    if (pid.stremioType != "movie" && pid.stremioType != "series") return media::Item{};
    // For an episode, the addon fetch target is the SHOW's bare id (baseId);
    // for a movie or a bare show id, baseId == stremioId.
    for (auto& addon : engine.addonsFor("meta", pid.stremioType, pid.baseId)) {
        media::Item item = resolveOne(addon, engine, pid, pid.baseId);
        if (!item.ratingKey.empty()) return item;
    }
    return media::Item{};
}

std::vector<media::Item> resolveEpisodes(stremio::AddonEngine& engine, const std::string& showRatingKey) {
    stremio::ParsedId pid = stremio::parseId(showRatingKey);
    if (pid.stremioType != "series") return {};
    for (auto& addon : engine.addonsFor("meta", "series", pid.stremioId)) {
        std::string url = engine.resourceUrl(addon, "meta", "series", pid.stremioId);
        nlohmann::json j;
        try {
            j = stremio::getSync(url);
        } catch (const std::exception& ex) {
            brls::Logger::warning("nuvio resolveEpisodes {}: {}", url, ex.what());
            continue;
        }
        auto meta = j.find("meta");
        if (meta == j.end() || !meta->is_object()) continue;
        media::Item show = stremio::parseMeta(*meta);
        auto eps = stremio::parseEpisodes(*meta, show);
        if (!eps.empty()) return eps;
    }
    return {};
}

namespace {
int64_t jintOrDefault(const nlohmann::json& j, const char* key, int64_t def) {
    return (j.contains(key) && !j[key].is_null()) ? jint(j, key) : def;
}
}  // namespace

/// How long a pulled snapshot is trusted before it is re-pulled. Short enough
/// that a title marked watched on another device shows up on the next screen
/// the user opens, long enough that browsing does not re-pull on every hop.
constexpr int64_t kStaleSec = 90;

void ProgressStore::ensureLoaded() {
    std::lock_guard<std::mutex> lock(mtx);
    if (loaded && std::chrono::steady_clock::now() - loadedAt < std::chrono::seconds(kStaleSec)) return;

    int profileId = AppConfig::instance().getNuvioProfileIndex();

    try {
        nlohmann::json rows = nuvio::rpc("sync_pull_watch_progress", {{"p_profile_id", profileId}, {"p_limit", 200}});
        progress.clear();
        if (rows.is_array()) {
            for (auto& r : rows) {
                WatchProgressRow row;
                row.contentId = jstr(r, "content_id");
                if (row.contentId.empty()) continue;
                row.contentType = jstr(r, "content_type");
                row.videoId = jstr(r, "video_id");
                row.season = jintOrDefault(r, "season", -1);
                row.episode = jintOrDefault(r, "episode", -1);
                row.positionMs = jint(r, "position");
                row.durationMs = jint(r, "duration");
                row.lastWatched = jint(r, "last_watched");
                progress.push_back(std::move(row));
            }
        }
    } catch (const std::exception& ex) {
        brls::Logger::warning("nuvio: watch progress pull failed: {}", ex.what());
    }

    try {
        nlohmann::json rows = nuvio::rpc(
            "sync_pull_watched_items", {{"p_profile_id", profileId}, {"p_page", 1}, {"p_page_size", 500}});
        watched.clear();
        if (rows.is_array()) {
            for (auto& r : rows) {
                WatchedRow row;
                row.contentId = jstr(r, "content_id");
                if (row.contentId.empty()) continue;
                row.contentType = jstr(r, "content_type");
                row.season = jintOrDefault(r, "season", -1);
                row.episode = jintOrDefault(r, "episode", -1);
                row.watchedAt = jint(r, "watched_at");
                watched.push_back(std::move(row));
            }
        }
    } catch (const std::exception& ex) {
        brls::Logger::warning("nuvio: watched items pull failed: {}", ex.what());
    }

    loaded = true;
    loadedAt = std::chrono::steady_clock::now();
}

void ProgressStore::invalidate() {
    std::lock_guard<std::mutex> lock(mtx);
    loaded = false;
}

std::vector<WatchProgressRow> ProgressStore::continueWatching(size_t limit) {
    std::lock_guard<std::mutex> lock(mtx);

    // ONE ROW PER TITLE, and for a show that means one row for the SHOW rather
    // than one per episode — a series with six part-watched episodes is still
    // one thing you are watching, and the reference collapses it the same way
    // (HomeViewModelContinueWatching groups by contentId). The collapse has to
    // happen BEFORE the watched filter, not after: filtering first would drop
    // the episode you just finished and surface the one before it, walking the
    // row backwards through the season instead of retiring it.
    std::unordered_map<std::string, const WatchProgressRow*> latest;
    for (const WatchProgressRow& row : progress) {
        if (row.positionMs <= 0) continue;
        auto it = latest.find(row.contentId);
        if (it == latest.end() || row.lastWatched > it->second->lastWatched) latest[row.contentId] = &row;
    }

    std::vector<WatchProgressRow> out;
    for (auto& [contentId, row] : latest) {
        (void)contentId;
        bool watchedFlag = false;
        for (auto& w : watched) {
            if (w.contentId == row->contentId && w.season == row->season && w.episode == row->episode) {
                watchedFlag = true;
                break;
            }
        }
        // A finished episode does NOT retire the show: the row carries on to
        // whatever comes after it, at zero progress. Marked here and resolved
        // by the caller, which is the side that can ask an addon what the next
        // episode actually is.
        WatchProgressRow copy = *row;
        copy.finished = watchedFlag;
        out.push_back(std::move(copy));
    }
    std::sort(out.begin(), out.end(),
        [](const WatchProgressRow& a, const WatchProgressRow& b) { return a.lastWatched > b.lastWatched; });
    if (out.size() > limit) out.resize(limit);
    return out;
}

std::optional<WatchProgressRow> ProgressStore::progressFor(
    const std::string& contentId, int64_t season, int64_t episode) {
    std::lock_guard<std::mutex> lock(mtx);
    for (auto& row : progress)
        if (row.contentId == contentId && row.season == season && row.episode == episode) return row;
    return std::nullopt;
}

bool ProgressStore::isWatched(const std::string& contentId, int64_t season, int64_t episode) {
    std::lock_guard<std::mutex> lock(mtx);
    for (auto& w : watched)
        if (w.contentId == contentId && w.season == season && w.episode == episode) return true;
    return false;
}

bool ProgressStore::recentlyPushed(const std::string& contentId, int64_t season, int64_t episode, int64_t withinSec) {
    std::string key = cacheKey(contentId, season, episode);
    std::lock_guard<std::mutex> lock(mtx);
    auto it = lastPushed.find(key);
    if (it == lastPushed.end()) return false;
    return std::chrono::steady_clock::now() - it->second < std::chrono::seconds(withinSec);
}

void ProgressStore::pushProgress(const WatchProgressRow& row) {
    int profileId = AppConfig::instance().getNuvioProfileIndex();
    nlohmann::json entry = {
        {"content_id", row.contentId},
        {"content_type", row.contentType},
        {"video_id", row.videoId},
        {"position", row.positionMs},
        {"duration", row.durationMs},
        {"last_watched", row.lastWatched},
    };
    if (row.season >= 0) entry["season"] = row.season;
    if (row.episode >= 0) entry["episode"] = row.episode;
    // progress_key: the reference sends it on every entry and it is the map key
    // its own store is built on — "<contentId>" for a movie, "<contentId>_s<S>e<E>"
    // for an episode (WatchProgressSyncService.episodeKey). Leaving the server to
    // derive one risked a second row per episode that its delta pull then never
    // matched to ours.
    entry["progress_key"] = (row.season >= 0 && row.episode >= 0)
                                ? fmt::format("{}_s{}e{}", row.contentId, row.season, row.episode)
                                : row.contentId;
    nuvio::rpc("sync_push_watch_progress",
        {{"p_profile_id", profileId}, {"p_entries", nlohmann::json::array({entry})},
            {"p_origin_client_id", nuvio::syncClientId()}});

    // A REWATCH takes the watched flag off. Being partway through something
    // already marked watched means exactly one thing — it is being watched
    // again — and leaving the flag on made Continue Watching answer with the
    // episode after it forever: start series 1 episode 1 again and the row
    // still offered episode 4, because episode 1 counted as finished and the
    // row walked past it. Not applied in the auto-watched zone at the end,
    // which is where the flag legitimately goes ON.
    bool nearEnd = row.durationMs >= 60000 && row.positionMs >= (row.durationMs * 9 / 10);
    if (!nearEnd && row.positionMs > 0 && isWatched(row.contentId, row.season, row.episode)) {
        try {
            clearWatched(row.contentId, row.season, row.episode);
        } catch (const std::exception& ex) {
            brls::Logger::warning("nuvio: could not clear watched on rewatch: {}", ex.what());
        }
    }

    std::lock_guard<std::mutex> lock(mtx);
    lastPushed[cacheKey(row.contentId, row.season, row.episode)] = std::chrono::steady_clock::now();
    bool replaced = false;
    for (auto& existing : progress) {
        if (existing.contentId == row.contentId && existing.season == row.season && existing.episode == row.episode) {
            existing = row;
            replaced = true;
            break;
        }
    }
    if (!replaced) progress.push_back(row);

    // Mirror the server's own auto-watched threshold (>=90% of a >=60s
    // duration, see sync_push_watch_progress) in the local cache immediately,
    // so Continue Watching drops the item without waiting on a full resync.
    if (row.durationMs >= 60000 && row.positionMs >= (row.durationMs * 9 / 10)) {
        bool already = false;
        for (auto& w : watched)
            if (w.contentId == row.contentId && w.season == row.season && w.episode == row.episode) already = true;
        if (!already) watched.push_back({row.contentId, row.contentType, row.season, row.episode, row.lastWatched});
    }
}

void ProgressStore::pushWatched(
    const std::string& contentId, const std::string& contentType, int64_t season, int64_t episode) {
    int profileId = AppConfig::instance().getNuvioProfileIndex();
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                      .count();
    nlohmann::json entry = {{"content_id", contentId}, {"content_type", contentType}, {"title", ""}, {"watched_at", now}};
    // season/episode are ALWAYS present here, explicitly null for a movie, as
    // the reference sends them (WatchedItemsSyncService.pushItemsToRemoteLocked).
    // Omitting a key the row shape declares is not the same as sending null.
    entry["season"] = season >= 0 ? nlohmann::json(season) : nlohmann::json(nullptr);
    entry["episode"] = episode >= 0 ? nlohmann::json(episode) : nlohmann::json(nullptr);
    nuvio::rpc("sync_push_watched_items",
        {{"p_profile_id", profileId}, {"p_items", nlohmann::json::array({entry})},
            {"p_origin_client_id", nuvio::syncClientId()}});

    std::lock_guard<std::mutex> lock(mtx);
    for (auto& w : watched) {
        if (w.contentId == contentId && w.season == season && w.episode == episode) {
            w.watchedAt = now;
            return;
        }
    }
    watched.push_back({contentId, contentType, season, episode, now});
}

void ProgressStore::clearWatched(const std::string& contentId, int64_t season, int64_t episode) {
    int profileId = AppConfig::instance().getNuvioProfileIndex();
    nlohmann::json key = {{"content_id", contentId}};
    if (season >= 0) key["season"] = season;
    if (episode >= 0) key["episode"] = episode;
    nuvio::rpc("sync_delete_watched_items",
        {{"p_profile_id", profileId}, {"p_keys", nlohmann::json::array({key})},
            {"p_origin_client_id", nuvio::syncClientId()}});

    std::lock_guard<std::mutex> lock(mtx);
    watched.erase(std::remove_if(watched.begin(), watched.end(),
                      [&](const WatchedRow& w) {
                          return w.contentId == contentId && w.season == season && w.episode == episode;
                      }),
        watched.end());
}

}  // namespace nuvio
