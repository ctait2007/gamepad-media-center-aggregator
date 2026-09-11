/*
    GMCA — hub visibility manager (Settings > Home Rows).

    Lists the current backend's hub rows (Continue Watching + Home hubs) and
    lets the user show/hide each one — a global (not per-server) setting
    applied wherever hubs render (Home, Movie/Show suggestions). See
    AppConfig::isHubHidden/setHubHidden. No reordering (unlike LibraryManager,
    which this otherwise mirrors): just a fetch + a list of toggles.
*/

#pragma once

#include <borealis.hpp>
#include <string>
#include <vector>

class LoadingSpinner;

class HubVisibilityManager : public brls::Box {
public:
    HubVisibilityManager();

    brls::View* getDefaultFocus() override;

    // --- called by the rows ---
    void toggleVisible(int index);

private:
    struct Entry {
        std::string identifier;
        std::string title;
        bool hidden = false;
    };

    void doRequest();
    void fetchSections();  // second stage of doRequest(), after Continue Watching
    void finishIfDone();
    void rebuild();

    std::vector<Entry> entries;
    bool loaded = false;
    int pendingFocus = 0;
    int pendingSections = 0;  // outstanding getSectionHubs() calls

    brls::Box* rowsBox = nullptr;
    brls::View* focusTarget = nullptr;
    LoadingSpinner* spinner = nullptr;
};
