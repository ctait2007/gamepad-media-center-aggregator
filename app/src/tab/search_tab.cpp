/*
    GMCA — Search (see search_tab.hpp).
*/

#include "tab/search_tab.hpp"
#include "view/recycling_grid.hpp"
#include "view/svg_image.hpp"
#include "view/video_source.hpp"
#include "view/video_card.hpp"
#include "view/recyling_video.hpp"
#include "utils/dialog.hpp"
#include "utils/keybind.hpp"
#include "utils/network_state.hpp"
#include "utils/offline_library.hpp"
#include "api/plex.hpp"
#include "api/backend.hpp"
#include <algorithm>
#include <vector>
#include <fstream>

using namespace brls::literals;  // for _i18n

namespace {

/// Shortest query worth firing a search for — MIN_SEARCH_QUERY_LENGTH in the
/// reference's SearchUiState.
constexpr size_t kMinQuery = 2;
/// The reference keeps eight (MAX_RECENT_SEARCHES).
constexpr size_t kMaxRecent = 8;
/// LIVE_SEARCH_DEBOUNCE_MS. Each run fans out across every addon catalog.
constexpr int kDebounceMs = 350;

}  // namespace

/// Persistent history (search.json): a JSON array, newest first, deduped.
class SearchHistory {
public:
    SearchHistory() {
        this->path = AppConfig::instance().configDir() + "/search.json";
        std::ifstream readFile(this->path);
        if (readFile.is_open()) {
            try {
                this->list = nlohmann::json::parse(readFile);
            } catch (const std::exception& e) {
                brls::Logger::error("load search history: {}", e.what());
            }
        }
        if (this->list.size() > kMaxRecent) this->list.resize(kMaxRecent);
    }

    const std::vector<std::string>& items() const { return this->list; }

    /// Newest first, and searching something already in the list MOVES it to
    /// the top rather than leaving it where it was (saveRecentSearch does the
    /// same) — the list is a most-recently-used list, not a set.
    void append(const std::string& searchTerm) {
        auto it = std::find(this->list.begin(), this->list.end(), searchTerm);
        if (it != this->list.end()) {
            if (it == this->list.begin()) return;
            this->list.erase(it);
        }
        this->list.insert(this->list.begin(), searchTerm);
        if (this->list.size() > kMaxRecent) this->list.resize(kMaxRecent);
        this->save();
    }

    void remove(const std::string& searchTerm) {
        auto it = std::find(this->list.begin(), this->list.end(), searchTerm);
        if (it == this->list.end()) return;
        this->list.erase(it);
        this->save();
    }

    void clear() {
        this->list.clear();
        this->save();
    }

private:
    void save() {
        std::ofstream writeFile(this->path);
        if (writeFile.is_open()) {
            nlohmann::json j(this->list);
            writeFile << j.dump(2);
            writeFile.close();
        }
    }

    std::string path;
    std::vector<std::string> list;
};

SearchTab::SearchTab() {
    this->inflateFromXMLRes("xml/tabs/search_tv.xml");
    brls::Logger::debug("SearchTab: create");

    this->history = std::make_unique<SearchHistory>();

    // the field and the two chrome buttons carry their own fill/border, drawn
    // under the focus halo rather than replaced by it
    for (brls::Box* b : {this->fieldBox.getView(), this->clearButton.getView()}) {
        b->setHideHighlightBackground(true);
    }
    this->styleField();
    this->clearHistory->setBackgroundColor(brls::Application::getTheme().getColor("color/pill"));

    this->fieldBox->registerClickAction([this](brls::View*) {
        this->openKeyboard();
        return true;
    });
    this->fieldBox->addGestureRecognizer(new brls::TapGestureRecognizer(this->fieldBox));

    this->clearButton->registerClickAction([this](brls::View*) {
        // clearing drops this button from the row, so hand focus back first
        brls::Application::giveFocus(this->fieldBox);
        this->setQuery("", false);
        return true;
    });
    this->clearButton->addGestureRecognizer(new brls::TapGestureRecognizer(this->clearButton));

    this->clearHistory->registerClickAction([this](brls::View*) {
        Dialog::cancelable("main/search/clear_history"_i18n, [this]() {
            this->history->clear();
            brls::Application::giveFocus(this->fieldBox);
            this->buildRecent();
        });
        return true;
    });
    this->clearHistory->addGestureRecognizer(new brls::TapGestureRecognizer(this->clearHistory));

    this->results->registerCell("Cell", VideoCardCell::create);
    // the row budgets for the title block only when Layout > poster titles is on
    this->results->itemExtraHeight = AppConfig::instance().getItem(AppConfig::POSTER_LABELS, false) ? 55 : 0;
}

