/*
    GMCA — Nuvio implementation of media::Backend (see nuvio/backend.hpp).
*/

#include "api/nuvio/backend.hpp"
#include "api/nuvio/sync.hpp"
#include <borealis/core/i18n.hpp>

using namespace brls::literals;

namespace nuvio {

NuvioBackend::NuvioBackend() {
    // Replace the delegate's default (Stremio account) addon resync with
    // Nuvio's own (the `addons` table) — see AddonEngine::resyncAddons.
    delegate.addonEngine().resyncAddons = []() { nuvio::resyncAddons(); };

    // Browsable catalogs + composed home rows + ratings, always on (same as
    // Stremio's addon-protocol navigation). The account-backed features
    // (library/watched-flag/progress sync) are stage 2 — off for now.
    caps_.sections = true;
    caps_.homeHubs = true;
    caps_.continueWatching = false;  // stage 2: watch_progress sync
    caps_.serverSort = false;
    caps_.serverFilter = false;
    caps_.genres = true;
    caps_.collections = false;
    caps_.playlists = false;
    caps_.related = false;
    caps_.personPages = false;
    caps_.globalSearch = false;
    caps_.recentlyAdded = false;
    caps_.markWatched = false;              // stage 2: watched_items sync
    caps_.listKind = media::ListKind::None;  // stage 2: library sync -> Watchlist
    caps_.ratings = true;
    caps_.skipIntro = false;
    caps_.transcode = false;
    caps_.serverProgress = false;  // stage 2
    caps_.downloadOriginal = false;
    caps_.multiProfile = true;  // Nuvio profile picker, see tab/nuvio_add.cpp
}

std::string NuvioBackend::subtitleMenuHint() const { return "main/nuvio/subtitle/none"_i18n; }

}  // namespace nuvio
