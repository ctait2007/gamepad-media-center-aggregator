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
constexpr float kPillRing     = 4;     // the accent focus ring on a tab pill

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

    this->trailing = new brls::Label();
    this->trailing->setFontSize(28);  // bodyMedium
    this->trailing->setSingleLine(true);
    this->trailing->setMarginLeft(16);
    this->trailing->setVisibility(brls::Visibility::GONE);
    this->addView(this->trailing);

    this->tick = new SVGImage();
    this->tick->setDimensions(40, 40);
    this->tick->setShrink(0);
    this->tick->setMarginLeft(16);
    this->tick->setVisibility(selected ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    this->addView(this->tick);

    this->applyColors();
    this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
}

void PlayerCard::setSelected(bool selected) {
    this->selected = selected;
    this->tick->setVisibility(
        selected && this->showTick ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    this->applyColors();
}

void PlayerCard::setShowTick(bool show) {
    this->showTick = show;
    this->setSelected(this->selected);
}

void PlayerCard::setTrailingText(const std::string& text) {
    this->trailing->setText(text);
    this->trailing->setVisibility(text.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    this->applyColors();
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
        if (this->trailing) this->trailing->setTextColor(dim);
        if (this->detailLabel) this->detailLabel->setTextColor(dim);
        dim.a = 0.72f;
        if (this->metaLabel) this->metaLabel->setTextColor(dim);
        return;
    }
    // Unselected cards are transparent in the reference; only the wash behind
    // them separates the list from the video.
    this->setBackgroundColor(nvgRGBA(255, 255, 255, focused ? 20 : 0));
    this->nameLabel->setTextColor(nvgRGB(255, 255, 255));
    if (this->trailing) this->trailing->setTextColor(nvgRGBA(255, 255, 255, 179));
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

PlayerPill::PlayerPill(const std::string& text, bool selected) : selected(selected) {
    this->setAxis(brls::Axis::ROW);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setJustifyContent(brls::JustifyContent::CENTER);
    // Snugger than the reference's 20dp: at our text size that left a lot of
    // empty pill either side of a short word like "All", which reads stretched.
    this->setPadding(18, 28, 18, 28);
    this->setCornerRadius(40);
    this->setBorderThickness(kPillRing);
    this->setShrink(0);
    this->setFocusable(true);
    this->setHideHighlightBackground(true);
    this->setHideHighlightBorder(true);

    this->label = new brls::Label();
    this->label->setText(text);
    this->label->setFontSize(28);  // labelLarge
    this->label->setFontWeight("medium");
    this->label->setSingleLine(true);
    this->addView(this->label);

    this->applyColors();
    this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
}

PlayerPill* PlayerPill::icon(const char* path) {
    auto* pill = new PlayerPill("", false);
    pill->label->setVisibility(brls::Visibility::GONE);
    pill->setPadding(18, 18, 18, 18);  // square, so the circle stays round
    pill->glyphPath = path;
    pill->glyph = new SVGImage();
    pill->glyph->setDimensions(40, 40);
    pill->glyph->setShrink(0);
    pill->addView(pill->glyph);
    pill->applyColors();
    return pill;
}

void PlayerPill::setSelected(bool selected) {
    this->selected = selected;
    this->applyColors();
}

void PlayerPill::applyColors() {
    auto theme = brls::Application::getTheme();
    NVGcolor accent = theme.getColor("color/app");
    bool focused = this->isFocused();

    if (this->glyph && this->glyphPath) {
        NVGcolor fg = this->selected ? nvgRGB(0, 0, 0) : nvgRGB(255, 255, 255);
        char svg[400];
        std::snprintf(svg, sizeof(svg),
            R"(<svg width="24" height="24" viewBox="0 0 24 24"><path d="%s" fill="%s"/></svg>)", this->glyphPath,
            hex(fg).c_str());
        this->glyph->setImageFromSVGString(svg);
    }

    // Focus is a RING in the accent, never a fill: a filled pill is how this
    // row says "selected", and using the same language for "focused" made the
    // two impossible to tell apart while moving along the row.
    if (focused) this->setBorderColor(accent);

    if (this->selected) {
        this->setBackgroundColor(nvgRGB(245, 245, 245));
        if (!focused) this->setBorderColor(nvgRGBA(0, 0, 0, 0));
        this->label->setTextColor(nvgRGB(0, 0, 0));
        return;
    }
    this->setBackgroundColor(theme.getColor("color/surface"));
    if (!focused) this->setBorderColor(theme.getColor("color/grey_2"));
    this->label->setTextColor(focused ? nvgRGB(255, 255, 255) : theme.getColor("font/grey"));
}

void PlayerPill::onFocusGained() {
    brls::Box::onFocusGained();
    this->applyColors();
}

void PlayerPill::onFocusLost() {
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
    // No indicator: these panels are short, focus already shows where you are,
    // and a bar down the edge of a floating list reads as clutter.
    this->scroll->setScrollingIndicatorVisible(false);

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
    this->cardList.push_back(card);
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
    this->cardList.push_back(card);
    if (!this->first) this->first = card;
    this->resize();
    return card;
}

void PlayerRail::clear() {
    this->list->clearViews();
    this->cardList.clear();
    this->first = nullptr;
    this->preferred = nullptr;
    this->contentHeight = 0;
    this->resize();
}

PlayerCard* PlayerRail::addSetting(const std::string& label, const std::string& value, std::function<void()> onClick) {
    auto* card = new PlayerCard(label, "", "", false);
    card->setTrailingText(value);
    if (this->first) card->setMarginTop(kRailGap);
    card->registerClickAction([onClick](brls::View*) {
        if (onClick) onClick();  // stays open: these are adjusted repeatedly
        return true;
    });
    this->contentHeight += cardHeight("", "") + (this->first ? kRailGap : 0);
    this->list->addView(card);
    this->cardList.push_back(card);
    if (!this->first) this->first = card;
    this->resize();
    return card;
}

namespace {

/// One end of a stepper: a small round button that fills with the accent when
/// focused, as the reference's StepperButton does.
class StepperButton : public brls::Box {
public:
    StepperButton(const char* glyphPath, std::function<void()> onClick) {
        this->setDimensions(64, 64);
        this->setShrink(0);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setJustifyContent(brls::JustifyContent::CENTER);
        this->setCornerRadius(32);
        this->setFocusable(true);
        this->setHideHighlight(true);
        this->path = glyphPath;

        this->glyph = new SVGImage();
        this->glyph->setDimensions(32, 32);
        this->addView(this->glyph);

        this->registerClickAction([onClick](brls::View*) {
            if (onClick) onClick();  // stays open: a stepper is pressed repeatedly
            return true;
        });
        this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
        this->render();
    }

    void onFocusGained() override {
        brls::Box::onFocusGained();
        this->render();
    }
    void onFocusLost() override {
        brls::Box::onFocusLost();
        this->render();
    }

private:
    void render() {
        NVGcolor accent = brls::Application::getTheme().getColor("color/app");
        bool focused = this->isFocused();
        this->setBackgroundColor(focused ? accent : nvgRGBA(255, 255, 255, 15));
        char svg[300];
        std::snprintf(svg, sizeof(svg),
            R"(<svg width="24" height="24" viewBox="0 0 24 24"><path d="%s" fill="%s"/></svg>)", this->path,
            hex(focused ? onAccent(accent) : nvgRGB(255, 255, 255)).c_str());
        this->glyph->setImageFromSVGString(svg);
    }

    SVGImage* glyph = nullptr;
    const char* path = nullptr;
};

const char* kMinus = "M19 13H5v-2h14v2z";
const char* kPlus  = "M19 13h-6v6h-2v-6H5v-2h6V5h2v6h6v2z";

}  // namespace

std::function<void(const std::string&)> PlayerRail::addStepper(const std::string& label, const std::string& value,
    std::function<void()> onDecrease, std::function<void()> onIncrease) {
    auto theme = brls::Application::getTheme();

    auto* section = new brls::Box();
    section->setAxis(brls::Axis::COLUMN);
    if (this->first) section->setMarginTop(kRailGap);

    auto* cap = new brls::Label();
    cap->setText(label);
    cap->setFontSize(28);  // bodyMedium
    cap->setTextColor(nvgRGB(255, 255, 255));
    cap->setMarginLeft(kCardPadH);
    section->addView(cap);

    auto* row = new brls::Box();
    row->setAxis(brls::Axis::ROW);
    row->setAlignItems(brls::AlignItems::CENTER);
    row->setMarginTop(20);  // 10dp
    row->setMarginLeft(kCardPadH);

    auto* minus = new StepperButton(kMinus, std::move(onDecrease));
    row->addView(minus);

    auto* valueBox = new brls::Box();
    valueBox->setWidth(168);  // valueWidth 84dp
    valueBox->setAlignItems(brls::AlignItems::CENTER);
    valueBox->setJustifyContent(brls::JustifyContent::CENTER);
    valueBox->setCornerRadius(kCardRadius);
    valueBox->setBackgroundColor(nvgRGBA(255, 255, 255, 15));
    valueBox->setPadding(kCardPadV, kCardPadH, kCardPadV, kCardPadH);
    valueBox->setMarginLeft(16);   // spacing.sm
    valueBox->setMarginRight(16);
    auto* valueLabel = new brls::Label();
    valueLabel->setText(value);
    valueLabel->setFontSize(28);
    valueLabel->setSingleLine(true);
    valueLabel->setTextColor(nvgRGB(255, 255, 255));
    valueBox->addView(valueLabel);
    row->addView(valueBox);

    row->addView(new StepperButton(kPlus, std::move(onIncrease)));
    section->addView(row);

    this->contentHeight += 40 + 20 + 64 + (this->first ? kRailGap : 0);
    this->list->addView(section);
    if (!this->first) this->first = minus;
    this->resize();

    return [valueLabel](const std::string& v) { valueLabel->setText(v); };
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

PlayerOverlay::PlayerOverlay(float padLeft, float padTop, float padBottom, bool anchorBottom) {
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
    this->column->setJustifyContent(
        anchorBottom ? brls::JustifyContent::FLEX_END : brls::JustifyContent::FLEX_START);
    this->column->setPositionType(brls::PositionType::ABSOLUTE);
    this->column->setPositionLeft(padLeft);
    this->column->setPositionTop(padTop);
    this->column->setWidth(brls::Application::contentWidth - padLeft * 2);
    this->column->setHeight(brls::Application::contentHeight - padTop - padBottom);
    this->addView(this->column);

    auto dismiss = [](brls::View*) {
        brls::Application::popActivity();
        return true;
    };
    this->registerAction("hints/back"_i18n, brls::BUTTON_B, dismiss);
    // Cross closes it too. On a panel with cards this never fires — the card
    // under the cursor takes A first — but on one that is all text (stream
    // info) it is the only thing between a press and the PLAYER underneath.
    this->registerAction("hints/ok"_i18n, brls::BUTTON_A, dismiss, true);
}

void PlayerOverlay::present() {
    // An overlay with nothing focusable inside it never takes focus, so focus
    // stays on the player behind and every press goes THERE: on the stream-info
    // panel, which is labels and nothing else, cross reached the player's
    // play/pause and brought the OSD back up over the top of it. Make the
    // overlay itself the focus target in that case so it captures input like
    // any other activity. Panels with cards are untouched — this would
    // otherwise steal the focus that belongs to the first card, since a
    // focusable Box returns ITSELF from getDefaultFocus before its children.
    if (!this->focusTargetView && !brls::Box::getDefaultFocus()) {
        this->setFocusable(true);
        this->setHideHighlightBorder(true);
        this->setHideHighlightBackground(true);
    }
    brls::Application::pushActivity(new brls::Activity(this));
}

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
    // No Close button: B closes the sheet, the hint bar already says so, and a
    // button that only repeats a hardware button costs a focus stop.
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

    // Where the reference puts its filter chips and season tabs. Built empty
    // and with no height of its own, so a panel that has none loses nothing.
    this->tabsBox = new brls::Box();
    this->tabsBox->setAxis(brls::Axis::ROW);
    this->tabsBox->setAlignItems(brls::AlignItems::CENTER);
    sheet->addView(this->tabsBox);

    this->bodyBox = new brls::Box();
    this->bodyBox->setAxis(brls::Axis::COLUMN);
    this->bodyBox->setGrow(1);
    sheet->addView(this->bodyBox);

    this->registerAction("hints/back"_i18n, brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    });
}

PlayerSidePanel::~PlayerSidePanel() {
    if (this->onDestroy) this->onDestroy();
}

void PlayerSidePanel::clearContents() {
    if (this->tabsBox) this->tabsBox->clearViews();
    if (this->bodyBox) this->bodyBox->clearViews();
}

void PlayerSidePanel::linkTabs(brls::View* firstRow) {
    if (!this->tabsBox) return;
    brls::View* firstTab = nullptr;
    for (brls::View* v : this->tabsBox->getChildren())
        if (v->isFocusable()) {
            firstTab = v;
            break;
        }
    if (!firstTab) return;

    if (firstRow) firstRow->setCustomNavigationRoute(brls::FocusDirection::UP, firstTab);
    for (brls::View* v : this->tabsBox->getChildren())
        if (v->isFocusable())
            v->setCustomNavigationRoute(brls::FocusDirection::DOWN, firstRow ? firstRow : (brls::View*)this->bodyBox);
}

void PlayerSidePanel::present() { brls::Application::pushActivity(new brls::Activity(this)); }

brls::View* PlayerSidePanel::getDefaultFocus() {
    if (this->focusTargetView) return this->focusTargetView;
    return brls::Box::getDefaultFocus();
}
