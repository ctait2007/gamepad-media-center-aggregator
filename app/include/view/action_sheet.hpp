/*
    GMCA — NuvioTV's NuvioDialog: the panel every long-press menu is drawn in.

    A scrim, a centred rounded panel (BackgroundElevated inside a hairline
    Border), a title, an optional subtitle, and a column of full-width pills.
    Nothing in it is media-specific: the callers decide what the pills say and
    do, so the poster menu, the Continue Watching menu and the detail page's
    Play menu are the same object with different lists — which is the point,
    since they are visually identical in the reference and were three different
    shapes here.

    Measurements are the reference's at its 2.0 density: width 520dp, radius
    xl (16), padding xl (24), lg (16) between every child; title titleLarge,
    subtitle bodyMedium in TextSecondary; a pill is a 40dp-tall
    RoundedCornerShape(50%) in BackgroundCard that inverts to white-on-dark
    when focused (tv-material3's focusedContainerColor/focusedContentColor
    defaults, which is what makes the first entry read as "selected").
*/

#pragma once

#include <borealis.hpp>

#include <functional>
#include <memory>
#include <string>

/// One full-width pill. Focus inverts it rather than ringing it, so the sheet
/// looks like the reference's — hence the manual colour swap: borealis' own
/// highlight is a border, and a border round a white fill reads as neither.
class SheetButton : public brls::Box {
public:
    SheetButton(const std::string& label, std::function<void()> onClick);

    /// Relabel in place — for an entry whose wording depends on a state that
    /// is still being fetched when the sheet opens (is it in the library?).
    void setLabel(const std::string& text);
    /// Replace what it does, for the same reason.
    void setOnClick(std::function<void()> handler) { *this->onClick = std::move(handler); }

    void onFocusGained() override;
    void onFocusLost() override;

private:
    void applyColors(bool focused);

    brls::Label* label = nullptr;
    /// held rather than captured, so it can be swapped after construction
    std::shared_ptr<std::function<void()>> onClick;
};

class ActionSheet : public brls::Box {
public:
    /// `subtitle` may be empty, in which case the line is dropped.
    ActionSheet(const std::string& title, const std::string& subtitle);

    /// Appends a pill. The FIRST one added takes focus when the sheet opens,
    /// as the reference's dialogs all request focus on their first button.
    /// `onClick` runs after the sheet has closed, so a navigation inside it
    /// does not have to unwind through a view that is being destroyed.
    /// Returns the pill, so a caller can relabel it once an async state lands.
    SheetButton* addAction(const std::string& label, std::function<void()> onClick);

    /// Pushes the sheet as its own translucent activity. Add every action
    /// first: the panel sizes itself to what it holds.
    void present();

    bool isTranslucent() override { return true; }

    brls::View* getDefaultFocus() override;

private:
    BRLS_BIND(brls::Box, panel, "sheet/panel");
    BRLS_BIND(brls::Box, scrim, "sheet/scrim");

    /// built in the constructor (the subtitle is optional and the actions are
    /// not known until the caller has added them), so held directly rather
    /// than looked up by id
    brls::Box* boxActions   = nullptr;
    brls::View* firstAction = nullptr;
};
