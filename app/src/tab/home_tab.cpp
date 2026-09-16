#include "tab/home_tab.hpp"
#include "utils/misc.hpp"
#include "view/recycling_grid.hpp"
#include "view/recyling_video.hpp"
#include "view/text_box.hpp"
#include "utils/image.hpp"
#include "view/loading_spinner.hpp"
#include "view/continue_card.hpp"
#include "view/svg_image.hpp"
#include "utils/rating.hpp"
#include "api/plex.hpp"
#include "api/backend.hpp"
#include "utils/keybind.hpp"
#include "utils/network_state.hpp"
#include "utils/offline_library.hpp"
#include "utils/offline_ui.hpp"
#include <algorithm>
#include <cctype>

using namespace brls::literals;  // for _i18n

HomeTab::HomeTab() {
    brls::Logger::debug("Tab HomeTab: create");
    this->inflateFromXMLRes("xml/tabs/home.xml");
    // modernHeroFullScreenBackdropEnabled: the backdrop leaves its corner and
    // takes the whole screen. The reference switches the same image between
    // TopEnd at a fraction of the width and TopStart filling it, so the two
    // edge fades that hide the corner version's seams go with it — there is no
    // seam left to hide once the art reaches both edges.
    if (AppConfig::instance().getItem(AppConfig::LAYOUT_FULLSCREEN_HERO, false)) {
        if (auto* art = this->getView("home/hero/art")) {
            art->setWidth(brls::Application::contentWidth);
            art->setHeight(brls::Application::contentHeight);
            art->setPositionLeft(0);
            art->setPositionRight(brls::View::AUTO);
        }
        if (auto* fade = this->getView("home/hero/fade/left")) fade->setVisibility(brls::Visibility::GONE);
    }
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
        this->upcomingRow = nullptr;
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
    this->upcomingRow = nullptr;

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
    if (this->resumeRow) {
        this->refreshResumeRow();
    } else {
        this->doRequest();
    }
}

/// Closing the player used to fall through Presenter's VIDEO_CLOSE straight
/// into doRequest(), so every "back out of a show" rebuilt every row on the
/// screen. The reference does not: its catalogs live in the view model and
/// survive the trip, and only Continue Watching has anything new to say.
///
/// NOTHING here re-pulls the catalogs, not even on a timer. A staleness TTL
/// used to, and on a long session it fired on every player close and rebuilt
/// every row -- dozens of catalogs and their posters -- at the one moment the
/// player's own buffers had not been handed back yet. That is where the
/// std::bad_alloc in a PS4 log came from: the refresh and the teardown were
/// bidding for the same heap. Triangle is the refresh, as it was asked to be.
void HomeTab::onVideoClose() {
    if (NetworkState::isOffline() || this->loading) return;
    if (this->resumeRow) {
        this->refreshResumeRow();
    } else {
        this->doRequest();
    }
}

void HomeTab::fetchResume() {
    // Layout > Show Continue Watching (the reference's layout_cw_enabled).
    // Gated at the FETCH, not at the render: with the row switched off there is
    // no reason to ask the backend for it at all.
    if (!AppConfig::instance().getItem(AppConfig::SHOW_CONTINUE, true)) return;
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
                // Two of them now, at most: Continue Watching and, under the
                // reference's Separate Upcoming Row sort, "Upcoming".
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
                // "Hide Unreleased Content": the reference filters its CATALOG
                // ROWS by this (filterReleasedItems), not Continue Watching —
                // something you are part-way through is by definition out.
                if (AppConfig::instance().getItem(AppConfig::LAYOUT_HIDE_UNRELEASED, false))
                    media::filterReleased(items);
                if (items.empty()) continue;

                RowData row;
                row.identifier = hub.hubIdentifier;
                row.title = hub.title;
                row.subtitle = hub.subtitle;
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
        empty->setFontSize(28);
        empty->setSingleLine(false);
        empty->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        empty->setTextColor(brls::Application::getTheme().getColor("font/grey"));
        empty->setMarginTop(80);
        empty->setMarginLeft(brls::getStyle()["main/content_padding_sides"]);
        empty->setMarginRight(brls::getStyle()["main/content_padding_sides"]);
        this->boxHome->addView(empty);
    }

    // Layout > Show hero section (the reference's layout_show_hero). Both
    // halves go: the artwork is absolute so hiding it just stops it drawing,
    // and the text block is a sibling of the scroll, so hiding THAT is what
    // gives the rows the space back.
    if (!AppConfig::instance().getItem(AppConfig::SHOW_HERO, true)) {
        if (auto* art = this->getView("home/hero/art")) art->setVisibility(brls::Visibility::GONE);
        this->boxHero->setVisibility(brls::Visibility::GONE);
    } else {
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
    }

    this->spinner->setSpinning(false);
    // First render of the session: put the cursor on the first card of the
    // top row rather than leaving it parked on the sidebar, so the app opens
    // ready to browse (and the hero already reflects what is selected).
    if (!this->focusedOnce && !this->renderedIds.empty()) {
        this->focusedOnce = true;
        this->restoreFocus = true;
    }
    this->tryRestoreFocus();
}

