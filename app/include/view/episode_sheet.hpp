/*
    GMCA — NuvioTV's EpisodeOptionsOverlay.

    An episode's long-press menu is NOT the poster dialog. The reference gives
    it the whole screen: the episode's own still as the backdrop under a
    horizontal scrim that is opaque on the left and barely there on the right,
    the S/E line, title and synopsis reading down the left, and the actions in
    a fixed-width column on the right — no panel around them, because the
    darkened artwork is the panel.

    The entries themselves are the poster sheet's pills (SheetButton), so both
    menus focus and invert identically.
*/

#pragma once

#include <borealis.hpp>

#include <functional>
#include <string>

#include "api/media/types.hpp"
#include "view/text_box.hpp"

class EpisodeSheet : public brls::Box {
public:
    explicit EpisodeSheet(const media::Item& episode);

    /// Appends an entry. The first one added takes focus when it opens.
    void addAction(const std::string& label, std::function<void()> onClick);

    /// Pushes it as its own activity. Add every entry first.
    void present();

    /// Opaque: it covers the screen, so there is nothing worth drawing behind it.
    bool isTranslucent() override { return false; }

    brls::View* getDefaultFocus() override;

private:
    BRLS_BIND(brls::Image, backdrop, "episode/sheet/backdrop");
    BRLS_BIND(brls::Box, boxActions, "episode/sheet/actions");
    BRLS_BIND(brls::Label, labelNumber, "episode/sheet/number");
    BRLS_BIND(TextBox, labelTitle, "episode/sheet/title");
    BRLS_BIND(TextBox, labelSummary, "episode/sheet/summary");

    brls::View* firstAction = nullptr;
};
