/*
    GMCA — NuvioTV's settings rows (see view/settings_cells.hpp).
*/

#include "view/settings_cells.hpp"

#include <cstdio>

#include "utils/dialog.hpp"
#include "view/settings_dialog.hpp"

using namespace brls::literals;

namespace settings_glyph {
const std::string& path(const std::string& name);
}

namespace {

// NuvioTV runs at density 2.0 on a 1080p screen, so its dp double into our 1:1
// design units. Every number below was read out of the reference's own source
// and then checked against a pixel scan of its screenshots: a row is exactly
// 120px tall, its rows sit 24px apart, and the focus ring is 4px.
//
//   padding      14dp horizontal (ToggleSettingsItem/NavigationSettingsItem),
//                md (12dp) vertical
//   icon         22dp, then lg (16dp) to the text
//   title        bodyLarge   16sp, Regular, TextPrimary (white)
//   subtitle     bodySmall   12sp, Regular, TextSecondary, xxs (2dp) above it
//   value        labelLarge  14sp, MEDIUM, TextSecondary, 10dp before it
//   chevron      20dp, TextSecondary
constexpr float kPadX = 28;
constexpr float kPadY = 24;
constexpr float kIconSize = 44;
constexpr float kIconGap = 32;
constexpr float kTitleSize = 32;
constexpr float kSubtitleSize = 24;
constexpr float kValueSize = 28;
constexpr float kTrailingSize = 40;
constexpr float kTrailingGap = 20;

// A section header is SettingsActionRow, which is padded 18dp instead of 14dp
// and puts an 18dp chevron in TextTertiary at the end.
constexpr float kHeaderPadX = 36;
constexpr float kHeaderTrailingSize = 36;

// The rows of a section are a LazyColumn spaced by md (12dp).
constexpr float kRowGap = 24;

// A row is 120px tall in the reference, measured off its screenshots. Compose
// gives a single line of text the FONT's line box (1.21x the size for Inter);
// nanovg normalises its ascender and descender to 1.0, so a Borealis label is
// exactly its font size tall. The row therefore has to be told its height, and
// the 4dp between title and subtitle has to absorb the rest: with these two,
// both baselines land within a pixel of where the reference puts them.
constexpr float kRowMinHeight = 120;
constexpr float kTitleSubtitleGap = 8;

// Compose lays a single line of text out at the FONT's line height, not at the
// style's — measured on the reference: a 32px title's baseline lands 31px below
// the row's padding, which is Inter's ascender (1984/2048). It only applies the
// style's lineHeight once the text wraps. Borealis behaves the same way round:
// nvgTextBounds ignores the line height for one line and nvgTextBoxBounds
// honours it for several, so these are the reference's tokens and both cases
// land where its do.
constexpr float kTitleLineHeight = 24.0f / 16.0f;      // bodyLarge
constexpr float kSubtitleLineHeight = 16.0f / 12.0f;   // bodySmall
constexpr float kValueLineHeight = 20.0f / 14.0f;      // labelLarge

// Tracking, which the Material3 styles carry and Borealis had no notion of.
// It is what made a settings row read tighter than the reference's: measured
// on its own screenshots, "Loading Overlay" is 257px wide there and was 244
// here, which is the 0.5sp bodyLarge asks for, doubled.
constexpr float kTitleTracking = 1.0f;     // bodyLarge   0.5sp
constexpr float kSubtitleTracking = 0.8f;  // bodySmall   0.4sp
constexpr float kValueTracking = 0.2f;     // labelLarge  0.1sp

// The focus ring is a pill — NuvioTV shapes every settings row with
// `RoundedCornerShape(SettingsPillRadius)`, radii.full.
constexpr float kPillRadius = 999;

// The toggle: 46x24dp track, xxs (2dp) padding, a 20dp white knob.
constexpr float kToggleWidth = 92;
constexpr float kToggleHeight = 48;
constexpr float kToggleKnob = 40;
constexpr float kTogglePad = 4;

/// "#RRGGBB" of an NVGcolor.
std::string hex(NVGcolor c) {
    auto to8 = [](float f) {
        int v = static_cast<int>(f * 255.0f + 0.5f);
        return v < 0 ? 0 : (v > 255 ? 255 : v);
    };
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", to8(c.r), to8(c.g), to8(c.b));
    return buf;
}

std::string svg24(const std::string& path, NVGcolor color) {
    return "<svg width=\"24\" height=\"24\" viewBox=\"0 0 24 24\"><path d=\"" + path + "\" fill=\"" +
        hex(color) + "\"/></svg>";
}

NVGcolor themeColor(const char* name) { return brls::Application::getTheme().getColor(name); }

/// NuvioColorScheme's text ramp, which is fixed rather than per-palette:
/// TextPrimary is white, TextSecondary neutral400, TextTertiary neutral600.
NVGcolor primaryText() { return nvgRGB(0xFF, 0xFF, 0xFF); }
NVGcolor secondaryText() { return themeColor("font/grey"); }
NVGcolor tertiaryText() { return nvgRGB(0x80, 0x80, 0x80); }

/// The row's own fill. containerColor and focusedContainerColor are BOTH
/// `Background` in the reference, so a row is a dark pill that does not change
/// when it takes focus — only the ring around it appears.
NVGcolor rowFill() { return themeColor("brls/background"); }

SVGImage* makeGlyph(const std::string& path, NVGcolor color, float size) {
    auto* image = new SVGImage();
    image->setWidth(size);
    image->setHeight(size);
    image->setShrink(0);
    image->setImageFromSVGString(svg24(path, color));
    return image;
}

}  // namespace

