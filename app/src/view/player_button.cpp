#include "view/player_button.hpp"

#include "view/svg_image.hpp"

namespace player_icon {
// Material Symbols, 24x24, the same set NuvioTV's control row draws.
const char* PLAY      = "M8 5v14l11-7z";
const char* PAUSE     = "M6 19h4V5H6v14zm8-14v14h4V5h-4z";
const char* SKIP_NEXT = "M6 18l8.5-6L6 6v12zM16 6v12h2V6h-2z";
const char* CLOSED_CAPTION =
    "M19 4H5c-1.11 0-2 .9-2 2v12c0 1.1.89 2 2 2h14c1.1 0 2-.9 2-2V6c0-1.1-.9-2-2-2zm-8 7H9.5v-.5h-2v3h2V13H11v1c0 "
    ".55-.45 1-1 1H7c-.55 0-1-.45-1-1v-4c0-.55.45-1 1-1h3c.55 0 1 .45 1 1v1zm7 0h-1.5v-.5h-2v3h2V13H18v1c0 .55-.45 "
    "1-1 1h-3c-.55 0-1-.45-1-1v-4c0-.55.45-1 1-1h3c.55 0 1 .45 1 1v1z";
const char* AUDIO =
    "M3 9v6h4l5 5V4L7 9H3zm13.5 3c0-1.77-1.02-3.29-2.5-4.03v8.05c1.48-.73 2.5-2.25 2.5-4.02zM14 3.23v2.06c2.89.86 5 "
    "3.54 5 6.71s-2.11 5.85-5 6.71v2.06c4.01-.91 7-4.49 7-8.77s-2.99-7.86-7-8.77z";
const char* SOURCES  = "M6.99 11L3 15l3.99 4v-3H14v-2H6.99v-3zM21 9l-3.99-4v3H10v2h7.01v3L21 9z";
const char* EPISODES = "M3 13h2v-2H3v2zm0 4h2v-2H3v2zm0-8h2V7H3v2zm4 4h14v-2H7v2zm0 4h14v-2H7v2zM7 7v2h14V7H7z";
const char* INFO =
    "M11 7h2v2h-2zm0 4h2v6h-2zm1-9C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm0 18c-4.41 "
    "0-8-3.59-8-8s3.59-8 8-8 8 3.59 8 8-3.59 8-8 8z";
}  // namespace player_icon

namespace {

/// 48dp circle, 24dp glyph, 4dp apart — the reference's ControlButton sizes at
/// its 2.0 density.
constexpr float kSize  = 96;
constexpr float kGlyph = 48;

std::string hex(NVGcolor c) {
    auto to8 = [](float f) {
        int v = (int)(f * 255.0f + 0.5f);
        return v < 0 ? 0 : (v > 255 ? 255 : v);
    };
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", to8(c.r), to8(c.g), to8(c.b));
    return buf;
}

}  // namespace

PlayerButton::PlayerButton() {
    this->setAxis(brls::Axis::ROW);
    this->setDimensions(kSize, kSize);
    this->setShrink(0);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setJustifyContent(brls::JustifyContent::CENTER);
    this->setCornerRadius(kSize / 2);
    this->setFocusable(true);
    this->setHideHighlight(true);
    this->setMarginLeft(4);
    this->setMarginRight(4);

    this->glyph = new SVGImage();
    this->glyph->setDimensions(kGlyph, kGlyph);
    this->glyph->setShrink(0);
    this->addView(this->glyph);

    this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
    this->render();
}

void PlayerButton::setIconPath(const char* path) {
    this->iconPath = path;
    this->render();
}

void PlayerButton::setOnClick(std::function<void()> onClick) {
    this->registerClickAction([onClick](brls::View*) {
        if (onClick) onClick();
        return true;
    });
}

void PlayerButton::render() {
    // White disc + dark glyph when focused; transparent + white glyph otherwise.
    this->setBackgroundColor(
        this->focused ? nvgRGB(255, 255, 255) : nvgRGBA(0, 0, 0, 0));
    if (!this->iconPath) return;
    char svg[1600];
    std::snprintf(svg, sizeof(svg), R"(<svg width="24" height="24" viewBox="0 0 24 24"><path d="%s" fill="%s"/></svg>)",
        this->iconPath, hex(this->focused ? nvgRGB(10, 10, 10) : nvgRGB(255, 255, 255)).c_str());
    this->glyph->setImageFromSVGString(svg);
}

void PlayerButton::onFocusGained() {
    brls::Box::onFocusGained();
    this->focused = true;
    this->render();
}

void PlayerButton::onFocusLost() {
    brls::Box::onFocusLost();
    this->focused = false;
    this->render();
}

brls::View* PlayerButton::create() { return new PlayerButton(); }
