#include <borealis/views/hint.hpp>
#include <algorithm>
#include "utils/config.hpp"
#include "utils/event.hpp"
#include "view/button_close.hpp"
#include "view/mpv_core.hpp"
#include "view/player_setting.hpp"
#include "api/backend.hpp"  // full media::Backend definition (subtitleMenuHint)

using namespace brls::literals;

/// The subtitle-delay overlay, following the reference's own
/// (PlayerScreen.SubtitleDelayOverlay): a titled card with the delay beside
/// the heading, a tick-marked track with a wide thumb under it, and a Reset
/// beneath that. LEFT/RIGHT step by 100 ms over the reference's +/-180 s range.
/// Translucent, so the subtitles it is aligning stay visible underneath.
class SubsyncOverlay : public brls::Box {
public:
    explicit SubsyncOverlay(std::function<void(double)> onClose) : onClose(std::move(onClose)) {
        this->setAxis(brls::Axis::COLUMN);
        this->setJustifyContent(brls::JustifyContent::CENTER);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setGrow(1.0f);
        this->setFocusable(true);
        this->setHideHighlightBackground(true);
        this->setHideHighlightBorder(true);

        // A dim behind the card. The reference draws this over its own
        // subtitle overlay, which stays on screen underneath — without
        // something to push it back, the rails behind read through the card
        // and the two panels fight each other.
        auto* scrim = new brls::Box();
        scrim->setPositionType(brls::PositionType::ABSOLUTE);
        scrim->setPositionTop(0);
        scrim->setPositionLeft(0);
        scrim->setWidth(brls::Application::contentWidth);
        scrim->setHeight(brls::Application::contentHeight);
        scrim->setBackgroundColor(nvgRGBA(0, 0, 0, 150));
        this->addView(scrim);

        auto* card = new brls::Box(brls::Axis::COLUMN);
        card->setWidth(brls::Application::contentWidth * 0.6f);
        card->setCornerRadius(52);
        card->setBackgroundColor(nvgRGBA(0x0F, 0x0F, 0x0F, 0xCC));
        card->setPadding(40, 52, 40, 52);

        // heading — delay, on one row
        auto* head = new brls::Box(brls::Axis::ROW);
        head->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        head->setAlignItems(brls::AlignItems::CENTER);
        head->setWidth(brls::Application::contentWidth * 0.6f - 104);

        auto* heading = new brls::Label();
        heading->setText("main/player/panel/sub_delay"_i18n);
        heading->setFontSize(44);
        heading->setTextColor(nvgRGB(255, 255, 255));
        head->addView(heading);

        this->value = new brls::Label();
        this->value->setFontSize(44);
        this->value->setTextColor(nvgRGBA(255, 255, 255, 242));
        head->addView(this->value);
        card->addView(head);

        // track: a flat rail, five ticks along it, and the thumb over the top
        this->slider = new brls::Box();
        auto* slider = this->slider;
        slider->setWidth(brls::Application::contentWidth * 0.6f - 104);
        slider->setHeight(48);
        slider->setMarginTop(36);
        // Focusable, so the track and Reset are two places to be rather than
        // one silent surface with hidden keys — the reference moves focus
        // between them the same way.
        slider->setFocusable(true);
        slider->setHideHighlightBackground(true);

        auto* track = new brls::Box();
        track->setPositionType(brls::PositionType::ABSOLUTE);
        track->setPositionTop(20);
        track->setPositionLeft(0);
        track->setWidth(brls::Application::contentWidth * 0.6f - 104);
        track->setHeight(8);
        track->setCornerRadius(4);
        track->setBackgroundColor(nvgRGBA(255, 255, 255, 38));
        slider->addView(track);

        this->trackWidth = brls::Application::contentWidth * 0.6f - 104;
        for (int i = 0; i < 5; i++) {
            auto* tick = new brls::Box();
            tick->setPositionType(brls::PositionType::ABSOLUTE);
            tick->setWidth(2);
            tick->setHeight(i == 2 ? 26 : 18);
            tick->setPositionTop(i == 2 ? 11 : 15);
            tick->setPositionLeft((this->trackWidth - 2) * i / 4);
            tick->setBackgroundColor(nvgRGBA(0x4A, 0xA3, 0xFF, 133));
            slider->addView(tick);
        }

        this->thumb = new brls::Box();
        this->thumb->setPositionType(brls::PositionType::ABSOLUTE);
        this->thumb->setPositionTop(16);
        this->thumb->setWidth(kThumbWidth);
        this->thumb->setHeight(16);
        this->thumb->setCornerRadius(8);
        this->thumb->setBackgroundColor(nvgRGBA(0x4A, 0xA3, 0xFF, 242));
        slider->addView(this->thumb);
        card->addView(slider);

        // reset
        this->resetCard = new brls::Box();
        this->resetCard->setFocusable(true);
        this->resetCard->setHideHighlightBackground(true);
        this->resetCard->setJustifyContent(brls::JustifyContent::CENTER);
        this->resetCard->setAlignItems(brls::AlignItems::CENTER);
        this->resetCard->setMarginTop(32);
        this->resetCard->setWidth(brls::Application::contentWidth * 0.6f - 104);
        this->resetCard->setCornerRadius(24);
        this->resetCard->setBackgroundColor(nvgRGBA(255, 255, 255, 28));
        this->resetCard->setPaddingTop(18);
        this->resetCard->setPaddingBottom(18);
        auto* resetLabel = new brls::Label();
        resetLabel->setText("main/player/panel/sub_delay_reset"_i18n);
        resetLabel->setFontSize(28);
        resetLabel->setTextColor(nvgRGB(255, 255, 255));
        this->resetCard->addView(resetLabel);
        card->addView(this->resetCard);

        auto* hint = new brls::Label();
        hint->setText(brls::Hint::getKeyIcon(brls::BUTTON_B) + "  " + "hints/back"_i18n);
        hint->setFontSize(24);
        hint->setTextColor(nvgRGBA(255, 255, 255, 140));
        hint->setMarginTop(28);
        card->addView(hint);

        this->addView(card);

        // Local source of truth for the display: mpv's writes are async, so a
        // read straight after one returns the OLD value. Seeded once, tracked
        // here, written out through the `set` command (which parses the value
        // with the option's own parser — the typed setter was being dropped).
        this->delay = MPVCore::instance().getOptionDouble("sub-delay");
        this->refresh();

        slider->registerAction(
            "\uE08F", brls::BUTTON_NAV_LEFT,
            [this](brls::View*) {
                this->nudge(-kStep);
                return true;
            },
            true, true);
        slider->registerAction(
            "\uE08E", brls::BUTTON_NAV_RIGHT,
            [this](brls::View*) {
                this->nudge(kStep);
                return true;
            },
            true, true);
        slider->setCustomNavigationRoute(brls::FocusDirection::DOWN, this->resetCard);
        this->resetCard->setCustomNavigationRoute(brls::FocusDirection::UP, slider);
        this->resetCard->registerClickAction([this](brls::View*) {
            this->delay = 0;
            this->apply();
            return true;
        });
        this->resetCard->setActionAvailable(brls::BUTTON_A, true);

        this->registerAction("hints/back"_i18n, brls::BUTTON_B, [this](brls::View*) {
            // Commit on the way out as well as on every nudge, so what the bar
            // says is what mpv has however the overlay was left.
            this->apply();
            if (this->onClose) this->onClose(this->delay);
            brls::Application::popActivity();
            return true;
        });
    }

