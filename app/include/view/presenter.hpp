#pragma once

#include <utils/event.hpp>

namespace brls {
class View;
}

/// true when the current focus lives inside `view` — lets a refreshing tab
/// know whether it owned the focus (and should restore it after rebuilding)
/// without stealing it from the sidebar when loading in the background
bool hasFocusWithin(brls::View* view);

class Presenter {
public:
    Presenter();
    virtual ~Presenter();

    virtual void doRequest() = 0;

    /// What closing the player means for this screen. A full re-fetch by
    /// default, which is right for a page whose whole content is about the
    /// thing that was just played; Home overrides it, because NuvioTV does not
    /// reload its catalogs on the way back from playback.
    virtual void onVideoClose() { this->doRequest(); }

protected:
    MPVCustomEvent::Subscription customEventSubscribeID;
};