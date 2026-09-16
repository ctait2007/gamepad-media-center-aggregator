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

std::string svgString(const std::string& path, NVGcolor color) {
    char svg[1400];
    std::snprintf(svg, sizeof(svg),
        R"(<svg width="24" height="24" viewBox="0 0 24 24"><path d="%s" fill="%s"/></svg>)", path.c_str(),
        hex(color).c_str());
    return svg;
}

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

std::string SettingsNavItem::svgFor(const std::string& path, NVGcolor color) { return svgString(path, color); }

SettingsNavItem::SettingsNavItem(
    const std::string& icon, const std::string& title, std::function<void()> onSelect) {
    auto theme = brls::Application::getTheme();

    this->setAxis(brls::Axis::ROW);
    this->setAlignItems(brls::AlignItems::CENTER);
    // SettingsRailButton, at the reference's own numbers (components.settings
    // .railItemHeight 56dp, 18dp horizontal padding, an 18dp glyph 10dp before
    // the title, an 18dp chevron) doubled for density 2.0 -- and checked
    // against a pixel scan of its screenshots: 112 tall, 28 apart, 440 wide,
    // the glyph inset 36.
    this->setHeight(112);
    this->setPadding(0, 36, 0, 36);
    this->setMarginTop(14);
    this->setMarginBottom(14);
    this->setCornerRadius(56);
    this->setHighlightCornerRadius(56);
    this->setHighlightPadding(-brls::Application::getStyle()["brls/highlight/stroke_width"]);
    this->setFocusable(true);
    // Keep the pill's own fill under the ring: borealis would otherwise paint
    // brls/highlight/background over it, and the reference's rail button keeps
    // its container colour when focused.
    this->setHideHighlightBackground(true);

    this->icon = glyph(icon, theme.getColor("font/grey"), 36);
    this->icon->setMarginRight(20);
    this->iconPath = icon;
    this->addView(this->icon);

    this->label = new brls::Label();
    this->label->setText(title);
    this->label->setFontSize(32);  // titleMedium 16sp
    this->label->setLineHeight(24.0f / 16.0f);
    this->label->setLetterSpacing(0.3f);  // titleMedium 0.15sp
    this->label->setFontWeight("medium");
    this->label->setSingleLine(true);
    this->label->setGrow(1);
    this->label->setTextColor(theme.getColor("font/grey"));
    this->addView(this->label);

    // TextTertiary, not the list-value accent: the reference's chevrons are chrome
    this->addView(glyph(kChevron, nvgRGB(0x80, 0x80, 0x80), 36));

    this->setActive(false);

    this->registerClickAction([onSelect](brls::View*) {
        onSelect();
        return true;
    });
    this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
}

void SettingsNavItem::setActive(bool active) {
    this->active = active;
    this->restyle();
}

void SettingsNavItem::onFocusGained() {
    brls::Box::onFocusGained();
    this->restyle();
}

void SettingsNavItem::onFocusLost() {
    brls::Box::onFocusLost();
    this->restyle();
}

/// SettingsRailButton: BackgroundCard while selected OR focused, Background
/// otherwise; the title goes TextPrimary and SemiBold with it, and the glyph
/// follows the title.
void SettingsNavItem::restyle() {
    const bool lit = this->active || this->isFocused();
    auto theme = brls::Application::getTheme();
    this->setBackgroundColor(lit ? theme.getColor("color/surface") : theme.getColor("brls/background"));
    // A selected-but-unfocused entry keeps a hairline of the focus ring.
    if (this->active && !this->isFocused()) {
        this->setBorderColor(theme.getColor("brls/highlight/color1"));
        this->setBorderThickness(2);
    } else {
        this->setBorderThickness(0);
    }
    const NVGcolor text = lit ? nvgRGB(0xFF, 0xFF, 0xFF) : theme.getColor("font/grey");
    this->label->setTextColor(text);
    this->label->setFontWeight(lit ? "semibold" : "medium");
    this->icon->setImageFromSVGString(svgFor(this->iconPath, text));
}
