/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <view/auto_tab_frame.hpp>
#include <view/presenter.hpp>
#include "api/plex.hpp"
#include <string>
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
