/*
    GMCA — Plex video player.
    Pipeline: PLEX_MIGRATION.md §2.7 (direct play, universal transcoder, timeline, scrobble).
*/

#pragma once

#include <borealis.hpp>
#include <utils/event.hpp>
#include <api/plex/types.hpp>

class VideoView;

class PlayerView : public brls::Box {
public:
    /// versionIndex selects which item.media[] source to play (default -1 = the
    /// first accessible version, i.e. unchanged Plex/Jellyfin behavior). The
    /// Stremio source picker passes an explicit index to honor the user's choice.
    PlayerView(const plex::Item& item, const int64_t seekMs = 0, int versionIndex = -1);
    ~PlayerView();

    /// Loads the show's episode list (previous/next navigation)
    void setSeries(const std::string& showRatingKey);
    void setTitie(const std::string& title);

#ifdef ANDROID
    void willDisappear(bool resetState) override {
        if (brls::Application::getThemeVariant() == brls::ThemeVariant::LIGHT)
            brls::Application::getTheme().addColor("brls/clear", nvgRGBA(235, 235, 235, 255));
        else
            brls::Application::getTheme().addColor("brls/clear", nvgRGBA(45, 45, 45, 255));
    }

    void willAppear(bool resetState) override {
        brls::Application::getTheme().addColor("brls/clear", nvgRGBA(0, 0, 0, 0));
    }
#endif

private:
    void setChapters(const std::vector<plex::Chapter>& chaps, int64_t durationMs);
    /// Fetches fresh metadata then resolves the playback URL via the backend
    void playMedia(const int64_t seekMs);
    /// Resolves the playback URL through the active backend (resolvePlayback,
    /// which decides direct vs transcode internally). forceDirect bypasses
    /// transcoding for the direct-play fallback after a transcode playback error
    /// (helps the Vita hardware decoder, which can choke on the transcoded stream).
    void startPlayback(const int64_t seekMs, bool forceDirect = false);
    /// Tears the current Plex transcode session down server-side (no-op for
    /// direct play or non-Plex backends). Fire-and-forget; safe to call after
    /// `this` is gone.
    void stopTranscode();
    /// On a transcode playback error, retry once in direct play (helps Vita,
    /// where the hardware decoder can choke on the transcoded stream). Returns
    /// true when a fallback was started (so the error dialog is suppressed).
    bool tryDirectPlayFallback();
    bool playIndex(int index);
    /// Feed VideoView the episode after the one playing (the "up next" card).
    void updateNextEpisode();
    /// theintrodb.org markers for the played item, handed to the player view.
    void resolveSkipMarkers();
    /// Picked from the episode sheet: present the SOURCE PICKER for that
    /// episode rather than starting it straight away. Auto-play is a feature
    /// this app has not been asked for yet, and switching episode is exactly
    /// the moment the user wants a say in which release they get.
    void chooseEpisodeSource(int index);
    /// Swap the playing item for `ep` on the source at `mediaIndex`, in place —
    /// no second player.
    void switchTo(const plex::Item& ep, const std::vector<plex::Media>& sources, int mediaIndex);
    /// Raise the startup screen with this item's artwork, and hand the pause
    /// screen the same. Called on every deliberate (re)start, and again when
    /// fresh metadata lands with better art than we opened with.
    void showStartupScreen();
    /// Resolves external subtitle sidecars for the current item through the
    /// backend (Stremio addons), lazily and only when the played item changes.
    /// Plex/Jellyfin embed theirs in the Media streams, so this is a no-op there.
    void resolveExternalSubtitles();
    /// Every subtitle this playback can offer that is NOT already inside the
    /// file: the addon sidecars resolved above, plus the sidecar streams a
    /// backend listed on the chosen Media (Plex/Jellyfin). One list, because
    /// they are attached the same way.
    std::vector<plex::Stream> sidecarSubtitles() const;
    /// Fetches sidecarSubtitles()[index] ourselves, writes it into the subtitle
    /// cache as UTF-8, and sub-adds the LOCAL FILE. See the note on the
    /// definition for why mpv is never handed the remote url.
    void attachSubtitle(int index);
    /// Attaches the sidecar matching the preferred-language setting
    /// (PLAYER_SUBTITLE_LANG), once per load. No-op when the setting is "off",
    /// nothing matches, or the user has already picked one by hand.
    void autoSelectSubtitle();
    /// POST /:/timeline report (time/duration in ms)
    void reportTimeline(const std::string& state, int64_t timeMs);
    void reportStop();
    /// Marks as watched via /:/scrobble beyond the threshold (90%)
    void maybeScrobble(int64_t timeMs);
    bool toggleQuality();

    // Playback
    /// Title / episode line / "via <stream>", from the item and the chosen
    /// stream rather than from a pre-joined string handed in by the caller.
    void applyIdentity();

    std::string itemId;  // ratingKey
    /// playMethod: "directplay" | "transcode" (VideoProfile display)
    std::string playMethod;
    /// stable play-session id for the whole playback session
    std::string sessionId;
    /// Plex universal-transcoder session, extracted from the resolved transcode
    /// URL so stopTranscode() can free it server-side. Empty for direct play or
    /// non-Plex backends. Regenerated (by the backend) on every (re)start.
    std::string transcodeSession;
    plex::Item item;     // fresh metadata (media/chapters/markers)
    plex::Media stream;  // selected version
    /// caller-chosen source index (Stremio picker); -1 = first accessible.
    /// Reset to -1 on episode switch so binge auto-picks the best source.
    int preferredVersion = -1;
    bool scrobbled = false;
    /// guards tryDirectPlayFallback so a failing stream falls back at most once
    /// per (re)load; reset by playMedia on every deliberate (re)start
    bool directPlayFallback = false;
    /// how many silent retries a DIRECT stream has had this (re)load after
    /// coming back with nothing to play — debrid links do that intermittently.
    /// Reset by playMedia on every deliberate (re)start.
    int reloadRetries = 0;
    std::vector<plex::Item> episodes;

    /// External subtitle sidecars (Stremio addons) for the current item, resolved
    /// lazily at play time and sub-add'ed on each (re)load. `externalSubsItem` is
    /// the ratingKey they belong to, so quality/track switches (same item) don't
    /// re-fetch while an episode switch does. `mpvLoaded` guards the async->sub-add
    /// timing (add on load OR when the fetch lands, whichever is last).
    std::vector<plex::Stream> externalSubs;
    std::string externalSubsItem;
    /// ratingKey the skip markers were last looked up for, so a quality or
    /// track switch does not re-fetch the same episode.
    std::string skipMarkersItem;
    bool mpvLoaded = false;
    /// Which sidecarSubtitles() entry is attached right now, or -1. The picker
    /// reads it: an attached sidecar is an ordinary mpv track, but mpv's own
    /// track list cannot say WHICH of a language's dozen entries it came from.
    int selectedSidecar = -1;
    /// One preferred-language auto-attach per (re)load, so re-entering the
    /// panel or switching source never overrides a hand-picked track.
    bool autoSubTried = false;

    MPVEvent::Subscription eventSubscribeID;
    brls::VoidEvent::Subscription exitSubscribeID;
    brls::Event<int>::Subscription playSubscribeID;
    brls::VoidEvent::Subscription settingSubscribeID;
    MPVCustomEvent::Subscription customEventSubscribeID;
    VideoView* view = nullptr;
};
