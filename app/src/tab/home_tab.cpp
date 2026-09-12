#include "tab/home_tab.hpp"
#include "view/recycling_grid.hpp"
#include "view/recyling_video.hpp"
#include "view/text_box.hpp"
#include "utils/image.hpp"
#include "view/loading_spinner.hpp"
#include "api/plex.hpp"
#include "api/backend.hpp"
#include "utils/keybind.hpp"
#include "utils/network_state.hpp"
#include "utils/offline_library.hpp"
#include "utils/offline_ui.hpp"
#include <algorithm>

using namespace brls::literals;  // for _i18n

HomeTab::HomeTab() {
    brls::Logger::debug("Tab HomeTab: create");
    this->inflateFromXMLRes("xml/tabs/home.xml");
    // centered spinner overlay shown while the home hubs load (the rows are
    // built into boxHome only once the response arrives).
    this->spinner = new LoadingSpinner();
    this->addView(this->spinner);
}

HomeTab::~HomeTab() {
    if (this->focusSubscribed) brls::Application::getGlobalFocusChangeEvent()->unsubscribe(this->focusSub);
    brls::Logger::debug("View HomeTab: delete");
}

brls::View* HomeTab::create() { return new HomeTab(); }

/// The home screen mirrors the rows configured server-side: "Continue
/// watching" then the hubs from /hubs, with their localized titles
/// (X-Plex-Language) — PLEX_MIGRATION.md §2.5. Continue Watching and every
/// hub are fetched in parallel, then merged into one list and ordered by
/// AppConfig::getHubOrder() (Settings > Home Rows) before any row is built,
/// so a user-chosen position applies to Continue Watching exactly like any
/// other row instead of it always being pinned first.
void HomeTab::doRequest() {
    // drop any previous offline empty overlay -> back to the scrollable list
    if (this->offlineEmpty) {
        this->removeView(this->offlineEmpty);
        this->offlineEmpty = nullptr;
        this->scroll->setVisibility(brls::Visibility::VISIBLE);
    }

    // offline: the server hubs are unavailable — show one poster row per
    // downloaded library instead (same row layout as online) (SPEC AC13)
    if (NetworkState::isOffline()) {
        this->boxHome->clearViews();
        this->resumeRow = nullptr;
        auto& lib = OfflineLibrary::instance();
        bool any = false;
        for (auto& s : lib.sections()) {
            auto items = lib.sectionItems(s.key);
            if (items.empty()) continue;
            RecylingVideo* row = new RecylingVideo();
            row->setTitle(s.title);
            row->setFrameHeight(brls::getStyle()["app/card/poster/row"]);
            row->setItemWidth(brls::getStyle()["app/card/poster/width"]);
            row->setSidePadding(brls::getStyle()["main/content_padding_sides"]);
            row->setItems(items);
            this->boxHome->addView(row);
            any = true;
        }
        // nothing downloaded: offline empty state (icon + message + Retry),
        // centered by filling the tab (scroll hidden) instead of scrolling
        if (!any) {
            this->scroll->setVisibility(brls::Visibility::GONE);
            this->offlineEmpty = offline_ui::makeEmpty();
            this->addView(this->offlineEmpty);
        }
        return;
    }

    // clearViews destroys the focused card when refreshing after playback:
    // remember to give the focus back to the first rebuilt row, otherwise it
    // falls back to the sidebar and the user loses track of it
    this->restoreFocus = hasFocusWithin(this);
    this->spinner->setSpinning(true);
    this->boxHome->clearViews();
    this->resumeRow = nullptr;

    this->pendingRows.clear();
    this->renderedIds.clear();
    this->hubsError.clear();
    this->loading = true;
    this->rendered = false;
    int gen = ++this->requestGen;
    // ORDER MATTERS, and not for style: brls::async is a SINGLE background
    // thread draining its queue serially, so whichever backend verb is queued
    // first runs to completion before the other one starts. The hub rows are
    // the whole screen, so they go first; Continue Watching (which on some
    // backends resolves every row through its own HTTP meta lookup, and used
    // to hold the hubs behind it for as long as that took — leaving Home
    // blank the entire time) queues behind them and splices itself in when it
    // lands.
    this->fetchHubs();
    this->fetchResume();

    // Continue Watching resolves each row through addon meta lookups (and,
    // on some backends, a remote round trip with its own — much longer —
    // timeout) and can legitimately take a while, or fail to call back at
    // all if something downstream hangs. Before this fallback, that used to
    // block Home from showing even the already-fetched hub rows, forever.
    // Render whatever's ready after a few seconds instead of waiting
    // indefinitely; a Continue Watching row that arrives late still fills in
    // on the next tab visit (willAppear's refreshResumeRow, or a full
    // refresh if it never arrived at all).
    ASYNC_RETAIN
    brls::delay(12000, [ASYNC_TOKEN, gen]() {
        ASYNC_RELEASE
        if (gen != this->requestGen || this->rendered) return;  // already handled
        brls::Logger::warning("HomeTab: fetch timed out after 12s, rendering what's ready");
        this->loading = false;
        this->renderRows();
    });
}

