/*
    GMCA — Nuvio implementation of media::Backend.

    Nuvio speaks the Stremio addon protocol, so every navigation, catalog,
    item-detail, playback and subtitle verb below just forwards to an internal
    stremio::StremioBackend — reused UNCHANGED, per MULTI_BACKEND.md. Only the
    account layer differs: sign-in, profile selection, addon-list resync, and
    watch progress/history go through Nuvio's Supabase backend
    (api/nuvio/auth.hpp, api/nuvio/sync.hpp, api/nuvio/progress.hpp) instead of
    api.strem.io. See tab/nuvio_add.cpp for sign-in + profile pick.

    Account state: Continue Watching, progress-aware Next Up (resume an
    in-progress episode, advance to the next fresh one, or Replay once the
    whole show is finished), mark watched/unwatched — backed by ProgressStore
    (watch_progress + watched_items) — and the account LIBRARY, backed by
    LibraryStore (sync_pull_library / sync_push_library_items /
    sync_delete_library_items). Both are the account's, not the device's: an
    item saved here shows up in NuvioTV, and one saved there shows up here.
*/

#pragma once

#include "api/backend.hpp"
#include "api/nuvio/library.hpp"
#include "api/nuvio/progress.hpp"
#include "api/stremio/backend.hpp"

namespace nuvio {

class NuvioBackend : public media::Backend {
public:
    NuvioBackend();

    media::BackendType type() const override { return media::BackendType::Nuvio; }
    const media::Capabilities& caps() const override { return caps_; }

    // ---- navigation: unchanged, delegated to the Stremio addon engine --------
    void listSections(media::Then<media::Container<media::Section>> then, media::OnError error) override {
        delegate.listSections(then, error);
    }
    std::vector<std::pair<std::string, std::string>> sectionTabs(const std::string& sectionId) override {
        return delegate.sectionTabs(sectionId);
    }
    std::vector<media::DiscoverCatalog> discoverCatalogs() override { return delegate.discoverCatalogs(); }
    void searchHubs(const std::string& query, media::Then<media::Container<media::Hub>> then,
        media::OnError error) override {
        delegate.searchHubs(query, then, error);
    }
    void getHomeHubs(int count, bool excludeContinueWatching, media::Then<media::Container<media::Hub>> then,
        media::OnError error) override {
        delegate.getHomeHubs(count, excludeContinueWatching, then, error);
    }
    void getSectionHubs(const std::string& sectionId, int count, media::Then<media::Container<media::Hub>> then,
        media::OnError error) override {
        delegate.getSectionHubs(sectionId, count, then, error);
    }
    // Nuvio-specific: built from ProgressStore (watch_progress), not
    // delegated — the delegate's own implementation calls Stremio's account
    // datastore, which a Nuvio connection has no valid token for. See backend.cpp.
    void getContinueWatching(int count, media::Then<media::Container<media::Hub>> then, media::OnError error) override;
    void getLibraryGrid(const std::string& sectionId, const media::GridQuery& q, size_t start, size_t size,
        media::Then<media::Container<media::Item>> then, media::OnError error) override {
        delegate.getLibraryGrid(sectionId, q, start, size, then, error);
    }
    void getCollectionChildren(const std::string& collectionId, size_t start, size_t size,
        media::Then<media::Container<media::Item>> then, media::OnError error) override {
        delegate.getCollectionChildren(collectionId, start, size, then, error);
    }
    void getHubPage(const std::string& hubKey, size_t start, size_t size, media::Then<media::Container<media::Item>> then,
        media::OnError error) override {
        delegate.getHubPage(hubKey, start, size, then, error);
    }
    void getItemDetail(const std::string& id, bool full, media::Then<media::Item> then, media::OnError error) override {
        delegate.getItemDetail(id, full, then, error);
    }
    // Nuvio-specific ONLY in that the delegate's episodes come back from an
    // addon, which knows nothing about what this account has watched — the
    // watched flag is stamped on afterwards (see backend.cpp). Without it
    // marking an episode watched changed nothing anyone could see.
    void getAllEpisodes(const std::string& showId, bool includeStreams, media::Then<media::Container<media::Item>> then,
        media::OnError error) override;
    void getChildren(const std::string& id, media::Then<media::Container<media::Item>> then, media::OnError error) override;
    // Nuvio-specific: progress-aware (see backend.cpp) — the delegate's own
    // implementation is a stage-1 Stremio stub that always offers episode 1.
    void getNextUp(
        const std::string& showId, std::function<void(media::Item, bool)> then, media::OnError error) override;
    void getExtras(const std::string& id, media::Then<media::Container<media::Item>> then, media::OnError error) override {
        delegate.getExtras(id, then, error);
    }
    void getRelated(const std::string& id, int count, media::Then<media::Container<media::Hub>> then,
        media::OnError error) override {
        delegate.getRelated(id, count, then, error);
    }
    void getPersonMedia(const std::string& personId, int count, media::Then<media::Container<media::Item>> then,
        media::OnError error) override {
        delegate.getPersonMedia(personId, count, then, error);
    }
    void search(const std::string& query, media::MediaKind kind, int limit, media::Then<media::Container<media::Item>> then,
        media::OnError error) override {
        delegate.search(query, kind, limit, then, error);
    }
    void getRecentlyAdded(size_t start, size_t size, media::Then<media::Container<media::Item>> then,
        media::OnError error) override {
        delegate.getRecentlyAdded(start, size, then, error);
    }
    void getGenres(const std::string& sectionId, media::MediaKind kind, media::Then<media::Container<media::Section>> then,
        media::OnError error) override {
        delegate.getGenres(sectionId, kind, then, error);
    }
    void getCollections(const std::string& sectionId, size_t start, size_t size,
        media::Then<media::Container<media::Item>> then, media::OnError error) override {
        delegate.getCollections(sectionId, start, size, then, error);
    }
    void getPlaylists(size_t start, size_t size, media::Then<media::Container<media::Item>> then,
        media::OnError error) override {
        delegate.getPlaylists(start, size, then, error);
    }
    void getPlaylistItems(const std::string& playlistId, size_t start, size_t size,
        media::Then<media::Container<media::Item>> then, media::OnError error) override {
        delegate.getPlaylistItems(playlistId, start, size, then, error);
    }

