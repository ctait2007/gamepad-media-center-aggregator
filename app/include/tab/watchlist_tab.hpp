/*
    GMCA — the Library tab, following NuvioTV's Library screen.

    The account's saved movies and shows: Plex's watchlist, Jellyfin/Emby's
    favorites, a Nuvio or Stremio account's library, or the on-device list when
    the backend has none (utils/local_library.hpp decides which — this screen
    only ever asks `personal::`).

    Filtering is entirely CLIENT-side and the whole list is fetched once, which
    is what the reference does and what these filters need: Genre and Year are
    built from what the library actually contains, so they cannot be asked of a
    server a page at a time. Five dropdowns, as in the reference:

        Type      All / Movies / Series
        Sort      Added ↓ / Added ↑ / Title A–Z / Title Z–A
        Genre     All + every genre present
        Year      All + every release year present
        Watched   All / Watched / Unwatched
*/

#pragma once

#include <string>
#include <vector>

#include <api/plex/types.hpp>
#include <view/auto_tab_frame.hpp>

class RecyclingGrid;
class DiscoverPicker;

class WatchlistTab : public AttachedView {
public:
    WatchlistTab();

    void onCreate() override;

    brls::View* getDefaultFocus() override;

    static brls::View* create();

private:
    BRLS_BIND(brls::Label, labelTitle, "library/title");
    BRLS_BIND(brls::Label, labelBrand, "library/brand");
    BRLS_BIND(brls::Box, boxTop, "library/filters/top");
    BRLS_BIND(brls::Box, boxBottom, "library/filters/bottom");
    BRLS_BIND(brls::Box, boxGrid, "library/grid_box");

    /// Pulls the whole list, then rebuilds the filters from it.
    void reload();
    /// Recomputes the Genre and Year option lists from `all` and repaints
    /// every picker's current value.
    void refreshFilters();
    /// Applies the five filters and the sort, and hands the result to the grid.
    void applyFilters();

    RecyclingGrid* grid = nullptr;
    DiscoverPicker* pickerType = nullptr;
    DiscoverPicker* pickerSort = nullptr;
    DiscoverPicker* pickerGenre = nullptr;
    DiscoverPicker* pickerYear = nullptr;
    DiscoverPicker* pickerWatched = nullptr;

    std::vector<plex::Item> all;
    std::vector<std::string> genres;  // "" (All) first
    std::vector<int64_t> years;       // 0 (All) first

    int typeIndex = 0;
    int sortIndex = 0;
    int genreIndex = 0;
    int yearIndex = 0;
    int watchedIndex = 0;
    bool loading = false;
};
