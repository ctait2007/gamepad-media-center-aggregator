/*
    GMCA — the Library tab (see watchlist_tab.hpp).
*/

#include "tab/watchlist_tab.hpp"

#include <algorithm>
#include <set>

#include "api/backend.hpp"
#include "utils/keybind.hpp"
#include "utils/local_library.hpp"
#include "utils/network_state.hpp"
#include "view/discover_picker.hpp"
#include "view/recycling_grid.hpp"
#include "view/video_card.hpp"
#include "view/video_source.hpp"

using namespace brls::literals;  // for _i18n

namespace {

/// The whole library in one request: every filter below is client-side and two
/// of them are built FROM the list, so a page at a time would mean a Genre
/// dropdown that grows as you scroll.
constexpr size_t kFetchAll = 1000;

enum Sort { SORT_ADDED_DESC = 0, SORT_ADDED_ASC, SORT_TITLE_AZ, SORT_TITLE_ZA };
enum Watched { WATCHED_ALL = 0, WATCHED_YES, WATCHED_NO };
enum Type { TYPE_ALL = 0, TYPE_MOVIE, TYPE_SHOW };

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

}  // namespace

WatchlistTab::WatchlistTab() {
    this->inflateFromXMLRes("xml/tabs/watchlist.xml");
    brls::Logger::debug("WatchlistTab: create");

    this->labelTitle->setText(media::listI18n(personal::kind(), "title"));
    // The reference prints its own wordmark opposite the title; ours names the
    // account the list belongs to, which is the same information for a Nuvio
    // connection and honest for every other one.
    const std::string& sid = AppConfig::instance().getUser().server_id;
    for (const AppServer& srv : AppConfig::instance().getServers()) {
        if (srv.id != sid) continue;
        this->labelBrand->setText(srv.name);
        break;
    }

    struct Spec {
        const char* caption;
        DiscoverPicker** slot;
        brls::Box* row;
        bool wide;
    };
    const Spec specs[] = {
        {"main/library/filter/type", &this->pickerType, this->boxTop.getView(), true},
        {"main/library/filter/sort", &this->pickerSort, this->boxTop.getView(), true},
        {"main/library/filter/genre", &this->pickerGenre, this->boxBottom.getView(), false},
        {"main/library/filter/year", &this->pickerYear, this->boxBottom.getView(), false},
        {"main/library/filter/watched", &this->pickerWatched, this->boxBottom.getView(), false},
    };
    for (const Spec& spec : specs) {
        auto* p = new DiscoverPicker(brls::getStr(spec.caption));
        p->setGrow(1);
        p->setWidth(brls::View::AUTO);
        if (!spec.row->getChildren().empty()) p->setMarginLeft(24);  // spacing.md
        spec.row->addView(p);
        *spec.slot = p;
    }

    this->pickerType->onSelect([this](int picked) {
        this->typeIndex = picked;
        // Genre and Year list what the FILTERED set contains, so narrowing the
        // type can strand a selection that no longer exists; start them over.
        this->genreIndex = this->yearIndex = 0;
        this->refreshFilters();
        this->applyFilters();
    });
    this->pickerSort->onSelect([this](int picked) {
        this->sortIndex = picked;
        this->refreshFilters();
        this->applyFilters();
    });
    this->pickerGenre->onSelect([this](int picked) {
        this->genreIndex = picked;
        this->refreshFilters();
        this->applyFilters();
    });
    this->pickerYear->onSelect([this](int picked) {
        this->yearIndex = picked;
        this->refreshFilters();
        this->applyFilters();
    });
    this->pickerWatched->onSelect([this](int picked) {
        this->watchedIndex = picked;
        this->refreshFilters();
        this->applyFilters();
    });

    this->grid = new RecyclingGrid();
    this->grid->setGrow(1.f);
    this->grid->registerCell("Cell", VideoCardCell::create);
    this->grid->spanCount = 6;
    this->grid->itemImageRatio = 1.5f;
    this->grid->itemExtraHeight = AppConfig::instance().getItem(AppConfig::POSTER_LABELS, false) ? brls::getStyle()["app/card/labels"] : 0;
    // paddingTop: the top row's focus ring is drawn ~5px outside its frame, so
    // flush under the filter row it came out clipped (same reason Discover's
    // grid carries one).
    this->grid->setPadding(16, 0, brls::getStyle()["main/content_padding_top_bottom"], 0);
    this->boxGrid->addView(this->grid);
}

brls::View* WatchlistTab::create() { return new WatchlistTab(); }

brls::View* WatchlistTab::getDefaultFocus() { return this->pickerType; }

void WatchlistTab::onCreate() {
    // Triangle, as everywhere else in the app. It had drifted back to
    // BUTTON_BACK — the TOUCHPAD on a DualShock, which the hint bar draws as a
    // bare "S" and nobody would think to press.
    auto actionRefresh = [this](...) {
        this->reload();
        return true;
    };
    this->registerAction("hints/refresh"_i18n, brls::BUTTON_Y, actionRefresh);
    this->registerAction(KeyBind::getRefresh(), actionRefresh);

    this->refreshFilters();
    this->reload();
}