    // ---- item actions: watched_items sync (see backend.cpp) -------------------
    void markWatched(const std::string& id) override;
    void markUnwatched(const std::string& id) override;

    // ---- library: the account's "Saved" list (see library.hpp) ----------------
    bool canList(const media::Item& item) const override;
    void listWatchlist(const std::string& sortField, media::MediaKind kind, size_t start, size_t size,
        media::Then<media::Container<media::Item>> then, media::OnError error) override;
    void getWatchlistState(const media::Item& item, media::Then<bool> then, media::OnError error) override;
    void setWatchlisted(
        const media::Item& item, bool add, std::function<void()> then, media::OnError error) override;

    // ---- playback: unchanged, delegated ---------------------------------------
    media::PlaybackSource resolvePlayback(
        const media::Item& item, const media::Media& version, const media::PlaybackOptions& opts) override {
        return delegate.resolvePlayback(item, version, opts);
    }
    std::string subtitleSidecarUrl(const std::string& streamKey) const override {
        return delegate.subtitleSidecarUrl(streamKey);
    }
    void getSubtitles(const media::Item& item, media::Then<std::vector<media::Stream>> then, media::OnError error) override {
        delegate.getSubtitles(item, then, error);
    }
    // Nuvio-branded wording (the delegate's own hint says "your Stremio
    // account" — wrong here), see backend.cpp.
    std::string subtitleMenuHint() const override;
    // Nuvio-specific: pushes to watch_progress, throttled (see backend.cpp).
    void reportProgress(const std::string& id, media::PlayState state, int64_t posMs, int64_t durMs,
        const std::string& sessionId) override;

    // ---- url helpers: unchanged, delegated ------------------------------------
    std::string imageUrl(const std::string& path, int width = 0, int height = 0) const override {
        return delegate.imageUrl(path, width, height);
    }
    std::string downloadUrl(const std::string& partKey) const override { return delegate.downloadUrl(partKey); }
    HTTP::Header authHeaders() const override { return delegate.authHeaders(); }

private:
    /// Stamps viewCount/viewOffset from the ProgressStore onto items an addon
    /// returned, so watched badges and resume bars reflect this account.
    void applyWatchState(media::Container<media::Item>& c);

    media::Capabilities caps_;
    stremio::StremioBackend delegate;
    ProgressStore progressStore;
    LibraryStore libraryStore;
};

}  // namespace nuvio
