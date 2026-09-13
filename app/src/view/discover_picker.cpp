/*
    GMCA — Discover filter dropdown (see discover_picker.hpp).
*/

#include "view/discover_picker.hpp"

#include "view/svg_image.hpp"

#include <cstdio>

namespace {

/// Material "expand_more" (U+E5CF), inlined the way DisclosureCell inlines its
/// chevron so no new asset file is needed.
const char* kChevronDown = "M16.59 8.59L12 13.17 7.41 8.59 6 10l6 6 6-6z";

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

DiscoverPicker::DiscoverPicker(const std::string& caption) : caption(caption) {
    auto theme = brls::Application::getTheme();

    this->setAxis(brls::Axis::ROW);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setHeight(112);
    this->setPadding(0, 28, 0, 32);
    this->setCornerRadius(24);
    this->setHighlightCornerRadius(28);
    this->setBorderThickness(3);
    this->setFocusable(true);
    // keep our own fill and border under the halo
    this->setHideHighlightBackground(true);

    auto* text = new brls::Box();
    text->setAxis(brls::Axis::COLUMN);
    text->setJustifyContent(brls::JustifyContent::CENTER);
    text->setGrow(1);

    auto* cap = new brls::Label();
    cap->setText(caption);
    cap->setFontSize(24);  // bodySmall
    cap->setSingleLine(true);
    cap->setTextColor(theme.getColor("font/grey"));
    text->addView(cap);

    this->value = new brls::Label();
    this->value->setFontSize(32);  // titleMedium
    this->value->setFontWeight("medium");
    this->value->setSingleLine(true);
    this->value->setMarginTop(2);
    this->value->setTextColor(theme.getColor("brls/text"));
    text->addView(this->value);

    this->addView(text);

    char svg[400];
    std::snprintf(svg, sizeof(svg),
        R"(<svg width="24" height="24" viewBox="0 0 24 24"><path d="%s" fill="%s"/></svg>)", kChevronDown,
        hex(theme.getColor("font/grey")).c_str());
    auto* chevron = new SVGImage();
    chevron->setWidth(36);
    chevron->setHeight(36);
    chevron->setShrink(0);
    chevron->setMarginLeft(16);
    chevron->setImageFromSVGString(svg);
    this->addView(chevron);

    this->restyle();

    this->registerClickAction([this](brls::View*) {
        this->open();
        return true;
    });
    this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
}

void DiscoverPicker::setOptions(
    const std::vector<std::string>& options, int selected, const std::string& emptyValue) {
    this->options = options;
    this->selected = selected;
    bool ok = selected >= 0 && selected < (int)options.size();
    this->value->setText(ok ? options[(size_t)selected] : emptyValue);
}

void DiscoverPicker::open() {
    if (this->options.empty()) return;
    int cur = this->selected >= 0 && this->selected < (int)this->options.size() ? this->selected : 0;
    auto cb = this->callback;
    auto* dropdown = new brls::Dropdown(
        this->caption, this->options,
        [cb](int picked) {
            if (cb) cb(picked);
        },
        cur);
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void DiscoverPicker::onFocusGained() {
    brls::Box::onFocusGained();
    this->focused = true;
    this->restyle();
}

void DiscoverPicker::onFocusLost() {
    brls::Box::onFocusLost();
    this->focused = false;
    this->restyle();
}

void DiscoverPicker::restyle() {
    auto theme = brls::Application::getTheme();
    if (this->focused) {
        NVGcolor accent = theme.getColor("color/app");
        NVGcolor fill = accent;
        fill.a = 0.14f;
        this->setBackgroundColor(fill);
        this->setBorderColor(accent);
    } else {
        this->setBackgroundColor(theme.getColor("color/pill"));
        this->setBorderColor(theme.getColor("color/grey_2"));
    }
}
