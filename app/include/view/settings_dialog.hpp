/*
    GMCA — NuvioTV's SettingsSingleChoiceDialog, which is how the reference
    asks "which one?".

    It replaces the bottom sheet borealis puts up for a SelectorCell. A sheet
    slides up from the edge and covers the row it belongs to; the reference
    centres a small panel over a dimmed screen and lists the options as cards,
    the current one lifted and ticked. Every picker in the app goes through
    this, so the answer to "which one?" always looks the same.

    Its numbers are the reference's, doubled (density 2.0 at 1080p):

      panel     420x320 dp, BackgroundElevated, a hairline Border, radii.xl,
                padded spacing.xl with spacing.lg between its parts
      title     titleLarge over an optional bodyMedium subtitle
      option    a 10 dp card padded spacing.lg, spacing.sm apart, holding a
                bodyLarge title over an optional bodySmall description, an
                optional trailing note, and a check on the chosen one
*/

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace settings_dialog {

/// One row of the list. `description` and `trailing` are both optional — the
/// reference uses the first for "what this option means" and the second for a
/// value that belongs to the option rather than to the choice.
struct Option {
    std::string title;
    std::string description;
    std::string trailing;
};

/// Put the dialog up. `onPick` fires with the chosen index and the dialog
/// closes itself; dismissing without choosing fires `onDismiss` instead, which
/// is what the restart-prompt cells hang off.
void choose(const std::string& title, const std::string& subtitle, const std::vector<Option>& options,
    int selected, std::function<void(int)> onPick, std::function<void()> onDismiss = nullptr);

/// The common case: plain labels, no descriptions.
void choose(const std::string& title, const std::vector<std::string>& labels, int selected,
    std::function<void(int)> onPick, std::function<void()> onDismiss = nullptr);

}  // namespace settings_dialog
