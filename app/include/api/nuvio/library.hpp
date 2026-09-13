/*
    GMCA — Nuvio account library ("Saved"), the list NuvioTV's Library screen
    shows. See library.cpp.

    Nuvio keeps it in Supabase behind three RPCs, and unlike watch_progress its
    rows carry FULL metadata inline (name/poster/background/description/
    release_info/imdb_rating/genres/addon_base_url), so a row renders a card
    without an addon round trip — the mirror image of ProgressStore, which has
    to resolve everything through /meta.

        sync_pull_library          p_profile_id, p_limit, p_offset
        sync_push_library_items    p_profile_id, p_items[]
        sync_delete_library_items  p_profile_id, p_keys[{content_id, content_type}]

    Cached like ProgressStore: lazily pulled once, updated locally on every
    push so the entry a user just toggled reads back correctly without waiting
    for a resync.
*/

#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "api/media/types.hpp"

namespace nuvio {

struct LibraryRow {
    std::string contentId;
    std::string contentType;  // "movie" | "series"
    std::string name;
    std::string poster;
    std::string background;
    std::string description;
    std::string releaseInfo;
    std::string addonBaseUrl;
    double imdbRating = 0;
    std::vector<std::string> genres;
    int64_t addedAt = 0;  // epoch ms
};

class LibraryStore {
public:
    /// Pulls sync_pull_library exactly once. Thread-safe; call from inside a
    /// backend verb's brls::async body.
    void ensureLoaded();
    void invalidate();

    /// Newest first (added_at descending), as the reference's default sort.
    std::vector<LibraryRow> rows();
    bool contains(const std::string& contentId, const std::string& contentType);

    /// Upserts one row and updates the cache. Synchronous.
    void add(const LibraryRow& row);
    /// Deletes one row and updates the cache. Synchronous.
    void remove(const std::string& contentId, const std::string& contentType);

private:
    std::mutex mtx;
    bool loaded = false;
    std::vector<LibraryRow> list;
};

/// A library row as a card the grid can render and open.
media::Item itemFromLibraryRow(const LibraryRow& row);
/// The other direction, for saving whatever the user was looking at.
LibraryRow libraryRowFromItem(const media::Item& item);

}  // namespace nuvio
