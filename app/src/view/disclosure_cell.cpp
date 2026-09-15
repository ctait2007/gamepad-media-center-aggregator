/*
    GMCA — disclosure cell (see disclosure_cell.hpp).
*/

#include "view/disclosure_cell.hpp"

#include "view/settings_cells.hpp"

#include <cstdio>

namespace {

/// Material "chevron_right" (U+E5CC) as a path, geometrically centered in a
/// 24x24 box (the glyph spans y 6..18, centered on 12). %s is the fill color.
const char* kChevronSVG =
    R"(<svg width="24" height="24" viewBox="0 0 24 24"><path d="M10 6L8.59 7.41 13.17 12l-4.58 4.59L10 18l6-6z" fill="%s"/></svg>)";

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

}  // namespace

DisclosureCell::DisclosureCell() {
    // Same row shape as every other settings row (title over subtitle, pill
    // focus ring, no fill change) — see view/settings_cells.hpp.
    this->parts = settings_row::skin(this, this->title, this->detail);
    this->registerStringXMLAttribute("subtitle", [this](std::string value) {
        settings_row::setSubtitle(this->parts, value);
    });
    this->registerStringXMLAttribute("icon", [this](std::string value) {
        settings_row::setIcon(this, this->parts, value);
    });

    // DetailCell inflated a title + a value Label. We don't use the value text:
    // hide it and append an SVG chevron the Box centers vertically
    // (alignItems="center") — the geometry the Material glyph could not give us.
    this->detail->setVisibility(brls::Visibility::GONE);

    // Match DetailCell's value color so the chevron reads as chrome, not accent.
    // TextSecondary, the colour the reference gives a trailing chevron.
    const NVGcolor color = brls::Application::getTheme().getColor("font/grey");
    char svg[320];
    std::snprintf(svg, sizeof(svg), kChevronSVG, hex(color).c_str());

    this->chevron = new SVGImage();
    this->chevron->setWidth(40);
    this->chevron->setHeight(40);
    this->chevron->setShrink(0);
    this->chevron->setMarginLeft(20);
    this->chevron->setImageFromSVGString(svg);
    this->addView(this->chevron);
}

void DisclosureCell::onLayout() { settings_row::keepPillRing(this); }

brls::View* DisclosureCell::create() { return new DisclosureCell(); }
