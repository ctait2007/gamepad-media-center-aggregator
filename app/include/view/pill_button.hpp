/*
    GMCA — a Nuvio-style selector pill.

    NuvioTV uses the same fully-rounded chip for every inline selector: the
    source screen's addon filters, the show page's season picker. One widget so
    they cannot drift apart — the active pill is a filled light capsule with
    dark text, an inactive one the translucent surface with normal text.
*/

#pragma once

#include <borealis.hpp>
#include <functional>
#include <string>

class PillButton : public brls::Box {
public:
    PillButton(const std::string& text, bool active, std::function<void()> onPress);

    /// Re-style in place when the selection moves, so switching pills does not
    /// have to rebuild (and re-focus) the whole row.
    void setActive(bool active);

private:
    brls::Label* label = nullptr;
    bool active = false;
};
