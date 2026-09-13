/*
    GMCA — the player's five panels (see player_panels.hpp).
*/

#include "view/player_panels.hpp"

#include "api/backend.hpp"
#include "utils/config.hpp"
#include "view/mpv_core.hpp"
#include "view/player_panel.hpp"
#include "view/player_setting.hpp"

#include <algorithm>
#include <fmt/format.h>
#include <memory>

using namespace brls::literals;

namespace {

// The reference's content padding for each overlay, doubled. They differ per
// panel there and they differ here (SubtitleSelectionOverlay 52/36/76,
// AudioSelectionOverlay 44/28/64, StreamInfoOverlay xxxl/36/36).
constexpr float kSubPadX = 104, kSubPadTop = 72, kSubPadBottom = 152;
constexpr float kAudPadX = 88, kAudPadTop = 56, kAudPadBottom = 128;
constexpr float kInfoPadX = 96, kInfoPadTop = 72, kInfoPadBottom = 72;

// Rail widths: RailColumn(width = 300.dp) for subtitles, 444/268 for audio.
constexpr float kSubRailWidth = 600;
constexpr float kAudTrackWidth = 888;
constexpr float kAudControlWidth = 536;
constexpr float kRailGap = 28;  // 14dp between the reference's rails

/// The overlay's title, in the reference's headlineMedium above the rails.
brls::Label* overlayTitle(const std::string& text) {
    auto* l = new brls::Label();
    l->setText(text);
    l->setFontSize(48);
    l->setFontWeight("medium");
    l->setTextColor(nvgRGB(255, 255, 255));
    l->setMarginBottom(24);  // spacing.md
    return l;
}

/// A row of rails, bottom-aligned like the reference's.
brls::Box* railRow() {
    auto* row = new brls::Box();
    row->setAxis(brls::Axis::ROW);
    // Each rail is its own height and they end together at the bottom, which
    // is what the reference's bottom-anchored overlays look like.
    row->setAlignItems(brls::AlignItems::FLEX_END);
    return row;
}

std::string trackTitle(int64_t n, const char* kind, const std::string& fallback) {
    auto& mpv = MPVCore::instance();
    std::string title = mpv.getString(fmt::format("track-list/{}/title", n));
    if (title.empty()) title = mpv.getString(fmt::format("track-list/{}/lang", n));
    if (title.empty()) title = fallback;
    (void)kind;
    return title;
}

std::string trackDetail(int64_t n) {
    auto& mpv = MPVCore::instance();
    std::vector<std::string> bits;
    std::string lang = mpv.getString(fmt::format("track-list/{}/lang", n));
    if (!lang.empty() && lang != "und") bits.push_back(lang);
    std::string codec = mpv.getString(fmt::format("track-list/{}/codec", n));
    if (!codec.empty()) bits.push_back(codec);
    int64_t channels = mpv.getInt(fmt::format("track-list/{}/demux-channel-count", n));
    if (channels > 0) bits.push_back(fmt::format("{}ch", channels));
    std::string out;
    for (auto& b : bits) out += (out.empty() ? "" : " · ") + b;
    return out;
}

std::string formatSize(int64_t bytes) {
    if (bytes >= 1073741824LL) return fmt::format("{:.1f} GB", bytes / 1073741824.0);
    if (bytes >= 1048576LL) return fmt::format("{:.1f} MB", bytes / 1048576.0);
    if (bytes >= 1024LL) return fmt::format("{:.1f} KB", bytes / 1024.0);
    return fmt::format("{} B", bytes);
}

std::string formatBitrate(int64_t bps) {
    if (bps >= 1000000) return fmt::format("{:.1f} Mbps", bps / 1000000.0);
    if (bps >= 1000) return fmt::format("{:.0f} kbps", bps / 1000.0);
    return fmt::format("{} bps", bps);
}

/// The reference's formatResolution: the pixel count and the name for it.
std::string formatResolution(int w, int h) {
    int maxDim = std::max(w, h);
    const char* label = maxDim >= 3600   ? "4K"
                        : maxDim >= 2400 ? "1440p"
                        : maxDim >= 1800 ? "1080p"
                        : maxDim >= 1200 ? "720p"
                        : maxDim >= 800  ? "480p"
                                         : nullptr;
    if (label) return fmt::format("{} × {} ({})", w, h, label);
    return fmt::format("{} × {} ({}p)", w, h, std::min(w, h));
}

}  // namespace

