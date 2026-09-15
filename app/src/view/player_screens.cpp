/*
    GMCA — the player's loading and pause screens (see player_screens.hpp).

    Sizes are the reference's dp doubled: NuvioTV runs at density 2.0 on a
    1080p TV and this app's design units are 1:1 with the pixels there, so
    every dp in LoadingOverlay.kt / PauseOverlay.kt is twice the number here.
*/

#include "view/player_screens.hpp"
#include "view/player_button.hpp"
#include "view/player_panel.hpp"
#include "view/svg_image.hpp"
#include "utils/image.hpp"
#include "utils/misc.hpp"

#include <borealis/core/i18n.hpp>
#include <fmt/format.h>

using namespace brls::literals;

namespace {

// LoadingOverlay.kt: the logo box is 320x180 dp, the step text sits 94 dp
// below the centre of the screen.
constexpr float kLogoWidth = 640, kLogoHeight = 360;
constexpr float kStageOffset = 188;

// PauseOverlay.kt's content padding (start/end huge, top 40, bottom 120) and
// the gaps between its blocks (spacing.md / sm / md / lg, then 20 and md).
// TOP-anchored, unlike the reference, whose column arranges to Bottom. Bottom
// anchoring means the block's position depends on how much there is IN it: an
// episode with a long synopsis sits where the reference puts it, and a film
// with two lines and no episode title slides down to the frame edge. Only one
// of those can be right, and it is the one that does not move. 264 px is where
// the reference's own block starts, measured off its screenshot, so the case
// it was drawn for lands exactly where it does — and every other case now
// lands there too instead of somewhere lower.
constexpr float kPausePadX = 112, kPausePadTop = 276, kPausePadBottom = 80;
constexpr float kPauseLogoHeight = 192;

/// A full-screen absolutely-positioned layer, the unit both screens stack.
brls::Box* fullLayer() {
    auto* b = new brls::Box();
    b->setPositionType(brls::PositionType::ABSOLUTE);
    b->setPositionTop(0);
    b->setPositionLeft(0);
    b->setWidth(brls::Application::contentWidth);
    b->setHeight(brls::Application::contentHeight);
    return b;
}

brls::Label* label(float size, NVGcolor color) {
    auto* l = new brls::Label();
    l->setFontSize(size);
    l->setTextColor(color);
    // NO marquee. A borealis Label scrolls its text whenever an ANCESTOR holds
    // focus and the text overruns its width — and the player view holds focus
    // the whole time these are up, so the synopsis slid back and forth across
    // the screen, clipped at both ends, instead of wrapping onto three lines.
    l->setAutoAnimate(false);
    return l;
}

/// The reference caps its synopsis at three lines. borealis' Label has no row
/// limit, so cap the text instead — roughly three lines of the pause screen's
/// column at its font size, cut on a word.
std::string threeLines(const std::string& text) {
    constexpr size_t kBudget = 260;
    if (text.size() <= kBudget) return text;
    size_t cut = text.rfind(' ', kBudget);
    if (cut == std::string::npos || cut < kBudget / 2) cut = kBudget;
    return text.substr(0, cut) + "…";
}

}  // namespace

// ---- LoadingScreen ---------------------------------------------------------