    bool isTranslucent() override { return true; }
    brls::View* getDefaultFocus() override { return this->slider ? this->slider : (brls::View*)this; }

private:
    // The reference's own range and step (SubtitleDelayConfig.kt).
    static constexpr double kMin = -180.0, kMax = 180.0, kStep = 0.1;
    static constexpr float kThumbWidth = 44;

    std::function<void(double)> onClose;
    brls::Label* value = nullptr;
    brls::Box* thumb = nullptr;
    brls::Box* slider = nullptr;
    brls::Box* resetCard = nullptr;
    float trackWidth = 0;
    double delay = 0;

    void nudge(double d) {
        this->delay = std::clamp(this->delay + d, kMin, kMax);
        this->apply();
    }
    /// Push the value to mpv and read back what it made of it — the read is
    /// the only way to tell a write mpv took from one it quietly refused.
    void apply() {
        auto& mpv = MPVCore::instance();
        mpv.setOption("sub-delay", fmt::format("{:.1f}", this->delay));
        this->refresh();
        brls::Logger::debug("subtitles: sub-delay set to {:.1f}, mpv reports '{}'", this->delay,
            mpv.getString("sub-delay"));
    }
    void refresh() {
        this->value->setText(fmt::format("{:+.1f} s", this->delay));
        float fraction = (float)((this->delay - kMin) / (kMax - kMin));
        this->thumb->setPositionLeft((this->trackWidth - kThumbWidth) * fraction);
    }
};

