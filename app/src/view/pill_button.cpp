#include "view/pill_button.hpp"

PillButton::PillButton(const std::string& text, bool active, std::function<void()> onPress) {
    this->setAxis(brls::Axis::ROW);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setJustifyContent(brls::JustifyContent::CENTER);
    this->setHeight(44);
    this->setPadding(0, 20, 0, 20);
    this->setMarginRight(10);
    this->setCornerRadius(22);  // Nuvio chip = fully rounded
    this->setHighlightCornerRadius(26);
    this->setFocusable(true);
    // Keep the pill's OWN fill when focused. borealis otherwise paints
    // brls/highlight/background over it, which hid the active pill's light
    // capsule and left dark text on a dark fill. The focus ring still draws.
    this->setHideHighlightBackground(true);

    this->label = new brls::Label();
    this->label->setText(text);
    this->label->setFontSize(18);
    this->addView(this->label);

    this->setActive(active);

    this->registerClickAction([onPress](brls::View*) {
        onPress();
        return true;
    });
    this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
}

void PillButton::setActive(bool active) {
    this->active = active;
    auto theme = brls::Application::getTheme();
    this->setBackgroundColor(active ? theme.getColor("brls/text") : theme.getColor("color/pill"));
    if (this->label) this->label->setTextColor(active ? theme.getColor("brls/background") : theme.getColor("brls/text"));
}
