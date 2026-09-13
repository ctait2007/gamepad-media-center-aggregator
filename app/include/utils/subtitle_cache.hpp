/*
    GMCA — on-disk cache for external subtitle files.

    The player never hands mpv a remote subtitle url. `sub-add <http url>` makes
    mpv open the file itself, on its own command worker and through libavformat
    rather than the libcurl stack the rest of the app uses; on the PS4 build
    every one of those came back "error running command" while the same links
    played fine in the reference app. That build is also --disable-iconv, so
    mpv cannot convert a subtitle that is not already UTF-8 — it reads the whole
    file to guess the charset and then fails the load.

    So we do what NuvioTV does for its own mpv engine (PlayerRuntimeController
    TrackSelection.selectAddonSubtitle): fetch the file ourselves, normalize it
    to UTF-8, write it into a cache directory under a name derived from the url,
    and sub-add the LOCAL PATH. mpv then only ever opens a local, UTF-8,
    correctly-suffixed file.
*/

#pragma once

#include <string>

namespace subtitle_cache {

/// Downloads `url` into the cache (once — a second call for the same url
/// returns the file already there) and returns its absolute path. Blocking:
/// call it from a worker, never from the UI thread. Throws on failure.
std::string fetch(const std::string& url);

/// Drops every cached file. Called when the played item changes: one video's
/// subtitles are of no use to the next, and a console's storage is not free.
void clear();

}  // namespace subtitle_cache
