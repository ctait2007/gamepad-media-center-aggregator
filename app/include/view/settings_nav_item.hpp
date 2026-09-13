/*
    GMCA — one entry in the Settings category rail.

    NuvioTV's settings screen is not one long list: a rail of category cards
    down the left picks what the right-hand pane shows (SettingsScreen.kt's
    SettingsSectionSpec list). This is one of those cards — an icon, the
    category name and a disclosure chevron in a fully-rounded pill.

    Two states, both of which the reference shows at once: the SELECTED
    category (the one the pane is displaying) carries an accent outline over a
    faintly accent-tinted fill, and the FOCUSED one carries borealis' own focus
    halo on top of whatever it already is. So moving down the rail does not
    change the pane until you press select — the same select-to-switch rule the
    main nav rail follows.
*/

#pragma once

#include <borealis.hpp>
#include <functional>
#include <string>

class SVGImage;

class SettingsNavItem : public brls::Box {
public:
    /// @param icon  a 24x24 SVG path (the `d` attribute), drawn in the text
    ///              colour; Material's own paths, as DisclosureCell does it,
    ///              so no new asset files are needed.
    SettingsNavItem(const std::string& icon, const std::string& title, std::function<void()> onSelect);

    /// Mark this the category the detail pane is showing.
    void setActive(bool active);

private:
    brls::Label* label = nullptr;
    bool active = false;
};
