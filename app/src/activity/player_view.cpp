/*
    GMCA — Plex video player.
    Verified pipeline: PLEX_MIGRATION.md §2.7.
    Units: mpv positions in seconds, Plex API in milliseconds,
    transcoder offset in whole seconds.
*/

#include <cstdlib>

#include "activity/player_view.hpp"
#include "api/plex.hpp"
#include "api/backend.hpp"
#include "utils/dialog.hpp"
#include "utils/misc.hpp"
#include "utils/subtitle_cache.hpp"
#include "view/mpv_core.hpp"
#include "view/player_panels.hpp"
#include "view/player_setting.hpp"
#include "view/video_view.hpp"
#include "view/video_profile.hpp"
#include "view/audio_player.hpp"
#include "tab/source_list.hpp"

using namespace brls::literals;

/// "Watched" threshold: default value of the server preference
/// LibraryVideoPlayedThreshold
static const double SCROBBLE_THRESHOLD = 0.90;

namespace {

/// What mpv actually holds, logged after an attach. An external subtitle that
/// fails to load and one that loads but cannot be DRAWN look identical from the
/// outside, and this is the line that tells them apart: a track listed here
/// with sel=yes means the file is in and selected, and anything still missing
/// on screen is the renderer's end (see the subtitle-font note in MPVCore).
void logSubtitleTracks(const char* when) {
    auto& mpv = MPVCore::instance();
    int64_t count = mpv.getInt("track-list/count");
    std::string subs;
    for (int64_t n = 0; n < count; n++) {
        if (mpv.getString(fmt::format("track-list/{}/type", n)) != "sub") continue;
        subs += fmt::format(" [id={} ext={} lang={} sel={}]", mpv.getInt(fmt::format("track-list/{}/id", n)),
            mpv.getString(fmt::format("track-list/{}/external", n)),
            mpv.getString(fmt::format("track-list/{}/lang", n)),
            mpv.getString(fmt::format("track-list/{}/selected", n)));
    }
    brls::Logger::info(
        "subtitles: {} sid={} tracks:{}", when, mpv.getString("sid"), subs.empty() ? " none" : subs);
}

}  // namespace