namespace settings_row {

Parts skin(brls::Box* row, brls::Label* title, brls::Label* value, bool header) {
    Parts parts;

    row->setHeight(brls::View::AUTO);
    row->setMinHeight(kRowMinHeight);
    row->setPaddingTop(kPadY);
    row->setPaddingBottom(kPadY);
    row->setPaddingLeft(header ? kHeaderPadX : kPadX);
    row->setPaddingRight(header ? kHeaderPadX : kPadX);
    row->setMarginBottom(kRowGap);
    row->setAlignItems(brls::AlignItems::CENTER);
    // A row is a dark pill on the lighter group card, and it stays that colour
    // when focused: only the ring around it appears.
    row->setBackgroundColor(rowFill());
    row->setHideHighlightBackground(true);
    row->setHighlightCornerRadius(kPillRadius);
    // Compose draws a border INSIDE the component's bounds; Borealis draws its
    // ring outside by the stroke width. Pulling the padding back by that much
    // puts the ring exactly on the row's edge, where the reference's is.
    row->setHighlightPadding(-brls::Application::getStyle()["brls/highlight/stroke_width"]);
    // Borealis rules a hairline under every RecyclerCell. NuvioTV rules one
    // only at the END of a collapsible section, never between rows.
    row->setLineTop(0);
    row->setLineBottom(0);

    // The leading glyph slot sits in front of everything, empty until a row
    // names an icon.
    parts.icon = new SVGImage();
    parts.icon->setWidth(kIconSize);
    parts.icon->setHeight(kIconSize);
    parts.icon->setShrink(0);
    parts.icon->setMarginRight(kIconGap);
    parts.icon->setVisibility(brls::Visibility::GONE);
    row->addView(parts.icon, 0);

    // Title and subtitle stack; the title Label itself is reused so that
    // setText()/setTextColor() on the cell keep working.
    parts.column = new brls::Box(brls::Axis::COLUMN);
    parts.column->setGrow(1);
    parts.column->setShrink(1);
    parts.column->setHeight(brls::View::AUTO);
    parts.column->setAlignItems(brls::AlignItems::FLEX_START);

    size_t titleIndex = 0;
    for (size_t i = 0; i < row->getChildren().size(); i++) {
        if (row->getChildren()[i] == title) {
            titleIndex = i;
            break;
        }
    }
    row->removeView(title, false);
    title->setFontSize(kTitleSize);
    title->setLineHeight(kTitleLineHeight);
    title->setLetterSpacing(kTitleTracking);
    title->setTextColor(primaryText());
    title->setGrow(0);
    title->setMarginRight(0);
    title->setWidth(brls::View::AUTO);
    title->setSingleLine(true);
    parts.column->addView(title);

    parts.subtitle = new brls::Label();
    parts.subtitle->setFontSize(kSubtitleSize);
    parts.subtitle->setLineHeight(kSubtitleLineHeight);
    parts.subtitle->setLetterSpacing(kSubtitleTracking);
    parts.subtitle->setTextColor(secondaryText());
    parts.subtitle->setMarginTop(kTitleSubtitleGap);
    parts.subtitle->setWidth(brls::View::AUTO);
    parts.subtitle->setVisibility(brls::Visibility::GONE);
    parts.column->addView(parts.subtitle);

    row->addView(parts.column, titleIndex);

    if (value) {
        value->setFontSize(kValueSize);
        value->setLineHeight(kValueLineHeight);
        value->setLetterSpacing(kValueTracking);
        // labelLarge is Medium, the one weight a settings row asks for.
        value->setFontWeight("medium");
        // The reference caps a row's value at one line and ellipsizes it.
        value->setSingleLine(true);
        value->setTextColor(secondaryText());
        value->setMarginLeft(kTrailingGap);
    }
    return parts;
}

void setSubtitle(const Parts& parts, const std::string& text) {
    if (!parts.subtitle) return;
    parts.subtitle->setText(text);
    parts.subtitle->setVisibility(text.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
}

void setIcon(brls::View* row, const Parts& parts, const std::string& name) {
    if (!parts.icon) return;
    const std::string& d = settings_glyph::path(name);
    if (d.empty()) {
        parts.icon->setVisibility(brls::Visibility::GONE);
        return;
    }
    // The reference tints it `Primary` while the row is focused and
    // TextSecondary otherwise -- and its Primary is neutral500, a GREY, not the
    // accent. Measured off its own screenshots: #9E9E9E focused, #B3B3B3 not.
    auto repaint = [parts, row, d]() {
        const bool focused = row->isFocused();
        parts.icon->setImageFromSVGString(svg24(d, focused ? nvgRGB(0x9E, 0x9E, 0x9E) : secondaryText()));
    };
    repaint();
    parts.icon->setVisibility(brls::Visibility::VISIBLE);
    row->getFocusEvent()->subscribe([repaint](brls::View*) { repaint(); });
}

void keepPillRing(brls::View* row) {
    row->setHighlightCornerRadius(row->getHeight() / 2);
    row->setCornerRadius(row->getHeight() / 2);
}

SVGImage* makeChevron(bool header) {
    return makeGlyph("M10 6L8.59 7.41 13.17 12l-4.58 4.59L10 18l6-6z", header ? tertiaryText() : secondaryText(),
        header ? kHeaderTrailingSize : kTrailingSize);
}

SVGImage* makeExpandMore() {
    return makeGlyph("M16.59 8.59L12 13.17 7.41 8.59 6 10l6 6 6-6z", tertiaryText(), kHeaderTrailingSize);
}

}  // namespace settings_row

// ---------------------------------------------------------------- SelectorCell

/// NuvioTV asks "which one?" with a centred panel, not a bottom sheet — see
/// view/settings_dialog.hpp. Every picker in the app is a SelectorCell, so
/// pointing this one handler at that dialog is what moves all of them.
SelectorCell::SelectorCell() {
    this->parts = settings_row::skin(this, this->title, this->detail);
    this->addView(settings_row::makeChevron());

    this->registerClickAction([this](View* view) {
        std::vector<settings_dialog::Option> options;
        options.reserve(this->data.size());
        for (size_t i = 0; i < this->data.size(); i++)
            options.push_back({this->data[i], i < this->descriptions.size() ? this->descriptions[i] : "",
                i < this->trailings.size() ? this->trailings[i] : ""});
        settings_dialog::choose(
            this->title->getFullText(), this->parts.subtitle->getFullText(), options, this->selection,
            [this](int selected) { this->setSelection(selected, false); },
            [this]() { this->dismissEvent.fire(this->selection); });
        return true;
    });

    this->registerStringXMLAttribute("subtitle", [this](std::string value) {
        settings_row::setSubtitle(this->parts, value);
    });
    this->registerStringXMLAttribute("icon", [this](std::string value) {
        settings_row::setIcon(this, this->parts, value);
    });
    this->registerBoolXMLAttribute("quitApp", [this](bool value) {
        if (value) this->dismissEvent.subscribe([](int selected) { Dialog::quitApp(); });
    });
}

void SelectorCell::setDescriptions(std::vector<std::string> d) { this->descriptions = std::move(d); }

void SelectorCell::setTrailings(std::vector<std::string> t) { this->trailings = std::move(t); }

void SelectorCell::onLayout() { settings_row::keepPillRing(this); }

brls::View* SelectorCell::create() { return new SelectorCell(); }

// ----------------------------------------------------------------- BooleanCell

BooleanCell::BooleanCell() {
    this->parts = settings_row::skin(this, this->title, this->detail);
    // The reference has no "On"/"Off" text — the pill IS the state.
    this->detail->setVisibility(brls::Visibility::GONE);

    this->pill = new brls::Box(brls::Axis::ROW);
    this->pill->setWidth(kToggleWidth);
    this->pill->setHeight(kToggleHeight);
    this->pill->setShrink(0);
    this->pill->setMarginLeft(24);
    this->pill->setCornerRadius(kToggleHeight / 2);
    this->pill->setPadding(kTogglePad, kTogglePad, kTogglePad, kTogglePad);
    this->pill->setAlignItems(brls::AlignItems::CENTER);

    this->knob = new brls::Box();
    this->knob->setWidth(kToggleKnob);
    this->knob->setHeight(kToggleKnob);
    this->knob->setCornerRadius(kToggleKnob / 2);
    this->knob->setBackgroundColor(nvgRGB(255, 255, 255));
    this->pill->addView(this->knob);
    this->addView(this->pill);

    this->registerStringXMLAttribute("subtitle", [this](std::string value) {
        settings_row::setSubtitle(this->parts, value);
    });
    this->registerStringXMLAttribute("icon", [this](std::string value) {
        settings_row::setIcon(this, this->parts, value);
    });
}

void BooleanCell::syncPill() {
    const bool on = this->isOn();
    this->pillState = on ? 1 : 0;
    // Checked track = the accent at 0.35 alpha; unchecked = the border grey.
    NVGcolor accent = themeColor("color/app");
    this->pill->setBackgroundColor(
        on ? nvgRGBAf(accent.r, accent.g, accent.b, 0.35f) : nvgRGB(0x33, 0x33, 0x33));
    this->pill->setJustifyContent(on ? brls::JustifyContent::FLEX_END : brls::JustifyContent::FLEX_START);
}

void BooleanCell::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
    brls::FrameContext* ctx) {
    // State reaches us through init(), setOn() and the base class' own click
    // action, none of which is virtual — so the pill catches up at draw time
    // rather than by shadowing a method the callers do not go through.
    if (this->pillState != (this->isOn() ? 1 : 0)) this->syncPill();
    brls::BooleanCell::draw(vg, x, y, width, height, style, ctx);
}

