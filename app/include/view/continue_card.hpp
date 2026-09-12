/*
    GMCA — NuvioTV's Continue Watching tile.

    A landscape 16:9 still with the time left in the top-right and the identity
    over the bottom-left, instead of the portrait poster the other rows use.
    For an episode that identity is three lines — "S1 E1", the show, the episode
    name — because the row is a list of places to resume, not a list of titles.

    Only the presentation differs from an ordinary row: selection, the context
    menu and the watched/progress bookkeeping stay VideoDataSource's.
*/

#pragma once

#include "view/video_card.hpp"
#include "view/video_source.hpp"

class ContinueCardCell : public BaseCardCell {
public:
    ContinueCardCell() { this->inflateFromXMLRes("xml/view/continue_card.xml"); }

    static ContinueCardCell* create() { return new ContinueCardCell(); }

    BRLS_BIND(brls::Label, labelEpisode, "continue/card/episode");
    BRLS_BIND(brls::Label, labelName, "continue/card/title");
    BRLS_BIND(brls::Label, labelSubtitle, "continue/card/subtitle");
    BRLS_BIND(brls::Label, labelLeft, "continue/card/left");
    BRLS_BIND(brls::Box, boxLeft, "continue/card/left_box");
    BRLS_BIND(brls::Rectangle, rectProgress, "video/card/progress");
};

class ContinueDataSource : public VideoDataSource {
public:
    using VideoDataSource::VideoDataSource;

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override;
};

/// "27m" / "1h 29m" — the reference's phrasing, not a clock reading (sec2Time
/// would print "27:14", which reads like a timestamp rather than a duration).
std::string humanDuration(int64_t ms);
