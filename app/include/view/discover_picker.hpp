/*
    GMCA — one of Discover's three filter dropdowns.

    NuvioTV's Discover screen filters a flat catalog list with three of these
    side by side (SearchDiscoverSection.kt's DiscoverDropdownPicker): Type,
    Catalog, Genre. Each is a rounded, bordered box with a small caption over
    the current value and a chevron on the right; focus turns the border the
    accent colour and tints the fill. Selecting one opens a Dropdown of the
    options and reports the chosen index.
*/

#pragma once

#include <borealis.hpp>
#include <functional>
#include <string>
#include <vector>

class SVGImage;

class DiscoverPicker : public brls::Box {
public:
    /// @param caption the field name, printed small above the value
    explicit DiscoverPicker(const std::string& caption);

    /// Replace the options and the shown value. `selected` indexes `options`;
    /// out of range (or an empty list) just prints `emptyValue`.
    void setOptions(const std::vector<std::string>& options, int selected, const std::string& emptyValue = "—");

    /// Called with the index the user picked.
    void onSelect(std::function<void(int)> cb) { this->callback = std::move(cb); }

    void onFocusGained() override;
    void onFocusLost() override;

private:
    void restyle();
    void open();

    brls::Label* value = nullptr;
    std::string caption;
    std::vector<std::string> options;
    int selected = 0;
    bool focused = false;
    std::function<void(int)> callback;
};