void BooleanCell::onLayout() { settings_row::keepPillRing(this); }

brls::View* BooleanCell::create() { return new BooleanCell(); }

// ------------------------------------------------------------------ ActionCell

ActionCell::ActionCell() {
    this->parts = settings_row::skin(this, this->title, nullptr);
    this->checkbox->setVisibility(brls::Visibility::GONE);
    this->chevron = settings_row::makeChevron();
    this->addView(this->chevron);

    this->registerStringXMLAttribute("subtitle", [this](std::string value) {
        settings_row::setSubtitle(this->parts, value);
    });
    this->registerStringXMLAttribute("icon", [this](std::string value) {
        settings_row::setIcon(this, this->parts, value);
    });
}

void ActionCell::onLayout() { settings_row::keepPillRing(this); }

brls::View* ActionCell::create() { return new ActionCell(); }

// ------------------------------------------------------------------- InputCell

InputCell::InputCell() {
    this->parts = settings_row::skin(this, this->title, this->detail);

    this->registerStringXMLAttribute("subtitle", [this](std::string value) {
        settings_row::setSubtitle(this->parts, value);
    });
    this->registerStringXMLAttribute("icon", [this](std::string value) {
        settings_row::setIcon(this, this->parts, value);
    });
}

void InputCell::onLayout() { settings_row::keepPillRing(this); }

