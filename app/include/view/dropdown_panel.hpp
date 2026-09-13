/*
    GMCA — the menu a DiscoverPicker drops.

    NuvioTV anchors it: a Material3 DropdownMenu pinned under the picker it
    belongs to, exactly as wide as that picker, not the full-screen bottom
    sheet borealis' own Dropdown puts up. SearchDiscoverSection.kt's
    DiscoverDropdownPicker does it with a DropdownMenu whose width is the
    anchor's measured width and whose height is capped at 320dp; the panel is
    BackgroundCard inside a hairline Border at radius 14dp, and each row is a
    rounded strip that fills with the accent when focused. Doubled for our 2.0
    density: width from the anchor, max height 640, radius 28, rows 96 tall.

    Focus opens on the CURRENT value rather than on the first row, which is
    what the reference goes to some trouble to do (its focus requester retries
    for several frames): on a Year list of thirty entries, opening on the first
    row would put the user nowhere near what they have selected.
*/

#pragma once

#include <borealis.hpp>

#include <functional>
#include <string>
#include <vector>

/// One row of the panel. Focus fills it with the accent, the current value
/// carries a dimmer accent wash when it is not the focused row — the
/// reference's Secondary/FocusBackground pair.
class DropdownRow : public brls::Box {
public:
    DropdownRow(const std::string& text, bool selected);

    void onFocusGained() override;
    void onFocusLost() override;

private:
    void applyColors(bool focused);

    brls::Label* label = nullptr;
    bool selected = false;
};

class DropdownPanel : public brls::Box {
public:
    /// @param anchor the view the panel hangs off — its on-screen x, y and
    ///        width are read once, here, so it must already be laid out.
    DropdownPanel(brls::View* anchor, const std::vector<std::string>& options, int selected,
        std::function<void(int)> onSelect);

    /// Pushes the panel as its own translucent activity.
    void present();

    bool isTranslucent() override { return true; }

    brls::View* getDefaultFocus() override;

private:
    brls::View* focusRow = nullptr;
};
