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
    void joinFetch();    // decrements pendingJoins; renders once both land
    void renderRows();   // sorts pendingRows by saved order, builds the views
    void refreshResumeRow();
    void tryRestoreFocus();

    // set when a refresh destroys the focused row (e.g. after closing the
    // player): the first rebuilt row takes the focus back so the user can
    // see where it landed
    bool restoreFocus = false;

    std::vector<RowData> pendingRows;
    int pendingJoins = 0;
    std::string hubsError;  // set by fetchHubs()'s error branch, shown after render

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
