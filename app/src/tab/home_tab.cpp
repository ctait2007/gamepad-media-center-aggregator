#include "tab/home_tab.hpp"
#include "view/recyling_video.hpp"
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

HomeTab::~HomeTab() { brls::Logger::debug("View HomeTab: delete"); }

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
    this->hubsError.clear();
    this->loading = true;
    this->rendered = false;
    int gen = ++this->requestGen;
    this->pendingJoins = 2;
    this->fetchResume();
    this->fetchHubs();

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
                this->pendingRows.push_back(std::move(row));
                break;  // only one Continue Watching hub is meaningful
            }
            this->joinFetch();
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Logger::warning("home continueWatching: {}", ex);
            this->joinFetch();
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
            this->joinFetch();
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->hubsError = ex;
            this->joinFetch();
        });
}

void HomeTab::joinFetch() {
    if (--this->pendingJoins > 0) return;
    if (this->rendered) return;  // the fallback timeout in doRequest() already rendered
    this->loading = false;
    this->renderRows();

    if (!this->hubsError.empty()) {
        std::string ex = this->hubsError;
        this->hubsError.clear();
        auto dialog = new brls::Dialog(ex);
        dialog->addButton("hints/retry"_i18n, [this]() { brls::sync([this]() { this->doRequest(); }); });
        dialog->addButton("hints/cancel"_i18n, []() {});
        dialog->open();
    }
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

    for (auto& row : this->pendingRows) {
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
        this->boxHome->addView(view);
    }

    this->spinner->setSpinning(false);
    this->tryRestoreFocus();
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

void HomeTab::onCreate() {
    auto actionRefresh = [this](brls::View* view) {
        this->doRequest();
        return true;
    };

    this->registerAction("hints/refresh"_i18n, brls::BUTTON_BACK, actionRefresh);
    this->registerAction(KeyBind::getRefresh(), actionRefresh);

    this->doRequest();
}