PlayerView::PlayerView(const plex::Item& item, const int64_t seekMs, int versionIndex)
    : itemId(item.ratingKey), item(item), preferredVersion(versionIndex) {
    // take sole ownership of MPVCore: if music was playing, the audio controller
    // must stop owning the shared event bus (else it reports this video's
    // progress against the audio track and auto-advances over it). SPEC.md §11.
    AudioPlayer::instance().release();
    float width = brls::Application::contentWidth;
    float height = brls::Application::contentHeight;
    view = new VideoView();
    view->setDimensions(width, height);
    view->setWidthPercentage(100);
    view->setHeightPercentage(100);
    view->setId("video");
    this->setDimensions(width, height);
    this->addView(view);
    view->registerVideoQuality([this](...) { return this->toggleQuality(); });
    // direct-access OSD pickers; &stream lets them switch transcode-side
    // tracks (the Vita default) as well as embedded ones
    view->registerVideoSubtitle([this](...) {
        // The sidecars are the player's to attach, so the panel reports the
        // pick back rather than touching mpv itself: index into
        // sidecarSubtitles(), or -1 for "None".
        player_panels::showSubtitles(&this->stream, this->sidecarSubtitles(), this->selectedSidecar, [this](int i) {
            if (i < 0) {
                this->selectedSidecar = -1;
                // A deliberate "None" must stick: without this the next event
                // that reaches autoSelectSubtitle would put the preferred
                // language straight back on.
                this->autoSubTried = true;
                return;
            }
            this->autoSubTried = true;
            this->attachSubtitle(i);
        });
        return true;
    });
    view->registerVideoAudio([this](...) {
        player_panels::showAudio(&this->stream);
        return true;
    });
    // The source re-selector. Switching source is the same operation the
    // pre-playback picker performs — pin preferredVersion and reload from the
    // current position — so a change here survives as the item's choice.
    view->registerSources([this](...) {
        int64_t pos = int64_t(MPVCore::instance().playback_time) * 1000;
        player_panels::showSources(
            this->item.title, this->item.media, this->preferredVersion,
            [this, pos](int picked) {
                if (picked == this->preferredVersion) return;
                this->preferredVersion = picked;
                MPVCore::instance().reset();
                this->playMedia(pos);
            },
            // Refresh: re-resolve the streams from the addons and come back
            // with whatever they say now, keeping the position.
            [this, pos]() {
                this->preferredVersion = -1;
                MPVCore::instance().reset();
                this->playMedia(pos);
            });
        return true;
    });
    view->registerStreamInfo([this](...) {
        player_panels::showStreamInfo(&this->stream, this->stream.addonName);
        return true;
    });
    // transcode stream failed to play -> retry once in direct play before the
    // error dialog (Vita hardware decode can reject the transcoded stream)
    view->registerError([this](...) { return this->tryDirectPlayFallback(); });

    // stable session identifier (24 characters)
    this->sessionId = misc::randHex(12);

    auto& mpv = MPVCore::instance();

    brls::Application::pushActivity(new brls::Activity(this), brls::TransitionAnimation::NONE);

    playSubscribeID = view->getPlayEvent()->subscribe([this](int index) { this->playIndex(index); });

    settingSubscribeID = view->getSettingEvent()->subscribe([]() {
        brls::View* setting = new PlayerSetting();
        brls::Application::pushActivity(new brls::Activity(setting));
    });

    eventSubscribeID = mpv.getEvent()->subscribe([this](MpvEventEnum event) {
        auto& mpv = MPVCore::instance();
        switch (event) {
        case MpvEventEnum::MPV_RESUME:
            this->reportTimeline("playing", int64_t(mpv.video_progress) * 1000);
            view->getProfile()->init(this->playMethod);
            break;
        case MpvEventEnum::MPV_PAUSE:
            this->reportTimeline("paused", int64_t(mpv.video_progress) * 1000);
            break;
        case MpvEventEnum::LOADING_END:
            this->reportTimeline("playing", int64_t(mpv.playback_time) * 1000);
            break;
        case MpvEventEnum::MPV_STOP:
            this->mpvLoaded = false;
            this->reportStop();
            break;
        case MpvEventEnum::MPV_LOADED: {
            // Sidecars are NOT bulk-attached here any more. mpv drops external
            // tracks on every loadfile, so this used to re-issue one sub-add per
            // resolved subtitle — a dozen or more, each one a network fetch on
            // mpv's own worker pool. See attachSubtitle() for what replaced it.
            this->mpvLoaded = true;
            this->view->setLoadingStage("main/player/loading/buffering"_i18n);
            this->autoSelectSubtitle();
            break;
        }
        case MpvEventEnum::UPDATE_PROGRESS:
            // report cadence: every 10 s
            if (mpv.video_progress % 10 == 0) {
                this->reportTimeline("playing", int64_t(mpv.video_progress) * 1000);
                this->maybeScrobble(int64_t(mpv.video_progress) * 1000);
            }
            break;
        default:;
        }
    });
    customEventSubscribeID = mpv.getCustomEvent()->subscribe([this](const std::string& event, void* data) {
        if (event == QUALITY_CHANGE) {
            // Quality/audio/subtitle change: the HLS transcode carries a single
            // audio track and no selectable subtitle, so switching means asking
            // the server for a fresh transcode and reloading it. Mirror
            // playIndex and reset() mpv first: reloading in place kept the old
            // (Vita hardware) decoder pinned, so the switch stalled and then
            // failed with a playback error. Read the position before reset()
            // zeroes it so the new transcode resumes where we were.
            int64_t pos = int64_t(MPVCore::instance().playback_time) * 1000;
            MPVCore::instance().reset();
            this->playMedia(pos);
        } else if (event == "PreviousTrack") {
            this->view->playNext(-1);
        } else if (event == "NextTrack") {
            this->view->playNext(1);
        }
    });

    this->playMedia(seekMs > 0 ? seekMs : item.viewOffset);

    // Report stop when application exit
    this->exitSubscribeID = brls::Application::getExitEvent()->subscribe([this]() {
        if (!MPVCore::instance().isStopped()) this->reportStop();
    });
}

PlayerView::~PlayerView() {
    auto& mpv = MPVCore::instance();
    mpv.getEvent()->unsubscribe(eventSubscribeID);
    mpv.getCustomEvent()->unsubscribe(customEventSubscribeID);
    view->getPlayEvent()->unsubscribe(playSubscribeID);
    view->getSettingEvent()->unsubscribe(settingSubscribeID);

    brls::sync([&mpv]() { mpv.getCustomEvent()->fire(VIDEO_CLOSE, nullptr); });

    PlayerSetting::selectedSubtitle = 0;
    PlayerSetting::selectedAudio = 0;

    if (!mpv.isStopped()) this->reportStop();
    // Free the server-side transcode session on exit (else it lingers orphaned).
    this->stopTranscode();
    brls::Application::getExitEvent()->unsubscribe(this->exitSubscribeID);
    brls::Logger::debug("trying delete PlayerView...");
}

