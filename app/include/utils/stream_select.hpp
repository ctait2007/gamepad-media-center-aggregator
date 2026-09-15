/*
    GMCA — StreamAutoPlaySelector.kt, ported.

    NuvioTV can pick a stream for you instead of putting the list up, on one of
    three modes (StreamAutoPlayMode):

      MANUAL        always show the list.
      FIRST_STREAM  play the first playable one, in the order the addons came
                    back in.
      REGEX_MATCH   play the first playable one whose text matches a pattern.

    Over all three sits the binge group: a Stremio addon may stamp every stream
    of one release with the same `behaviorHints.bingeGroup`, and the reference
    prefers a stream carrying the group of the one PLAYING over anything its
    mode would otherwise choose — so an episode of a binge run keeps the same
    release, the same encode and the same debrid account rather than drifting
    between them. That preference wins even in MANUAL mode, which is why it is
    checked before the mode is looked at at all.

    The regex half carries the reference's one oddity verbatim: it reads the
    NEGATIVE LOOKAHEADS out of the user's own pattern and applies them a second
    time as a plain exclusion. A pattern like "(?!.*(CAM|TS))1080p" only rules
    out a CAM when the lookahead happens to sit where the match starts, and the
    people writing these patterns mean it to rule one out wherever it appears.
*/

#pragma once

#include <api/media/types.hpp>

#include <string>
#include <vector>

namespace stream_select {

/// StreamAutoPlayMode, in the reference's own order (its enum ordinal is what
/// the setting stores).
enum class Mode { Manual = 0, FirstStream = 1, RegexMatch = 2 };

/// The index into `sources` to play, or -1 for "put the list up".
///
/// @param bingeGroup  the group of the stream playing now, or "" when nothing
///                    is playing (a first play rather than the next episode).
/// @param preferBinge streamAutoPlayPreferBingeGroupForNextEpisode. With it on
///                    and a group to match, a matching stream wins outright —
///                    and in MANUAL mode, where the mode itself chooses
///                    nothing, only a match auto-plays: failing to find one
///                    puts the list up rather than picking something else.
int pick(const std::vector<media::Media>& sources, Mode mode, const std::string& regex,
    const std::string& bingeGroup, bool preferBinge);

/// Every text the reference's regex is run against, joined — the addon's name,
/// the stream's two lines and its url.
std::string searchableText(const media::Media& m);

}  // namespace stream_select