LoadingScreen::LoadingScreen() {
    this->setPositionType(brls::PositionType::ABSOLUTE);
    this->setPositionTop(0);
    this->setPositionLeft(0);
    this->setWidth(brls::Application::contentWidth);
    this->setHeight(brls::Application::contentHeight);
    this->setBackgroundColor(nvgRGB(0, 0, 0));
    this->setVisibility(brls::Visibility::GONE);

    this->backdrop = new brls::Image();
    this->backdrop->setPositionType(brls::PositionType::ABSOLUTE);
    this->backdrop->setPositionTop(0);
    this->backdrop->setPositionLeft(0);
    this->backdrop->setWidth(brls::Application::contentWidth);
    this->backdrop->setHeight(brls::Application::contentHeight);
    // Crop to fill, held to the top — the reference aligns its backdrop TopEnd
    // so a face in the upper third survives the crop rather than the sky.
    this->backdrop->setScalingType(brls::ImageScalingType::FILL);
    this->backdrop->setImageAlign(brls::ImageAlignment::TOP);
    this->backdrop->setVisibility(brls::Visibility::INVISIBLE);
    this->addView(this->backdrop);

    // The reference's scrim is a four-stop vertical ramp (30% at the top to
    // 90% at the bottom). borealis takes two stops, so this is the same ramp
    // with a flat lift under it to bring the middle back up to the reference's.
    auto* tint = fullLayer();
    tint->setBackgroundColor(nvgRGBA(0, 0, 0, 26));
    this->addView(tint);

    auto* ramp = fullLayer();
    ramp->setBackground(brls::ViewBackground::VERTICAL_LINEAR);
    ramp->setBackgroundStartColor(nvgRGBA(0, 0, 0, 77));
    ramp->setBackgroundEndColor(nvgRGBA(0, 0, 0, 230));
    this->addView(ramp);

    // The centred column: a logo (or the title in its place) with the step
    // text below the middle of the screen.
    auto* centre = fullLayer();
    centre->setAxis(brls::Axis::COLUMN);
    centre->setJustifyContent(brls::JustifyContent::CENTER);
    centre->setAlignItems(brls::AlignItems::CENTER);
    this->addView(centre);

    this->logo = new brls::Image();
    this->logo->setWidth(kLogoWidth);
    this->logo->setHeight(kLogoHeight);
    this->logo->setScalingType(brls::ImageScalingType::FIT);
    this->logo->setVisibility(brls::Visibility::GONE);
    centre->addView(this->logo);

    this->titleLabel = label(48, nvgRGB(255, 255, 255));
    this->titleLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    this->titleLabel->setWidth(brls::Application::contentWidth - 192);
    this->titleLabel->setVisibility(brls::Visibility::GONE);
    centre->addView(this->titleLabel);

    // Positioned rather than flowed, so the logo stays dead centre however
    // long the step text is — that is what the reference's offset does.
    auto* stageBox = fullLayer();
    stageBox->setAxis(brls::Axis::COLUMN);
    stageBox->setJustifyContent(brls::JustifyContent::CENTER);
    stageBox->setAlignItems(brls::AlignItems::CENTER);
    stageBox->setPositionTop(kStageOffset);
    this->addView(stageBox);

    this->stageLabel = label(24, nvgRGBA(255, 255, 255, 184));
    this->stageLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    this->stageLabel->setWidth(brls::Application::contentWidth - 192);
    stageBox->addView(this->stageLabel);
}

