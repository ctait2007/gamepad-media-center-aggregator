/*
    GMCA — Home hero banner (NuvioTV's home screen).

    A wide backdrop for one featured item at the top of Home, with its title,
    a metadata line and its synopsis over the art, fading into the page
    background so the first poster row reads as continuing from it rather than
    sitting under a separate panel.

    Only the action pill inside it is focusable — a focus ring around the whole
    banner reads as a rendering fault. Pressing it opens the same detail page
    the item's poster would.
*/

#pragma once

#include <borealis.hpp>
#include "api/media/types.hpp"

class HomeHero : public brls::Box {
public:
    explicit HomeHero(const media::Item& item);
    ~HomeHero() override;

private:
    media::Item item;
};
