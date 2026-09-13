/*
    GMCA — player panel chrome (see player_panel.hpp).
*/

#include "view/player_panel.hpp"

#include "view/svg_image.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

using namespace brls::literals;

namespace {

// The reference's tokens at its 2.0 density, doubled:
constexpr float kCardRadius   = 24;  // radii.md, 12dp
constexpr float kCardPadH     = 24;  // spacing.md
constexpr float kCardPadV     = 16;  // spacing.sm
constexpr float kFocusRing    = 4;   // spacing.xxs
constexpr float kRailGap      = 16;  // spacing.sm between cards
constexpr float kPanelWidth   = 1040;  // 520dp
constexpr float kPanelPadding = 48;    // spacing.xl
constexpr float kPanelRadius  = 32;    // spacing.lg on the left corners

/// Material "check" — the tick the reference puts on the current track.
const char* kCheck = "M9 16.17L4.83 12l-1.42 1.41L9 19 21 7l-1.41-1.41z";

std::string hex(NVGcolor c) {
    auto to8 = [](float f) {
        int v = (int)(f * 255.0f + 0.5f);
        return v < 0 ? 0 : (v > 255 ? 255 : v);
    };
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", to8(c.r), to8(c.g), to8(c.b));
    return buf;
}

/// What a card will measure once laid out: its padding, the name, and each
/// optional line under it. Estimated rather than measured because the rail has
/// to know before yoga runs.
float cardHeight(const std::string& detail, const std::string& meta) {
    float h = kCardPadV * 2 + 40;  // padding + the name line
    if (!detail.empty()) h += 8 + 30;
    if (!meta.empty()) h += 8 + 30;
    return h;
}

NVGcolor onAccent(NVGcolor accent) {
    float luma = 0.299f * accent.r + 0.587f * accent.g + 0.114f * accent.b;
    return luma > 0.55f ? nvgRGB(16, 16, 16) : nvgRGB(255, 255, 255);
}

/// Full-bleed layer, positioned absolutely over the whole screen. The overlay's
/// wash is three of these stacked, which is how the scaffold draws it.
brls::Box* layer() {
    auto* b = new brls::Box();
    b->setPositionType(brls::PositionType::ABSOLUTE);
    b->setPositionTop(0);
    b->setPositionLeft(0);
    b->setWidth(brls::Application::contentWidth);
    b->setHeight(brls::Application::contentHeight);
    return b;
}

}  // namespace