void WatchlistTab::reload() {
    if (this->loading) return;
    if (NetworkState::isOffline() && !personal::isLocal()) {
        this->grid->setEmpty(
            "main/download/offline_title"_i18n, "main/download/offline_section"_i18n, "icon/ico-cloud.svg");
        return;
    }
    this->loading = true;
    this->grid->showSkeleton();

    ASYNC_RETAIN
    personal::list("", media::MediaKind::Any, 0, kFetchAll,
        [ASYNC_TOKEN](const media::Container<media::Item>& r) {
            ASYNC_RELEASE
            this->loading = false;
            this->all = r.Items;
            this->refreshFilters();
            this->applyFilters();
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->loading = false;
            this->grid->setError(ex);
        });
}

void WatchlistTab::refreshFilters() {
    // main/media/genres/* does not exist, so these two printed their own key
    // back at the user. The strings that do exist are the sidebar's.
    this->pickerType->setOptions(
        {"main/library/type/all"_i18n, "main/stremio/movies"_i18n, "main/stremio/series"_i18n}, this->typeIndex);
    this->pickerSort->setOptions({"main/library/sort/added_desc"_i18n, "main/library/sort/added_asc"_i18n,
                                     "main/library/sort/title_az"_i18n, "main/library/sort/title_za"_i18n},
        this->sortIndex);
    this->pickerWatched->setOptions(
        {"main/library/type/all"_i18n, "main/library/watched/yes"_i18n, "main/library/watched/no"_i18n},
        this->watchedIndex);

    // Genre and Year list only what is actually there, and only within the
    // current Type — a Genre nothing matches is a dead option.
    std::set<std::string> genreSet;
    std::set<int64_t> yearSet;
    for (const plex::Item& it : this->all) {
        if (this->typeIndex == TYPE_MOVIE && it.type != plex::mediaTypeMovie) continue;
        if (this->typeIndex == TYPE_SHOW && it.type != plex::mediaTypeShow) continue;
        for (const std::string& g : it.genres)
            if (!g.empty()) genreSet.insert(g);
        if (it.year > 0) yearSet.insert(it.year);
    }

    this->genres.assign(1, "");
    this->genres.insert(this->genres.end(), genreSet.begin(), genreSet.end());
    std::vector<std::string> genreLabels{"main/library/type/all"_i18n};
    for (size_t i = 1; i < this->genres.size(); i++) genreLabels.push_back(this->genres[i]);
    if (this->genreIndex >= (int)this->genres.size()) this->genreIndex = 0;
    this->pickerGenre->setOptions(genreLabels, this->genreIndex);

    this->years.assign(1, 0);
    this->years.insert(this->years.end(), yearSet.rbegin(), yearSet.rend());  // newest first
    std::vector<std::string> yearLabels{"main/library/type/all"_i18n};
    for (size_t i = 1; i < this->years.size(); i++) yearLabels.push_back(std::to_string(this->years[i]));
    if (this->yearIndex >= (int)this->years.size()) this->yearIndex = 0;
    this->pickerYear->setOptions(yearLabels, this->yearIndex);
}

void WatchlistTab::applyFilters() {
    std::vector<plex::Item> out;
    for (const plex::Item& it : this->all) {
        if (this->typeIndex == TYPE_MOVIE && it.type != plex::mediaTypeMovie) continue;
        if (this->typeIndex == TYPE_SHOW && it.type != plex::mediaTypeShow) continue;
        if (this->watchedIndex == WATCHED_YES && !it.played()) continue;
        if (this->watchedIndex == WATCHED_NO && it.played()) continue;
        if (this->genreIndex > 0 && (size_t)this->genreIndex < this->genres.size()) {
            const std::string& want = this->genres[this->genreIndex];
            if (std::find(it.genres.begin(), it.genres.end(), want) == it.genres.end()) continue;
        }
        if (this->yearIndex > 0 && (size_t)this->yearIndex < this->years.size()) {
            if (it.year != this->years[this->yearIndex]) continue;
        }
        out.push_back(it);
    }

    switch (this->sortIndex) {
        case SORT_ADDED_ASC:
            std::stable_sort(out.begin(), out.end(),
                [](const plex::Item& a, const plex::Item& b) { return a.addedAt < b.addedAt; });
            break;
        case SORT_TITLE_AZ:
            std::stable_sort(out.begin(), out.end(),
                [](const plex::Item& a, const plex::Item& b) { return lower(a.title) < lower(b.title); });
            break;
        case SORT_TITLE_ZA:
            std::stable_sort(out.begin(), out.end(),
                [](const plex::Item& a, const plex::Item& b) { return lower(b.title) < lower(a.title); });
            break;
        case SORT_ADDED_DESC:
        default:
            std::stable_sort(out.begin(), out.end(),
                [](const plex::Item& a, const plex::Item& b) { return a.addedAt > b.addedAt; });
            break;
    }

    if (out.empty()) {
        auto kind = personal::kind();
        // An empty LIBRARY and an empty FILTER are different problems, and
        // "nothing saved yet" would be a lie about the second.
        bool filtered = !this->all.empty();
        this->grid->setEmpty(filtered ? "main/library/no_matches"_i18n : media::listI18n(kind, "empty_title"),
            filtered ? "main/library/no_matches_sub"_i18n : media::listI18n(kind, "empty_sub"),
            "icon/ico-bookmark.svg");
        return;
    }
    this->grid->setDataSource(new VideoDataSource(out));
}