SearchTab::~SearchTab() { brls::Logger::debug("SearchTab: deleted"); }

brls::View* SearchTab::create() { return new SearchTab(); }

brls::View* SearchTab::getDefaultFocus() { return this->fieldBox; }

void SearchTab::onCreate() {
    // + / START opens the keyboard from anywhere in the tab
    this->registerAction("main/tabs/search"_i18n, brls::BUTTON_START, [this](...) {
        this->openKeyboard();
        return true;
    });
    this->setQuery(this->currentSearch, false);
}

void SearchTab::openKeyboard() {
    brls::Application::getImeManager()->openForText(
        [this](const std::string& text) { this->setQuery(text, true); }, "main/search/hint"_i18n, "", 64,
        this->currentSearch, 0);
}

/// Exactly one of the three panes is up at any moment; the field's DOWN route
/// follows whichever it is, otherwise it would point into a hidden subtree.
void SearchTab::showPane(brls::View* visible) {
    for (brls::View* v : {(brls::View*)this->recentScroll.getView(), (brls::View*)this->rowsScroll.getView(),
             (brls::View*)this->results.getView()}) {
        v->setVisibility(v == visible ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }
    if (visible != this->recentScroll.getView())
        this->fieldBox->setCustomNavigationRoute(brls::FocusDirection::DOWN, visible);
}

void SearchTab::styleField() {
    auto theme = brls::Application::getTheme();
    for (brls::Box* b : {this->fieldBox.getView(), this->clearButton.getView()}) {
        b->setBackgroundColor(theme.getColor("color/pill"));
        b->setBorderColor(theme.getColor("color/grey_2"));
    }
    if (this->currentSearch.empty()) {
        this->inputLabel->setText("main/search/placeholder"_i18n);
        this->inputLabel->setTextColor(theme.getColor("font/grey"));
    } else {
        this->inputLabel->setText(this->currentSearch);
        this->inputLabel->setTextColor(theme.getColor("brls/text"));
    }
    this->clearButton->setVisibility(
        this->currentSearch.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
}

void SearchTab::setQuery(const std::string& query, bool remember) {
    this->currentSearch = query;
    this->styleField();

    // trim for the length test, exactly as the reference does
    std::string trimmed = query;
    trimmed.erase(0, trimmed.find_first_not_of(" \t"));
    size_t end = trimmed.find_last_not_of(" \t");
    trimmed.erase(end == std::string::npos ? 0 : end + 1);

    uint64_t gen = ++this->generation;

    if (trimmed.size() < kMinQuery) {
        this->showPane(this->recentScroll.getView());
        this->buildRecent();
        return;
    }

    if (remember) {
        this->history->append(trimmed);
        this->buildRecent();
    }
    // the grid shows the skeleton while the fan-out runs; if the backend comes
    // back with per-catalog rows it takes over from there
    this->showPane(this->results.getView());
    this->results->showSkeleton();

    // Debounced so a query that changes again before it fires costs nothing.
    ASYNC_RETAIN
    brls::delay(kDebounceMs, [ASYNC_TOKEN, trimmed, gen]() {
        ASYNC_RELEASE
        if (gen != this->generation) return;
        this->doSearch(trimmed);
    });
}

/// One recent-search row: the query as a wide button, its remove button beside
/// it. The reference lays them out exactly so, remove on the trailing edge.
void SearchTab::buildRecent() {
    auto theme = brls::Application::getTheme();

    // never leave focus on a view we are about to destroy
    brls::View* focus = brls::Application::getCurrentFocus();
    for (brls::View* v = focus; v != nullptr; v = v->getParent()) {
        if (v == this->recentList.getView()) {
            brls::Application::giveFocus(this->fieldBox);
            break;
        }
    }
    this->recentList->clearViews();

    const auto items = this->history->items();  // copy: the row callbacks mutate it
    bool any = !items.empty();
    this->recentHeader->setVisibility(any ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    this->recentEmpty->setVisibility(any ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    if (!any) {
        // nothing below the field to reach; a self-route is a silent no-op
        // where a stale one would point at a destroyed row
        this->fieldBox->setCustomNavigationRoute(brls::FocusDirection::DOWN, this->fieldBox.getView());
        return;
    }

    std::vector<brls::Box*> queries, removes;

    for (const std::string& term : items) {
        auto* row = new brls::Box();
        row->setAxis(brls::Axis::ROW);
        row->setAlignItems(brls::AlignItems::CENTER);
        row->setWidthPercentage(100);
        row->setMarginBottom(20);

        auto* query = new brls::Box();
        query->setGrow(1);
        query->setHeight(96);
        query->setAlignItems(brls::AlignItems::CENTER);
        query->setPaddingLeft(32);
        query->setPaddingRight(32);
        query->setCornerRadius(24);
        query->setHighlightCornerRadius(28);
        query->setBackgroundColor(theme.getColor("color/pill"));
        query->setFocusable(true);
        query->setHideHighlightBackground(true);
        auto* label = new brls::Label();
        label->setText(term);
        label->setFontSize(32);
        label->setSingleLine(true);
        query->addView(label);
        query->registerClickAction([this, term](brls::View*) {
            // re-running bumps it back to the top of the list
            this->setQuery(term, true);
            return true;
        });
        query->addGestureRecognizer(new brls::TapGestureRecognizer(query));
        row->addView(query);
        queries.push_back(query);

        auto* remove = new brls::Box();
        remove->setDimensions(96, 96);
        remove->setMarginLeft(24);
        remove->setAlignItems(brls::AlignItems::CENTER);
        remove->setJustifyContent(brls::JustifyContent::CENTER);
        remove->setCornerRadius(24);
        remove->setHighlightCornerRadius(28);
        remove->setBackgroundColor(theme.getColor("color/pill"));
        remove->setFocusable(true);
        remove->setHideHighlightBackground(true);
        auto* glyph = new SVGImage();
        glyph->setDimensions(32, 32);
        glyph->setImageFromSVGRes("icon/ico-close.svg");
        remove->addView(glyph);
        remove->registerAction(
            "main/search/remove"_i18n, brls::BUTTON_A,
            [this, term](brls::View*) {
                this->history->remove(term);
                // the row is about to be destroyed: rebuild on the next frame
                // so the click is not still unwinding through the dead view
                brls::sync([this]() { this->buildRecent(); });
                return true;
            },
            false, false, brls::SOUND_CLICK);
        remove->addGestureRecognizer(new brls::TapGestureRecognizer(remove));
        row->addView(remove);
        removes.push_back(remove);

        this->recentList->addView(row);
    }

    // Two columns that navigate independently, as the reference wires them:
    // down the queries on the left, down the remove buttons on the right, and
    // left/right to cross between the two (that part falls out of the row's
    // own child order). Without this, Down from the field landed on "Clear
    // history" — the first focusable in reading order — and Up from a remove
    // button jumped back into the query column.
    this->fieldBox->setCustomNavigationRoute(brls::FocusDirection::DOWN, queries.front());
    this->clearHistory->setCustomNavigationRoute(brls::FocusDirection::DOWN, queries.front());
    queries.front()->setCustomNavigationRoute(brls::FocusDirection::UP, this->fieldBox.getView());
    for (size_t i = 0; i < removes.size(); i++) {
        removes[i]->setCustomNavigationRoute(
            brls::FocusDirection::UP, i == 0 ? this->clearHistory.getView() : removes[i - 1]);
        removes[i]->setCustomNavigationRoute(
            brls::FocusDirection::DOWN, i + 1 < removes.size() ? removes[i + 1] : removes[i]);
    }
}

void SearchTab::doSearch(const std::string& searchTerm) {
    // offline: the local catalog has no addons to group by
    if (NetworkState::isOffline()) {
        this->doFlatSearch(searchTerm);
        return;
    }

    uint64_t gen = this->generation;
    std::string term = searchTerm;
    ASYNC_RETAIN
    AppConfig::instance().backend().searchHubs(
        searchTerm,
        [ASYNC_TOKEN, gen, term](const media::Container<media::Hub>& r) {
            ASYNC_RELEASE
            if (gen != this->generation) return;
            // No grouping on offer (Plex/Jellyfin/Emby, or no addon catalog
            // advertises `search`): the flat grid is the right answer.
            if (r.Items.empty()) {
                this->doFlatSearch(term);
                return;
            }
            this->showHubs(r.Items);
        },
        [ASYNC_TOKEN, gen, term](const std::string& ex) {
            ASYNC_RELEASE
            if (gen != this->generation) return;
            brls::Logger::warning("searchHubs: {}", ex);
            this->doFlatSearch(term);
        });
}

void SearchTab::showHubs(const std::vector<media::Hub>& hubs) {
    // never leave focus on a row we are about to destroy
    brls::View* focus = brls::Application::getCurrentFocus();
    for (brls::View* v = focus; v != nullptr; v = v->getParent()) {
        if (v == this->rowsBox.getView()) {
            brls::Application::giveFocus(this->fieldBox);
            break;
        }
    }
    this->rowsBox->clearViews();

    float frameHeight = brls::getStyle()["app/card/poster/row"];
    for (const media::Hub& h : hubs) {
        if (h.items.empty()) continue;
        auto* row = new RecylingVideo();
        row->setTitle(h.title);
        row->setSubtitle(h.subtitle);
        row->setFrameHeight(frameHeight);
        // The tab already carries the page's side padding, so the row adds
        // only enough for a card's focus ring (drawn ~5px outside its frame)
        // — anything more and the titles sit indented twice over.
        row->setSidePadding(8);
        row->setItems(h.items);
        this->rowsBox->addView(row);
    }
    this->showPane(this->rowsScroll.getView());
}

void SearchTab::doFlatSearch(const std::string& searchTerm) {
    // offline: search the local catalog (title contains, case-insensitive)
    // instead of the server (SPEC §4.4)
    if (NetworkState::isOffline()) {
        this->showPane(this->results.getView());
        auto items = OfflineLibrary::instance().search(searchTerm);
        if (items.empty()) {
            this->results->setEmpty(
                "main/search/no_results"_i18n, "main/search/no_results_sub"_i18n, "icon/ico-search.svg");
        } else {
            auto* ds = new VideoDataSource(items);
            ds->setLocalContext(true);
            this->results->setDataSource(ds);
        }
        return;
    }

    uint64_t gen = this->generation;
    ASYNC_RETAIN
    // a single page: search does not paginate reliably
    AppConfig::instance().backend().search(searchTerm, media::MediaKind::Any, 40,
        [ASYNC_TOKEN, gen](const media::Container<media::Item>& r) {
            ASYNC_RELEASE
            if (gen != this->generation) return;
            this->showPane(this->results.getView());
            if (r.Items.empty()) {
                this->results->setEmpty(
                    "main/search/no_results"_i18n, "main/search/no_results_sub"_i18n, "icon/ico-search.svg");
            } else {
                this->results->setDataSource(new VideoDataSource(r.Items));
            }
        },
        [ASYNC_TOKEN, gen](const std::string& ex) {
            ASYNC_RELEASE
            if (gen != this->generation) return;
            this->showPane(this->results.getView());
            this->results->setError(ex);
        });
}