PlayerCard::PlayerCard(const std::string& name, const std::string& detail, const std::string& meta, bool selected)
    : selected(selected) {
    this->setAxis(brls::Axis::ROW);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setPadding(kCardPadV, kCardPadH, kCardPadV, kCardPadH);
    this->setCornerRadius(kCardRadius);
    this->setBorderThickness(kFocusRing);
    this->setFocusable(true);
    // The reference rings a focused card and fills a selected one; borealis'
    // own highlight would draw a second, differently-shaped ring over both.
    this->setHideHighlightBackground(true);
    this->setHideHighlightBorder(true);

    auto* text = new brls::Box();
    text->setAxis(brls::Axis::COLUMN);
    text->setGrow(1);

    this->nameLabel = new brls::Label();
    this->nameLabel->setText(name);
    this->nameLabel->setFontSize(32);  // titleMedium
    this->nameLabel->setSingleLine(true);
    text->addView(this->nameLabel);

    if (!detail.empty()) {
        this->detailLabel = new brls::Label();
        this->detailLabel->setText(detail);
        this->detailLabel->setFontSize(24);  // bodySmall
        this->detailLabel->setSingleLine(true);
        this->detailLabel->setMarginTop(8);  // spacing.xs
        text->addView(this->detailLabel);
    }
    if (!meta.empty()) {
        this->metaLabel = new brls::Label();
        this->metaLabel->setText(meta);
        this->metaLabel->setFontSize(24);
        this->metaLabel->setSingleLine(true);
        this->metaLabel->setMarginTop(8);
        text->addView(this->metaLabel);
    }
    this->addView(text);

    this->tick = new SVGImage();
    this->tick->setDimensions(40, 40);
    this->tick->setShrink(0);
    this->tick->setMarginLeft(16);
    this->tick->setVisibility(selected ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    this->addView(this->tick);

    this->applyColors();
    this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
}

void PlayerCard::applyColors() {
    auto theme = brls::Application::getTheme();
    NVGcolor accent = theme.getColor("color/app");
    bool focused = this->isFocused();

    this->setBorderColor(focused ? accent : nvgRGBA(0, 0, 0, 0));

    if (this->selected) {
        this->setBackgroundColor(accent);
        NVGcolor fg = onAccent(accent);
        this->nameLabel->setTextColor(fg);
        this->paintTick(fg);
        NVGcolor dim = fg;
        dim.a = 0.82f;
        if (this->detailLabel) this->detailLabel->setTextColor(dim);
        dim.a = 0.72f;
        if (this->metaLabel) this->metaLabel->setTextColor(dim);
        return;
    }
    // Unselected cards are transparent in the reference; only the wash behind
    // them separates the list from the video.
    this->setBackgroundColor(nvgRGBA(255, 255, 255, focused ? 20 : 0));
    this->nameLabel->setTextColor(nvgRGB(255, 255, 255));
    if (this->detailLabel) this->detailLabel->setTextColor(nvgRGBA(255, 255, 255, 184));
    if (this->metaLabel) this->metaLabel->setTextColor(theme.getColor("font/tertiary"));
}

void PlayerCard::paintTick(NVGcolor color) {
    if (!this->tick || this->tick->getVisibility() != brls::Visibility::VISIBLE) return;
    char svg[300];
    std::snprintf(svg, sizeof(svg), R"(<svg width="24" height="24" viewBox="0 0 24 24"><path d="%s" fill="%s"/></svg>)",
        kCheck, hex(color).c_str());
    this->tick->setImageFromSVGString(svg);
}

void PlayerCard::onFocusGained() {
    brls::Box::onFocusGained();
    this->applyColors();
}

void PlayerCard::onFocusLost() {
    brls::Box::onFocusLost();
    this->applyColors();
}

PlayerRail::PlayerRail(const std::string& title, float width, float maxHeight, bool anchorBottom)
    : maxHeight(maxHeight) {
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(anchorBottom ? brls::JustifyContent::FLEX_END : brls::JustifyContent::FLEX_START);
    this->setWidth(width);

    if (!title.empty()) {
        auto* cap = new brls::Label();
        cap->setText(title);
        cap->setFontSize(28);  // labelLarge
        cap->setTextColor(brls::Application::getTheme().getColor("font/tertiary"));
        cap->setMarginBottom(kRailGap);
        this->addView(cap);
    }

    // ScrollingFrame never measures itself against its content, so the rail
    // keeps its own running total and sets the height as cards arrive.
    this->scroll = new brls::ScrollingFrame();
    this->scroll->setWidth(width);
    this->scroll->setHeight(0);

    this->list = new brls::Box();
    this->list->setAxis(brls::Axis::COLUMN);
    this->scroll->setContentView(this->list);
    this->addView(this->scroll);
}

void PlayerRail::resize() {
    this->scroll->setHeight(std::min(this->contentHeight, this->maxHeight));
}

PlayerCard* PlayerRail::addCard(
    const std::string& name, const std::string& detail, const std::string& meta, bool selected,
    std::function<void()> onClick) {
    auto* card = new PlayerCard(name, detail, meta, selected);
    if (this->first) card->setMarginTop(kRailGap);
    card->registerClickAction([onClick](brls::View*) {
        // Close first, then act: a handler that reloads playback must not do it
        // from inside a view the pop is about to destroy.
        brls::Application::popActivity(brls::TransitionAnimation::NONE, [onClick]() {
            if (onClick) onClick();
        });
        return true;
    });
    this->contentHeight += cardHeight(detail, meta) + (this->first ? kRailGap : 0);
    this->list->addView(card);
    if (!this->first) this->first = card;
    if (selected && !this->preferred) this->preferred = card;
    this->resize();
    return card;
}

PlayerCard* PlayerRail::addControl(const std::string& name, const std::string& value, std::function<void()> onClick) {
    auto* card = new PlayerCard(name, value, "", false);
    if (this->first) card->setMarginTop(kRailGap);
    card->registerClickAction([onClick](brls::View*) {
        if (onClick) onClick();  // stays open: a stepper is pressed repeatedly
        return true;
    });
    this->contentHeight += cardHeight(value, "") + (this->first ? kRailGap : 0);
    this->list->addView(card);
    if (!this->first) this->first = card;
    this->resize();
    return card;
}

void PlayerRail::relabel(PlayerCard* card, const std::string& name, const std::string& value) {
    if (!card) return;
    if (auto* box = dynamic_cast<brls::Box*>(card->getChildren().empty() ? nullptr : card->getChildren()[0])) {
        auto& kids = box->getChildren();
        if (kids.size() > 0)
            if (auto* l = dynamic_cast<brls::Label*>(kids[0])) l->setText(name);
        if (kids.size() > 1)
            if (auto* l = dynamic_cast<brls::Label*>(kids[1])) l->setText(value);
    }
}

PlayerOverlay::PlayerOverlay(float padLeft, float padTop, float padBottom) {
    this->setAxis(brls::Axis::COLUMN);
    this->setWidth(brls::Application::contentWidth);
    this->setHeight(brls::Application::contentHeight);

    // The scaffold's three layers, in its order: a horizontal gradient out of
    // the left edge, a flat tint, then a vertical one down from the top.
    auto* horizontal = layer();
    horizontal->setBackground(brls::ViewBackground::HORIZONTAL_LINEAR);
    horizontal->setBackgroundStartColor(nvgRGBA(0, 0, 0, 224));
    horizontal->setBackgroundEndColor(nvgRGBA(0, 0, 0, 0));
    this->addView(horizontal);

    auto* tint = layer();
    tint->setBackgroundColor(nvgRGBA(0, 0, 0, 87));
    this->addView(tint);

    auto* vertical = layer();
    vertical->setBackground(brls::ViewBackground::VERTICAL_LINEAR);
    vertical->setBackgroundStartColor(nvgRGBA(0, 0, 0, 153));
    vertical->setBackgroundEndColor(nvgRGBA(0, 0, 0, 0));
    this->addView(vertical);

    this->column = new brls::Box();
    this->column->setAxis(brls::Axis::COLUMN);
    this->column->setJustifyContent(brls::JustifyContent::FLEX_END);
    this->column->setPositionType(brls::PositionType::ABSOLUTE);
    this->column->setPositionLeft(padLeft);
    this->column->setPositionTop(padTop);
    this->column->setWidth(brls::Application::contentWidth - padLeft * 2);
    this->column->setHeight(brls::Application::contentHeight - padTop - padBottom);
    this->addView(this->column);

    this->registerAction("hints/back"_i18n, brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    });
}

