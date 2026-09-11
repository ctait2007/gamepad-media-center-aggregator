/*
    GMCA — hub visibility manager (Settings > Home Rows).

    Lists the current backend's hub rows (Continue Watching + every catalog
    row, uncapped) and lets the user show/hide and reorder each one — a
    global (not per-server) setting applied wherever hubs render (Home,
    Movie/Show suggestion tabs, catalog tabs). See AppConfig::isHubHidden/
    setHubHidden and getHubOrder/setHubOrder. Non-hidden rows always sort
    above hidden ones; only non-hidden rows can be reordered (mirrors
    LibraryManager's grab mode, dropping the icon column it doesn't need):
      - A       : grab / drop the focused row (non-hidden rows only)
      - D-pad ▲▼: move the row while grabbed
      - Y       : show / hide the focused row (when not grabbed)
      - B       : back (registered by the detail-view host)
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
    bool isGrabbed(int index) const { return this->grabbedIndex == index; }
    bool anyGrabbed() const { return this->grabbedIndex >= 0; }
    void toggleGrab(int index);
    void moveGrabbed(int delta);
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
    void sortEntries();   // non-hidden first (saved order), then hidden
    void persistOrder();  // saves the current non-hidden order
    void rebuild();

    std::vector<Entry> entries;
    bool loaded = false;
    int grabbedIndex = -1;
    int pendingFocus = 0;
    int pendingSections = 0;  // outstanding getSectionHubs() calls

    brls::Box* rowsBox = nullptr;
    brls::View* focusTarget = nullptr;
    LoadingSpinner* spinner = nullptr;
};
