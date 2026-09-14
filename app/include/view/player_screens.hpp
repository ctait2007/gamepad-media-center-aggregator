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
    activities: none takes focus, which is what lets the player's own buttons
    keep working underneath them.
*/

#pragma once

#include <borealis.hpp>
#include <api/plex/types.hpp>

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
/// last of an episode and offers the next one. Not focusable — VideoView says
/// what cross and circle mean while it is up, the same arrangement the pause
/// screen uses — so the OSD underneath keeps working.
class NextEpisodeCard : public brls::Box {
public:
    NextEpisodeCard();

    /// The episode being offered. Empty title hides the card.
    void setEpisode(const plex::Item& ep);

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