PlayerSetting::PlayerSetting() {
    this->inflateFromXMLRes("xml/view/player_setting.xml");
    brls::Logger::debug("PlayerSetting: create");

    this->registerAction("hints/cancel"_i18n, brls::BUTTON_B, [](brls::View* view) {
        brls::Application::popActivity();
        return true;
    });

    this->cancel->registerClickAction([](...) {
        brls::Application::popActivity();
        return true;
    });
    this->cancel->addGestureRecognizer(new brls::TapGestureRecognizer(this->cancel));

    auto& mpv = MPVCore::instance();
    auto& conf = AppConfig::instance();

/// Fullscreen
#if (defined(__APPLE__) || defined(__linux__) || defined(_WIN32)) && !defined(ANDROID)
    btnFullscreen->init(
        "main/setting/others/fullscreen"_i18n, conf.getItem(AppConfig::FULLSCREEN, false), [](bool value) {
            VideoContext::FULLSCREEN = value;
            AppConfig::instance().setItem(AppConfig::FULLSCREEN, value);
            brls::Application::getPlatform()->getVideoContext()->fullScreen(value);
        });

    btnAlwaysOnTop->init(
        "main/setting/others/always_on_top"_i18n, conf.getItem(AppConfig::ALWAYS_ON_TOP, false), [](bool value) {
            AppConfig::instance().setItem(AppConfig::ALWAYS_ON_TOP, value);
            brls::Application::getPlatform()->setWindowAlwaysOnTop(value);
        });
#else
    btnFullscreen->setVisibility(brls::Visibility::GONE);
    btnAlwaysOnTop->setVisibility(brls::Visibility::GONE);
#endif

    btnOSDOnToggle->init(
        "main/setting/playback/osd_on_toggle"_i18n, conf.getItem(AppConfig::OSD_ON_TOGGLE, true), [&conf](bool value) {
            MPVCore::OSD_ON_TOGGLE = value;
            conf.setItem(AppConfig::OSD_ON_TOGGLE, value);
        });

    /// Player mirror
    btnVideoMirror->init("main/setting/filter/mirror"_i18n,
        {
            "hints/off"_i18n,
            "main/setting/filter/hflip"_i18n,
            "main/setting/filter/vflip"_i18n,
        },
        MPVCore::VIDEO_FILTER, [&mpv](int value) {
            MPVCore::VIDEO_FILTER = value;
            switch (value) {
            case 1:
                mpv.command("set", "vf", "hflip");
                break;
            case 2:
                mpv.command("set", "vf", "vflip");
                break;
            default:
                mpv.command("set", "vf", "");
            }
            // 如果正在使用硬解，那么将硬解更新为 auto-copy，避免直接硬解因为不经过 cpu 处理导致镜像翻转无效
            if (MPVCore::HARDWARE_DEC) {
                const char* hwdec = value > 0 ? "auto-copy" : MPVCore::PLAYER_HWDEC_METHOD.c_str();
                mpv.command("set", "hwdec", hwdec);
                brls::Logger::info("MPV hardware decode: {}", hwdec);
            }
        });

    btnVideoRotation->init("main/setting/filter/rotation"_i18n,
        {
            "hints/off"_i18n,
            "90",
            "180",
            "270",
        },
        MPVCore::VIDEO_ROTATION, [&mpv](int value) {
            MPVCore::VIDEO_ROTATION = value;
            switch (value) {
            case 1:
                mpv.command("set", "video-rotate", "90");
                return;
            case 2:
                mpv.command("set", "video-rotate", "180");
                return;
            case 3:
                mpv.command("set", "video-rotate", "270");
                return;
            default:
                mpv.command("set", "video-rotate", "0");
            }
        });

    /// Player aspect
    btnVideoAspect->init("main/setting/aspect/header"_i18n,
        {
            "main/setting/aspect/auto"_i18n,
            "main/setting/aspect/stretch"_i18n,
            "main/setting/aspect/crop"_i18n,
            "4:3",
            "16:9",
        },
        conf.getOptionIndex(AppConfig::PLAYER_ASPECT), [&mpv, &conf](int value) {
            auto& opt = conf.getOptions(AppConfig::PLAYER_ASPECT);
            MPVCore::VIDEO_ASPECT = opt.options.at(value);
            mpv.setAspect(MPVCore::VIDEO_ASPECT);
            conf.setItem(AppConfig::PLAYER_ASPECT, MPVCore::VIDEO_ASPECT);
        });

    btnEqualizerReset->registerClickAction([this](View* view) {
        btnEqualizerBrightness->slider->setProgress(0.5f);
        btnEqualizerContrast->slider->setProgress(0.5f);
        btnEqualizerSaturation->slider->setProgress(0.5f);
        btnEqualizerGamma->slider->setProgress(0.5f);
        btnEqualizerHue->slider->setProgress(0.5f);
        return true;
    });
    registerHideBackground(btnEqualizerReset);
    setupEqualizer(btnEqualizerBrightness, "main/setting/equalizer/brightness"_i18n, Equalizer::BRIGHTNESS,
        mpv.getDouble("brightness"));
    setupEqualizer(
        btnEqualizerContrast, "main/setting/equalizer/contrast"_i18n, Equalizer::CONTRAST, mpv.getDouble("contrast"));
    setupEqualizer(btnEqualizerSaturation, "main/setting/equalizer/saturation"_i18n, Equalizer::SATURATION,
        mpv.getDouble("saturation"));
    setupEqualizer(btnEqualizerGamma, "main/setting/equalizer/gamma"_i18n, Equalizer::GAMMA, mpv.getDouble("hue"));
    setupEqualizer(btnEqualizerHue, "main/setting/equalizer/hue"_i18n, Equalizer::HUE, mpv.getDouble("gamma"));
}

