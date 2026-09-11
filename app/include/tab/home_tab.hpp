/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <view/auto_tab_frame.hpp>
#include <view/presenter.hpp>

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
    void doResume(RecylingVideo* row);
    void doHubs();
    void tryRestoreFocus();

    // set when a refresh destroys the focused row (e.g. after closing the
    // player): the first rebuilt row takes the focus back so the user can
    // see where it landed
    bool restoreFocus = false;

    // retained so willAppear can refresh just this row on every tab revisit,
    // not only the once-per-app-lifetime onCreate() / after-playback VIDEO_CLOSE
    // (AttachedView caches the tab, so onCreate never runs twice)
    RecylingVideo* resumeRow = nullptr;

    LoadingSpinner* spinner = nullptr;  // centered overlay while hubs load
    // offline empty state: added to the tab root (definite height) with the
    // scroll hidden, so it centers — a grow child of the scroll content would
    // be measured with an indefinite height and stay top-aligned
    brls::View* offlineEmpty = nullptr;

    BRLS_BIND(brls::Box, boxHome, "home/box");
    BRLS_BIND(brls::ScrollingFrame, scroll, "home/scroll");
};
