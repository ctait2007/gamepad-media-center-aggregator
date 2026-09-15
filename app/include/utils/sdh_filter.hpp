/*
    GMCA — SubtitleSdhFilter.kt, ported.

    "SDH" subtitles carry what a deaf viewer needs and a hearing one does not:
    sound descriptions in brackets ("[door creaks]"), speaker labels ("MARY:"),
    and the ">>" a CEA-608 caption uses for a change of speaker. The reference
    strips exactly those four shapes, in a fixed order, and drops a cue that has
    nothing left afterwards.

    The order is not incidental and is preserved: chevrons go first so that
    ">> NAME:" loses them and is THEN recognisable as a speaker label.
*/

#pragma once

#include <string>

namespace sdh {

/// One cue's text, filtered. Empty when nothing survives — the caller drops
/// the cue rather than showing a blank line.
std::string filterPlainText(const std::string& text);

/// A whole SRT or WebVTT document, cue by cue. Blocks whose text is emptied by
/// the filter are dropped outright; indices, timings and everything else are
/// passed through untouched.
///
/// ASS/SSA is NOT handled: its lines carry styling and drawing commands the
/// reference's own filter is not written for either, and its cues rarely carry
/// SDH in the first place.
std::string filterDocument(const std::string& body);

}  // namespace sdh
