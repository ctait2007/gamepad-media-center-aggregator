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

/// Subtitle track picker + sync.
///   `src`       supplies the transcode-side streams for backends that burn
///               subtitles in (Plex/Jellyfin); may be null.
///   `sidecars`  every subtitle that lives OUTSIDE the file — the addon tracks
///               and the backend's own sidecar streams. They are listed from
///               here rather than read back off mpv, because they are only
///               fetched when one is picked (PlayerView::attachSubtitle) and so
///               are not mpv tracks yet.
///   `selected`  index into `sidecars` of the one attached now, or -1.
///   `onPick`    attaches a sidecar by index; called with -1 for "None".
void showSubtitles(const plex::Media* src, const std::vector<plex::Stream>& sidecars, int selected,
    std::function<void(int)> onPick);

/// Audio track picker, delay and volume boost.
void showAudio(const plex::Media* src);

/// The sources sheet. `sources` is the item's media[] — one entry per addon
/// stream — `current` the index playing now, and `onPick` switches to another.
/// `onReload` re-resolves the streams; null hides the refresh chip.
void showSources(const std::string& subtitle, const std::vector<plex::Media>& sources, int current,
    std::function<void(int)> onPick, std::function<void()> onReload = nullptr);

/// The same sheet for an item whose sources are not resolved yet — it goes up
/// immediately, says it is loading, and fills itself when the addons answer.
/// This is what the episode sheet hands off to, rather than the full-screen
/// picker: switching episode mid-playback should stay inside the player.
void showSourcesFor(const plex::Item& item, const std::string& subtitle,
    std::function<void(plex::Item, std::vector<plex::Media>, int)> onPick);

/// The episode sheet: season tabs over the reference's own episode rows — a
/// still with the S/E code on it and a tick or eye badge, the title, the air
/// date and two lines of synopsis beside it. `episodes` is the player's own
/// list, in its own order, so `onPick` takes an index into it.
void showEpisodes(const std::string& subtitle, const std::vector<plex::Item>& episodes, int current,
    std::function<void(int)> onPick);

/// Everything the player knows about what is on screen right now.
void showStreamInfo(const plex::Media* src, const std::string& addonName);

}  // namespace player_panels