void LoadingScreen::setArtwork(
    const std::string& backdropUrl, const std::string& logoUrl, const std::string& title) {
    this->titleLabel->setText(title);

    Image::cancel(this->backdrop);
    this->backdrop->setVisibility(brls::Visibility::INVISIBLE);
    if (!backdropUrl.empty()) {
        brls::Image* target = this->backdrop;
        Image::load(target, backdropUrl, (int)brls::Application::contentWidth, 0, [target](bool ok, bool) {
            target->setVisibility(ok ? brls::Visibility::VISIBLE : brls::Visibility::INVISIBLE);
        });
    }

    Image::cancel(this->logo);
    this->logo->setVisibility(brls::Visibility::GONE);
    // The title stands in until the logo lands, and stays if it never does.
    this->titleLabel->setVisibility(title.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    if (logoUrl.empty()) return;

    brls::Image* logoView = this->logo;
    brls::Label* titleView = this->titleLabel;
    std::string fallback = title;
    Image::load(logoView, logoUrl, (int)kLogoWidth, 0, [logoView, titleView, fallback](bool ok, bool) {
        if (!ok) return;  // leave the title standing
        logoView->setVisibility(brls::Visibility::VISIBLE);
        titleView->setVisibility(brls::Visibility::GONE);
    });
}

void LoadingScreen::setStage(const std::string& text) {
    this->stageLabel->setText(text);
    this->stageLabel->setVisibility(text.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
}

void LoadingScreen::show() { this->setVisibility(brls::Visibility::VISIBLE); }

void LoadingScreen::hide() {
    this->setVisibility(brls::Visibility::GONE);
    // Drop the artwork with the screen: two full-frame textures are a lot to
    // hold for the rest of a film, and the next start reloads them anyway.
    Image::cancel(this->backdrop);
    Image::cancel(this->logo);
    this->backdrop->clear();
    this->logo->clear();
}

// ---- PauseScreen -----------------------------------------------------------

PauseScreen::PauseScreen() {
    this->setPositionType(brls::PositionType::ABSOLUTE);
    this->setPositionTop(0);
    this->setPositionLeft(0);
    this->setWidth(brls::Application::contentWidth);
    this->setHeight(brls::Application::contentHeight);
    this->setVisibility(brls::Visibility::GONE);

    // Same scrim as every other overlay over the video — this one IS the
    // reference's PlayerOverlayScaffold, which the panels borrow from.
    addPlayerScrim(this);

    this->clockLabel = label(68, nvgRGBA(255, 255, 255, 242));
    this->clockLabel->setPositionType(brls::PositionType::ABSOLUTE);
    this->clockLabel->setPositionTop(80);
    this->clockLabel->setPositionRight(kPausePadX);
    this->addView(this->clockLabel);

    // Top-anchored — see the padding note above.
    auto* column = fullLayer();
    column->setAxis(brls::Axis::COLUMN);
    column->setJustifyContent(brls::JustifyContent::FLEX_START);
    column->setPaddingLeft(kPausePadX);
    column->setPaddingRight(kPausePadX);
    column->setPaddingTop(kPausePadTop);
    column->setPaddingBottom(kPausePadBottom);
    this->addView(column);

    auto* watching = label(32, nvgRGB(0x80, 0x80, 0x80));
    watching->setText("main/player/pause/watching"_i18n);
    column->addView(watching);

    this->logo = new brls::Image();
    this->logo->setHeight(kPauseLogoHeight);
    this->logo->setScalingType(brls::ImageScalingType::FIT);
    // FLEX_START, not an image alignment: a column child stretches to the full
    // width by default, and a FIT image then centres itself inside all of it.
    // Shrinking the view to the artwork is what puts the wordmark on the left
    // margin, where the reference's BottomStart alignment puts it.
    this->logo->setAlignSelf(brls::AlignSelf::FLEX_START);
    this->logo->setMarginTop(24);
    this->logo->setVisibility(brls::Visibility::GONE);
    column->addView(this->logo);

    // Every line is given the column's inner width: a borealis Label only
    // wraps (or ellipsizes) once it has one, and without it a long synopsis
    // ran off both edges of the screen.
    const float textWidth = brls::Application::contentWidth - kPausePadX * 2;

    this->titleLabel = label(76, nvgRGB(255, 255, 255));
    this->titleLabel->setWidth(textWidth);
    this->titleLabel->setSingleLine(true);
    this->titleLabel->setMarginTop(24);
    column->addView(this->titleLabel);

    this->metaLabel = label(32, nvgRGB(0xB3, 0xB3, 0xB3));
    this->metaLabel->setMarginTop(16);
    column->addView(this->metaLabel);

    this->episodeLabel = label(44, nvgRGB(255, 255, 255));
    this->episodeLabel->setWidth(textWidth);
    this->episodeLabel->setSingleLine(true);
    this->episodeLabel->setMarginTop(24);
    column->addView(this->episodeLabel);

    this->summaryLabel = label(32, nvgRGB(0xB3, 0xB3, 0xB3));
    this->summaryLabel->setWidth(textWidth);
    this->summaryLabel->setMarginTop(32);
    column->addView(this->summaryLabel);

    this->castLabel = label(28, nvgRGB(0x80, 0x80, 0x80));
    this->castLabel->setText("main/player/pause/cast"_i18n);
    this->castLabel->setMarginTop(40);
    column->addView(this->castLabel);

    this->castRow = new brls::Box();
    this->castRow->setAxis(brls::Axis::ROW);
    this->castRow->setMarginTop(24);
    column->addView(this->castRow);
}

void PauseScreen::setItem(const plex::Item& item, const std::string& showTitle, const std::string& logoUrl) {
    auto set = [](brls::Label* l, const std::string& text) {
        l->setText(text);
        l->setVisibility(text.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    };

    bool isEpisode = item.type == "episode";
    std::string title = isEpisode && !item.grandparentTitle.empty() ? item.grandparentTitle
                        : !showTitle.empty()                        ? showTitle
                                                                    : item.title;

    // The logo replaces the title outright when it arrives, as the reference
    // does — a show's wordmark IS its title, and both together reads as a bug.
    set(this->titleLabel, title);
    Image::cancel(this->logo);
    this->logo->setVisibility(brls::Visibility::GONE);
    if (!logoUrl.empty()) {
        brls::Image* logoView = this->logo;
        brls::Label* titleView = this->titleLabel;
        Image::load(logoView, logoUrl, 0, (int)kPauseLogoHeight, [logoView, titleView](bool ok, bool) {
            if (!ok) return;
            logoView->setVisibility(brls::Visibility::VISIBLE);
            titleView->setVisibility(brls::Visibility::GONE);
        });
    }

    // "2024 • S1E3" — the year alone for a film.
    std::string meta;
    if (item.year > 0) meta = std::to_string(item.year);
    if (isEpisode && item.parentIndex > 0) {
        std::string se = fmt::format("S{}E{}", item.parentIndex, item.index);
        meta = meta.empty() ? se : meta + " • " + se;
    }
    set(this->metaLabel, meta);
    set(this->episodeLabel, isEpisode ? item.title : std::string{});
    set(this->summaryLabel, threeLines(item.summary));

    this->castRow->clearViews();
    for (size_t i = 0; i < item.roles.size() && i < 8; i++) {
        auto* chip = new brls::Box();
        chip->setBackgroundColor(nvgRGBA(255, 255, 255, 26));
        chip->setCornerRadius(24);
        chip->setPaddingLeft(36);
        chip->setPaddingRight(36);
        chip->setPaddingTop(20);
        chip->setPaddingBottom(20);
        if (i > 0) chip->setMarginLeft(28);
        auto* name = label(28, nvgRGB(255, 255, 255));
        name->setText(item.roles[i].tag);
        chip->addView(name);
        this->castRow->addView(chip);
    }
    this->castLabel->setVisibility(item.roles.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    this->castRow->setVisibility(item.roles.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
}

void PauseScreen::setClock(const std::string& text) { this->clockLabel->setText(text); }

void PauseScreen::show() { this->setVisibility(brls::Visibility::VISIBLE); }

void PauseScreen::hide() { this->setVisibility(brls::Visibility::GONE); }

// ---- NextEpisodeCard ----------------------------------------------------
//
// PostPlayOverlay.kt's AutoPlay card, at the reference's own numbers: 420 dp
// wide with a 14 dp corner over 0xE3191919 and a hairline white border, a
// 112x64 still with a 9 dp corner under a transparent-to-black-32% wash,
// "Next Episode" at 11 sp over "S2 E2 • The Scrub" at 14 sp semibold, and a
// bordered "Play" pill on the right. Bottom-right of the frame, 26 dp in,
// 122 dp up while the controls are showing and 30 up when they are not.
//
// FOCUSABLE, as the reference's Card is: it takes focus the moment it appears
// with the controls down (its onPlaced), select plays the next episode and
// back dismisses it. Hijacking cross globally instead — which is what this
// did first — left no way to pause over the last of an episode and no way to
// see what cross was about to do.

namespace {
constexpr float kNextCardWidth = 840, kNextCardRadius = 28;
constexpr float kNextPadX = 20, kNextPadY = 18;
constexpr float kNextStillWidth = 224, kNextStillHeight = 128, kNextStillRadius = 18;
constexpr float kNextRight = 52, kNextBottomOsd = 244, kNextBottomBare = 60;
constexpr float kNextPillWidth = 200;
}  // namespace

NextEpisodeCard::NextEpisodeCard() {
    this->setPositionType(brls::PositionType::ABSOLUTE);
    this->setPositionTop(0);
    this->setPositionLeft(0);
    this->setWidth(brls::Application::contentWidth);
    this->setHeight(brls::Application::contentHeight);
    this->setVisibility(brls::Visibility::GONE);
    // No scrim: this one sits over playing video, not over a stopped picture.

    this->card = new brls::Box();
    this->card->setPositionType(brls::PositionType::ABSOLUTE);
    this->card->setPositionRight(kNextRight);
    this->card->setPositionBottom(kNextBottomBare);
    this->card->setWidth(kNextCardWidth);
    this->card->setAxis(brls::Axis::ROW);
    this->card->setAlignItems(brls::AlignItems::CENTER);
    this->card->setCornerRadius(kNextCardRadius);
    this->card->setHighlightCornerRadius(kNextCardRadius + 4);
    this->card->setBackgroundColor(nvgRGBA(0x19, 0x19, 0x19, 0xE3));
    this->card->setBorderThickness(2);
    this->card->setBorderColor(nvgRGBA(255, 255, 255, 41));  // white 16%
    this->card->setPadding(kNextPadY, kNextPadX, kNextPadY, kNextPadX);
    this->card->setFocusable(true);
    // Keep the card's own fill under the focus halo: borealis would otherwise
    // paint brls/highlight/background over it and lose the panel colour.
    this->card->setHideHighlightBackground(true);
    this->addView(this->card);

    // The still, with the reference's transparent -> black 32% wash over it.
    auto* stillBox = new brls::Box();
    stillBox->setWidth(kNextStillWidth);
    stillBox->setHeight(kNextStillHeight);
    stillBox->setShrink(0);
    stillBox->setMarginRight(20);
    this->card->addView(stillBox);

    this->still = new brls::Image();
    this->still->setWidth(kNextStillWidth);
    this->still->setHeight(kNextStillHeight);
    this->still->setCornerRadius(kNextStillRadius);
    this->still->setScalingType(brls::ImageScalingType::FILL);
    stillBox->addView(this->still);

    auto* wash = new brls::Image();
    wash->setPositionType(brls::PositionType::ABSOLUTE);
    wash->setPositionTop(0);
    wash->setPositionLeft(0);
    wash->setWidth(kNextStillWidth);
    wash->setHeight(kNextStillHeight);
    wash->setCornerRadius(kNextStillRadius);
    wash->setScalingType(brls::ImageScalingType::STRETCH);
    wash->setImageFromRes("img/fade-bottom-dark.png");
    stillBox->addView(wash);

    auto* column = new brls::Box();
    column->setAxis(brls::Axis::COLUMN);
    column->setJustifyContent(brls::JustifyContent::CENTER);
    column->setGrow(1);
    this->card->addView(column);

    auto* kicker = label(22, nvgRGBA(255, 255, 255, 204));
    kicker->setFontWeight("medium");
    kicker->setText("main/player/up_next/label"_i18n);
    column->addView(kicker);

    this->titleLabel = label(28, nvgRGB(255, 255, 255));
    this->titleLabel->setFontWeight("semibold");
    this->titleLabel->setSingleLine(true);
    this->titleLabel->setMarginTop(4);
    // A Label needs a width before it will ellipsize rather than push the pill
    // beside it off the card.
    this->titleLabel->setWidth(kNextCardWidth - kNextPadX * 2 - kNextStillWidth - 20 - kNextPillWidth);
    column->addView(this->titleLabel);

    // The "Play" pill: a bordered capsule, as the reference draws it.
    auto* pill = new brls::Box();
    pill->setAxis(brls::Axis::ROW);
    pill->setAlignItems(brls::AlignItems::CENTER);
    pill->setJustifyContent(brls::JustifyContent::CENTER);
    pill->setHeight(56);
    pill->setShrink(0);
    pill->setPadding(0, 20, 0, 20);
    pill->setMarginLeft(16);
    pill->setCornerRadius(28);
    pill->setBorderThickness(2);
    pill->setBorderColor(nvgRGBA(255, 255, 255, 51));  // white 20%
    this->card->addView(pill);

    auto* glyph = new SVGImage();
    glyph->setWidth(28);
    glyph->setHeight(28);
    glyph->setMarginRight(6);
    glyph->setImageFromSVGRes("icon/ico-play.svg");
    pill->addView(glyph);

    auto* play = label(24, nvgRGB(255, 255, 255));
    play->setText("main/player/up_next/play"_i18n);
    pill->addView(play);
}

void NextEpisodeCard::onPlay(std::function<void()> cb) {
    this->card->registerClickAction([cb](brls::View*) {
        cb();
        return true;
    });
    this->card->addGestureRecognizer(new brls::TapGestureRecognizer(this->card));
}

brls::View* NextEpisodeCard::getDefaultFocus() { return this->card; }

void NextEpisodeCard::setEpisode(const plex::Item& ep) {
    // "S2 E2 • Fake ID" — the reference's season_episode_format and its own
    // single-spaced bullet, not the wider one the OSD's episode line uses.
    std::string line = fmt::format("S{} E{}", ep.parentIndex, ep.index);
    if (!ep.title.empty()) line += " • " + ep.title;
    this->titleLabel->setText(line);

    this->still->clear();
    const std::string& art = ep.thumb.empty() ? ep.parentThumb : ep.thumb;
    // An episode with no still leaves the card its text rather than a gap the
    // width of one — the reference always has artwork here, ours may not.
    this->still->getParent()->setVisibility(art.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    if (!art.empty()) Image::load(this->still, art, (int)kNextStillWidth);
}

void NextEpisodeCard::setOsdVisible(bool visible) {
    this->card->setPositionBottom(visible ? kNextBottomOsd : kNextBottomBare);
}

void NextEpisodeCard::show() { this->setVisibility(brls::Visibility::VISIBLE); }

void NextEpisodeCard::hide() { this->setVisibility(brls::Visibility::GONE); }

// ---- SkipButton ---------------------------------------------------------
//
// SkipIntroButton.kt at its own numbers: 0xFF1E1E1E at 85% (the accent, with a
// dark glyph and label on it, when focused), a 12 dp corner, 18 dp of
// horizontal and 12 dp of vertical padding around a 20 dp glyph, an 8 dp gap
// and a 14 sp label; then a 4 dp strip across the bottom, white at 15% behind
// the elapsed part of the 10 s auto-hide.
//
// Bottom LEFT, where the reference puts it — opposite "up next", which is why
// the two can be on screen together without either having to move.

namespace {
constexpr float kSkipRadius = 24, kSkipPadX = 36, kSkipPadY = 24;
constexpr float kSkipGlyph = 40, kSkipStrip = 8;
constexpr float kSkipLeft = 52, kSkipBottomOsd = 244, kSkipBottomBare = 60;

std::string svgHex(NVGcolor c) {
    auto to8 = [](float f) {
        int v = (int)(f * 255.0f + 0.5f);
        return v < 0 ? 0 : (v > 255 ? 255 : v);
    };
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", to8(c.r), to8(c.g), to8(c.b));
    return buf;
}
}  // namespace

SkipButton::SkipButton() {
    this->setPositionType(brls::PositionType::ABSOLUTE);
    this->setPositionTop(0);
    this->setPositionLeft(0);
    this->setWidth(brls::Application::contentWidth);
    this->setHeight(brls::Application::contentHeight);
    this->setVisibility(brls::Visibility::GONE);

    this->button = new brls::Box();
    this->button->setPositionType(brls::PositionType::ABSOLUTE);
    this->button->setPositionLeft(kSkipLeft);
    this->button->setPositionBottom(kSkipBottomBare);
    this->button->setAxis(brls::Axis::COLUMN);
    this->button->setCornerRadius(kSkipRadius);
    this->button->setHighlightCornerRadius(kSkipRadius + 4);
    this->button->setFocusable(true);
    // The reference recolours the card itself on focus rather than ringing it,
    // so borealis must not paint its own fill over ours.
    this->button->setHideHighlightBackground(true);
    this->addView(this->button);

    auto* row = new brls::Box();
    row->setAxis(brls::Axis::ROW);
    row->setAlignItems(brls::AlignItems::CENTER);
    row->setPadding(kSkipPadY, kSkipPadX, kSkipPadY, kSkipPadX);
    this->button->addView(row);

    this->glyph = new SVGImage();
    this->glyph->setDimensions(kSkipGlyph, kSkipGlyph);
    this->glyph->setShrink(0);
    this->glyph->setMarginRight(16);
    row->addView(this->glyph);

    this->textLabel = label(28, nvgRGB(255, 255, 255));
    this->textLabel->setFontWeight("medium");
    row->addView(this->textLabel);

    // The countdown, under the row and running its full width.
    this->track = new brls::Box();
    this->track->setHeight(kSkipStrip);
    this->track->setBackgroundColor(nvgRGBA(255, 255, 255, 38));  // white 15 %
    this->button->addView(this->track);

    this->fill = new brls::Box();
    this->fill->setHeight(kSkipStrip);
    this->fill->setWidth(0);
    this->fill->setBackgroundColor(nvgRGBA(0x1E, 0x1E, 0x1E, 217));
    this->track->addView(this->fill);

    this->render();
}

void SkipButton::render() {
    NVGcolor accent = brls::Application::getTheme().getColor("color/app");
    NVGcolor ink = this->focused ? nvgRGB(10, 10, 10) : nvgRGB(255, 255, 255);
    this->button->setBackgroundColor(this->focused ? accent : nvgRGBA(0x1E, 0x1E, 0x1E, 217));
    this->textLabel->setTextColor(ink);
    char svg[512];
    std::snprintf(svg, sizeof(svg),
        R"(<svg width="24" height="24" viewBox="0 0 24 24"><path d="%s" fill="%s"/></svg>)",
        player_icon::SKIP_NEXT, svgHex(ink).c_str());
    this->glyph->setImageFromSVGString(svg);
}

void SkipButton::draw(
    NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx) {
    bool now = brls::Application::getCurrentFocus() == this->button;
    if (now != this->focused) {
        this->focused = now;
        this->render();
    }
    brls::Box::draw(vg, x, y, w, h, style, ctx);
}

void SkipButton::setSegmentType(const std::string& type) {
    // getSkipLabel's own mapping, over the vocabulary SkipIntroRepository
    // normalises every provider into.
    std::string key = "main/player/skip/generic";
    if (type == "intro" || type == "op" || type == "mixed-op" || type == "opening")
        key = "main/player/skip/intro";
    else if (type == "outro" || type == "ed" || type == "mixed-ed" || type == "credits" || type == "ending")
        key = "main/player/skip/ending";
    else if (type == "recap")
        key = "main/player/skip/recap";
    this->textLabel->setText(brls::getStr(key));
}

void SkipButton::setProgress(float progress) {
    if (progress < 0) progress = 0;
    if (progress > 1) progress = 1;
    // Against the button's measured width rather than a percentage of it: the
    // row decides how wide this is and yoga has already sized it by now.
    float width = this->button->getWidth();
    this->fill->setWidth(width > 0 ? width * progress : 0);
}

void SkipButton::setCountdownVisible(bool visible) {
    this->track->setVisibility(visible ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
}

void SkipButton::onSkip(std::function<void()> cb) {
    this->button->registerClickAction([cb](brls::View*) {
        cb();
        return true;
    });
    this->button->addGestureRecognizer(new brls::TapGestureRecognizer(this->button));
}

brls::View* SkipButton::getDefaultFocus() { return this->button; }

void SkipButton::setOsdVisible(bool visible) {
    this->button->setPositionBottom(visible ? kSkipBottomOsd : kSkipBottomBare);
}

void SkipButton::show() { this->setVisibility(brls::Visibility::VISIBLE); }

void SkipButton::hide() { this->setVisibility(brls::Visibility::GONE); }
