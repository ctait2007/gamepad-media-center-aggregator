/*
    GMCA — a Nuvio-style selector pill.

    NuvioTV uses a fully-rounded chip for every inline selector, but it does
    NOT style them all alike, so this carries both of its variants:

      Season  (EpisodesSection.SeasonTabs) — the show page's season picker:
              titleMedium in a light capsule, focus drawn as a ring only.

      Filter  (StreamComponents.AddonFilterChips) — the source picker's
              All / per-addon row. A Material3 tv FilterChip: labelLarge, 36dp
              tall, hairline-bordered over BackgroundCard, and the SELECTED (or
              focused) chip filled with the theme accent (Secondary) with
              OnSecondary text — NOT the white capsule the season picker uses.
              In the reference, moving focus along this row also moves the
              selection, so focused and selected look the same on purpose.
*/

#pragma once

#include <borealis.hpp>
#include <functional>
#include <string>

class PillButton : public brls::Box {
public:
    enum class Style {
        Season,  ///< the show page's season picker
        Filter,  ///< the source picker's addon filter row
    };

    PillButton(const std::string& text, bool active, std::function<void()> onPress,
        Style style = Style::Season);

    /// Re-style in place when the selection moves, so switching pills does not
    /// have to rebuild (and re-focus) the whole row.
    void setActive(bool active);

    void onFocusGained() override;
    void onFocusLost() override;

private:
    void restyle();

    brls::Label* label = nullptr;
    Style style = Style::Season;
    bool active = false;
    bool focused = false;
};