void PlayerView::setSeries(const std::string& showRatingKey) {
    ASYNC_RETAIN
    // all episodes of the show
    AppConfig::instance().backend().getAllEpisodes(showRatingKey, true,
        [ASYNC_TOKEN](const media::Container<media::Item>& r) {
            ASYNC_RELEASE
            int index = -1;
            std::vector<std::string> values;
            for (size_t i = 0; i < r.Items.size(); i++) {
                auto& it = r.Items.at(i);
                if (it.ratingKey == this->itemId) index = i;
                values.push_back(fmt::format("S{}E{} - {}", it.parentIndex, it.index, it.title));
            }
            view->setList(values, index);
            this->episodes = std::move(r.Items);
            this->updateNextEpisode();
            // The sheet draws each episode's still, title, air date and
            // synopsis, so it needs the items themselves — which live here.
            std::string show = this->item.grandparentTitle.empty() ? this->item.title : this->item.grandparentTitle;
            view->registerEpisodes([this, show](...) {
                int cur = -1;
                for (size_t i = 0; i < this->episodes.size(); i++)
                    if (this->episodes[i].ratingKey == this->itemId) cur = (int)i;
                player_panels::showEpisodes(
                    show, this->episodes, cur, [this](int picked) { this->chooseEpisodeSource(picked); });
                return true;
            });
        },
        [ASYNC_TOKEN](const std::string& error) {
            ASYNC_RELEASE
            Dialog::show(error);
        });
}

void PlayerView::setTitie(const std::string& title) { this->view->setTitie(title); }

void PlayerView::setChapters(const std::vector<plex::Chapter>& chaps, int64_t durationMs) {
    std::vector<float> clips;
    if (durationMs > 0) {
        for (auto& c : chaps) {
            clips.push_back(float(c.startTimeOffset) / float(durationMs));
        }
    }
    this->view->setClipPoint(clips);
}

bool PlayerView::playIndex(int index) {
    if (index < 0 || index >= (int)this->episodes.size()) {
        return VideoView::close();
    }
    MPVCore::instance().reset();

    auto next = this->episodes.at(index);
    this->itemId = next.ratingKey;
    this->item = next;
    this->scrobbled = false;
    this->preferredVersion = -1;  // binge: auto-pick the best source for the new episode
    this->playMedia(0);
    this->updateNextEpisode();
    view->setTitie(next.grandparentTitle.empty()
                       ? fmt::format("S{}E{} — {}", next.parentIndex, next.index, next.title)
                       : fmt::format("{} · S{}E{} — {}", next.grandparentTitle, next.parentIndex, next.index,
                             next.title));
    return true;
}

void PlayerView::chooseEpisodeSource(int index) {
    if (index < 0 || index >= (int)this->episodes.size()) return;
    plex::Item ep = this->episodes.at(index);
    std::string title = ep.grandparentTitle.empty()
                            ? fmt::format("S{}E{} — {}", ep.parentIndex, ep.index, ep.title)
                            : fmt::format("{} · S{}E{} — {}", ep.grandparentTitle, ep.parentIndex, ep.index, ep.title);

    player_panels::showSourcesFor(
        ep, title, [this](plex::Item chosen, std::vector<plex::Media> sources, int mediaIndex) {
            this->switchTo(chosen, sources, mediaIndex);
        });
}

void PlayerView::switchTo(const plex::Item& ep, const std::vector<plex::Media>& sources, int mediaIndex) {
    MPVCore::instance().reset();
    this->itemId = ep.ratingKey;
    this->item = ep;
    this->item.media = sources;
    this->scrobbled = false;
    // The picker's index is into the list it just handed over, which is now
    // item.media — so playMedia's fast path plays exactly what was chosen
    // rather than re-resolving and possibly landing on a different release.
    this->preferredVersion = mediaIndex;
    this->playMedia(0);

    // Keep the Next button pointing at the episode after THIS one.
    for (size_t i = 0; i < this->episodes.size(); i++)
        if (this->episodes[i].ratingKey == ep.ratingKey) this->view->setPlayIndex((int)i);
    this->updateNextEpisode();
}

/// Hand the player the episode after the one playing, for the "up next" card.
/// Called every time the played item changes: the card is fed from the list
/// the sheet uses, so it offers exactly what Next would play.
void PlayerView::updateNextEpisode() {
    for (size_t i = 0; i + 1 < this->episodes.size(); i++) {
        if (this->episodes[i].ratingKey != this->itemId) continue;
        this->view->setNextEpisode(this->episodes[i + 1]);
        return;
    }
}

