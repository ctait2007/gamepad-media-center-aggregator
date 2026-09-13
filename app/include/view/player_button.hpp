/*
    GMCA — NuvioTV's player ControlButton.

    A 48dp circle, transparent until focused and a WHITE DISC with a dark glyph
    once it is — tv-material3's focusedContainerColor/focusedContentColor, the
    same inversion the long-press sheets use. borealis' own highlight is a ring,
    which reads as neither, so the fill is applied by hand on focus.

    The glyph is a Material icon path inlined and re-rendered in whichever colour
    the state calls for, the way SettingsNavItem and the Discover pickers inline
    theirs: the app's own SVG assets bake #FFFFFF, so a focused button would need
    a second, dark copy of every icon shipped beside it.
*/

#pragma once

#include <borealis.hpp>

#include <functional>
#include <string>

class SVGImage;

/// The Material icons NuvioTV's control row uses, as 24x24 path data.
namespace player_icon {
extern const char* PLAY;
extern const char* PAUSE;
extern const char* SKIP_NEXT;
extern const char* CLOSED_CAPTION;
extern const char* AUDIO;
extern const char* SOURCES;
extern const char* EPISODES;
extern const char* INFO;
}  // namespace player_icon

class PlayerButton : public brls::Box {
public:
    PlayerButton();

    /// One of player_icon's paths. Re-renders in the current state's colour.
    void setIconPath(const char* path);
    void setOnClick(std::function<void()> onClick);

    void onFocusGained() override;
    void onFocusLost() override;

    static brls::View* create();

private:
    void render();

    SVGImage* glyph = nullptr;
    const char* iconPath = nullptr;
    bool focused = false;
};
