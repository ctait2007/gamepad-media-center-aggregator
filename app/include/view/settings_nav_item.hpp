/*
    GMCA — one entry in the Settings category rail.

    NuvioTV's settings screen is not one long list: a rail of category cards
    down the left picks what the right-hand pane shows (SettingsScreen.kt's
    SettingsSectionSpec list). This is one of those cards — an icon, the
    category name and a disclosure chevron in a fully-rounded pill.

    Two states, both of which the reference shows at once (SettingsRailButton):
    SELECTED or FOCUSED lifts the pill from Background to BackgroundCard and
    takes its title to TextPrimary SemiBold, and a selected-but-unfocused entry
    keeps a hairline of the focus ring where a focused one gets the full 2dp.
    So moving down the rail does not change the pane until you press select —
    the same select-to-switch rule the main nav rail follows.
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

    void onFocusGained() override;
    void onFocusLost() override;

private:
    void restyle();
    static std::string svgFor(const std::string& path, NVGcolor color);

    brls::Label* label = nullptr;
    SVGImage* icon = nullptr;
    std::string iconPath;
    bool active = false;
};
