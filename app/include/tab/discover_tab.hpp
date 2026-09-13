/*
    GMCA — Discover: one browsing page for every catalog, on every addon.

    Addon backends have no libraries — they have catalogs, dozens of them,
    spread across whatever addons are installed. Giving each content type its
    own sidebar tab (Movies / Series) meant two rails' worth of near-identical
    sub-tabs and no way at all to reach a catalog's genres. NuvioTV solves it
    with one screen and three dropdowns (DiscoverScreen.kt /
    SearchDiscoverSection.kt), and this is that screen:

        Type     the content types any catalog serves (Movie / Series)
        Catalog  the catalogs of the chosen type, across every addon
        Genre    the chosen catalog's declared genres, "Default" for none

    Under them the reference prints "addon • type • genre", then the grid.
    Changing any picker refetches from the first page; the grid pages on with
    `skip`, as getLibraryGrid already does for the Home rows' "see all".

    Backends that DO have libraries (Plex, Jellyfin, Emby) return nothing from
    discoverCatalogs() and keep their per-library tabs — this view is never
    built for them.
*/

#pragma once

#include <api/media/types.hpp>
#include <view/auto_tab_frame.hpp>

#include <string>
#include <vector>

class RecyclingGrid;
class DiscoverPicker;

class DiscoverTab : public AttachedView {
public:
    DiscoverTab();

    brls::View* getDefaultFocus() override;

    static brls::View* create();

private:
    /// Rebuild the three pickers' options from `catalogs` and the current
    /// selection, then refetch. `reset` starts the grid over from page 0.
    void refreshFilters();
    void reload();
    void doRequest();

    /// Indices into `catalogs` whose type is the selected one.
    std::vector<size_t> catalogsOfType() const;

    /// Restore / remember the last filter by VALUE (type, catalog key, genre
    /// name) rather than by index, so an addon coming or going cannot move the
    /// selection under the user.
    void restoreSelection();
    void saveSelection() const;

    std::vector<media::DiscoverCatalog> catalogs;
    std::vector<std::string> types;      ///< distinct types, in catalog order
    std::vector<std::string> typeLabels; ///< localized, parallel to `types`
    size_t typeIndex = 0;
    size_t catalogIndex = 0;  ///< index into catalogsOfType()
    int genreIndex = 0;       ///< 0 = "Default" (no genre filter)

    size_t start = 0;
    size_t pageSize = 60;
    /// Bumped on every filter change; a late page from the previous filter
    /// carries a stale token and is dropped instead of being appended to the
    /// new grid.
    uint64_t generation = 0;

    RecyclingGrid* grid = nullptr;
    DiscoverPicker* pickerType = nullptr;
    DiscoverPicker* pickerCatalog = nullptr;
    DiscoverPicker* pickerGenre = nullptr;

    BRLS_BIND(brls::Box, boxFilters, "discover/filters");
    BRLS_BIND(brls::Box, boxGrid, "discover/grid_box");
    BRLS_BIND(brls::Label, labelMeta, "discover/meta");
};