/// NuvioTV splits the player's identity across three lines rather than one
/// string: the SHOW (or the movie and its year) in headlineMedium, the episode
/// as "S2 E1 • The Scrub" under it, and the stream's own name as "via …" under
/// that, the last only while paused. Derived from the item here instead of at
/// each call site, which used to hand over one pre-joined title.
void PlayerView::applyIdentity() {
    bool episode = this->item.type == plex::mediaTypeEpisode;
    if (episode && !this->item.grandparentTitle.empty()) {
        this->view->setMainTitle(this->item.grandparentTitle);
        std::string line = fmt::format("S{} E{}", this->item.parentIndex, this->item.index);
        if (!this->item.title.empty()) line += "  •  " + this->item.title;
        this->view->setEpisodeLine(line);
    } else if (episode) {
        this->view->setMainTitle(this->item.title);
        this->view->setEpisodeLine(fmt::format("S{} E{}", this->item.parentIndex, this->item.index));
    } else {
        this->view->setMainTitle(this->item.year ? fmt::format("{} ({})", this->item.title, this->item.year)
                                                : this->item.title);
        this->view->setEpisodeLine("");
    }
    // Neither control earns a place on the row when it has nothing to offer:
    // one source is not a choice, and an addon stream carries no bitrate
    // ladder to step down (the reference never transcodes, so it has no
    // quality button at all).
    this->view->setSourcesVisible(this->item.media.size() > 1);
    this->view->setVideoQualityVisible(this->stream.bitrate * 1000 >= 720000);

    // the addon's own one-liner for the stream, flattened
    std::string src = this->stream.label.empty() ? this->stream.detail : this->stream.label;
    for (char& c : src)
        if (c == '\n') c = ' ';
    this->view->setSourceLine(src);
}

/// The artwork the startup and pause screens stand on. An EPISODE carries the
/// show's backdrop under grandparentArt and, on most backends, nothing of its
/// own worth showing full-screen — so the show's art wins when it is there.
static std::string backdropOf(const plex::Item& item) {
    if (!item.grandparentArt.empty()) return item.grandparentArt;
    if (!item.art.empty()) return item.art;
    return item.thumb;
}

void PlayerView::showStartupScreen() {
    // The show's name, not the episode's — the screen is the wordmark of the
    // thing you picked, and its logo says the same.
    std::string title = !this->item.grandparentTitle.empty() ? this->item.grandparentTitle : this->item.title;
    this->view->showLoadingScreen(backdropOf(this->item), this->item.clearLogo, title);
    this->view->setLoadingStage("main/player/loading/preparing"_i18n);
    this->view->setPauseItem(this->item, title, this->item.clearLogo);
}

void PlayerView::playMedia(const int64_t seekMs) {
    // Capture/automation guard: in GMCA_NAV_PIPE mode a stray "Play" from the
    // screenshot harness must never actually start playback — doing so pushes a
    // watch-progress report to the server and pollutes Continue Watching. Bail
    // out immediately (the empty player pops itself, leaving us on the detail).
    if (std::getenv("GMCA_NAV_PIPE")) { VideoView::close(); return; }

    // Release any transcode session we were running before (re)loading. Covers
    // quality/track switches, episode navigation, and transcode->direct play.
    // Without it each reload orphaned a server-side session (verified on dev:
    // they stack up at ~0% progress and never free), starving new transcodes.
    this->stopTranscode();
    // deliberate (re)start: allow the direct-play fallback to trigger again
    this->directPlayFallback = false;
    this->reloadRetries = 0;
    this->showStartupScreen();

    // Fast path: the caller already resolved the exact source (Stremio source
    // picker passes the fully-resolved item + chosen index). Re-fetching would
    // re-resolve streams and could return a different order/set, silently playing
    // a different release than the one selected — so play the chosen one directly.
    {
        auto accessible = [](const plex::Media& m) {
            for (auto& p : m.parts)
                if (p.accessible && p.exists && !p.key.empty()) return true;
            return false;
        };
        if (this->preferredVersion >= 0 && this->preferredVersion < (int)this->item.media.size() &&
            accessible(this->item.media[this->preferredVersion])) {
            this->stream = this->item.media[this->preferredVersion];
            this->applyIdentity();
            this->setChapters(this->item.chapters, this->item.duration);
            this->startPlayback(seekMs);
            return;
        }
    }

    ASYNC_RETAIN
    // fresh metadata: Media/Part/Stream + chapters
    AppConfig::instance().backend().getItemDetail(
        this->itemId, true,
        [ASYNC_TOKEN, seekMs](const media::Item& item) {
            ASYNC_RELEASE
            this->item = item;
            // Now that the real metadata is in, the screen can show the real
            // artwork — the fast path already had it, this is the slow one.
            this->showStartupScreen();

            // caller-chosen source (Stremio picker) if it still resolves to an
            // accessible file; otherwise the first accessible version.
            const plex::Media* chosen = nullptr;
            auto accessible = [](const plex::Media& m) {
                for (auto& p : m.parts)
                    if (p.accessible && p.exists && !p.key.empty()) return true;
                return false;
            };
            if (this->preferredVersion >= 0 && this->preferredVersion < (int)this->item.media.size() &&
                accessible(this->item.media[this->preferredVersion])) {
                chosen = &this->item.media[this->preferredVersion];
            }
            for (auto& m : this->item.media) {
                if (chosen) break;
                if (accessible(m)) chosen = &m;
            }
            if (!chosen) {
                Dialog::show("main/player/error"_i18n, []() { VideoView::close(); });
                return;
            }
            this->stream = *chosen;
            this->applyIdentity();
            this->setChapters(this->item.chapters, this->item.duration);
            this->startPlayback(seekMs);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            Dialog::show(ex, []() { VideoView::close(); });
        });
}