PlayerSetting::~PlayerSetting() { brls::Logger::debug("PlayerSetting: delete"); }

void PlayerSetting::showSubsync(std::function<void(double)> onClose) {
    brls::Application::pushActivity(new brls::Activity(new SubsyncOverlay(std::move(onClose))));
}

void PlayerSetting::showAudioMenu(const plex::Media* src) {
    auto& mpv = MPVCore::instance();

    // embedded tracks (direct play, or the single track of a transcode)
    std::vector<std::string> embedded;
    int64_t count = mpv.getInt("track-list/count");
    for (int64_t n = 0; n < count; n++) {
        if (mpv.getString(fmt::format("track-list/{}/type", n)) != "audio") continue;
        std::string title = mpv.getString(fmt::format("track-list/{}/title", n));
        if (title.empty()) title = mpv.getString(fmt::format("track-list/{}/lang", n));
        if (title.empty()) title = fmt::format("{} {}", "main/player/audio"_i18n, embedded.size() + 1);
        embedded.push_back(title);
    }
    if (embedded.size() > 1) {
        int current = (int)(mpv.getInt("aid", 1) - 1);
        auto* dropdown = new brls::Dropdown(
            "main/player/audio"_i18n, embedded,
            [](int selected) {
                selectedAudio = selected + 1;
                MPVCore::instance().setInt("aid", selectedAudio);
            },
            current < 0 ? 0 : current);
        brls::Application::pushActivity(new brls::Activity(dropdown));
        return;
    }

    // transcode: tracks come from the Plex Media (re-transcode on change)
    std::vector<std::string> names;
    std::vector<int64_t> ids;
    if (src != nullptr && !src->parts.empty()) {
        for (auto& s : src->parts.front().streams) {
            if (s.streamType != plex::streamTypeAudio) continue;
            names.push_back(s.displayTitle);
            ids.push_back(s.id);
        }
    }
    if (names.size() > 1) {
        int current = 0;
        for (size_t i = 0; i < ids.size(); i++)
            if (ids[i] == selectedAudio) current = (int)i;
        auto* dropdown = new brls::Dropdown(
            "main/player/audio"_i18n, names,
            [ids](int selected) {
                selectedAudio = ids[selected];
                MPVCore::instance().getCustomEvent()->fire(QUALITY_CHANGE, nullptr);
            },
            current);
        brls::Application::pushActivity(new brls::Activity(dropdown));
        return;
    }

    brls::Application::notify("main/player/audio"_i18n);
}