/// AttachedView caches the tab's view after its first onCreate(), so a plain
/// tab revisit never re-runs doRequest() — only playback closing (Presenter's
/// VIDEO_CLOSE hook) happened to force a refresh. Re-pull just Continue
/// Watching here so newly-played/resumed items show up without a restart,
/// without refetching (and re-sorting/re-flickering) every catalog row too.
/// If Continue Watching has never had a row (e.g. brand new account), fall
/// back to a full refresh so it can appear at all.
///
/// `loading` guards against a call that lands mid-fetch: the tab's very
/// first activation runs onCreate() (starts doRequest()'s async fetches),
/// immediately followed — still synchronously, before anything has resolved
/// — by this willAppear(). Without the guard that always hit the "no
/// resumeRow yet" branch and fired a second, overlapping doRequest() that
/// raced the first and rendered every row twice.
void HomeTab::willAppear(bool resetState) {
    brls::Box::willAppear(resetState);
    if (NetworkState::isOffline() || this->loading) return;
    if (this->resumeRow)
        this->refreshResumeRow();
    else
        this->doRequest();
}

void HomeTab::fetchResume() {
    ASYNC_RETAIN
    AppConfig::instance().backend().getContinueWatching(20,
        [ASYNC_TOKEN](const media::Container<media::Hub>& r) {
            ASYNC_RELEASE
            for (auto& hub : r.Items) {
                if (hub.items.empty()) continue;
                if (AppConfig::instance().isHubHidden(hub.hubIdentifier)) continue;
                RowData row;
                row.identifier = hub.hubIdentifier;
                row.title = hub.title.empty() ? "main/home/resume"_i18n : hub.title;
                row.items = hub.items;
                if (hub.more && !hub.key.empty()) row.moreKey = hub.key;
                row.isResume = true;
                // hubs already on screen (the normal case): splice it in at
                // its saved position. Otherwise it joins the pending list and
                // renderRows() places it.
                if (this->rendered)
                    this->insertResumeRow(row);
                else
                    this->pendingRows.push_back(std::move(row));
                break;  // only one Continue Watching hub is meaningful
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            // Continue Watching is additive: its failure must never keep the
            // rest of Home off the screen.
            brls::Logger::warning("home continueWatching: {}", ex);
        });
}

void HomeTab::fetchHubs() {
    ASYNC_RETAIN
    AppConfig::instance().backend().getHomeHubs(20, true,
        [ASYNC_TOKEN](const media::Container<media::Hub>& r) {
            ASYNC_RELEASE
            for (auto& hub : r.Items) {
                if (hub.items.empty()) continue;
                // in case the server returns them despite excludeContinueWatching
                if (hub.hubIdentifier == "home.continue" || hub.hubIdentifier == "home.ondeck") continue;
                if (AppConfig::instance().isHubHidden(hub.hubIdentifier)) continue;

                // playlist hubs mix audio/photo/video: only video playlists
                // are playable in pleNx
                std::vector<plex::Item> items;
                items.reserve(hub.items.size());
                for (auto& item : hub.items) {
                    if (item.type == plex::mediaTypePlaylist && item.playlistType != "video") continue;
                    items.push_back(item);
                }
                if (items.empty()) continue;

                RowData row;
                row.identifier = hub.hubIdentifier;
                row.title = hub.title;
                row.items = std::move(items);
                if (hub.more && !hub.key.empty()) row.moreKey = hub.key;
                this->pendingRows.push_back(std::move(row));
            }
            this->loading = false;
            if (!this->rendered) this->renderRows();
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->loading = false;
            this->hubsError = ex;
            if (!this->rendered) this->renderRows();

            std::string msg = this->hubsError;
            this->hubsError.clear();
            auto dialog = new brls::Dialog(msg);
            dialog->addButton("hints/retry"_i18n, [this]() { brls::sync([this]() { this->doRequest(); }); });
            dialog->addButton("hints/cancel"_i18n, []() {});
            dialog->open();
        });
}

void HomeTab::renderRows() {
    this->rendered = true;
    // unlisted identifiers (e.g. a catalog added since the order was last
    // saved) keep their fetch-order relative position, sorted after every
    // identifier the user has actually placed
    std::vector<std::string> order = AppConfig::instance().getHubOrder();
    std::stable_sort(this->pendingRows.begin(), this->pendingRows.end(),
        [&order](const RowData& a, const RowData& b) {
            auto ra = std::find(order.begin(), order.end(), a.identifier);
            auto rb = std::find(order.begin(), order.end(), b.identifier);
            return ra < rb;
        });

    // Index every visible item by ratingKey so the focus handler can map the
    // focused card back to its metadata (a card's id IS its ratingKey).
    this->heroRows.clear();

    for (auto& row : this->pendingRows) {
        RecylingVideo* view = this->buildRow(row);
        this->heroRows[view] = row.items;
        this->boxHome->addView(view);
        this->renderedIds.push_back(row.identifier);
    }

    // Nothing to show is a STATE, not a blank screen. Zero rows almost always
    // means zero catalogs resolved (addon collection empty, a resync that
    // failed, everything hidden) — saying so beats an unexplained empty page
    // the user can only read as "the app is broken".
    if (this->renderedIds.empty()) {
        auto* empty = new brls::Label();
        empty->setText("main/home/empty"_i18n);
        empty->setFontSize(16);
        empty->setSingleLine(false);
        empty->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        empty->setTextColor(brls::Application::getTheme().getColor("font/grey"));
        empty->setMarginTop(80);
        empty->setMarginLeft(brls::getStyle()["main/content_padding_sides"]);
        empty->setMarginRight(brls::getStyle()["main/content_padding_sides"]);
        this->boxHome->addView(empty);
    }

    // Seed the hero before anything is focused, then let focus drive it.
    for (auto& row : this->pendingRows) {
        if (row.items.empty()) continue;
        this->showHero(row.items.front());
        break;
    }
    if (!this->focusSubscribed) {
        this->focusSub = brls::Application::getGlobalFocusChangeEvent()->subscribe(
            [this](brls::View*) { this->updateHeroFromFocus(); });
        this->focusSubscribed = true;
    }

    this->spinner->setSpinning(false);
    this->tryRestoreFocus();
}

RecylingVideo* HomeTab::buildRow(const RowData& row) {
    RecylingVideo* view = new RecylingVideo();
    view->setTitle(row.title);
    float frameHeight = brls::getStyle()["app/card/poster/row"];
    view->setFrameHeight(frameHeight);
    if (row.isResume) {
        view->setItemWidth(brls::getStyle()["app/card/poster/width"]);
        this->resumeRow = view;
    } else {
        // playlists AND music (artist/album/track): SQUARE covers (1:1)
        // — width = image height of the row (frame - 55 of labels,
        // video_card.xml metrics); everything else keeps 2:3 posters
        const std::string& t0 = row.items.front().type;
        bool square = t0 == plex::mediaTypePlaylist || t0 == plex::mediaTypeArtist ||
                      t0 == plex::mediaTypeAlbum || t0 == plex::mediaTypeTrack;
        view->setItemWidth(square ? frameHeight - 55 : brls::getStyle()["app/card/poster/width"]);
    }
    view->setSidePadding(brls::getStyle()["main/content_padding_sides"]);
    // truncated hub (more=1): "+" card to the full page
    if (!row.moreKey.empty())
        view->setItems(row.items, row.title, row.moreKey);
    else
        view->setItems(row.items);
    return view;
}

void HomeTab::insertResumeRow(const RowData& row) {
    if (row.items.empty()) return;
    // renderRows() put the "nothing to show" label up because it had no rows;
    // this one disproves it, so drop the label before splicing in.
    if (this->renderedIds.empty()) this->boxHome->clearViews();
    // index it belongs at among the rows already on screen: the first one the
    // saved order ranks AFTER it (unlisted identifiers rank last, so an
    // unplaced Continue Watching lands at the end rather than jumping the queue)
    std::vector<std::string> order = AppConfig::instance().getHubOrder();
    auto rank = [&order](const std::string& id) { return std::find(order.begin(), order.end(), id); };
    auto mine = rank(row.identifier);

    size_t idx = this->renderedIds.size();
    for (size_t i = 0; i < this->renderedIds.size(); i++) {
        if (mine < rank(this->renderedIds[i])) {
            idx = i;
            break;
        }
    }

    RecylingVideo* view = this->buildRow(row);
    this->heroRows[view] = row.items;
    this->boxHome->addView(view, idx);
    this->renderedIds.insert(this->renderedIds.begin() + idx, row.identifier);
}

void HomeTab::refreshResumeRow() {
    if (!this->resumeRow) return;
    RecylingVideo* row = this->resumeRow;
    ASYNC_RETAIN
    AppConfig::instance().backend().getContinueWatching(20,
        [ASYNC_TOKEN, row](const media::Container<media::Hub>& r) {
            ASYNC_RELEASE
            for (auto& hub : r.Items) {
                if (hub.items.empty()) continue;
                if (AppConfig::instance().isHubHidden(hub.hubIdentifier)) continue;
                std::string title = hub.title.empty() ? "main/home/resume"_i18n : hub.title;
                row->setTitle(title);
                if (hub.more && !hub.key.empty()) {
                    row->setItems(hub.items, title, hub.key);
                } else {
                    row->setItems(hub.items);
                }
                return;
            }
            row->setItems({});
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Logger::warning("home continueWatching refresh: {}", ex);
        });
}

void HomeTab::tryRestoreFocus() {
    if (!this->restoreFocus) return;
    this->restoreFocus = false;
    // deferred one frame: the rows added by this callback are not laid out
    // yet, and giveFocus before the first layout fails silently
    ASYNC_RETAIN
    brls::sync([ASYNC_TOKEN]() {
        ASYNC_RELEASE
        brls::View* target = this->boxHome->getDefaultFocus();
        if (target) brls::Application::giveFocus(target);
    });
}


/// Walk up from the focused view until a ratingKey we know is found: the
/// focusable node inside a card is a child ("video/card/pic_box"), so the id
/// that matters lives on an ancestor.
void HomeTab::updateHeroFromFocus() {
    if (this->heroRows.empty()) return;
    // Resolve the focused card to (row, index) by climbing to the cell (which
    // knows its index) and then to the row that owns it. View::id is protected,
    // so the recycler's own index is both the available route and the more
    // robust one — it survives two rows holding the same title.
    brls::View* v = brls::Application::getCurrentFocus();
    RecyclingGridItem* cell = nullptr;
    for (int guard = 0; v && guard < 10; v = v->getParent(), guard++) {
        if (!cell) cell = dynamic_cast<RecyclingGridItem*>(v);
        auto row = this->heroRows.find(v);
        if (row == this->heroRows.end()) continue;
        if (!cell) return;
        size_t idx = cell->getIndex();
        const std::vector<plex::Item>& items = row->second;
        // "+N more" trailing cards index past the end: leave the hero alone.
        if (idx >= items.size()) return;
        this->showHero(items[idx]);
        return;
    }
    // Focus on the sidebar or settings: the last selected item stays presented
    // rather than the hero blanking.
}

void HomeTab::showHero(const plex::Item& item) {
    if (item.ratingKey == this->heroShowing) return;  // same card, nothing to redraw
    this->heroShowing = item.ratingKey;

    auto* backdrop = dynamic_cast<brls::Image*>(this->getView("home/hero/backdrop"));
    auto* logo = dynamic_cast<brls::Image*>(this->getView("home/hero/logo"));
    auto* title = dynamic_cast<brls::Label*>(this->getView("home/hero/title"));
    auto* meta = dynamic_cast<brls::Label*>(this->getView("home/hero/meta"));
    auto* overview = dynamic_cast<TextBox*>(this->getView("home/hero/overview"));

    // Everything sits on the artwork's veil, so it is light in BOTH themes.
    const NVGcolor onArt = nvgRGB(0xF5, 0xF5, 0xF5);
    const NVGcolor onArtDim = nvgRGB(0xB3, 0xB3, 0xB3);

    if (backdrop) {
        Image::cancel(backdrop);
        std::string art = item.art.empty() ? item.grandparentArt : item.art;
        if (art.empty()) art = item.thumb;
        if (!art.empty()) Image::with(backdrop, art);
    }

    // Prefer the clear-logo artwork, exactly like the reference; fall back to
    // the title as text when the addon supplies none.
    if (logo && title) {
        Image::cancel(logo);
        if (!item.clearLogo.empty()) {
            Image::load(logo, item.clearLogo, 330, 96);
            logo->setVisibility(brls::Visibility::VISIBLE);
            title->setVisibility(brls::Visibility::GONE);
        } else {
            logo->setVisibility(brls::Visibility::GONE);
            title->setVisibility(brls::Visibility::VISIBLE);
            title->setText(item.grandparentTitle.empty() ? item.title : item.grandparentTitle);
            title->setTextColor(onArt);
        }
    }

    if (meta) {
        std::vector<std::string> bits;
        if (item.type == plex::mediaTypeShow) bits.push_back("main/stremio/series"_i18n);
        else if (item.type == plex::mediaTypeMovie) bits.push_back("main/stremio/movies"_i18n);
        for (auto& g : item.genres) {
            bits.push_back(g);
            break;  // one genre: the line is single-line and the year matters more
        }
        if (item.duration > 0) {
            int min = int(item.duration / 60000);
            bits.push_back(min >= 60 ? fmt::format("{} h {:02d}", min / 60, min % 60) : fmt::format("{} min", min));
        }
        if (item.year > 0) bits.push_back(std::to_string(item.year));
        if (item.rating > 0) bits.push_back(fmt::format("IMDb {:.1f}", item.rating));
        std::string line;
        for (size_t i = 0; i < bits.size(); i++) line += (i ? "  •  " : "") + bits[i];
        meta->setText(line);
        meta->setTextColor(onArtDim);
    }

    if (overview) {
        overview->setText(item.summary);
        overview->setTextColor(onArt);
    }
}

void HomeTab::onCreate() {
    auto actionRefresh = [this](brls::View* view) {
        this->doRequest();
        return true;
    };

    // Triangle, not BUTTON_BACK: on PS4 BUTTON_BACK is the TOUCHPAD, which the
    // hint bar drew as a bare "S" glyph and which nobody would think to press.
    // SDL reports DualShock Triangle as BUTTON_Y.
    this->registerAction("hints/refresh"_i18n, brls::BUTTON_Y, actionRefresh);
    this->registerAction(KeyBind::getRefresh(), actionRefresh);

    this->doRequest();
}
