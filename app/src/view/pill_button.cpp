#include "view/pill_button.hpp"

PillButton::PillButton(const std::string& text, bool active, std::function<void()> onPress, Style style)
    : style(style) {
    this->setAxis(brls::Axis::ROW);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setJustifyContent(brls::JustifyContent::CENTER);
    this->setFocusable(true);
    // Keep the pill's OWN fill when focused. borealis otherwise paints
    // brls/highlight/background over it, which hid the active pill's light
    // capsule and left dark text on a dark fill. The focus ring still draws.
    this->setHideHighlightBackground(true);

    this->label = new brls::Label();
    this->label->setText(text);

    if (style == Style::Filter) {
        // tv-material3 FilterChip: 36dp tall, 20dp corner, labelLarge text,
        // and a hairline border in the reference's Border grey. Chips sit
        // spacing.lg (16dp) apart.
        this->setHeight(72);
        this->setPadding(0, 32, 0, 32);
        this->setMarginRight(32);
        // Compose clamps the 20dp corner to half the 36dp height, so the chip
        // is a true stadium there; nanovg would draw the un-clamped arc as an
        // oval, so clamp it here too.
        this->setCornerRadius(36);
        this->setHighlightCornerRadius(40);
        this->setBorderThickness(2);
        this->label->setFontSize(28);
        this->label->setFontWeight("medium");
    } else {
        // NuvioTV's season chip: titleMedium text (16sp at its 2.0 density =
        // 32px here) in a fully-rounded capsule with generous padding.
        this->setHeight(62);
        this->setPadding(0, 30, 0, 30);
        this->setMarginRight(14);
        this->setCornerRadius(31);
        this->setHighlightCornerRadius(35);
        this->label->setFontSize(32);
        this->label->setFontWeight("medium");
    }
    this->addView(this->label);

    this->active = active;
    this->restyle();

    this->registerClickAction([onPress](brls::View*) {
        onPress();
        return true;
    });
    this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
}

void PillButton::setActive(bool active) {
    this->active = active;
    this->restyle();
}

void PillButton::onFocusGained() {
    brls::Box::onFocusGained();
    this->focused = true;
    this->restyle();
}

void PillButton::onFocusLost() {
    brls::Box::onFocusLost();
    this->focused = false;
    this->restyle();
}

void PillButton::restyle() {
    auto theme = brls::Application::getTheme();
    if (this->style == Style::Filter) {
        // The source picker draws on a dark scrim over the artwork in BOTH
        // themes, so these are the reference's own dark-surface values rather
        // than theme tokens that invert in light mode.
        const bool lit = this->active || this->focused;
        this->setBackgroundColor(lit ? theme.getColor("color/app") : nvgRGB(0x24, 0x24, 0x24));
        this->setBorderColor(lit ? theme.getColor("color/app") : nvgRGB(0x33, 0x33, 0x33));
        if (this->label)
            this->label->setTextColor(
                lit ? theme.getColor("brls/button/primary_enabled_text") : nvgRGB(0xB3, 0xB3, 0xB3));
        return;
    }
    this->setBackgroundColor(this->active ? theme.getColor("brls/text") : theme.getColor("color/pill"));
    if (this->label)
        this->label->setTextColor(
            this->active ? theme.getColor("brls/background") : theme.getColor("brls/text"));
}
