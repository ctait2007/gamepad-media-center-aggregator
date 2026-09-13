/*
    GMCA — Search, following NuvioTV's SearchScreen.kt.

    The screen is a field and a list. Focus lands on the field; opening it
    hands off to the platform's own keyboard (the PS4 IME dialog), and the
    search runs as soon as that returns — there is no separate submit step and
    no on-screen keyboard of our own, exactly as the reference has it.

    Below the field sit the recent searches: one focusable row per query, each
    with its own remove button, plus a "clear history" button in the section
    header. Selecting a row re-runs that search. Once the query is long enough
    to search on (MIN_QUERY, 2 as in the reference) the recent list gives way
    to the results.

    NOTE on "results as you type": the PS4's IME is a MODAL system dialog —
    borealis' Ps4ImeManager blocks on sceImeDialogGetStatus until the user
    confirms, and the dialog owns the screen while it is up. There is no
    per-keystroke callback to hook and nothing of ours is visible behind it, so
    on that platform the results necessarily appear the instant the keyboard
    closes. The debounce below is still real: it is what makes a field change
    search by itself rather than waiting for a button.
*/

#pragma once

#include <view/auto_tab_frame.hpp>

#include <memory>
#include <string>

class RecyclingGrid;
class SearchHistory;
class SVGImage;

class SearchTab : public AttachedView {
public:
    SearchTab();
    ~SearchTab();

    void onCreate() override;

    /// Focus starts in the field, as the reference does.
    brls::View* getDefaultFocus() override;

    static brls::View* create();

private:
    BRLS_BIND(brls::Box, fieldBox, "search/field");
    BRLS_BIND(brls::Label, inputLabel, "search/input");
    BRLS_BIND(SVGImage, searchSVG, "search/icon");
    BRLS_BIND(brls::Box, clearButton, "search/clear");
    BRLS_BIND(brls::ScrollingFrame, recentScroll, "search/recent_scroll");
    BRLS_BIND(brls::Box, recentHeader, "search/recent_header");
    BRLS_BIND(brls::Box, recentList, "search/recent_list");
    BRLS_BIND(brls::Box, recentEmpty, "search/recent_empty");
    BRLS_BIND(brls::Box, clearHistory, "search/clear_history");
    BRLS_BIND(RecyclingGrid, results, "search/results");

    /// Open the platform keyboard on the current query.
    void openKeyboard();
    /// Adopt a new query: repaint the field, then either show the recent list
    /// or schedule the search.
    void setQuery(const std::string& query, bool remember);
    void buildRecent();
    void doSearch(const std::string& searchTerm);
    void styleField();

    std::string currentSearch;
    /// Bumped on every query change; a debounce timer or a late response that
    /// carries a stale value belongs to a query the user has moved off.
    uint64_t generation = 0;
    std::unique_ptr<SearchHistory> history;
};
