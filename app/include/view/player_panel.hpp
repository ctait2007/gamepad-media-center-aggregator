/*
    GMCA — the chrome every player panel is drawn in, following NuvioTV.

    The reference has two shapes, and all five of its player panels are one or
    the other (PlayerOverlayScaffold.kt, and the panels beside it):

      PlayerOverlay   a full-screen wash over the still-running video — three
                      layers, exactly as the scaffold draws them: a horizontal
                      black→transparent gradient, a flat tint, then a vertical
                      one — with its content sitting in the bottom-left corner.
                      Subtitles, Audio and Stream information use it.

      PlayerSidePanel a solid sheet hinged to the right edge, full height,
                      rounded on its left corners only, with a title and a
                      Close button across the top. Sources and Episodes use it.

    Inside either, a list is a PlayerRail: a small tertiary caption over a
    scrolling column of PlayerCards. A card is the reference's track Card —
    rounded, padded, its name over one or two dimmer detail lines, filled with
    the accent and ticked when it is the current selection.

    Every measurement here is the reference's own at its 2.0 density, doubled
    into our 1:1 1080p units.
*/

#pragma once

#include <borealis.hpp>

class SVGImage;

#include <functional>
#include <string>
#include <vector>

/// One row of a rail. `detail` and `meta` are optional dimmer lines under the
/// name; `selected` fills it with the accent and adds the tick.
class PlayerCard : public brls::Box {
public:
    PlayerCard(const std::string& name, const std::string& detail, const std::string& meta, bool selected);

    void onFocusGained() override;
    void onFocusLost() override;

private:
    void applyColors();
    void paintTick(NVGcolor color);

    brls::Label* nameLabel = nullptr;
    brls::Label* detailLabel = nullptr;
    brls::Label* metaLabel = nullptr;
    SVGImage* tick = nullptr;
    bool selected = false;
};

/// A titled, scrolling column of cards. Fixed width, because the reference's
/// rails are all fixed-width and sit side by side.
class PlayerRail : public brls::Box {
public:
    /// @param maxHeight how tall the list may grow before it starts scrolling.
    ///        A rail sizes itself to what it holds and sits at the BOTTOM of
    ///        whatever it is in, because the reference's overlays are anchored
    ///        to the bottom of the screen and a short list has to end there
    ///        rather than float in the middle of it.
    /// @param anchorBottom true for an overlay rail, which ends at the bottom
    ///        of the screen; false for a side-panel rail, which starts at the
    ///        top of the sheet.
    PlayerRail(const std::string& title, float width, float maxHeight = 640, bool anchorBottom = true);

    /// Appends a card. The first card added with `selected` true is what the
    /// rail hands focus to when the panel opens; failing that, the first card.
    PlayerCard* addCard(const std::string& name, const std::string& detail, const std::string& meta, bool selected,
        std::function<void()> onClick);

    /// A card that is not a choice but a control — a ± stepper, a toggle. Same
    /// shape, never ticked, and its click handler does not close the panel.
    PlayerCard* addControl(const std::string& name, const std::string& value, std::function<void()> onClick);

    brls::View* focusTarget() const { return this->preferred ? this->preferred : this->first; }

    /// Relabels a control in place, for a stepper whose value has just changed.
    static void relabel(PlayerCard* card, const std::string& name, const std::string& value);

private:
    /// Re-derives the scroll height from what the rail now holds, capped.
    void resize();

    brls::ScrollingFrame* scroll = nullptr;
    brls::Box* list = nullptr;
    brls::View* first = nullptr;
    brls::View* preferred = nullptr;
    float maxHeight = 640;
    float contentHeight = 0;
};

/// The full-screen wash. Content goes in the bottom-left column returned by
/// content(); present() pushes it as its own translucent activity.
class PlayerOverlay : public brls::Box {
public:
    /// @param padLeft/padBottom the reference gives each of its three overlays
    ///        slightly different content padding; pass that panel's own.
    PlayerOverlay(float padLeft, float padTop, float padBottom);

    /// The bottom-anchored column every overlay fills.
    brls::Box* content() const { return this->column; }

    /// Focus here when the overlay opens. Defaults to the first focusable.
    void setFocusTarget(brls::View* v) { this->focusTargetView = v; }

    void present();

    bool isTranslucent() override { return true; }

    brls::View* getDefaultFocus() override;

private:
    brls::Box* column = nullptr;
    brls::View* focusTargetView = nullptr;
};

/// The right-hinged sheet. Content goes in body(); the header is built for you.
class PlayerSidePanel : public brls::Box {
public:
    explicit PlayerSidePanel(const std::string& title, const std::string& subtitle);

    brls::Box* body() const { return this->bodyBox; }

    void setFocusTarget(brls::View* v) { this->focusTargetView = v; }

    void present();

    bool isTranslucent() override { return true; }

    brls::View* getDefaultFocus() override;

private:
    brls::Box* bodyBox = nullptr;
    brls::View* closeButton = nullptr;
    brls::View* focusTargetView = nullptr;
};
