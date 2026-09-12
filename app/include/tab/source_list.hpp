/*
    GMCA — full-screen source picker (Stremio/Nuvio).

    Sources are NOT resolved when a movie or show detail page opens any more:
    resolving them means fanning out a /stream request to every addon, which is
    the slowest thing the app does, and most of the time the user is browsing,
    not playing. This view is what performs that fetch — it is presented when
    Play is pressed on a movie or an episode is selected, so the cost is paid
    once, deliberately, at the moment the user has actually chosen something.

    Layout follows NuvioTV's stream screen: the item's art behind, its title on
    the left, and on the right a filter row (refresh / All / one pill per addon)
    over a list of cards. Each card prints the addon's OWN text with its line
    breaks intact (media::Media::labelRaw/detailRaw) rather than our flattened
    one-liner, because addons format those lines deliberately.
*/

#pragma once

#include <borealis.hpp>
#include "api/media/types.hpp"

class RecyclingGrid;

class SourceList : public brls::Box {
public:
    /// @param item     the movie or episode to play (ratingKey drives the fetch)
    /// @param title    window/player title ("Show · S1E2 · Name" or "Movie (Year)")
    /// @param resumeMs resume position to hand the player, already computed by
    ///                 the caller (Next Up's progress-aware resume, for one) —
    ///                 passed through rather than re-derived, since the fresh
    ///                 detail fetch here carries a viewOffset addons know
    ///                 nothing about.
    SourceList(const media::Item& item, std::string title, int64_t resumeMs);
    ~SourceList() override;

    static brls::View* create();

private:
    void fetchSources();
    void renderSources();
    /// Rebuild the addon filter pills from whatever the fetch returned.
    void buildFilters();
    void applyFilter(const std::string& addon);
    void play(int mediaIndex);
    void showMessage(const std::string& text, bool spinner);

    media::Item item;
    std::string title;
    int64_t resumeMs = 0;
    std::string activeAddon;  // empty = "All"
    std::vector<media::Media> sources;
    bool loading = false;

    BRLS_BIND(brls::Label, labelTitle, "source/title");
    BRLS_BIND(brls::Label, labelSubtitle, "source/subtitle");
    BRLS_BIND(brls::Label, labelMeta, "source/meta");
    BRLS_BIND(brls::Image, imageBackdrop, "source/backdrop");
    BRLS_BIND(brls::Box, boxFilters, "source/filters");
    BRLS_BIND(brls::Box, boxList, "source/list");
    BRLS_BIND(brls::ScrollingFrame, scroll, "source/scroll");
    BRLS_BIND(brls::Box, boxMessage, "source/message");
    BRLS_BIND(brls::Label, labelMessage, "source/message/label");
};
