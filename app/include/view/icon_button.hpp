/*
    GMCA — button with icon + label (brls::Button does not support an icon).
    Styles: "primary" (gold background, dark text) and "bordered" (outline).
    XML attributes: icon (@res/...), text (@i18n/...), buttonStyle.
*/

#pragma once

#include <borealis.hpp>

class SVGImage;

class IconButton : public brls::Box {
public:
    IconButton();

    void setIcon(const std::string& res);
    void setText(const std::string& text);
    void setButtonStyle(const std::string& style);
    /// Muted = visually disabled (dim border + grey text) but STILL focusable, so
    /// a D-pad user can land on it and trigger the "why is this unavailable" help
    /// (the alternative — hiding it — strands the focus highlight on a gone view).
    void setMuted(bool muted);
    /// NuvioTV's ActionIconButton `selected`: the round hero action that is
    /// currently ON inverts — a white disc with a dark glyph — rather than
    /// swapping its icon alone. Only meaningful for the "icon" style.
    void setSelected(bool selected);

    static brls::View* create();

    void onFocusGained() override;
    void onFocusLost() override;

private:
    BRLS_BIND(SVGImage, icon, "icon_button/icon");
    BRLS_BIND(brls::Label, label, "icon_button/label");

    void applyStyle();
    /// The accent at low alpha — a focused "outline" button is tinted, not filled.
    NVGcolor focusTint() const;

    std::string styleName = "bordered";
    bool iconOnly = false;
    bool focused = false;
    bool muted = false;
    bool selected = false;
};
