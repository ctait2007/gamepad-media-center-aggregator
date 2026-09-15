/*
    GMCA — the player's two full-screen states, following NuvioTV's own.

      LoadingScreen   LoadingOverlay.kt — the item's backdrop under a scrim,
                      its logo in the middle, and (optionally) the step the
                      player is on underneath. Up from the moment a source is
                      chosen until the first frame actually plays.
      PauseScreen     PauseOverlay.kt + PlayerOverlayScaffold.kt — what you
                      are watching, spelled out, once a pause has lasted long
                      enough to mean the viewer has looked away.
      NextEpisodeCard PostPlayOverlay.kt in its AutoPlay mode — the still,
                      name and "Play" of the episode after this one, slid in
                      from the bottom right as the current one runs out.

    All three are plain child views of VideoView rather than pushed
    activities. The first two take no focus, which is what lets the player's
    own buttons keep working underneath them; the third takes focus while it
    is up, as the reference's own Card does.
*/

#pragma once

#include <borealis.hpp>
#include <api/plex/types.hpp>
#include <view/svg_image.hpp>

#include <functional>
#include <string>

/// The startup screen. Hidden until show(), and torn down by hide().
class LoadingScreen : public brls::Box {
public:
    LoadingScreen();

    /// The artwork to sit behind the step text: a backdrop and a cut-out logo,
    /// either of which may be empty. `title` is drawn in place of a logo that
    /// is missing or fails to load.
    void setArtwork(const std::string& backdropUrl, const std::string& logoUrl, const std::string& title);

    /// The step the player is on ("Building player…"). Ignored, and the line
    /// left blank, when the user has turned the step text off.
    void setStage(const std::string& text);

    void show();
    void hide();
    bool shown() { return this->getVisibility() == brls::Visibility::VISIBLE; }

private:
    brls::Image* backdrop = nullptr;
    brls::Image* logo = nullptr;
    brls::Label* titleLabel = nullptr;
    brls::Label* stageLabel = nullptr;
};

/// The pause screen. Takes no focus: VideoView decides what its buttons mean
/// while it is up (see VideoView::dismissPauseScreen).
class PauseScreen : public brls::Box {
public:
    PauseScreen();

    /// Everything the screen draws, from the item playing now. The logo falls
    /// back to the title, and any field that is empty drops out of the layout
    /// rather than leaving a gap.
    void setItem(const plex::Item& item, const std::string& showTitle, const std::string& logoUrl);

    void show();
    void hide();
    bool shown() { return this->getVisibility() == brls::Visibility::VISIBLE; }

    /// The wall clock, top right. Refreshed by VideoView along with its own.
    void setClock(const std::string& text);

private:
    brls::Label* clockLabel = nullptr;
    brls::Image* logo = nullptr;
    brls::Label* titleLabel = nullptr;
    brls::Label* metaLabel = nullptr;
    brls::Label* episodeLabel = nullptr;
    brls::Label* summaryLabel = nullptr;
    brls::Label* castLabel = nullptr;
    brls::Box* castRow = nullptr;
};

/// "Up next": NuvioTV's PostPlayOverlay in AutoPlay mode. Comes up over the
/// last of an episode and offers the next one. Focusable, as the reference's
/// Card is: it takes focus when it appears with the controls down, select
/// plays the next episode, and back dismisses it and hands focus back.
class NextEpisodeCard : public brls::Box {
public:
    NextEpisodeCard();

    /// The episode being offered. Empty title hides the card.
    void setEpisode(const plex::Item& ep);

    /// What select on the card does. Called once, by VideoView.
    void onPlay(std::function<void()> cb);

    /// Focus lands on the card itself rather than anything inside it.
    brls::View* getDefaultFocus() override;

    /// The card sits above the OSD when it is up and near the frame edge when
    /// it is not, as the reference's bottom padding does.
    void setOsdVisible(bool visible);

    void show();
    void hide();
    bool shown() { return this->getVisibility() == brls::Visibility::VISIBLE; }

private:
    brls::Box* card = nullptr;
    brls::Image* still = nullptr;
    brls::Label* titleLabel = nullptr;
};

/// "Skip Intro" / "Skip Recap" / "Skip Ending": NuvioTV's SkipIntroButton, at
/// its own numbers — bottom left, 0xFF1E1E1E at 85% going to the accent on
/// focus, a 12 dp corner, a skip-next glyph and a 14 sp label, and under them
/// the countdown strip that runs the 10 s it stays up for.
///
/// Focusable, like "up next" and for the same reason: it takes focus while the
/// controls are down, because it is the only control on the screen then.
class SkipButton : public brls::Box {
public:
    SkipButton();

    /// Which span is being offered — sets the wording, as the reference's
    /// getSkipLabel does off the same vocabulary.
    void setSegmentType(const std::string& type);

    /// 0..1 of the auto-hide elapsed; drives the strip. The reference stops the
    /// countdown while the controls are up, so the caller decides when to move.
    void setProgress(float progress);
    /// The strip is only drawn while the button is counting itself down.
    void setCountdownVisible(bool visible);

    void onSkip(std::function<void()> cb);
    brls::View* getDefaultFocus() override;

    /// Above the OSD while it is up, at the frame edge while it is not.
    void setOsdVisible(bool visible);

    void show();
    void hide();
    bool shown() { return this->getVisibility() == brls::Visibility::VISIBLE; }

    void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style,
        brls::FrameContext* ctx) override;

private:
    void render();

    brls::Box* button = nullptr;
    SVGImage* glyph = nullptr;
    brls::Label* textLabel = nullptr;
    brls::Box* track = nullptr;
    brls::Box* fill = nullptr;
    bool focused = false;
};