namespace player_panels {

void showSubtitles(const plex::Media* src) {
    auto& mpv = MPVCore::instance();

    auto* overlay = new PlayerOverlay(kSubPadX, kSubPadTop, kSubPadBottom);
    overlay->content()->addView(overlayTitle("main/player/subtitle"_i18n));

    auto* row = railRow();
    auto* langs = new PlayerRail("main/player/subtitle"_i18n, kSubRailWidth);

    int64_t sidActive = mpv.getInt("sid");
    langs->addCard("main/player/none"_i18n, "", "", sidActive == 0, []() {
        PlayerSetting::selectedSubtitle = 0;
        MPVCore::instance().setInt("sid", 0);
    });

    bool anyEmbedded = false;
    int64_t count = mpv.getInt("track-list/count");
    for (int64_t n = 0; n < count; n++) {
        if (mpv.getString(fmt::format("track-list/{}/type", n)) != "sub") continue;
        int64_t id = mpv.getInt(fmt::format("track-list/{}/id", n));
        std::string title = trackTitle(n, "sub", fmt::format("{} {}", "main/player/subtitle"_i18n, id));
        anyEmbedded = true;
        langs->addCard(title, trackDetail(n), "", id == sidActive, [id]() {
            PlayerSetting::selectedSubtitle = id;
            MPVCore::instance().setInt("sid", id);
        });
    }

    // Transcode-side streams: no embedded subs in the HLS stream, so the
    // choice re-transcodes with the picked stream burned in.
    if (!anyEmbedded && src != nullptr && !src->parts.empty()) {
        for (auto& s : src->parts.front().streams) {
            if (s.streamType != plex::streamTypeSubtitle) continue;
            int64_t id = s.id;
            langs->addCard(s.displayTitle, "", "", id == PlayerSetting::selectedSubtitle, [id]() {
                PlayerSetting::selectedSubtitle = id;
                MPVCore::instance().getCustomEvent()->fire(QUALITY_CHANGE, nullptr);
            });
        }
    }

    // Nothing at all: say why, the way the backend wants to say it.
    if (!anyEmbedded && (src == nullptr || src->parts.empty())) {
        std::string hint = AppConfig::instance().backend().subtitleMenuHint();
        if (!hint.empty()) langs->addCard(hint, "", "", false, []() {});
    }

    row->addView(langs);

    // The reference's second rail is subtitle STYLE; ours is sync, which is
    // the one control the player actually owns.
    auto* tools = new PlayerRail("main/setting/playback/subsync"_i18n, kSubRailWidth);
    tools->setMarginLeft(kRailGap);
    tools->addCard(fmt::format("{:+.1f} s", mpv.getDouble("sub-delay")), "main/player/panel/subsync_hint"_i18n, "", false,
        []() {
            // Deferred: pushing straight from the click would have this
            // overlay's own pop swallow the one we just put up.
            brls::sync([]() { PlayerSetting::showSubsync(); });
        });
    row->addView(tools);

    overlay->content()->addView(row);
    overlay->setFocusTarget(langs->focusTarget());
    overlay->present();
}

void showAudio(const plex::Media* src) {
    auto& mpv = MPVCore::instance();

    auto* overlay = new PlayerOverlay(kAudPadX, kAudPadTop, kAudPadBottom);
    overlay->content()->addView(overlayTitle("main/player/audio"_i18n));

    auto* row = railRow();
    auto* tracks = new PlayerRail("main/player/audio"_i18n, kAudTrackWidth);

    int64_t aidActive = mpv.getInt("aid");
    bool anyEmbedded = false;
    int64_t count = mpv.getInt("track-list/count");
    for (int64_t n = 0; n < count; n++) {
        if (mpv.getString(fmt::format("track-list/{}/type", n)) != "audio") continue;
        int64_t id = mpv.getInt(fmt::format("track-list/{}/id", n));
        std::string title = trackTitle(n, "audio", fmt::format("{} {}", "main/player/audio"_i18n, id));
        anyEmbedded = true;
        tracks->addCard(title, trackDetail(n), "", id == aidActive, [id]() {
            PlayerSetting::selectedAudio = id;
            MPVCore::instance().setInt("aid", id);
        });
    }

    if (!anyEmbedded && src != nullptr && !src->parts.empty()) {
        for (auto& s : src->parts.front().streams) {
            if (s.streamType != plex::streamTypeAudio) continue;
            int64_t id = s.id;
            tracks->addCard(s.displayTitle, "", "", id == PlayerSetting::selectedAudio, [id]() {
                PlayerSetting::selectedAudio = id;
                MPVCore::instance().getCustomEvent()->fire(QUALITY_CHANGE, nullptr);
            });
        }
    }
    row->addView(tracks);

    // The narrow column of controls beside the tracks. The reference's are
    // delay, amplification, centre mix and a persist toggle; mpv gives us the
    // first two honestly, so those are the two that are here.
    auto* controls = new PlayerRail("main/player/panel/audio_controls"_i18n, kAudControlWidth);
    controls->setMarginLeft(24);  // spacing.md

    // Each stepper relabels the readout above it, so the pair has to outlive
    // this function — held by shared_ptr rather than captured by reference to
    // a local that is gone the moment the panel is on screen.
    auto delayCard = std::make_shared<PlayerCard*>(nullptr);
    auto volCard   = std::make_shared<PlayerCard*>(nullptr);

    auto delayText = []() { return fmt::format("{:+.0f} ms", MPVCore::instance().getDouble("audio-delay") * 1000); };
    auto volText   = []() { return fmt::format("{:.0f} %", MPVCore::instance().getDouble("volume")); };

    *delayCard = controls->addControl("main/player/panel/delay"_i18n, delayText(), nullptr);
    auto bumpDelay = [delayCard, delayText](double deltaSec) {
        return [delayCard, delayText, deltaSec]() {
            auto& m = MPVCore::instance();
            m.setDouble("audio-delay", m.getDouble("audio-delay") + deltaSec);
            PlayerRail::relabel(*delayCard, "main/player/panel/delay"_i18n, delayText());
        };
    };
    controls->addControl("main/player/panel/delay_minus"_i18n, "", bumpDelay(-0.05));
    controls->addControl("main/player/panel/delay_plus"_i18n, "", bumpDelay(0.05));

    *volCard = controls->addControl("main/player/panel/boost"_i18n, volText(), nullptr);
    auto bumpVol = [volCard, volText](double delta) {
        return [volCard, volText, delta]() {
            auto& m = MPVCore::instance();
            m.setDouble("volume", std::clamp(m.getDouble("volume") + delta, 0.0, 200.0));
            PlayerRail::relabel(*volCard, "main/player/panel/boost"_i18n, volText());
        };
    };
    controls->addControl("main/player/panel/boost_minus"_i18n, "", bumpVol(-5));
    controls->addControl("main/player/panel/boost_plus"_i18n, "", bumpVol(5));

    row->addView(controls);

    overlay->content()->addView(row);
    overlay->setFocusTarget(tracks->focusTarget());
    overlay->present();
}

void showSources(const std::string& subtitle, const std::vector<plex::Media>& sources, int current,
    std::function<void(int)> onPick) {
    auto* panel = new PlayerSidePanel("main/player/sources"_i18n, subtitle);

    if (sources.empty()) {
        auto* empty = new brls::Label();
        empty->setText("main/player/panel/sources_empty"_i18n);
        empty->setFontSize(32);
        empty->setTextColor(nvgRGBA(255, 255, 255, 179));
        panel->body()->addView(empty);
        panel->present();
        return;
    }

    auto* rail = new PlayerRail("", 944, brls::Application::contentHeight - 320, false);  // the sheet's width less its padding
    for (size_t i = 0; i < sources.size(); i++) {
        const plex::Media& m = sources[i];
        std::string name = m.label.empty() ? m.addonName : m.label;
        std::string meta = m.addonName;
        if (!m.videoResolution.empty()) meta += (meta.empty() ? "" : " · ") + m.videoResolution;
        int index = (int)i;
        rail->addCard(name, m.detail, meta, index == current, [onPick, index]() {
            if (onPick) onPick(index);
        });
    }
    panel->body()->addView(rail);
    panel->setFocusTarget(rail->focusTarget());
    panel->present();
}

void showEpisodeTitles(const std::string& subtitle, const std::vector<std::string>& titles, int current,
    std::function<void(int)> onPick) {
    auto* panel = new PlayerSidePanel("main/player/episode"_i18n, subtitle);

    auto* rail = new PlayerRail("", 944, brls::Application::contentHeight - 320, false);
    for (size_t i = 0; i < titles.size(); i++) {
        int index = (int)i;
        rail->addCard(titles[i], "", "", index == current, [onPick, index]() {
            if (onPick) onPick(index);
        });
    }
    panel->body()->addView(rail);
    panel->setFocusTarget(rail->focusTarget());
    panel->present();
}

namespace {

/// One labelled field, in the reference's InfoItem shape: a tertiary caption
/// over a white value. Skipped entirely when there is no value, as it is there.
void addField(brls::Box* row, const std::string& label, const std::string& value) {
    if (value.empty()) return;
    auto* col = new brls::Box();
    col->setAxis(brls::Axis::COLUMN);
    col->setMarginRight(72);  // 36dp between fields

    auto* cap = new brls::Label();
    cap->setText(label);
    cap->setFontSize(24);  // bodySmall
    cap->setTextColor(brls::Application::getTheme().getColor("font/tertiary"));
    col->addView(cap);

    auto* val = new brls::Label();
    val->setText(value);
    val->setFontSize(32);  // titleMedium
    val->setSingleLine(true);
    val->setTextColor(nvgRGB(255, 255, 255));
    col->addView(val);

    row->addView(col);
}

brls::Box* section(brls::Box* parent, const std::string& title) {
    auto* cap = new brls::Label();
    cap->setText(title);
    cap->setFontSize(24);  // labelMedium
    cap->setFontWeight("semibold");
    cap->setTextColor(brls::Application::getTheme().getColor("font/tertiary"));
    cap->setMarginTop(32);  // spacing.lg between sections
    parent->addView(cap);

    auto* row = new brls::Box();
    row->setAxis(brls::Axis::ROW);
    row->setMarginTop(8);  // spacing.xs
    parent->addView(row);
    return row;
}

}  // namespace

void showStreamInfo(const plex::Media* src, const std::string& addonName) {
    auto& mpv = MPVCore::instance();

    auto* overlay = new PlayerOverlay(kInfoPadX, kInfoPadTop, kInfoPadBottom);
    auto* col = overlay->content();
    col->addView(overlayTitle("main/player/info"_i18n));

    // SOURCE — who served this, and under what name.
    if (!addonName.empty() || (src && !src->label.empty())) {
        auto* row = section(col, "main/player/panel/sec_source"_i18n);
        addField(row, "main/player/panel/addon"_i18n, addonName);
        if (src) addField(row, "main/player/panel/stream"_i18n, src->label);
    }

    // FILE
    std::string filename = mpv.getString("filename");
    int64_t fileSize = mpv.getInt("file-size");
    if (!filename.empty() || fileSize > 0) {
        auto* row = section(col, "main/player/panel/sec_file"_i18n);
        addField(row, "main/player/panel/filename"_i18n, filename);
        addField(row, "main/player/panel/size"_i18n, fileSize > 0 ? formatSize(fileSize) : "");
        addField(row, "main/player/panel/container"_i18n, mpv.getString("file-format"));
    }

    // VIDEO
    int w = (int)mpv.getInt("width"), h = (int)mpv.getInt("height");
    std::string vcodec = mpv.getString("video-codec");
    double fps = mpv.getDouble("container-fps");
    int64_t vbitrate = mpv.getInt("video-bitrate");
    if (w > 0 || !vcodec.empty()) {
        auto* row = section(col, "main/player/panel/sec_video"_i18n);
        addField(row, "main/player/panel/codec"_i18n, vcodec);
        addField(row, "main/player/panel/resolution"_i18n, w > 0 && h > 0 ? formatResolution(w, h) : "");
        addField(row, "main/player/panel/framerate"_i18n, fps > 0 ? fmt::format("{:.3f} fps", fps) : "");
        addField(row, "main/player/panel/bitrate"_i18n, vbitrate > 0 ? formatBitrate(vbitrate) : "");
    }

    // AUDIO
    std::string acodec = mpv.getString("audio-codec-name");
    int64_t achannels = mpv.getInt("audio-params/channel-count");
    int64_t arate = mpv.getInt("audio-params/samplerate");
    if (!acodec.empty() || achannels > 0) {
        auto* row = section(col, "main/player/panel/sec_audio"_i18n);
        addField(row, "main/player/panel/codec"_i18n, acodec);
        addField(row, "main/player/panel/channels"_i18n, achannels > 0 ? fmt::format("{}", achannels) : "");
        addField(row, "main/player/panel/samplerate"_i18n, arate > 0 ? fmt::format("{} kHz", arate / 1000) : "");
        addField(row, "main/player/panel/bitrate"_i18n,
            mpv.getInt("audio-bitrate") > 0 ? formatBitrate(mpv.getInt("audio-bitrate")) : "");
    }

    // SUBTITLE
    int64_t sid = mpv.getInt("sid");
    if (sid > 0) {
        auto* row = section(col, "main/player/panel/sec_subtitle"_i18n);
        addField(row, "main/player/panel/name"_i18n, mpv.getString("current-tracks/sub/title"));
        addField(row, "main/player/panel/codec"_i18n, mpv.getString("current-tracks/sub/codec"));
        addField(row, "main/player/panel/language"_i18n, mpv.getString("current-tracks/sub/lang"));
    }

    overlay->present();
}

}  // namespace player_panels
