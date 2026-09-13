/*
    GMCA — anchored dropdown menu (see dropdown_panel.hpp).
*/

#include "view/dropdown_panel.hpp"

using namespace brls::literals;

namespace {

// The reference's measurements at its 2.0 density (dp -> px):
constexpr float kRowHeight   = 96;   // DropdownMenuItem min height, 48dp
constexpr float kRowGap      = 8;    // vertical 4dp around each strip
constexpr float kListPadding = 16;   // DropdownMenuVerticalPadding, 8dp
constexpr float kSidePadding = 12;   // horizontal 6dp around each strip
constexpr float kMaxHeight   = 640;  // heightIn(max = 320.dp)
constexpr float kGap         = 8;    // the shadow gap under the anchor

/// Text colour that reads on the accent — the reference derives its
/// OnSecondary the same way (CustomThemePalette.foreground(accent)), because
/// the accent is user-settable and a fixed black or white gets one theme wrong.
NVGcolor onAccent(NVGcolor accent) {
    float luma = 0.299f * accent.r + 0.587f * accent.g + 0.114f * accent.b;
    return luma > 0.55f ? nvgRGB(16, 16, 16) : nvgRGB(255, 255, 255);
}

}  // namespace

DropdownRow::DropdownRow(const std::string& text, bool selected) : selected(selected) {
    this->setAxis(brls::Axis::ROW);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setHeight(kRowHeight);
    this->setPaddingLeft(24);  // DropdownMenuItem content padding, 12dp
    this->setPaddingRight(24);
    this->setCornerRadius(20);  // RoundedCornerShape(10.dp)
    this->setFocusable(true);
    // the strip's own fill IS the focus indication, as in the reference; a
    // border round it on top of that reads as two competing highlights
    this->setHideHighlightBackground(true);
    this->setHideHighlightBorder(true);

    this->label = new brls::Label();
    this->label->setText(text);
    this->label->setFontSize(28);  // labelLarge
    this->label->setSingleLine(true);
    this->label->setGrow(1);
    this->addView(this->label);

    this->applyColors(false);
    this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
}

void DropdownRow::applyColors(bool focused) {
    auto theme = brls::Application::getTheme();
    NVGcolor accent = theme.getColor("color/app");
    if (focused) {
        this->setBackgroundColor(accent);
        this->label->setTextColor(onAccent(accent));
        return;
    }
    if (this->selected) {
        NVGcolor wash = accent;
        wash.a = 0.20f;
        this->setBackgroundColor(wash);
    } else {
        this->setBackgroundColor(nvgRGBA(0, 0, 0, 0));
    }
    this->label->setTextColor(theme.getColor("brls/text"));
}

void DropdownRow::onFocusGained() {
    brls::Box::onFocusGained();
    this->applyColors(true);
}

void DropdownRow::onFocusLost() {
    brls::Box::onFocusLost();
    this->applyColors(false);
}

DropdownPanel::DropdownPanel(
    brls::View* anchor, const std::vector<std::string>& options, int selected, std::function<void(int)> onSelect) {
    auto theme = brls::Application::getTheme();

    this->setAxis(brls::Axis::ROW);
    this->setWidth(brls::Application::contentWidth);
    this->setHeight(brls::Application::contentHeight);

    // Tapping (or B-ing) anywhere outside closes, like the reference's
    // onDismissRequest.
    auto* scrim = new brls::Box();
    scrim->setPositionType(brls::PositionType::ABSOLUTE);
    scrim->setPositionTop(0);
    scrim->setPositionLeft(0);
    scrim->setWidth(brls::Application::contentWidth);
    scrim->setHeight(brls::Application::contentHeight);
    scrim->setHideClickAnimation(true);
    scrim->registerClickAction([](brls::View*) {
        brls::Application::popActivity();
        return true;
    });
    scrim->addGestureRecognizer(new brls::TapGestureRecognizer(scrim));
    this->addView(scrim);

    float anchorX = anchor->getX();
    float anchorY = anchor->getY();
    float anchorW = anchor->getWidth();
    float anchorH = anchor->getHeight();

    float wanted = kListPadding * 2 + (float)options.size() * (kRowHeight + kRowGap);
    float height = wanted < kMaxHeight ? wanted : kMaxHeight;

    // Below the picker normally; above it when there is not room below, which
    // is what a Material DropdownMenu does and what the second filter row
    // needs (its pickers sit low enough that a full list would run off).
    float top = anchorY + anchorH + kGap;
    if (top + height > brls::Application::contentHeight - kGap) {
        float above = anchorY - kGap - height;
        top = above >= kGap ? above : brls::Application::contentHeight - kGap - height;
        if (top < kGap) top = kGap;
    }

    auto* panel = new brls::Box();
    panel->setPositionType(brls::PositionType::ABSOLUTE);
    panel->setPositionLeft(anchorX);
    panel->setPositionTop(top);
    panel->setWidth(anchorW);
    panel->setHeight(height);
    panel->setCornerRadius(28);      // RoundedCornerShape(14.dp)
    panel->setBorderThickness(2);    // spacing.hairline
    panel->setBackgroundColor(theme.getColor("color/surface"));  // BackgroundCard
    panel->setBorderColor(theme.getColor("color/grey_2"));       // Border
    panel->setClipsToBounds(true);
    this->addView(panel);

    auto* scroll = new brls::ScrollingFrame();
    scroll->setGrow(1);
    scroll->setWidth(anchorW);

    auto* list = new brls::Box();
    list->setAxis(brls::Axis::COLUMN);
    list->setPaddingTop(kListPadding);
    list->setPaddingBottom(kListPadding);
    list->setPaddingLeft(kSidePadding);
    list->setPaddingRight(kSidePadding);

    for (size_t i = 0; i < options.size(); i++) {
        bool isSelected = (int)i == selected;
        auto* row = new DropdownRow(options[i], isSelected);
        if (i > 0) row->setMarginTop(kRowGap);
        int index = (int)i;
        row->registerClickAction([onSelect, index](brls::View*) {
            // Close before reporting: the callback typically reloads the
            // screen the picker lives on, and it must not do that from inside
            // a view this pop is about to destroy.
            brls::Application::popActivity(brls::TransitionAnimation::NONE, [onSelect, index]() {
                if (onSelect) onSelect(index);
            });
            return true;
        });
        if (isSelected) this->focusRow = row;
        list->addView(row);
    }

    scroll->setContentView(list);
    panel->addView(scroll);

    this->registerAction("hints/back"_i18n, brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    });
}

void DropdownPanel::present() { brls::Application::pushActivity(new brls::Activity(this)); }

brls::View* DropdownPanel::getDefaultFocus() {
    // Open on what is currently selected, not on the first row.
    if (this->focusRow) return this->focusRow;
    return brls::Box::getDefaultFocus();
}