void PlayerView::startPlayback(const int64_t seekMs, bool forceDirect) {
    // We are about to (re)load: mpv will drop any sub-add'ed tracks. Clear the
    // loaded flag so a subtitle fetch landing mid-load waits for MPV_LOADED,
    // and forget which sidecar was attached — the track it named no longer
    // exists, and the preferred-language pick is owed a fresh run.
    // (External subtitles are resolved AFTER the playback task is queued — see
    // the note at the end of this function.)
    this->mpvLoaded = false;
    this->selectedSidecar = -1;
    this->autoSubTried = false;
    this->view->setLoadingStage("main/player/loading/building"_i18n);

    media::PlaybackOptions opts;
    opts.seekMs = seekMs;
    opts.bitrateCap = MPVCore::VIDEO_QUALITY;
    // forceDirect: the transcode->direct-play fallback re-resolves with direct
    // play forced (resolvePlayback returns the direct source when set).
    opts.forceDirectPlay = MPVCore::FORCE_DIRECTPLAY || forceDirect;
    opts.audioStreamId = PlayerSetting::selectedAudio;
    opts.subtitleStreamId = PlayerSetting::selectedSubtitle;
    opts.burnSubtitles = PlayerSetting::selectedSubtitle > 0;
    // transcode target codec: kept identical to the former hard-coded value
    // (MPVCore::VIDEO_CODEC was never wired into the Plex transcoder — see
    // MULTI_BACKEND.md §6); revisit when exposing the codec choice per backend
    opts.videoCodec = "h264";
    opts.sessionId = this->sessionId;

    // copies for the worker thread (avoids racing on this->item during a switch)
    media::Item item = this->item;
    media::Media version = this->stream;

    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, item, version, opts]() {
        try {
            // resolvePlayback runs the transcode decision synchronously and
            // throws on failure; the direct-play fallback is internal. The Plex
            // universal-transcoder request (incl. the Vita 1080p height cap) is
            // built here — see PlexBackend::resolvePlayback.
            media::PlaybackSource src = AppConfig::instance().backend().resolvePlayback(item, version, opts);
            brls::sync([ASYNC_TOKEN, src]() {
                ASYNC_RELEASE
                // A backend may report "nothing playable" with an empty url
                // (e.g. Stremio with no direct/debrid stream) instead of throwing
                // across the async/TU boundary; surface it as a player error.
                if (src.url.empty()) {
                    Dialog::show("main/player/error"_i18n, []() { VideoView::close(); });
                    return;
                }
                this->playMethod = src.playMethod;
                // Remember the backend transcode session so we can tear it down
                // server-side on reload/exit — the dev Vita fix. Set by the backend
                // (Plex); empty for direct play or backends without a server
                // transcode session, leaving stopTranscode a safe no-op.
                this->transcodeSession = src.transcodeSession;
                MPVCore::instance().setUrl(src.url, src.mpvExtra);
                // A deliberate (re)start PLAYS. mpv's `pause` is a global
                // property that survives loadfile, so a file loaded while the
                // player happened to be paused comes up paused: it shows a
                // first frame and sits there until someone presses play. There
                // are two ordinary ways to be paused at this moment — the user
                // paused before opening a panel, and MPVCore pausing on window
                // focus loss (which restores only if it was playing when focus
                // went) — and both made switching episode from the sheet look
                // like the pick did nothing at all.
                MPVCore::instance().command("set", "pause", "no");
                this->view->setLoadingStage("main/player/loading/starting"_i18n);
            });
        } catch (const std::exception& ex) {
            std::string msg = ex.what();
            brls::sync([ASYNC_TOKEN, msg]() {
                ASYNC_RELEASE
                Dialog::show(msg, []() { VideoView::close(); });
            });
        }
    });

    // Resolve external subtitles AFTER queuing the playback task above. brls::async
    // is a single FIFO worker thread (not a pool): the Stremio subtitle fan-out
    // (ensureLoaded + one getSync per subtitles addon, up to a 15 s timeout each)
    // would otherwise run to completion BEFORE the fast resolvePlayback task and
    // stall the video start behind it. Queuing playback first lets mpv start
    // loading while subtitles resolve; the mpvLoaded/autoSelectSubtitle handoff
    // picks the preferred language whenever the fetch lands. (No-op for
    // Plex/Jellyfin: getSubtitles returns synchronously.)
    this->resolveExternalSubtitles();
}