void PlayerOverlay::present() { brls::Application::pushActivity(new brls::Activity(this)); }

brls::View* PlayerOverlay::getDefaultFocus() {
    if (this->focusTargetView) return this->focusTargetView;
    return brls::Box::getDefaultFocus();
}

PlayerSidePanel::PlayerSidePanel(const std::string& title, const std::string& subtitle) {
    auto theme = brls::Application::getTheme();

    this->setAxis(brls::Axis::ROW);
    this->setJustifyContent(brls::JustifyContent::FLEX_END);
    this->setWidth(brls::Application::contentWidth);
    this->setHeight(brls::Application::contentHeight);

    auto* scrim = layer();
    scrim->setBackgroundColor(nvgRGBA(0, 0, 0, 110));
    scrim->setHideClickAnimation(true);
    scrim->registerClickAction([](brls::View*) {
        brls::Application::popActivity();
        return true;
    });
    scrim->addGestureRecognizer(new brls::TapGestureRecognizer(scrim));
    this->addView(scrim);

    auto* sheet = new brls::Box();
    sheet->setAxis(brls::Axis::COLUMN);
    // Rounded on the hinge side ONLY, as the reference clips it (topStart and
    // bottomStart). borealis' corner radius is uniform, so the sheet is drawn
    // one radius wider than it looks and hangs that much off the right edge —
    // the two corners that should be square end up outside the screen.
    sheet->setPositionType(brls::PositionType::ABSOLUTE);
    sheet->setPositionTop(0);
    sheet->setPositionLeft(brls::Application::contentWidth - kPanelWidth);
    sheet->setWidth(kPanelWidth + kPanelRadius);
    sheet->setHeight(brls::Application::contentHeight);
    sheet->setPadding(kPanelPadding, kPanelPadding + kPanelRadius, kPanelPadding, kPanelPadding);
    sheet->setBackgroundColor(theme.getColor("color/grey_1"));  // BackgroundElevated
    // Rounded on the hinge side only, as the reference clips it.
    sheet->setCornerRadius(kPanelRadius);
    sheet->setClipsToBounds(true);
    this->addView(sheet);

    auto* header = new brls::Box();
    header->setAxis(brls::Axis::ROW);
    header->setAlignItems(brls::AlignItems::CENTER);
    header->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    header->setMarginBottom(32);  // spacing.lg

    auto* head = new brls::Label();
    head->setText(title);
    head->setFontSize(48);  // headlineSmall
    head->setFontWeight("medium");
    head->setSingleLine(true);
    head->setGrow(1);
    header->addView(head);

    auto* close = new PlayerCard("hints/back"_i18n, "", "", false);
    close->setShrink(0);
    close->registerClickAction([](brls::View*) {
        brls::Application::popActivity();
        return true;
    });
    header->addView(close);
    this->closeButton = close;
    sheet->addView(header);

    if (!subtitle.empty()) {
        auto* sub = new brls::Label();
        sub->setText(subtitle);
        sub->setFontSize(32);  // bodyLarge
        sub->setSingleLine(true);
        sub->setTextColor(theme.getColor("font/grey"));
        sub->setMarginBottom(32);
        sheet->addView(sub);
    }

    this->bodyBox = new brls::Box();
    this->bodyBox->setAxis(brls::Axis::COLUMN);
    this->bodyBox->setGrow(1);
    sheet->addView(this->bodyBox);

    this->registerAction("hints/back"_i18n, brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    });
}

void PlayerSidePanel::present() { brls::Application::pushActivity(new brls::Activity(this)); }

brls::View* PlayerSidePanel::getDefaultFocus() {
    if (this->focusTargetView) return this->focusTargetView;
    if (this->closeButton) return this->closeButton;
    return brls::Box::getDefaultFocus();
}
