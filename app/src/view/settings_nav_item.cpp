/*
    GMCA — Settings category rail entry (see settings_nav_item.hpp).
*/

#include "view/settings_nav_item.hpp"

#include "view/svg_image.hpp"

#include <cstdio>

namespace {

/// "#RRGGBB" of an NVGcolor.
std::string hex(NVGcolor c) {
    auto to8 = [](float f) -> int {
        int v = static_cast<int>(f * 255.0f + 0.5f);
        return v < 0 ? 0 : (v > 255 ? 255 : v);
    };
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", to8(c.r), to8(c.g), to8(c.b));
    return std::string(buf);
}

/// Material "chevron_right" (U+E5CC), the same path DisclosureCell draws.
const char* kChevron = "M10 6L8.59 7.41 13.17 12l-4.58 4.59L10 18l6-6z";

SVGImage* glyph(const std::string& path, NVGcolor color, float size) {
    char svg[1400];
    std::snprintf(svg, sizeof(svg),
        R"(<svg width="24" height="24" viewBox="0 0 24 24"><path d="%s" fill="%s"/></svg>)", path.c_str(),
        hex(color).c_str());
    auto* img = new SVGImage();
    img->setWidth(size);
    img->setHeight(size);
    img->setShrink(0);
    img->setImageFromSVGString(svg);
    return img;
}

}  // namespace

SettingsNavItem::SettingsNavItem(
    const std::string& icon, const std::string& title, std::function<void()> onSelect) {
    auto theme = brls::Application::getTheme();

    this->setAxis(brls::Axis::ROW);
    this->setAlignItems(brls::AlignItems::CENTER);
    // SettingsScreen's rail entry, measured off the reference: a 440 x 100
    // capsule every 140, with the glyph inset 42 from its left edge.
    this->setHeight(100);
    this->setPadding(0, 32, 0, 42);
    this->setMarginBottom(40);
    this->setCornerRadius(50);
    this->setHighlightCornerRadius(54);
    this->setFocusable(true);
    // Keep the pill's own fill (and the selected outline) under the halo:
    // borealis would otherwise paint brls/highlight/background over both.
    this->setHideHighlightBackground(true);

    auto* iconView = glyph(icon, theme.getColor("brls/text"), 40);
    iconView->setMarginRight(24);
    this->addView(iconView);

    this->label = new brls::Label();
    this->label->setText(title);
    this->label->setFontSize(32);  // titleMedium
    this->label->setFontWeight("medium");
    this->label->setSingleLine(true);
    this->label->setGrow(1);
    this->label->setTextColor(theme.getColor("brls/text"));
    this->addView(this->label);

    // grey, not the list-value accent: the reference's chevrons are chrome
    this->addView(glyph(kChevron, theme.getColor("font/grey"), 32));

    this->setActive(false);

    this->registerClickAction([onSelect](brls::View*) {
        onSelect();
        return true;
    });
    this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
}

void SettingsNavItem::setActive(bool active) {
    this->active = active;
    NVGcolor accent = brls::Application::getTheme().getColor("color/app");
    if (active) {
        NVGcolor fill = accent;
        fill.a = 0.14f;
        this->setBackgroundColor(fill);
        this->setBorderColor(accent);
        this->setBorderThickness(3);
    } else {
        this->setBackgroundColor(brls::Application::getTheme().getColor("color/pill"));
        this->setBorderThickness(0);
    }
}