void PlayerView::resolveExternalSubtitles() {
    // Per-video set, same across every source/quality — skip when already resolved
    // for the current item (startPlayback re-enters on quality/track switches).
    const std::string& key = this->item.ratingKey;
    if (key.empty() || key == this->externalSubsItem) return;
    this->externalSubsItem = key;
    this->externalSubs.clear();  // drop the previous item's subs before the switch lands
    this->selectedSidecar = -1;
    subtitle_cache::clear();     // the previous item's downloads are dead weight now

    ASYNC_RETAIN
    AppConfig::instance().backend().getSubtitles(
        this->item,
        [ASYNC_TOKEN, key](std::vector<media::Stream> subs) {
            ASYNC_RELEASE
            // a newer switch superseded this fetch -> its result is stale
            if (key != this->externalSubsItem) return;
            brls::Logger::info("PlayerView: {} external subtitle(s) resolved", subs.size());
            this->externalSubs = std::move(subs);
            // The file may already be playing by the time these land — that is
            // the usual case, since the fan-out is slower than the video start.
            this->autoSelectSubtitle();
        },
        [ASYNC_TOKEN, key](const std::string& ex) {
            ASYNC_RELEASE
            // resolution failed (offline / addon error): leave the set empty, the
            // player still plays; no dialog (subtitles are best-effort).
            brls::Logger::warning("PlayerView: external subtitles failed: {}", ex);
        });
}

std::vector<plex::Stream> PlayerView::sidecarSubtitles() const {
    std::vector<plex::Stream> out;
    // The backend listed these on the chosen Media at detail time (Plex and
    // Jellyfin sidecar files). They are urls like the addon ones, and they are
    // attached the same way, so the picker sees a single list.
    for (const auto& part : this->stream.parts) {
        for (const auto& st : part.streams) {
            if (st.streamType != media::streamTypeSubtitle || st.key.empty()) continue;
            out.push_back(st);
        }
    }
    out.insert(out.end(), this->externalSubs.begin(), this->externalSubs.end());
    return out;
}

void PlayerView::attachSubtitle(int index) {
    std::vector<plex::Stream> subs = this->sidecarSubtitles();
    if (index < 0 || index >= (int)subs.size()) return;
    const plex::Stream sub = subs[index];
    if (sub.key.empty()) return;

    std::string url = AppConfig::instance().backend().subtitleSidecarUrl(sub.key);
    std::string title = sub.displayTitle.empty() ? sub.language : sub.displayTitle;
    std::string lang = sub.languageTag.empty() ? sub.language : sub.languageTag;
    this->selectedSidecar = index;

    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, url, title, lang, index]() {
        // WE fetch the file, not mpv. `sub-add <http url>` asks mpv to open the
        // url on its own command worker: that path went through libavformat
        // rather than the libcurl stack the rest of the app uses, and on the PS4
        // build every single one of them came back "error running command" while
        // the same links worked in the reference app. It is also the path that
        // has to guess the file's charset, which this mpv (built --disable-iconv)
        // cannot convert away from. Downloading here sidesteps both: mpv only
        // ever opens a local, UTF-8, correctly-suffixed file.
        std::string path;
        try {
            path = subtitle_cache::fetch(url);
        } catch (const std::exception& ex) {
            brls::Logger::warning("PlayerView: subtitle download failed ({}): {}", url, ex.what());
        }
        // Falling back to the url is not much of a fallback, but it costs
        // nothing and it is what the reference does on the same failure.
        std::string file = path.empty() ? url : path;
        brls::sync([ASYNC_TOKEN, file, title, lang, index]() {
            ASYNC_RELEASE
            if (this->selectedSidecar != index) return;  // superseded while downloading
            auto& mpv = MPVCore::instance();
            // "cached" both adds and selects, and re-selects without a second
            // load when this file is already a track — which is exactly what a
            // deterministic cache path buys us on the second visit.
            mpv.command("sub-add", file.c_str(), "cached", title.c_str(), lang.c_str());
            // Via the `set` command, not setInt: sub-visibility is a FLAG
            // property and mpv will not convert a flag from INT64, so the
            // property write would be dropped without a word.
            mpv.command("set", "sub-visibility", "yes");
            // sub-add is asynchronous and does its own loading, so the track
            // list is only meaningful a moment later. No `this` — the player
            // may well be gone by then, and MPVCore is a singleton.
            brls::delay(1500, []() { logSubtitleTracks("after attach"); });
        });
    });
}

