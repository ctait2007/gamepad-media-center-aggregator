/*
    GMCA — NuvioTV's settings rows.

    NuvioTV does not draw a settings row the way Borealis does. Its rows
    (SettingsDesignSystem.kt / PlaybackSettingsScreen.kt) are:

        [icon]  Title                                  value  >
                Subtitle explaining what this does

    a leading Material glyph tinted with the accent while focused, a two-line
    title/subtitle column, then either a value + chevron or a toggle pill — and
    the focus state is a pill-shaped ring, NOT a change of fill (containerColor
    and focusedContainerColor are both `Background` in the reference).

    Borealis' cells are one line, right-align an accent-coloured value and fill
    on focus. Rather than fork the cells, this re-skins them in place: the
    classes below derive from the Borealis ones — so every existing
    BRLS_BIND(brls::BooleanCell, ...) and init() call keeps working — and
    rearrange their inflated views in the constructor.
*/

#pragma once

#include <borealis.hpp>

#include <string>
#include <vector>

#include "view/svg_image.hpp"

namespace settings_row {

/// The views a re-skin adds to a cell, so the cell can keep talking to them.
struct Parts {
    brls::Box* column = nullptr;      ///< holds title + subtitle
    brls::Label* subtitle = nullptr;  ///< the second line, GONE until set
    SVGImage* icon = nullptr;         ///< leading glyph, GONE until set
};

/// Re-skin a cell whose root is the row Box: reshape the padding, height and
/// focus ring, drop the title into a column with a subtitle under it, and put a
/// leading icon slot in front. `title` and `value` are the cell's own labels
/// (`value` may be null for cells that have none).
Parts skin(brls::Box* row, brls::Label* title, brls::Label* value);

void setSubtitle(const Parts& parts, const std::string& text);

/// Point the leading slot at one of the Material glyphs in glyphs.cpp. An
/// unknown name leaves the slot empty, which reads as a row without an icon
/// rather than as a missing asset.
void setIcon(brls::View* row, const Parts& parts, const std::string& name);

/// Keep the focus ring a pill as the row grows: Borealis takes a corner RADIUS
/// where the reference takes a shape, so a fixed large value draws an ellipse
/// once it exceeds half the height. Call from onLayout().
void keepPillRing(brls::View* row);

/// A right-pointing chevron sized and coloured for the trailing slot. Drawn as
/// an SVG for the same reason DisclosureCell does it: a Label centres an icon
/// glyph on the TEXT font's metrics, which lands it a few pixels high.
SVGImage* makeChevron();

/// NuvioTV's ExpandMore, for a section header that is open.
SVGImage* makeExpandMore();

}  // namespace settings_row

/// A picker row. Clicking it opens NuvioTV's centred choice dialog
/// (view/settings_dialog.hpp) rather than a Borealis dropdown sheet.
class SelectorCell : public brls::SelectorCell {
  public:
    SelectorCell();

    /// The second line the dialog shows under each option. NuvioTV writes one
    /// for the choices whose names do not explain themselves
    /// (SettingsPickerOption.description). Pass one per option, or none.
    void setDescriptions(std::vector<std::string> descriptions);

    void onLayout() override;

    static brls::View* create();

  private:
    settings_row::Parts parts;
    std::vector<std::string> descriptions;
    brls::Event<int> dismissEvent;
};

/// A switch row. The reference's toggle is a 46x24dp pill with a white knob,
/// not the word "On" — so the value Label goes and a pill takes its place.
class BooleanCell : public brls::BooleanCell {
  public:
    BooleanCell();

    void onLayout() override;

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
        brls::FrameContext* ctx) override;

    static brls::View* create();

  private:
    void syncPill();

    settings_row::Parts parts;
    brls::Box* pill = nullptr;
    brls::Box* knob = nullptr;
    int pillState = -1;  ///< last state the pill was drawn for; -1 = never
};

/// An action row: a title, a subtitle and a chevron. Stands in for Borealis'
/// RadioCell wherever GMCA used one as "tap to do something" rather than as a
/// choice in a list.
class ActionCell : public brls::RadioCell {
  public:
    ActionCell();

    void onLayout() override;

    static brls::View* create();

  private:
    settings_row::Parts parts;
    SVGImage* chevron = nullptr;
};

/// A text-entry row.
class InputCell : public brls::InputCell {
  public:
    InputCell();

    void onLayout() override;

    static brls::View* create();

  private:
    settings_row::Parts parts;
};

/// One of NuvioTV's collapsible settings sections: a header row that reads
/// "Open"/"Closed" with an ExpandMore/ChevronRight, the section's own rows, and
/// a hairline divider closing it off (playbackCollapsibleSection()).
///
/// In XML it wraps the rows it owns:
///
///     <SettingsSection title="..." subtitle="..." expanded="true">
///         <BooleanCell id="..." />
///     </SettingsSection>
class SettingsSection : public brls::Box {
  public:
    SettingsSection();

    void onLayout() override;
    void setExpanded(bool expanded);
    bool isExpanded() const { return expanded; }

    void handleXMLElement(tinyxml2::XMLElement* element) override;

    static brls::View* create();

  private:
    void updateHeader();

    brls::Box* header = nullptr;
    brls::Label* headerTitle = nullptr;
    brls::Label* headerSubtitle = nullptr;
    brls::Label* headerValue = nullptr;
    SVGImage* headerIcon = nullptr;
    brls::Box* content = nullptr;
    brls::Box* divider = nullptr;
    bool expanded = false;
};
