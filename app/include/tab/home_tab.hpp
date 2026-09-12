/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <view/auto_tab_frame.hpp>
#include <view/presenter.hpp>
#include "api/plex.hpp"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class RecylingVideo;
class LoadingSpinner;

class HomeTab : public AttachedView, public Presenter {
public:
    HomeTab();
    ~HomeTab() override;

    void onCreate() override;

    void doRequest() override;

    void willAppear(bool resetState = false) override;

    static brls::View* create();

private:
    // one renderable Home row, collected by fetchResume()/fetchHubs() before
    // being sorted (AppConfig::getHubOrder) and rendered together — letting
    // Continue Watching take part in the same reorder pool as every other
    // hub instead of always being pinned first
    struct RowData {
        std::string identifier;
        std::string title;
        std::string moreKey;  // hub.key when hub.more, else empty
        std::vector<plex::Item> items;
        bool isResume = false;
    };

    /// Point the hero at whatever card currently holds focus. Subscribed to
    /// borealis' global focus-change event: the hero is not focusable itself
    /// and never scrolls, it just follows the selection like NuvioTV's.
    void updateHeroFromFocus();
    void showHero(const plex::Item& item);

    /// Put `url`'s artwork in the hero's logo slot, for the item whose
    /// ratingKey is `key`. Reveals the logo only once its pixels are in;
    /// on failure (or no url at all) it asks resolveHeroLogo() to look one
    /// up and, failing that, falls back to the text title.
    void applyHeroLogo(const std::string& key, const std::string& url);
    /// Fetch `key`'s full metadata to find a logo the catalog row did not
    /// carry. Debounced and memoised — a scroll must not fire one per card.
    void resolveHeroLogo(const std::string& key);
    /// Text title for `key`, used when no logo can be had.
    void showHeroTitleText(const std::string& key);

    /// ratingKey -> logo url found in the item's full metadata; "" means
    /// "looked, there is none". Also suppresses repeat lookups.
    std::unordered_map<std::string, std::string> heroLogos;
    /// keys with a lookup scheduled or in flight (dedupe without recording a
    /// verdict, so an aborted lookup can be retried)
    std::unordered_set<std::string> heroLogoPending;
    /// logo urls that failed to load (metahub 404s the logos it does not
    /// have, though Cinemeta advertises one for every IMDb id)
    std::unordered_set<std::string> heroLogoFailed;
    std::string heroTitleText;  // title of the item in the hero right now
    size_t heroGeneration = 0;  // bumped per showHero; stale callbacks drop out

    /// Row view -> the items it shows. The focus event hands us a View; this
    /// is the bridge from the focused card back to the metadata the hero needs.
    std::unordered_map<brls::View*, std::vector<plex::Item>> heroRows;
    std::string heroShowing;  // ratingKey currently in the hero
    brls::GenericEvent::Subscription focusSub{};
    bool focusSubscribed = false;  // guards the unsubscribe in the destructor

    void fetchResume();
    void fetchHubs();
    void renderRows();   // sorts pendingRows by saved order, builds the views
    /// Splices a Continue Watching row into an ALREADY rendered Home, at the
    /// position the saved order puts it in. Used when it resolves after the
    /// hub rows did — which is the normal case, not the exception (see
    /// fetchHubs' comment on brls::async being a single serial thread).
    void insertResumeRow(const RowData& row);
    RecylingVideo* buildRow(const RowData& row);
    void refreshResumeRow();
    void tryRestoreFocus();

    // set when a refresh destroys the focused row (e.g. after closing the
    // player): the first rebuilt row takes the focus back so the user can
    // see where it landed
    bool restoreFocus = false;

    std::vector<RowData> pendingRows;
    // identifiers of the rows currently in boxHome, in display order — lets a
    // late Continue Watching row find the index it belongs at without a
    // full (flickering, focus-losing) rebuild
    std::vector<std::string> renderedIds;
    std::string hubsError;  // set by fetchHubs()'s error branch, shown after render
    // guards doRequest() against a second, overlapping call: the tab's FIRST
    // activation runs onCreate() (which calls doRequest(), kicking off async
    // fetches) immediately followed, still synchronously, by willAppear() —
    // at that point resumeRow is still null (nothing has resolved yet), so
    // without this guard willAppear() would fire a second doRequest() that
    // races the first and double-adds every row (build report: rows "loop")
    bool loading = false;
    // true once renderRows() has actually run for the current doRequest()
    // cycle — guards against the fallback timeout (below) and the real
    // fetchResume()/fetchHubs() completion both trying to render
    bool rendered = false;
    // bumped on every doRequest(): captured by the fallback timeout so a
    // stale one (from a doRequest() that was itself superseded by a manual
    // refresh before it finished) can tell it no longer applies
    int requestGen = 0;

    // retained so willAppear can refresh just this row on every tab revisit,
    // not only the once-per-app-lifetime onCreate() / after-playback VIDEO_CLOSE
    // (AttachedView caches the tab, so onCreate never runs twice) — null until
    // a Continue Watching hub with items has actually been rendered once
    RecylingVideo* resumeRow = nullptr;

    LoadingSpinner* spinner = nullptr;  // centered overlay while hubs load
    // offline empty state: added to the tab root (definite height) with the
    // scroll hidden, so it centers — a grow child of the scroll content would
    // be measured with an indefinite height and stay top-aligned
    brls::View* offlineEmpty = nullptr;

    BRLS_BIND(brls::Box, boxHome, "home/box");
    BRLS_BIND(brls::ScrollingFrame, scroll, "home/scroll");
};
