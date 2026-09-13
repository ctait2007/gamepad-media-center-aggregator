/*
    GMCA — the player's five panels, following NuvioTV's own.

    They replace the bottom-sheet dropdowns the player used to put up, which
    covered the video and looked nothing like the reference:

      subtitles     SubtitleSelectionOverlay.kt — a bottom-left "Subtitles"
                    title over a language rail and a sync rail.
      audio         AudioSelectionOverlay.kt — the same shape: a track rail
                    beside a narrower column of delay/volume controls.
      sources       StreamSourcesSidePanel.kt — the right-hinged sheet, the
                    item's addon streams, filtered by addon. This is the one
                    the player had NO button for at all.
      episodes      EpisodesSidePanel.kt — the same sheet, season tabs over
                    the episode list, the current one marked.
      stream info   StreamInfoOverlay.kt — the bottom-left block of labelled
                    fields, in the reference's SOURCE/FILE/VIDEO/AUDIO/
                    SUBTITLE sections, read off mpv and the chosen stream.
*/

#pragma once

#include <api/plex/types.hpp>

#include <functional>
#include <string>
#include <vector>

namespace player_panels {

/// Subtitle track picker + sync. `src` supplies the transcode-side streams for
/// backends that burn subtitles in (Plex/Jellyfin); may be null.
void showSubtitles(const plex::Media* src);

/// Audio track picker, delay and volume boost.
void showAudio(const plex::Media* src);

/// The sources sheet. `sources` is the item's media[] — one entry per addon
/// stream — `current` the index playing now, and `onPick` switches to another.
void showSources(const std::string& subtitle, const std::vector<plex::Media>& sources, int current,
    std::function<void(int)> onPick);

/// The episode sheet. `titles` are the rows exactly as the player's own
/// episode list has them, `current` the one playing.
void showEpisodeTitles(const std::string& subtitle, const std::vector<std::string>& titles, int current,
    std::function<void(int)> onPick);

/// Everything the player knows about what is on screen right now.
void showStreamInfo(const plex::Media* src, const std::string& addonName);

}  // namespace player_panels