brls::View* InputCell::create() { return new InputCell(); }

// -------------------------------------------------------------- SettingsSection

SettingsSection::SettingsSection() : brls::Box(brls::Axis::COLUMN) {
    this->setWidth(brls::View::AUTO);
    this->setHeight(brls::View::AUTO);

    // The header is an action row: title, subtitle, "Open"/"Closed" and the
    // chevron that turns into an ExpandMore once the section is open.
    this->header = new brls::Box(brls::Axis::ROW);
    this->header->setFocusable(true);
    this->headerTitle = new brls::Label();
    this->header->addView(this->headerTitle);
    this->headerValue = new brls::Label();
    this->header->addView(this->headerValue);

    settings_row::Parts headerParts = settings_row::skin(this->header, this->headerTitle, this->headerValue, true);
    this->headerSubtitle = headerParts.subtitle;
    this->headerIcon = settings_row::makeChevron(true);
    this->header->addView(this->headerIcon);

    this->header->registerClickAction([this](View*) {
        this->setExpanded(!this->expanded);
        return true;
    });
    this->addView(this->header);

    this->content = new brls::Box(brls::Axis::COLUMN);
    this->content->setWidth(brls::View::AUTO);
    this->content->setHeight(brls::View::AUTO);
    this->content->setVisibility(brls::Visibility::GONE);
    this->addView(this->content);

    // The reference closes an expanded section with a hairline in Border.
    this->divider = new brls::Box();
    this->divider->setWidth(brls::View::AUTO);
    this->divider->setHeight(2);
    this->divider->setMarginLeft(8);
    this->divider->setMarginRight(8);
    this->divider->setMarginTop(8);
    this->divider->setMarginBottom(8);
    this->divider->setBackgroundColor(nvgRGB(0x33, 0x33, 0x33));
    this->divider->setVisibility(brls::Visibility::GONE);
    this->addView(this->divider);

    this->registerStringXMLAttribute("title", [this](std::string value) {
        this->headerTitle->setText(value);
    });
    this->registerStringXMLAttribute("subtitle", [this](std::string value) {
        settings_row::setSubtitle({nullptr, this->headerSubtitle, nullptr}, value);
    });
    this->registerBoolXMLAttribute("expanded", [this](bool value) { this->setExpanded(value); });

    this->updateHeader();
}

// Setting a size on a parentless view lays it out on the spot, so this runs
// once from inside the constructor before the header exists.
void SettingsSection::onLayout() {
    if (this->header) settings_row::keepPillRing(this->header);
}

void SettingsSection::setExpanded(bool value) {
    this->expanded = value;
    this->content->setVisibility(value ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    this->divider->setVisibility(value ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    this->updateHeader();
}

void SettingsSection::updateHeader() {
    this->headerValue->setText(this->expanded ? "hints/open"_i18n : "hints/closed"_i18n);
    this->header->removeView(this->headerIcon, true);
    this->headerIcon = this->expanded ? settings_row::makeExpandMore() : settings_row::makeChevron(true);
    this->header->addView(this->headerIcon);
}

void SettingsSection::handleXMLElement(tinyxml2::XMLElement* element) {
    this->content->addView(View::createFromXMLElement(element));
}

brls::View* SettingsSection::create() { return new SettingsSection(); }