void PlayerView::autoSelectSubtitle() {
    if (!this->mpvLoaded || this->autoSubTried) return;
    std::vector<plex::Stream> subs = this->sidecarSubtitles();
    if (subs.empty()) return;

    // Preferred language: "auto" follows the app locale, "off" disables auto-
    // selection, otherwise an explicit 2-letter code (PLAYER_SUBTITLE_LANG).
    std::string pref = AppConfig::instance().getItem(AppConfig::PLAYER_SUBTITLE_LANG, std::string("auto"));
    if (pref == "off") {
        this->autoSubTried = true;
        return;
    }
    if (pref == "auto") {
        std::string loc = brls::Application::getLocale();  // "es", "en-US", "zh-Hans"...
        pref = loc.substr(0, loc.find('-'));
    }
    if (pref.empty()) {
        this->autoSubTried = true;
        return;
    }

    for (size_t i = 0; i < subs.size(); i++) {
        if (subs[i].languageTag != pref) continue;
        this->autoSubTried = true;
        this->attachSubtitle((int)i);
        return;
    }
    // Nothing in the preferred language: stop looking for this load rather than
    // re-scanning on every event.
    this->autoSubTried = true;
}

bool PlayerView::tryDirectPlayFallback() {
    auto& mpv = MPVCore::instance();

    // A DIRECT stream that came back with nothing to play gets retried before
    // giving up. Debrid links (TorBox's requestdl, and the resolvers in front
    // of it) hand back an error body or an empty redirect often enough that the
    // SAME url fails and then works seconds later.
    //
    // AFTER A PAUSE, and more than once. The reference retries twice, 1.5 s
    // apart, keeping its loading overlay up (PlayerRuntimeControllerError
    // Recovery: MAX_STARTUP_AUTO_RETRIES/RETRY_DELAY_MS). An immediate re-
    // request of a link that has only just failed tends to fail identically —
    // which is exactly what the second attempt did, "loading failed" on the
    // heels of the first — so the wait is the part that does the work.
    if (this->playMethod != "transcode") {
        constexpr int kMaxReloadRetries = 2;
        constexpr long kRetryDelayMs = 1500;
        if (this->reloadRetries >= kMaxReloadRetries) return false;
        this->reloadRetries++;
        int64_t pos = int64_t(mpv.playback_time) * 1000;  // read before reset() zeroes it
        brls::Logger::warning("PlayerView: stream returned nothing to play ({}) — retry {}/{} in {} ms",
            mpv.getError(), this->reloadRetries, kMaxReloadRetries, kRetryDelayMs);
        mpv.reset();
        ASYNC_RETAIN
        brls::delay(kRetryDelayMs, [ASYNC_TOKEN, pos]() {
            ASYNC_RELEASE
            this->startPlayback(pos);
        });
        return true;  // handled: no error dialog
    }

    // only recover a failed transcode, and only once per (re)load
    if (this->directPlayFallback) return false;
    this->directPlayFallback = true;

    int64_t pos = int64_t(mpv.playback_time) * 1000;  // read before reset() zeroes it
    brls::Logger::error("PlayerView: transcode playback failed ({}) — falling back to direct play at {} ms",
        mpv.getError(), pos);
    mpv.reset();            // release the (Vita hardware) decoder held by the failed stream
    this->stopTranscode();  // drop the dead transcode session server-side
    this->startPlayback(pos, /*forceDirect=*/true);  // re-resolve, forcing direct play
    // surface the reason to the user too, so bug reports carry the mpv code
    brls::Application::notify(fmt::format("{} ({})", "main/player/direct_fallback"_i18n, mpv.getError()));
    return true;  // handled: no error dialog
}