RecylingVideo* HomeTab::buildRow(const RowData& row) {
    RecylingVideo* view = new RecylingVideo();
    view->setTitle(row.title);
    view->setSubtitle(row.subtitle);
    float frameHeight = brls::getStyle()["app/card/poster/row"];
    if (row.isResume) {
        // NuvioTV's Continue Watching is a row of LANDSCAPE tiles with the
        // text printed over them, so the row is only as tall as a 16:9 card
        // (+ the 5px the cell pads itself with on each side for its focus
        // ring) — no label strip underneath.
        float wide = brls::getStyle()["app/card/wide/width"];
        view->setFrameHeight(wide * 9.f / 16.f + 10);
        view->setItemWidth(wide);
        view->setSidePadding(brls::getStyle()["main/content_padding_sides"]);
        view->setContinueItems(row.items);
        if (row.identifier == "home.upcoming")
            this->upcomingRow = view;
        else
            this->resumeRow = view;
        return view;
    }
    view->setFrameHeight(frameHeight);
    {
        // playlists AND music (artist/album/track): SQUARE covers (1:1)
        // — width = image height of the row (frame less the label block,
        // video_card.xml metrics); everything else keeps 2:3 posters
        const std::string& t0 = row.items.front().type;
        bool square = t0 == plex::mediaTypePlaylist || t0 == plex::mediaTypeArtist ||
                      t0 == plex::mediaTypeAlbum || t0 == plex::mediaTypeTrack;
        view->setItemWidth(square ? frameHeight - brls::getStyle()["app/card/labels"]
                             : brls::getStyle()["app/card/poster/width"]);
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
    if (!this->resumeRow && !this->upcomingRow) return;
    RecylingVideo* row = this->resumeRow;
    RecylingVideo* up = this->upcomingRow;
    ASYNC_RETAIN
    AppConfig::instance().backend().getContinueWatching(20,
        [ASYNC_TOKEN, row, up](const media::Container<media::Hub>& r) {
            ASYNC_RELEASE
            // Whichever of the two rows is on screen takes the hub of its own
            // name; a row whose hub has gone away is emptied rather than left
            // showing what it had before the episode was finished.
            bool gotResume = false, gotUpcoming = false;
            for (auto& hub : r.Items) {
                if (hub.items.empty()) continue;
                if (AppConfig::instance().isHubHidden(hub.hubIdentifier)) continue;
                bool isUpcoming = hub.hubIdentifier == "home.upcoming";
                RecylingVideo* target = isUpcoming ? up : row;
                if (!target) continue;
                std::string title = hub.title.empty() ? "main/home/resume"_i18n : hub.title;
                target->setTitle(title);
                // stays a Continue Watching row across refreshes: setItems
                // would quietly swap the landscape tiles back for posters
                target->setContinueItems(hub.items);
                (isUpcoming ? gotUpcoming : gotResume) = true;
            }
            if (row && !gotResume) row->setContinueItems({});
            if (up && !gotUpcoming) up->setContinueItems({});
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
    this->heroGeneration++;

    // Anything an earlier lookup found for this item is folded back in: a row
    // from a library catalog carries little more than a title and a poster,
    // and the hero is where that shows.
    this->heroItem = item;
    auto known = this->heroMeta.find(item.ratingKey);
    if (known != this->heroMeta.end()) mergeHeroFields(this->heroItem, known->second);

    this->renderHero();
    // NuvioTV enriches the focused hero item the same way (its home screen's
    // "hero enrichment"): the rows stay cheap and only what is on screen gets
    // a metadata round trip.
    if (this->heroItem.summary.empty() || this->heroItem.clearLogo.empty()) this->enrichHero(item.ratingKey);
}

/// Fill in only what `into` is missing. The row's own values win: they are what
/// the catalog the user is looking at chose to show.
void HomeTab::mergeHeroFields(plex::Item& into, const plex::Item& from) {
    if (into.summary.empty()) into.summary = from.summary;
    if (into.clearLogo.empty()) into.clearLogo = from.clearLogo;
    if (into.art.empty()) into.art = from.art;
    if (into.genres.empty()) into.genres = from.genres;
    if (into.year == 0) into.year = from.year;
    if (into.releaseInfo.empty()) into.releaseInfo = from.releaseInfo;
    if (into.rating == 0) into.rating = from.rating;
    if (into.duration == 0) into.duration = from.duration;
    if (into.title.empty()) into.title = from.title;
}

namespace {

// NuvioTV's hero meta is built out of three pieces, laid out in a row with a
// sm (8dp -> 16px) gap between every one of them. These are those pieces.

/// HeroMetaDivider: a round xs (4dp) dot in TextTertiary at 78% opacity.
/// NOT the "*" bullet glyph -- drawing one as text put the separator on the
/// text baseline and sized it to the font, where the reference has a dot
/// centred on the row whatever the type around it does.
brls::Box* heroMetaDot() {
    auto* dot = new brls::Box();
    dot->setWidth(8);
    dot->setHeight(8);
    dot->setShrink(0);
    dot->setCornerRadius(4);
    dot->setBackgroundColor(nvgRGBA(0x80, 0x80, 0x80, 199));  // TextTertiary @ .78
    return dot;
}

/// labelMedium (12sp Medium, 0.5sp tracking) -- the style every hero meta
/// string uses, in TextSecondary unless it is the Continue Watching highlight,
/// which the reference draws SemiBold in TextPrimary.
brls::Label* heroMetaLabel(const std::string& text, NVGcolor color, bool semibold, bool flexible) {
    auto* label = new brls::Label();
    label->setText(text);
    label->setFontSize(24);
    label->setLetterSpacing(1.0f);
    label->setTextColor(color);
    label->setSingleLine(true);
    label->setFontWeight(semibold ? "semibold" : "medium");
    // Only the leading run of row one is allowed to give way (the reference
    // gives it weight(1, fill = false) and leaves everything after it alone).
    label->setShrink(flexible ? 1.0f : 0.0f);
    return label;
}

}  // namespace

/// Appends one child to a hero meta row, with the reference's sm gap before it.
void HomeTab::addHeroMeta(brls::Box* row, brls::View* view) {
    if (row->getChildren().size() > 0) view->setMarginLeft(16);
    row->addView(view);
}

/// The rating as the reference draws it: the source's own mark in a square box
/// (30dp -> 60px) with the value xs (4dp -> 8px) after it. Returns false when
/// the item has no rating or ratings are switched off, in which case nothing is
/// appended and the caller's layout decisions can rely on that.
bool HomeTab::addHeroRating(brls::Box* row, const plex::Item& item) {
    // homeImdbRatingsVisibility.HIDE_ALL takes the hero's rating with it, the
    // same as the pills on a detail page (utils/rating.hpp).
    if (!AppConfig::instance().getItem(AppConfig::LAYOUT_SHOW_RATINGS, true)) return false;

    std::string res, value;
    float aspect = 1.0f;
    if (auto info = rating::parseRatingImage(item.ratingImage, item.rating)) {
        res    = info->icon;
        value  = info->value;
        aspect = info->aspect;
    } else if (item.rating > 0) {
        res   = "icon/ico-star.svg";
        value = fmt::format("{:.1f}", item.rating);
    } else {
        return false;
    }

    // ContentScale.Fit inside a 60x60 box, which is what Modifier.size(30.dp)
    // on the reference's AsyncImage means: the glyph keeps its aspect and the
    // box keeps the row's height honest.
    auto* icon = new SVGImage();
    icon->setShrink(0);
    icon->setWidth(aspect >= 1.0f ? 60.f : 60.f * aspect);
    icon->setHeight(aspect >= 1.0f ? 60.f / aspect : 60.f);
    icon->setImageFromSVGRes(res);
    this->addHeroMeta(row, icon);

    auto* label = heroMetaLabel(value, nvgRGB(0xB3, 0xB3, 0xB3), false, false);
    label->setMarginLeft(8);  // imdbMetaSpacing = xs
    row->addView(label);
    return true;
}

void HomeTab::renderHero() {
    const plex::Item& item = this->heroItem;

    auto* backdrop = dynamic_cast<brls::Image*>(this->getView("home/hero/backdrop"));
    auto* title    = dynamic_cast<brls::Label*>(this->getView("home/hero/title"));
    auto* row1     = dynamic_cast<brls::Box*>(this->getView("home/hero/meta"));
    auto* row2     = dynamic_cast<brls::Box*>(this->getView("home/hero/meta2"));
    auto* overview = dynamic_cast<TextBox*>(this->getView("home/hero/overview"));

    // ModernHomeHero scales the hero's type down when the posters are
    // portrait -- descriptionScale 0.90, titleScale 0.92 -- and leaves it alone
    // when they are landscape. Portrait is this app's default too, so without
    // this the synopsis drew a size larger than the reference's and fitted a
    // line less into the same column.
    static const bool kLandscape = AppConfig::instance().getItem(AppConfig::LAYOUT_LANDSCAPE_POSTERS, false);
    if (!kLandscape) {
        if (title) title->setFontSize(56 * 0.92f);
        if (overview) overview->setFontSize(28 * 0.90f);
    }

    // Everything sits on the artwork's veil, so it is light in BOTH themes.
    const NVGcolor onArt      = nvgRGB(0xF5, 0xF5, 0xF5);
    const NVGcolor onArtDim   = nvgRGB(0xB3, 0xB3, 0xB3);  // TextSecondary

    if (backdrop) {
        Image::cancel(backdrop);
        std::string art = item.art.empty() ? item.grandparentArt : item.art;
        if (art.empty()) art = item.thumb;
        // 1280 wide, not the addon's `original`: this fills the screen behind
        // everything and reloads on every focus change, so it is the single
        // most expensive texture in the app (see StremioBackend::imageUrl).
        if (!art.empty()) Image::load(backdrop, art, 1280, 720);
    }

    // The cut-out logo is the title, as in NuvioTV. Nothing is revealed until
    // the outcome is known, so a logo that 404s cannot leave the hero blank.
    // An episode borrows the show's: a logo is a show-level thing.
    this->heroTitleText = item.grandparentTitle.empty() ? item.title : item.grandparentTitle;
    if (title) title->setTextColor(onArt);
    this->applyHeroLogo(item.ratingKey, item.clearLogo);

    const bool isSeries = item.type == plex::mediaTypeShow || item.type == plex::mediaTypeEpisode ||
        item.type == plex::mediaTypeSeason;
    const bool resuming = item.duration > 0 && item.viewOffset > 0 && item.duration > item.viewOffset;

    // --- the Continue Watching highlight (ModernHomeHero's
    // secondaryHighlightText), which decides where the rating goes ----------
    // An item you are part way through says how much is LEFT; one queued up as
    // the next episode says NEXT UP. Everything the reference derives from
    // Trakt/Simkl state we cannot know, so those cases collapse into these two.
    std::string highlight;
    if (resuming) {
        int64_t minutes = (item.duration - item.viewOffset) / 60000;
        int64_t h = minutes / 60, m = minutes % 60;
        if (h > 0)
            highlight = brls::getStr("main/hero/hours_min_left", std::to_string(h), std::to_string(m));
        else
            highlight = brls::getStr("main/hero/min_left", std::to_string(std::max<int64_t>(m, 1)));
    } else if (item.type == plex::mediaTypeEpisode && item.viewOffset == 0) {
        highlight = "main/hero/next_up"_i18n;
    }
    std::transform(highlight.begin(), highlight.end(), highlight.begin(),
        [](unsigned char c) { return std::toupper(c); });

    // reserveImdbInSecondary: a series, or anything carrying a highlight, shows
    // its rating on row two beside that highlight; a plain movie shows it at
    // the end of row one.
    const bool ratingOnRow2 = isSeries || !highlight.empty();

    // --- row one: what this is --------------------------------------------
    if (row1) {
        row1->clearViews();

        // leadingMetaText: the episode (or the content type) and the first
        // genre, joined by a literal bullet -- the reference's
        // joinToString(" • "), which is a different separator from the dot
        // between the row's own elements.
        std::vector<std::string> leading;
        if (item.type == plex::mediaTypeEpisode && (item.parentIndex > 0 || item.index > 0)) {
            std::string ep = fmt::format("S{} E{}", item.parentIndex, item.index);
            if (!item.title.empty()) ep += " · " + item.title;
            leading.push_back(ep);
        } else if (item.type == plex::mediaTypeShow) {
            leading.push_back("main/stremio/series"_i18n);
        } else if (item.type == plex::mediaTypeMovie) {
            leading.push_back("main/stremio/movies"_i18n);
        }
        if (!item.genres.empty() && !item.genres.front().empty()) leading.push_back(item.genres.front());

        std::string leadingText;
        for (size_t i = 0; i < leading.size(); i++) leadingText += (i ? " • " : "") + leading[i];

        // The trailing group: runtime, year. An episode of something you are
        // resuming has no runtime of its own in the reference's hero preview,
        // so only ask for one where it would have.
        std::vector<std::string> trailing;
        if (!isSeries) {
            std::string runtime = misc::formatRuntime(item.duration);
            if (!runtime.empty()) trailing.push_back(runtime);
        }
        // extractYearOrRange: the provider's string verbatim, so a show reads
        // "2007-2019" and not just its first year.
        if (!item.releaseInfo.empty())
            trailing.push_back(item.releaseInfo);
        else if (item.year > 0)
            trailing.push_back(std::to_string(item.year));

        if (!leadingText.empty()) this->addHeroMeta(row1, heroMetaLabel(leadingText, onArtDim, false, true));
        for (size_t i = 0; i < trailing.size(); i++) {
            this->addHeroMeta(row1, heroMetaDot());
            this->addHeroMeta(row1, heroMetaLabel(trailing[i], onArtDim, false, false));
        }
        bool ratedHere = !ratingOnRow2 && this->addHeroRating(row1, item);
        // Modifier.size(30.dp) on the rating mark makes the row that carries it
        // 60px tall; a row of plain labelMedium is 24.
        row1->setHeight(ratedHere ? 60.f : 24.f);
        row1->setVisibility(row1->getChildren().empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    }

    // --- row two: the Continue Watching highlight and its rating -----------
    if (row2) {
        row2->clearViews();
        if (!highlight.empty()) this->addHeroMeta(row2, heroMetaLabel(highlight, onArt, true, true));
        if (ratingOnRow2) {
            bool hadHighlight = !row2->getChildren().empty();
            // The reference only puts a divider here when both sides exist.
            size_t before = row2->getChildren().size();
            if (hadHighlight) this->addHeroMeta(row2, heroMetaDot());
            if (!this->addHeroRating(row2, item) && hadHighlight) {
                // no rating after all -- take the divider back off
                while (row2->getChildren().size() > before) row2->removeView(row2->getChildren().back());
            }
        }
        bool rated = row2->getChildren().size() > (highlight.empty() ? 0u : 1u);
        row2->setHeight(rated ? 60.f : 24.f);
        row2->setVisibility(row2->getChildren().empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    }

    if (overview) {
        overview->setText(item.summary);
        overview->setTextColor(onArt);
    }
}

void HomeTab::showHeroTitleText(const std::string& key) {
    if (key != this->heroShowing) return;
    auto* logo = dynamic_cast<brls::Image*>(this->getView("home/hero/logo"));
    auto* title = dynamic_cast<brls::Label*>(this->getView("home/hero/title"));
    if (logo) logo->setVisibility(brls::Visibility::GONE);
    if (title) {
        title->setText(this->heroTitleText);
        title->setVisibility(brls::Visibility::VISIBLE);
    }
}

void HomeTab::applyHeroLogo(const std::string& key, const std::string& url) {
    auto* logo = dynamic_cast<brls::Image*>(this->getView("home/hero/logo"));
    auto* title = dynamic_cast<brls::Label*>(this->getView("home/hero/title"));
    if (!logo || !title) return;

    // Nothing to try: no url on the row, and the lookup either found none or
    // has not run yet.
    if (url.empty() || this->heroLogoFailed.count(url)) {
        if (url.empty()) brls::Logger::info("hero logo absent: {}", this->heroTitleText);
        this->showHeroTitleText(key);
        return;
    }

    // Neither slot is shown while the fetch is in flight — swapping a text
    // title out for a logo a moment later is worse than a brief gap, and the
    // callback below always fires, so the gap always ends.
    Image::cancel(logo);
    logo->setVisibility(brls::Visibility::GONE);
    title->setVisibility(brls::Visibility::GONE);

    size_t gen = this->heroGeneration;
    ASYNC_RETAIN
    // 500 wide like NuvioTV's hero logo, not the 330 of the slot: the slot is
    // in design units and the panel is 1080p, so 500 is roughly 1:1 there.
    Image::load(logo, url, 500, 0, [ASYNC_TOKEN, key, url, gen](bool ok, bool retryable) {
        ASYNC_RELEASE
        // focus moved on while this was in flight: that newer item owns the
        // hero now, and re-showing this one would fight it
        if (gen != this->heroGeneration || key != this->heroShowing) return;
        auto* view = dynamic_cast<brls::Image*>(this->getView("home/hero/logo"));
        if (ok) {
            if (view) view->setVisibility(brls::Visibility::VISIBLE);
            return;
        }
        if (retryable) {
            // never got to run — show the title for now, but leave the url
            // alone so the next visit tries it properly
            this->showHeroTitleText(key);
            return;
        }
        // The url was advertised but nothing usable came back. Remember that,
        // so re-focusing this card does not re-request it, and go looking for
        // a better one. Logged at info: when a logo is missing on a device,
        // this line is the difference between a fetch that failed and a url
        // that was never offered.
        brls::Logger::info("hero logo failed: {} ({})", this->heroTitleText, url);
        this->heroLogoFailed.insert(url);
        this->showHeroTitleText(key);
        // the row's url was a dud; the full metadata may know a better one
        this->heroItem.clearLogo.clear();
        this->enrichHero(key);
    });
}

void HomeTab::enrichHero(const std::string& key) {
    if (key.empty()) return;
    // Answered already: whatever the full metadata had for this item is in
    // heroMeta (possibly nothing), and showHero folded it in.
    if (this->heroMeta.count(key)) return;
    // One lookup in flight per item. Recorded separately from heroMeta so that
    // a lookup which never runs (see below) leaves no verdict behind — an item
    // flicked past must stay retryable, not be remembered as having nothing.
    if (!this->heroPending.insert(key).second) return;

    // Catalog rows are metaPreview objects: a library catalog typically sends
    // a title, a poster and nothing else, and plenty of metadata addons only
    // put `logo` in the full meta. So the hero asks for the full record of the
    // item the user is actually looking at — NuvioTV's home screen does the
    // same, and for the same reason. One fetch per item rested on, never one
    // per card scrolled past: the delay drops the ones flicked through.
    ASYNC_RETAIN
    brls::delay(300, [ASYNC_TOKEN, key]() {
        ASYNC_RELEASE
        if (key != this->heroShowing) {  // flicked past: not worth a request
            this->heroPending.erase(key);
            return;
        }
        ASYNC_RETAIN
        AppConfig::instance().backend().getItemDetail(
            key, false,
            [ASYNC_TOKEN, key](const media::Item& full) {
                ASYNC_RELEASE
                this->heroPending.erase(key);
                this->heroMeta[key] = full;
                if (key != this->heroShowing) return;
                // merge into what is on screen and redraw in place: the item
                // has not changed, only what we know about it
                plex::Item before = this->heroItem;
                mergeHeroFields(this->heroItem, full);
                if (this->heroItem.clearLogo != before.clearLogo || this->heroItem.summary != before.summary ||
                    this->heroItem.genres != before.genres || this->heroItem.year != before.year ||
                    this->heroItem.rating != before.rating || this->heroItem.duration != before.duration)
                    this->renderHero();
            },
            [ASYNC_TOKEN, key](const std::string& ex) {
                ASYNC_RELEASE
                // no verdict recorded: a failed request is not proof of absence
                this->heroPending.erase(key);
                brls::Logger::warning("hero enrich {}: {}", key, ex);
            });
    });
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