void PlayerSetting::showSubtitleMenu(const plex::Media* src) {
    auto& mpv = MPVCore::instance();

    std::vector<std::string> names = {"main/player/none"_i18n};
    // each entry's selection action; index aligned with `names`
    std::vector<std::function<void()>> actions = {[]() {
        selectedSubtitle = 0;
        MPVCore::instance().setInt("sid", 0);
    }};
    int current = 0;

    // embedded subtitle tracks (sid). Sidecar Plex subs are sub-add'ed into
    // mpv on direct play, so they show up here too.
    int64_t count = mpv.getInt("track-list/count");
    int64_t sidActive = mpv.getInt("sid");
    for (int64_t n = 0; n < count; n++) {
        if (mpv.getString(fmt::format("track-list/{}/type", n)) != "sub") continue;
        std::string title = mpv.getString(fmt::format("track-list/{}/title", n));
        if (title.empty()) title = mpv.getString(fmt::format("track-list/{}/lang", n));
        int64_t id = mpv.getInt(fmt::format("track-list/{}/id", n));
        if (title.empty()) title = fmt::format("{} {}", "main/player/subtitle"_i18n, id);
        if (id == sidActive) current = (int)names.size();
        names.push_back(title);
        actions.push_back([id]() {
            selectedSubtitle = id;
            MPVCore::instance().setInt("sid", id);
        });
    }

    // transcode: no embedded subs in the HLS stream -> Plex stream ids
    // (burned in, re-transcode on change)
    if (names.size() == 1 && src != nullptr && !src->parts.empty()) {
        for (auto& s : src->parts.front().streams) {
            if (s.streamType != plex::streamTypeSubtitle) continue;
            int64_t id = s.id;
            if (id == selectedSubtitle) current = (int)names.size();
            names.push_back(s.displayTitle);
            actions.push_back([id]() {
                selectedSubtitle = id;
                MPVCore::instance().getCustomEvent()->fire(QUALITY_CHANGE, nullptr);
            });
        }
    }

    // No subtitle track at all: let the backend guide the user (Stremio with no
    // `subtitles` addon in the collection). Informational entry, no-op on select.
    if (names.size() == 1) {
        std::string hint = AppConfig::instance().backend().subtitleMenuHint();
        if (!hint.empty()) {
            names.push_back(hint);
            actions.push_back([]() {});
        }
    }

    // trailing entry: subtitle sync (sub-delay) — opens a presets picker
    size_t syncIndex = names.size();
    double subDelay = mpv.getDouble("sub-delay");
    names.push_back(fmt::format("{} ({:+.1f} s)", "main/setting/playback/subsync"_i18n, subDelay));

    auto* dropdown = new brls::Dropdown(
        "main/player/subtitle"_i18n, names,
        [actions, syncIndex](int selected) {
            if ((size_t)selected != syncIndex) {
                if (selected >= 0 && (size_t)selected < actions.size()) actions[selected]();
                return;
            }
            // open the live sync overlay, deferred so this dropdown finishes
            // closing first (otherwise its pop would immediately eat the
            // overlay we just pushed — "nothing happens")
            brls::sync([]() { PlayerSetting::showSubsync(); });
        },
        current);
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void PlayerSetting::setupEqualizer(brls::SliderCell* cell, const std::string& title, Equalizer item, double initValue) {
    if (initValue < -100)
        initValue = -100;
    else if (initValue > 100)
        initValue = 100;

    cell->detail->setWidth(50);
    cell->title->setWidth(116);
    cell->title->setMarginRight(0);
    cell->slider->setStep(0.05f);
    cell->slider->setMarginRight(0);
    cell->slider->setPointerSize(20);
    cell->setDetailText(fmt::format("{:.0f}", initValue));
    cell->init(title, (initValue + 100) * 0.005f, [cell, item](float value) {
        auto& mpv = MPVCore::instance();
        int data = (int)(value * 200 - 100);
        cell->setDetailText(std::to_string(data));
        switch (item) {
        case Equalizer::BRIGHTNESS:
            mpv.setInt("brightness", data);
            break;
        case Equalizer::CONTRAST:
            mpv.setInt("contrast", data);
            break;
        case Equalizer::SATURATION:
            mpv.setInt("saturation", data);
            break;
        case Equalizer::GAMMA:
            mpv.setInt("gamma", data);
            break;
        case Equalizer::HUE:
            mpv.setInt("hue", data);
            break;
        default:;
        }
    });
    registerHideBackground(cell->getDefaultFocus());
}

void PlayerSetting::registerHideBackground(brls::View* view) {
    view->getFocusEvent()->subscribe([this](...) { this->setBackgroundColor(nvgRGBAf(0.0f, 0.0f, 0.0f, 0.0f)); });
    view->getFocusLostEvent()->subscribe(
        [this](...) { this->setBackgroundColor(brls::Application::getTheme().getColor("brls/backdrop")); });
}