void PlayerView::stopTranscode() {
    if (this->transcodeSession.empty()) return;
    auto& conf = AppConfig::instance();
    // Fire-and-forget: getAction copies url+token, so it is safe even if this
    // PlayerView is being destroyed. A stale session id just 404s server-side.
    plex::getAction(conf.getUrl(), conf.getToken(), nullptr, plex::apiTranscodeStop,
        HTTP::encode_form({{"session", this->transcodeSession}}));
    this->transcodeSession.clear();
}

void PlayerView::reportTimeline(const std::string& state, int64_t timeMs) {
    media::PlayState st = state == "paused"    ? media::PlayState::Paused
                          : state == "stopped" ? media::PlayState::Stopped
                                               : media::PlayState::Playing;
    // mpv's duration, whenever the item has none of its own. A Stremio EPISODE
    // never does — runtime lives on the show's meta, not on the entries of its
    // videos[] — so every episode reported a duration of 0, and a backend that
    // (rightly) refuses to store progress without one dropped the lot. That is
    // the whole of Continue Watching for a series.
    int64_t durMs = this->item.duration;
    if (durMs <= 0) durMs = int64_t(MPVCore::instance().duration * 1000);
    AppConfig::instance().backend().reportProgress(this->itemId, st, timeMs, durMs, this->sessionId);
}

void PlayerView::reportStop() {
    int64_t timeMs = int64_t(MPVCore::instance().playback_time) * 1000;
    this->reportTimeline("stopped", timeMs);
    this->maybeScrobble(timeMs);
    brls::Logger::debug("PlayerView reportStop {}", this->sessionId);
}

void PlayerView::maybeScrobble(int64_t timeMs) {
    // state=stopped is NOT enough to mark as watched: explicit scrobble required.
    // mpv's duration when the item has none of its own — a Stremio episode never
    // does, so this test divided by zero-duration and nothing was EVER marked
    // watched automatically. That is what "finished items are not marked
    // watched" was.
    int64_t durMs = this->item.duration;
    if (durMs <= 0) durMs = int64_t(MPVCore::instance().duration * 1000);
    if (this->scrobbled || durMs <= 0) return;
    if (double(timeMs) / double(durMs) < SCROBBLE_THRESHOLD) return;
    this->scrobbled = true;
    AppConfig::instance().backend().markWatched(this->itemId);
}

bool PlayerView::toggleQuality() {
    std::vector<std::string> options = {"main/player/auto"_i18n};
    std::vector<int64_t> values = {0};
    int64_t videoBitRate = this->stream.bitrate * 1000;  // Plex: kbps -> bps

    if (videoBitRate >= 15000000) options.push_back("20 Mbps"), values.push_back(20000000);
    if (videoBitRate >= 10000000) options.push_back("15 Mbps"), values.push_back(15000000);
    if (videoBitRate >= 8000000) options.push_back("10 Mbps"), values.push_back(10000000);
    if (videoBitRate >= 6000000) options.push_back("8 Mbps"), values.push_back(8000000);
    if (videoBitRate >= 4000000) options.push_back("6 Mbps"), values.push_back(6000000);
    if (videoBitRate >= 3000000) options.push_back("4 Mbps"), values.push_back(4000000);
    if (videoBitRate >= 1500000) options.push_back("3 Mbps"), values.push_back(3000000);
    if (videoBitRate >= 720000) options.push_back("1.5 Mbps"), values.push_back(1500000);
    options.push_back("720 kbps"), values.push_back(720000);
    options.push_back("420 kbps"), values.push_back(420000);

    auto it = std::find(values.begin(), values.end(), MPVCore::VIDEO_QUALITY);
    if (it == values.end()) it = values.begin();

    brls::Dropdown* dropdown = new brls::Dropdown(
        "main/player/quality"_i18n, options,
        [values](int selected) {
            MPVCore::VIDEO_QUALITY = values[selected];
            // remember the choice across launches (Vita users had to re-lower
            // it every session otherwise — see config.cpp default)
            AppConfig::instance().setItem(AppConfig::PLAYER_VIDEO_QUALITY, MPVCore::VIDEO_QUALITY);
            MPVCore::instance().getCustomEvent()->fire(QUALITY_CHANGE, nullptr);
            return true;
        },
        std::distance(values.begin(), it));

    brls::Application::pushActivity(new brls::Activity(dropdown));
    return true;
}
