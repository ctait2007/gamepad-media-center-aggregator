/*
    GMCA — the on-device library, and the facade that routes to it.

    NuvioTV's library is LOCAL by default (LibrarySourceMode.LOCAL): a list
    kept on the device, with account sync offered as an option rather than
    required. GMCA had the opposite — the only personal list it knew was the
    backend's own, so an install driven by a plain list of addon URLs (no
    Stremio account) had no library AT ALL: the sidebar tab was removed, and
    the detail pages and long-press menus dropped the entry.

    LocalLibrary is that missing list. `personal::` is the seam every UI call
    site now goes through: the backend's list where there is one (Plex
    watchlist, Jellyfin favorites, a signed-in Stremio account's library), this
    one where there is not, and neither the detail page nor the sheet has to
    know which it got.
*/

#pragma once

#include <borealis/core/singleton.hpp>

#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "api/backend.hpp"

class LocalLibrary : public brls::Singleton<LocalLibrary> {
public:
    LocalLibrary();

    bool contains(const std::string& ratingKey) const;
    /// Newest first; adding something already in the list is a no-op (it does
    /// NOT bump it — the library is a set ordered by when it was saved, unlike
    /// the search history, which is a most-recently-used list).
    void add(const media::Item& item);
    void remove(const std::string& ratingKey);
    /// Newest first, filtered by kind (Any keeps everything).
    std::vector<media::Item> items(media::MediaKind kind) const;

private:
    void save() const;  // caller holds the lock

    mutable std::mutex mutex;
    std::string path;
    std::vector<media::Item> list;  // newest first
};

/// The app's personal list, whichever it turns out to be.
namespace personal {

/// Never None: a backend without a list of its own falls back to the on-device
/// Library, so the tab, the detail-page button and the sheet entry are always
/// there to offer.
media::ListKind kind();
/// Does the LOCAL list back this? (Decides whether a call is a round trip.)
bool isLocal();

bool canList(const media::Item& item);
void state(const media::Item& item, media::Then<bool> then, media::OnError error = nullptr);
void setListed(const media::Item& item, bool add, std::function<void()> then, media::OnError error);
void list(const std::string& sortField, media::MediaKind kind, size_t start, size_t size,
    media::Then<media::Container<media::Item>> then, media::OnError error);

}  // namespace personal
