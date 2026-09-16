/*
    Copyright 2020-2021 natinusala

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include "tab/setting_tab.hpp"
#include "utils/theme_palette.hpp"
#include "view/connection_switcher.hpp"
#include "activity/server_list.hpp"
#include "activity/hint_activity.hpp"
#include "activity/changelog_activity.hpp"
#include "utils/config.hpp"
#include "utils/image.hpp"
#include "utils/thread.hpp"
#include <curl/curl.h>
#include "view/mpv_core.hpp"
#include "view/selector_cell.hpp"
#include "view/library_manager.hpp"
#include "view/hub_visibility_manager.hpp"
#include "view/settings_nav_item.hpp"
#include "api/plex.hpp"
#include "api/introdb.hpp"
#include "api/media/langs.hpp"
#include "utils/dialog.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

#ifdef __SWITCH__
#include "utils/overclock.hpp"
#endif
#ifdef __linux__
#include <borealis/platforms/desktop/steam_deck.hpp>
#endif

using namespace brls::literals;  // for _i18n

class SettingAbout : public brls::Box {
public:
    SettingAbout() {
        this->inflateFromXMLRes("xml/view/setting_about.xml");

        this->labelTitle->setText(AppVersion::getPackageName());
        this->labelVersion->setText(fmt::format("v{}-{} ({})", AppVersion::getVersion(), AppVersion::getCommit(),
#if defined(BOREALIS_USE_D3D11)
            "D3D11"
#elif defined(BOREALIS_USE_DEKO3D)
            "Deko3D"
#elif defined(BOREALIS_USE_GXM)
            "GXM"
#else
            "OpenGL"
#endif
            ));
        this->labelGithub->setText("https://github.com/" + AppVersion::git_repo);
        this->btnGithub->registerClickAction([this](...) {
            std::string url = this->labelGithub->getFullText();
            brls::Application::getPlatform()->openBrowser(url);
            return true;
        });
        this->btnGithub->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnGithub));

        auto& mpv = MPVCore::instance();
        int libass = mpv.getInt("libass-version");
        this->labelThirdpart->setText(fmt::format("{} ffmpeg/{} libass/{:x}.{:x}.{:x}\n{}",
            mpv.getString("mpv-version"), mpv.getString("ffmpeg-version"), (libass >> 28) & 0xf,
            (libass >> 20) & 0xfffff, (libass >> 12) & 0xff, curl_version()));
        brls::Logger::debug("dialog SettingAbout: create");
    }

    ~SettingAbout() { brls::Logger::debug("dialog SettingAbout: delete"); }

private:
    BRLS_BIND(brls::Label, labelTitle, "setting/about/title");
    BRLS_BIND(brls::Label, labelVersion, "setting/about/version");
    BRLS_BIND(brls::Label, labelGithub, "setting/about/github");
    BRLS_BIND(brls::Label, labelThirdpart, "setting/about/thirdpart");
    BRLS_BIND(brls::Box, btnGithub, "setting/box/github");
};

class TutorialFont : public brls::Box {
public:
    TutorialFont() {
        const std::string confDir = AppConfig::instance().configDir();
        this->inflateFromXMLRes("xml/view/tutorial_font.xml");
        fontA3->setText(fmt::format(fmt::runtime("main/setting/tutorial/font_a3"_i18n), confDir));
    }

private:
    BRLS_BIND(brls::Label, fontA3, "tutorial/font_a3");
};

SettingTab::SettingTab() {
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/settings.xml");
    // The connection/profile lives in the sidebar avatar + connection switcher
    // now, not here — Settings is purely app configuration.
}

void SettingTab::onCreate() {
    auto& conf = AppConfig::instance();

/// Hardware decode
#ifdef __SWITCH__
    btnOverClock->init(
        "main/setting/others/overclock"_i18n, conf.getItem(AppConfig::OVERCLOCK, false), [&conf](bool value) {
            SwitchSys::setClock(value);
            conf.setItem(AppConfig::OVERCLOCK, value);
        });
#else
    btnOverClock->setVisibility(brls::Visibility::GONE);
#endif

/// Hardware decode
#ifdef PS4
    btnHWDEC->setVisibility(brls::Visibility::GONE);
#else
    btnHWDEC->init("main/setting/playback/hwdec"_i18n, MPVCore::HARDWARE_DEC, [&conf](bool value) {
        if (MPVCore::HARDWARE_DEC == value) return;
        MPVCore::HARDWARE_DEC = value;
        MPVCore::instance().restart();
        conf.setItem(AppConfig::PLAYER_HWDEC, value);
    });
#endif

#if defined(ANDROID)
    auto& voOption = conf.getOptions(AppConfig::MPV_VO);
    selectorVO->init("main/setting/playback/vo"_i18n, voOption.options, conf.getOptionIndex(AppConfig::MPV_VO),
        [&voOption](int selected) {
            if (MPVCore::VO == voOption.options[selected]) return;
            MPVCore::VO = voOption.options[selected];
            AppConfig::instance().setItem(AppConfig::MPV_VO, MPVCore::VO);
            MPVCore::instance().restart();
        });
#else
    selectorVO->setVisibility(brls::Visibility::GONE);
#endif

    /// Decode quality
    btnQuality->init("main/setting/playback/low_quality"_i18n, MPVCore::LOW_QUALITY, [&conf](bool value) {
        if (MPVCore::LOW_QUALITY == value) return;
        MPVCore::LOW_QUALITY = value;
        MPVCore::instance().restart();
        conf.setItem(AppConfig::PLAYER_LOW_QUALITY, value);
    });

    btnSubFallback->init("main/setting/playback/subs_fallback"_i18n, MPVCore::SUBS_FALLBACK, [&conf](bool value) {
        if (MPVCore::SUBS_FALLBACK == value) return;
        MPVCore::SUBS_FALLBACK = value;
        MPVCore::instance().restart();
        conf.setItem(AppConfig::PLAYER_SUBS_FALLBACK, value);
    });

    // Preferred external-subtitle language (Stremio addon sidecars). "auto"
    // follows the app language, "off" disables auto-selection; otherwise a
    // 2-letter code. Values/labels are built from the shared language catalog so
    // they stay in sync with what the backend can resolve.
    std::vector<std::string> subLangValues = {"auto", "off"};
    std::vector<std::string> subLangLabels = {
        "main/setting/playback/subtitle_lang/auto"_i18n,
        "main/setting/playback/subtitle_lang/off"_i18n,
    };
    // The reference puts the language's own code at the right of each row
    // (SettingsPickerOption.trailing), uppercased.
    std::vector<std::string> catalogCodes;
    for (auto& l : media::subtitleLangCatalog()) {
        std::string up = l.code;
        for (auto& ch : up) ch = (char)std::toupper((unsigned char)ch);
        catalogCodes.push_back(up);
    }
    std::vector<std::string> langTrailings = {"", ""};
    for (auto& l : media::subtitleLangCatalog()) {
        subLangValues.push_back(l.code);
        subLangLabels.push_back(l.display);
    }
    langTrailings.insert(langTrailings.end(), catalogCodes.begin(), catalogCodes.end());
    std::string subLangCur = conf.getItem(AppConfig::PLAYER_SUBTITLE_LANG, std::string("auto"));
    auto subLangIt = std::find(subLangValues.begin(), subLangValues.end(), subLangCur);
    int subLangIndex = subLangIt != subLangValues.end() ? (int)(subLangIt - subLangValues.begin()) : 0;
    selectorSubLang->init("main/setting/playback/subtitle_lang/header"_i18n, subLangLabels, subLangIndex,
        [subLangValues](int selected) {
            AppConfig::instance().setItem(AppConfig::PLAYER_SUBTITLE_LANG, subLangValues[selected]);
        });
    selectorSubLang->setTrailings(langTrailings);

    // secondaryPreferredLanguage, off the same catalog as the preferred one,
    // with "None" where that list has "Automatic" — a second language is an
    // addition, not a fallback.
    std::vector<std::string> secLangValues = {""};
    std::vector<std::string> secLangLabels = {"main/setting/playback/sub_secondary_none"_i18n};
    std::vector<std::string> secTrailings = {""};
    for (auto& l : media::subtitleLangCatalog()) {
        secLangValues.push_back(l.code);
        secLangLabels.push_back(l.display);
    }
    secTrailings.insert(secTrailings.end(), catalogCodes.begin(), catalogCodes.end());
    std::string secCur = conf.getItem(AppConfig::SUB_SECONDARY_LANG, std::string(""));
    auto secIt = std::find(secLangValues.begin(), secLangValues.end(), secCur);
    int secIndex = secIt != secLangValues.end() ? (int)(secIt - secLangValues.begin()) : 0;
    selectorSubSecondaryLang->init("main/setting/playback/sub_secondary_lang"_i18n, secLangLabels, secIndex,
        [secLangValues](int selected) {
            AppConfig::instance().setItem(AppConfig::SUB_SECONDARY_LANG, secLangValues[selected]);
        });
    selectorSubSecondaryLang->setTrailings(secTrailings);

    btnSubOnlyPreferred->init("main/setting/playback/sub_only_preferred"_i18n,
        conf.getItem(AppConfig::SUB_ONLY_PREFERRED_LANGS, false),
        [&conf](bool value) { conf.setItem(AppConfig::SUB_ONLY_PREFERRED_LANGS, value); });

    // useForcedSubtitles. Auto-selection only, and only when the audio is
    // already in the preferred subtitle language — see PlayerView::
    // autoSelectSubtitle. The picker still lists everything.
    btnSubUseForced->init("main/setting/playback/sub_use_forced"_i18n,
        conf.getItem(AppConfig::SUB_USE_FORCED, true),
        [&conf](bool value) { conf.setItem(AppConfig::SUB_USE_FORCED, value); });

    btnDirectPlay->init("main/setting/playback/force_directplay"_i18n, MPVCore::FORCE_DIRECTPLAY, [&conf](bool value) {
        if (MPVCore::FORCE_DIRECTPLAY == value) return;
        MPVCore::FORCE_DIRECTPLAY = value;
        conf.setItem(AppConfig::FORCE_DIRECTPLAY, value);
    });

#if defined(__PSV__)
    selectorCodec->setVisibility(brls::Visibility::GONE);
#else
    auto& codecOption = conf.getOptions(AppConfig::TRANSCODEC);
    selectorCodec->init("main/setting/playback/transcodec"_i18n, {"AVC/H264", "HEVC/H265", "AV1"},
        conf.getOptionIndex(AppConfig::TRANSCODEC), [&codecOption](int selected) {
            MPVCore::VIDEO_CODEC = codecOption.options[selected];
            AppConfig::instance().setItem(AppConfig::TRANSCODEC, MPVCore::VIDEO_CODEC);
        });
#endif

#if defined(__PS4__) || defined(__PSV__) || defined(TRIMUI)
    selectorAudioChannels->setVisibility(brls::Visibility::GONE);
#else
    auto& audioChannelsOption = conf.getOptions(AppConfig::AUDIO_CHANNELS);
    selectorAudioChannels->init("main/setting/playback/audio_channels"_i18n, {"Auto", "Stereo", "Mono"},
        conf.getOptionIndex(AppConfig::AUDIO_CHANNELS), [&audioChannelsOption](int selected) {
            MPVCore::AUDIO_CHANNELS = audioChannelsOption.options[selected];
            AppConfig::instance().setItem(AppConfig::AUDIO_CHANNELS, MPVCore::AUDIO_CHANNELS);
            MPVCore::instance().restart();
        });
#endif

    auto& inmemoryOption = conf.getOptions(AppConfig::PLAYER_INMEMORY_CACHE);
    selectorInmemory->init("main/setting/playback/in_memory_cache"_i18n, inmemoryOption.options,
        conf.getValueIndex(AppConfig::PLAYER_INMEMORY_CACHE, 1), [&inmemoryOption](int selected) {
            if (MPVCore::INMEMORY_CACHE == inmemoryOption.values[selected]) return;
            MPVCore::INMEMORY_CACHE = inmemoryOption.values[selected];
            AppConfig::instance().setItem(AppConfig::PLAYER_INMEMORY_CACHE, MPVCore::INMEMORY_CACHE);
            MPVCore::instance().restart();
        });

    btnShowFPS->init("main/setting/ui/show_fps"_i18n, brls::Application::getFPSStatus(), [&conf](bool value) {
        brls::Application::setFPSStatus(value);
        conf.setItem(AppConfig::SHOW_FPS, value);
    });

    int scaleIndex = conf.getOptionIndex(AppConfig::APP_UI_SCALE, 1);
    selectorScale->init("main/setting/ui/scale/header"_i18n,
        {
            "main/setting/ui/scale/544p"_i18n,
            "main/setting/ui/scale/720p"_i18n,
            "main/setting/ui/scale/900p"_i18n,
            "main/setting/ui/scale/1080p"_i18n,
        },
        scaleIndex, [scaleIndex](int selected) {
            if (scaleIndex == selected) return;
            auto& conf = AppConfig::instance();
            auto& scaleOption = conf.getOptions(AppConfig::APP_UI_SCALE);
            conf.setItem(AppConfig::APP_UI_SCALE, scaleOption.options[selected]);
        });

    selectorVSync->init("main/setting/ui/vsync"_i18n, {"hints/off"_i18n, "hints/on"_i18n, "1/2", "1/3", "1/4"},
        VideoContext::swapInterval, [&conf](int selected) {
            if (selected == VideoContext::swapInterval) return;
            brls::Application::setSwapInterval(selected);
            conf.setItem(AppConfig::SWAP_INTERVAL, selected);
        });

    btnOSDOnToggle->init("main/setting/playback/osd_on_toggle"_i18n, MPVCore::OSD_ON_TOGGLE, [&conf](bool value) {
        MPVCore::OSD_ON_TOGGLE = value;
        conf.setItem(AppConfig::OSD_ON_TOGGLE, value);
    });

    // AudioLanguageSelectionDialog: its two workable special options, then the
    // whole language catalog, exactly as the reference lists it. (Its third
    // special, "Original language", reads TMDB's original_language, which
    // nothing here carries.) Picking one sets mpv's alang -- see
    // MPVCore::init -- so the file's English track is selected when there is
    // one, whichever spelling it is tagged with.
    std::vector<std::string> audioLangValues = {"default", "device"};
    std::vector<std::string> audioLangLabels = {
        "main/setting/playback/audio_lang_option/default"_i18n,
        "main/setting/playback/audio_lang_option/device"_i18n,
    };
    std::vector<std::string> audioTrailings = {"", ""};
    for (auto& l : media::subtitleLangCatalog()) {
        audioLangValues.push_back(l.code);
        audioLangLabels.push_back(l.display);
    }
    audioTrailings.insert(audioTrailings.end(), catalogCodes.begin(), catalogCodes.end());
    std::string audioCur = conf.getItem(AppConfig::PLAYER_AUDIO_LANG, std::string("default"));
    auto audioIt = std::find(audioLangValues.begin(), audioLangValues.end(), audioCur);
    int audioLangIdx = audioIt != audioLangValues.end() ? (int)(audioIt - audioLangValues.begin()) : 0;
    selectorAudioLang->init("main/setting/playback/audio_lang"_i18n, audioLangLabels, audioLangIdx,
        [audioLangValues](int selected) {
            AppConfig::instance().setItem(AppConfig::PLAYER_AUDIO_LANG, audioLangValues[selected]);
        });
    selectorAudioLang->setTrailings(audioTrailings);

    btnOsdClock->init("main/setting/playback/osd_clock"_i18n, MPVCore::OSD_CLOCK, [&conf](bool value) {
        MPVCore::OSD_CLOCK = value;
        conf.setItem(AppConfig::OSD_CLOCK, value);
    });

    btnLoadingScreen->init("main/setting/playback/loading_screen"_i18n, MPVCore::LOADING_SCREEN, [&conf](bool value) {
        MPVCore::LOADING_SCREEN = value;
        conf.setItem(AppConfig::LOADING_SCREEN, value);
    });

    btnLoadingStages->init("main/setting/playback/loading_stages"_i18n, MPVCore::LOADING_STAGES, [&conf](bool value) {
        MPVCore::LOADING_STAGES = value;
        conf.setItem(AppConfig::LOADING_STAGES, value);
    });

    btnPauseScreen->init("main/setting/playback/pause_screen"_i18n, MPVCore::PAUSE_SCREEN, [&conf](bool value) {
        MPVCore::PAUSE_SCREEN = value;
        conf.setItem(AppConfig::PAUSE_SCREEN, value);
    });

    btnNextEpisode->init(
        "main/setting/playback/next_episode_card"_i18n, MPVCore::NEXT_EPISODE_CARD, [&conf](bool value) {
            MPVCore::NEXT_EPISODE_CARD = value;
            conf.setItem(AppConfig::NEXT_EPISODE_CARD, value);
        });

    // theintrodb.org, the marker source behind the reference's skip button and
    // the outro half of its "up next" rule. Its read API needs no key, so
    // unlike the reference — which gates on a build-time INTRODB_API_URL — this
    // is simply on.
    btnIntroDb->init("main/setting/playback/introdb"_i18n, MPVCore::INTRODB, [&conf](bool value) {
        MPVCore::INTRODB = value;
        conf.setItem(AppConfig::INTRODB, value);
        if (!value) introdb::clearCache();
    });

    // AMOLED, from the reference's Appearance section. Applied straight away
    // (applyTheme reruns the whole pass and applyAmoled has the last word), so
    // no relaunch — the surfaces switch only does anything with the first on,
    // which is the reference's own `amoledMode && amoledSurfacesMode`.
    btnAmoled->init("main/setting/ui/amoled"_i18n, conf.getItem(AppConfig::AMOLED_MODE, false), [](bool value) {
        auto& c = AppConfig::instance();
        c.setItem(AppConfig::AMOLED_MODE, value);
        c.applyTheme(c.backend().type());
    });
    btnAmoledSurfaces->init("main/setting/ui/amoled_surfaces"_i18n, conf.getItem(AppConfig::AMOLED_SURFACES, false),
        [](bool value) {
            auto& c = AppConfig::instance();
            c.setItem(AppConfig::AMOLED_SURFACES, value);
            c.applyTheme(c.backend().type());
        });

    // skipIntroEnabled, which gates only the BUTTON. The markers it would use
    // also decide when "up next" comes up, so this must not take the lookup
    // down with it — the reference keeps the two apart for the same reason.
    // rememberAudioDelayPerDevice. Turning it off also forgets what is stored,
    // so the next launch starts level rather than restoring a delay the viewer
    // has just said they do not want kept.
    btnAudioDelayRemember->init("main/setting/playback/audio_delay_remember"_i18n,
        conf.getItem(AppConfig::AUDIO_DELAY_REMEMBER, true), [&conf](bool value) {
            conf.setItem(AppConfig::AUDIO_DELAY_REMEMBER, value);
            if (!value) conf.setItem(AppConfig::AUDIO_DELAY_MS, 0);
        });

    // playback_parental_guide. The overlay is decoration over the first
    // seconds of playback; with it off nothing is even looked up.
    btnParentalGuide->init("main/setting/playback/parental_guide"_i18n, MPVCore::PARENTAL_GUIDE,
        [&conf](bool value) {
            MPVCore::PARENTAL_GUIDE = value;
            conf.setItem(AppConfig::PARENTAL_GUIDE, value);
        });

    btnSkipIntroEnabled->init("main/setting/playback/skip_intro_enabled"_i18n, MPVCore::SKIP_INTRO_ENABLED,
        [&conf](bool value) {
            MPVCore::SKIP_INTRO_ENABLED = value;
            conf.setItem(AppConfig::SKIP_INTRO_ENABLED, value);
        });

    // subtitleStripSdh. Applied where a sidecar is cached, so it also decides
    // what is on disk; the cache key carries the choice.
    btnSubStripSdh->init("main/setting/playback/sub/strip_sdh"_i18n,
        conf.getItem(AppConfig::SUB_STRIP_SDH, false),
        [&conf](bool value) { conf.setItem(AppConfig::SUB_STRIP_SDH, value); });

    // autoSkipSegmentTypes: a flag per segment type, not one switch. All three
    // start off, as the reference's set starts empty.
    struct AutoSkip {
        brls::BooleanCell* cell;
        bool* flag;
        AppConfig::Item key;
        const char* label;
    };
    for (const AutoSkip& a : std::vector<AutoSkip>{
             {btnAutoSkipIntro, &MPVCore::AUTO_SKIP_INTRO, AppConfig::AUTO_SKIP_INTRO,
                 "main/setting/playback/auto_skip/intro"},
             {btnAutoSkipRecap, &MPVCore::AUTO_SKIP_RECAP, AppConfig::AUTO_SKIP_RECAP,
                 "main/setting/playback/auto_skip/recap"},
             {btnAutoSkipOutro, &MPVCore::AUTO_SKIP_OUTRO, AppConfig::AUTO_SKIP_OUTRO,
                 "main/setting/playback/auto_skip/outro"},
         }) {
        bool* flag = a.flag;
        AppConfig::Item key = a.key;
        a.cell->init(brls::getStr(a.label), *flag, [flag, key](bool value) {
            *flag = value;
            AppConfig::instance().setItem(key, value);
        });
    }

    // ---- Subtitle face -----------------------------------------------------
    // The player's own panel could already move all of this, but only at mpv:
    // nothing was written down, so every launch started from the defaults
    // again. These are the reference's keys, ranges and defaults, and the
    // player picks a change up without a restart.
    auto applySubs = []() { MPVCore::instance().applySubtitleStyle(); };

    std::vector<std::string> sizeLabels;
    std::vector<int> sizeValues;
    for (int v = 50; v <= 200; v += 10) {
        sizeLabels.push_back(fmt::format("{} %", v));
        sizeValues.push_back(v);
    }
    auto indexOf = [](const std::vector<int>& values, int current) {
        for (size_t i = 0; i < values.size(); i++)
            if (values[i] == current) return (int)i;
        return 0;
    };
    static std::vector<int> kSizes = sizeValues;
    selectorSubSize->init("main/setting/playback/sub/size"_i18n, sizeLabels, indexOf(kSizes, MPVCore::SUB_SIZE),
        [applySubs](int selected) {
            MPVCore::SUB_SIZE = kSizes[selected];
            AppConfig::instance().setItem(AppConfig::SUB_SIZE, MPVCore::SUB_SIZE);
            applySubs();
        });

    // The reference's slider runs -20..50 a step at a time; a selector cannot
    // carry seventy rows, so it steps by 5 and the player panel keeps the fine
    // control it always had.
    std::vector<std::string> offLabels;
    static std::vector<int> kOffsets;
    kOffsets.clear();
    for (int v = -20; v <= 50; v += 5) {
        offLabels.push_back(fmt::format("{} %", v));
        kOffsets.push_back(v);
    }
    selectorSubOffset->init("main/setting/playback/sub/offset"_i18n, offLabels,
        indexOf(kOffsets, MPVCore::SUB_OFFSET), [applySubs](int selected) {
            MPVCore::SUB_OFFSET = kOffsets[selected];
            AppConfig::instance().setItem(AppConfig::SUB_OFFSET, MPVCore::SUB_OFFSET);
            applySubs();
        });

    btnSubBold->init("main/setting/playback/sub/bold"_i18n, MPVCore::SUB_BOLD, [applySubs](bool value) {
        MPVCore::SUB_BOLD = value;
        AppConfig::instance().setItem(AppConfig::SUB_BOLD, value);
        applySubs();
    });

    btnSubOutline->init("main/setting/playback/sub/outline"_i18n, MPVCore::SUB_OUTLINE, [applySubs](bool value) {
        MPVCore::SUB_OUTLINE = value;
        AppConfig::instance().setItem(AppConfig::SUB_OUTLINE, value);
        applySubs();
    });

    std::vector<std::string> widthLabels;
    static std::vector<int> kWidths2;
    kWidths2.clear();
    for (int v = 1; v <= 5; v++) {
        widthLabels.push_back(std::to_string(v));
        kWidths2.push_back(v);
    }
    selectorSubOutlineWidth->init("main/setting/playback/sub/outline_width"_i18n, widthLabels,
        indexOf(kWidths2, MPVCore::SUB_OUTLINE_WIDTH), [applySubs](int selected) {
            MPVCore::SUB_OUTLINE_WIDTH = kWidths2[selected];
            AppConfig::instance().setItem(AppConfig::SUB_OUTLINE_WIDTH, MPVCore::SUB_OUTLINE_WIDTH);
            applySubs();
        });

    // The reference's own three swatch lists, in its own order. mpv takes
    // #AARRGGBB, which is also how the reference stores them.
    struct Swatch {
        const char* label;
        const char* argb;
    };
    auto initSwatches = [&conf, applySubs, indexOf](auto&& cell, const std::vector<Swatch>& swatches,
                            AppConfig::Item key, std::string* target, const char* label) {
        std::vector<std::string> labels;
        int index = 0;
        for (size_t i = 0; i < swatches.size(); i++) {
            labels.push_back(brls::getStr(swatches[i].label));
            if (*target == swatches[i].argb) index = (int)i;
        }
        cell->init(brls::getStr(label), labels, index, [&swatches, key, target, applySubs](int selected) {
            *target = swatches[selected].argb;
            AppConfig::instance().setItem(key, *target);
            applySubs();
        });
    };
    static const std::vector<Swatch> kTextColors = {
        {"main/setting/playback/sub/color/white", "#FFFFFFFF"},
        {"main/setting/playback/sub/color/silver", "#FFD9D9D9"},
        {"main/setting/playback/sub/color/yellow", "#FFFFFF00"},
        {"main/setting/playback/sub/color/cyan", "#FF00FFFF"},
        {"main/setting/playback/sub/color/green", "#FF00FF00"},
        {"main/setting/playback/sub/color/magenta", "#FFFF00FF"},
        {"main/setting/playback/sub/color/coral", "#FFFF6B6B"},
        {"main/setting/playback/sub/color/orange", "#FFFFA500"},
        {"main/setting/playback/sub/color/mint", "#FF90EE90"},
    };
    static const std::vector<Swatch> kBgColors = {
        {"main/setting/playback/sub/color/transparent", "#00000000"},
        {"main/setting/playback/sub/color/black", "#FF000000"},
        {"main/setting/playback/sub/color/black_half", "#80000000"},
        {"main/setting/playback/sub/color/near_black", "#FF1A1A1A"},
        {"main/setting/playback/sub/color/charcoal", "#FF2D2D2D"},
    };
    static const std::vector<Swatch> kOutlineColors = {
        {"main/setting/playback/sub/color/black", "#FF000000"},
        {"main/setting/playback/sub/color/near_black", "#FF1A1A1A"},
        {"main/setting/playback/sub/color/graphite", "#FF333333"},
        {"main/setting/playback/sub/color/white", "#FFFFFFFF"},
    };
    initSwatches(selectorSubTextColor, kTextColors, AppConfig::SUB_TEXT_COLOR, &MPVCore::SUB_TEXT_COLOR,
        "main/setting/playback/sub/text_color");
    // NuvioTV keeps opacity separate from the colour swatch (subtitle_style_
    // text_opacity), so picking a colour does not undo a chosen opacity.
    std::vector<std::string> opacityLabels;
    std::vector<int> opacityValues;
    for (int v = 100; v >= 30; v -= 10) {
        opacityLabels.push_back(fmt::format("{} %", v));
        opacityValues.push_back(v);
    }
    selectorSubOpacity->init("main/setting/playback/sub/opacity"_i18n, opacityLabels,
        indexOf(opacityValues, std::clamp(MPVCore::SUB_TEXT_OPACITY, 30, 100)),
        [&conf, opacityValues, applySubs](int selected) {
            MPVCore::SUB_TEXT_OPACITY = opacityValues[selected];
            conf.setItem(AppConfig::SUB_TEXT_OPACITY, MPVCore::SUB_TEXT_OPACITY);
            applySubs();
        });

    initSwatches(selectorSubBgColor, kBgColors, AppConfig::SUB_BG_COLOR, &MPVCore::SUB_BG_COLOR,
        "main/setting/playback/sub/bg_color");
    initSwatches(selectorSubOutlineColor, kOutlineColors, AppConfig::SUB_OUTLINE_COLOR,
        &MPVCore::SUB_OUTLINE_COLOR, "main/setting/playback/sub/outline_color");

    // When "up next" comes up: the reference's three PlayerSettingsDataStore
    // keys, at its own ranges. Its two sliders run over doubled integers —
    // 194..200 for the percentage and 0..7 for the minutes — so that the half
    // steps survive; the rows here are those same steps, spelled out.
    selectorNextEpisodeMode->init("main/setting/playback/next_episode_mode"_i18n,
        {
            "main/setting/playback/next_episode_threshold/percentage"_i18n,
            "main/setting/playback/next_episode_threshold/minutes"_i18n,
        },
        std::clamp(MPVCore::NEXT_EPISODE_MODE, 0, 1), [&conf](int selected) {
            MPVCore::NEXT_EPISODE_MODE = selected;
            conf.setItem(AppConfig::NEXT_EPISODE_MODE, selected);
        });
    selectorNextEpisodeMode->setDescriptions({
        "main/setting/playback/next_episode_threshold/percentage_desc"_i18n,
        "main/setting/playback/next_episode_threshold/minutes_desc"_i18n,
    });

    std::vector<std::string> percentOptions;
    for (int half = 194; half <= 200; half++) percentOptions.push_back(fmt::format("{:.1f}%", half / 2.0));
    selectorNextEpisodePercent->init("main/setting/playback/next_episode_percent"_i18n, percentOptions,
        std::clamp(MPVCore::NEXT_EPISODE_PERCENT, 194, 200) - 194, [&conf](int selected) {
            MPVCore::NEXT_EPISODE_PERCENT = 194 + selected;
            conf.setItem(AppConfig::NEXT_EPISODE_PERCENT, MPVCore::NEXT_EPISODE_PERCENT);
        });

    std::vector<std::string> minuteOptions;
    for (int half = 0; half <= 7; half++) minuteOptions.push_back(fmt::format("{:.1f} min", half / 2.0));
    selectorNextEpisodeMinutes->init("main/setting/playback/next_episode_minutes"_i18n, minuteOptions,
        std::clamp(MPVCore::NEXT_EPISODE_MINUTES, 0, 7), [&conf](int selected) {
            MPVCore::NEXT_EPISODE_MINUTES = selected;
            conf.setItem(AppConfig::NEXT_EPISODE_MINUTES, selected);
        });

    // StreamAutoPlayMode. The reference states its three as sentences; these
    // are the same three, in its own order (the enum's ordinal is what is
    // stored). The pattern row only exists for the third.
    int autoplayMode = std::clamp(conf.getItem(AppConfig::STREAM_AUTOPLAY_MODE, 0), 0, 2);
    selectorStreamAutoplayMode->init("main/setting/playback/stream_autoplay_mode"_i18n,
        {
            "main/setting/playback/stream_autoplay/manual"_i18n,
            "main/setting/playback/stream_autoplay/first"_i18n,
            "main/setting/playback/stream_autoplay/regex"_i18n,
        },
        autoplayMode, [&conf, this](int selected) {
            conf.setItem(AppConfig::STREAM_AUTOPLAY_MODE, selected);
            inputStreamAutoplayRegex->setVisibility(
                selected == 2 ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        });
    selectorStreamAutoplayMode->setDescriptions({
        "main/setting/playback/stream_autoplay/manual_desc"_i18n,
        "main/setting/playback/stream_autoplay/first_desc"_i18n,
        "main/setting/playback/stream_autoplay/regex_desc"_i18n,
    });

    inputStreamAutoplayRegex->init("main/setting/playback/stream_autoplay_regex"_i18n,
        conf.getItem(AppConfig::STREAM_AUTOPLAY_REGEX, std::string("")),
        [&conf](std::string value) { conf.setItem(AppConfig::STREAM_AUTOPLAY_REGEX, value); });
    inputStreamAutoplayRegex->setVisibility(
        autoplayMode == 2 ? brls::Visibility::VISIBLE : brls::Visibility::GONE);

    // streamAutoPlayPreferBingeGroupForNextEpisode. On, as it is there: a run
    // of episodes should stay on the release it started on.
    btnPreferBingeGroup->init("main/setting/playback/prefer_binge_group"_i18n,
        conf.getItem(AppConfig::PREFER_BINGE_GROUP, true),
        [&conf](bool value) { conf.setItem(AppConfig::PREFER_BINGE_GROUP, value); });

    // streamAutoPlayNextEpisodeEnabled — the card starts the next episode
    // itself rather than offering it.
    btnNextEpisodeAutoplay->init("main/setting/playback/next_episode_autoplay"_i18n,
        MPVCore::NEXT_EPISODE_AUTOPLAY, [&conf](bool value) {
            MPVCore::NEXT_EPISODE_AUTOPLAY = value;
            conf.setItem(AppConfig::NEXT_EPISODE_AUTOPLAY, value);
        });

    // stillWatchingEnabled. Only ever reached with auto-play on, as the
    // reference's shouldEnterStillWatchingPrompt requires both — on its own it
    // would be asking about episodes the viewer chose one by one.
    btnStillWatching->init("main/setting/playback/still_watching"_i18n, MPVCore::STILL_WATCHING,
        [&conf](bool value) {
            MPVCore::STILL_WATCHING = value;
            conf.setItem(AppConfig::STILL_WATCHING, value);
        });

    std::vector<std::string> thresholdOptions;
    for (int n = 2; n <= 6; n++) thresholdOptions.push_back(fmt::format("{}", n));
    selectorStillWatchingThreshold->init("main/setting/playback/still_watching_threshold"_i18n, thresholdOptions,
        std::clamp(MPVCore::STILL_WATCHING_THRESHOLD, 2, 6) - 2, [&conf](int selected) {
            MPVCore::STILL_WATCHING_THRESHOLD = 2 + selected;
            conf.setItem(AppConfig::STILL_WATCHING_THRESHOLD, MPVCore::STILL_WATCHING_THRESHOLD);
        });

    btnTouchGesture->init("main/setting/playback/touch_gesture"_i18n, MPVCore::TOUCH_GESTURE, [&conf](bool value) {
        MPVCore::TOUCH_GESTURE = value;
        conf.setItem(AppConfig::TOUCH_GESTURE, value);
    });

    btnTvOsdMode->init("main/setting/control/tv_osd"_i18n, MPVCore::OSD_TV_MODE, [&conf](bool value) {
        MPVCore::OSD_TV_MODE = value;
        conf.setItem(AppConfig::PLAYER_TV_MODE, value);
    });

    btnClipPoint->init("main/setting/playback/clip_point"_i18n, MPVCore::CLIP_POINT, [&conf](bool value) {
        MPVCore::CLIP_POINT = value;
        conf.setItem(AppConfig::CLIP_POINT, value);
    });

#ifdef __SWITCH__
    btnTutorialOpenApp->registerClickAction([](...) -> bool {
        brls::Application::pushActivity(new HintActivity());
        return true;
    });
    btnTutorialError->registerClickAction([](...) -> bool {
        auto view = brls::View::createFromXMLResource("view/tutorial_error.xml");
        auto dialog = new brls::Dialog(dynamic_cast<brls::Box*>(view));
        dialog->addButton("hints/ok"_i18n, []() {});
        dialog->open();
        return true;
    });
#else
    btnTutorialOpenApp->setVisibility(brls::Visibility::GONE);
    btnTutorialError->setVisibility(brls::Visibility::GONE);
#endif
    btnTutorialFont->registerClickAction([](...) -> bool {
        auto dialog = new brls::Dialog(new TutorialFont());
        dialog->addButton("hints/ok"_i18n, []() {});
        dialog->open();
        return true;
    });

    btnOpenConfig->registerClickAction([](...) -> bool {
        const std::string confDir = AppConfig::instance().configDir();
#if defined(__SWITCH__) || defined(__PSV__) || defined(__PS4__) || defined(ANDROID)
        Dialog::show("main/setting/others/config_dir"_i18n + ":\n" + confDir);
#else
#ifdef __linux__
        if (!brls::isSteamDeck())
#endif
        {
            brls::Application::getPlatform()->openBrowser(confDir);
        }
#endif
        return true;
    });

/// Fullscreen
#if (defined(__APPLE__) || defined(__linux__) || defined(_WIN32)) && !defined(ANDROID) && !defined(TRIMUI)
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

    btnSingle->init("main/setting/others/single"_i18n, conf.getItem(AppConfig::SINGLE, false), [](bool value) {
        AppConfig::instance().setItem(AppConfig::SINGLE, value);
        MPVCore::instance().restart();
    });
#else
    btnFullscreen->setVisibility(brls::Visibility::GONE);
    btnAlwaysOnTop->setVisibility(brls::Visibility::GONE);
    btnSingle->setVisibility(brls::Visibility::GONE);
#endif

#if (defined(__APPLE__) || defined(__linux__) || defined(_WIN32)) && !defined(TRIMUI)
    int keyIndex = conf.getOptionIndex(AppConfig::KEYMAP);
    selectorKeymap->init("main/setting/others/keymap/header"_i18n,
        {
            "main/setting/others/keymap/xbox"_i18n,
            "main/setting/others/keymap/ps"_i18n,
            "main/setting/others/keymap/keyboard"_i18n,
        },
        keyIndex, [keyIndex](int selected) {
            if (keyIndex == selected) return;
            auto& conf = AppConfig::instance();
            auto& keyOptions = conf.getOptions(AppConfig::KEYMAP);
            conf.setItem(AppConfig::KEYMAP, keyOptions.options[selected]);
        });
#else
    selectorKeymap->setVisibility(brls::Visibility::GONE);
#endif

    // App language
    int langIndex = conf.getOptionIndex(AppConfig::APP_LANG);
    selectorLang->init("main/setting/others/language/header"_i18n,
        {
            "main/setting/others/language/auto"_i18n,
            "English",
            "简体中文",
            "繁體中文",
            "日本語",
            "한국어",
            "Русский",
            "Deutsch",
            "Français",
            "Español",
            "Português",
            "Czech",
            "Українська",
            "Türkçe",
            "Tiếng việt",
        },
        langIndex, [langIndex](int selected) {
            if (langIndex == selected) return;
            auto& conf = AppConfig::instance();
            auto& langOptions = conf.getOptions(AppConfig::APP_LANG);
            conf.setItem(AppConfig::APP_LANG, langOptions.options[selected]);
        });

    // App theme
    int themeIndex = conf.getOptionIndex(AppConfig::APP_THEME);
    selectorTheme->init("main/setting/others/theme/header"_i18n,
        {
            "main/setting/others/theme/1"_i18n,
            "main/setting/others/theme/2"_i18n,
            "main/setting/others/theme/3"_i18n,
        },
        themeIndex, [themeIndex](int selected) {
            if (themeIndex == selected) return;
            auto& conf = AppConfig::instance();
            auto& themeOptions = conf.getOptions(AppConfig::APP_THEME);
            conf.setItem(AppConfig::APP_THEME, themeOptions.options[selected]);
        });

    // Colour theme: NuvioTV's own list, plus an "Automatic" that keeps the
    // per-backend brand palette this app had before.
    {
        const auto& themes = plenx::namedThemes();
        std::vector<std::string> labels{"main/setting/others/accent/auto"_i18n};
        std::vector<std::string> ids{"auto"};
        for (const auto& t : themes) {
            labels.emplace_back(t.name);
            ids.emplace_back(t.id);
        }
        std::string current = conf.getItem(AppConfig::ACCENT_THEME, std::string("auto"));
        auto it = std::find(ids.begin(), ids.end(), current);
        int accentIndex = it == ids.end() ? 0 : (int)(it - ids.begin());
        selectorAccent->init("main/setting/others/accent/header"_i18n, labels, accentIndex,
            [ids](int selected) {
                auto& c = AppConfig::instance();
                c.setItem(AppConfig::ACCENT_THEME, ids[(size_t)selected]);
                // Repaint now rather than on relaunch: applyTheme rewrites the
                // accent tokens and most views read them at draw time. Views
                // that cached a colour when they were built (and icons already
                // uploaded under the old accent key) catch up as they are
                // rebuilt — the tabs you navigate to next.
                c.applyTheme(c.backend().type());
                brls::Application::notify("main/setting/others/accent/applied"_i18n);
            });
    }

    // AppFont. The reference's own three families and its own order, each name
    // printed as it prints it. The stash is filled once, as the window comes
    // up, so this lands on the next launch (the cell's own quitApp prompt).
    selectorFont->init("main/setting/ui/font"_i18n, {"Inter", "DM Sans", "Open Sans"},
        std::clamp(conf.getItem(AppConfig::APPEARANCE_FONT, 0), 0, 2),
        [&conf](int selected) { conf.setItem(AppConfig::APPEARANCE_FONT, selected); });

    auto& threadOpt = conf.getOptions(AppConfig::REQUEST_THREADS);
    auto thIt = std::find(threadOpt.values.begin(), threadOpt.values.end(), ThreadPool::max_thread_num);
    size_t thIndex = thIt != threadOpt.values.end() ? thIt - threadOpt.values.begin() : 0;
    inputThreads->init("main/setting/network/threads"_i18n, threadOpt.options,
        conf.getValueIndex(AppConfig::REQUEST_THREADS, thIndex), [&threadOpt](int selected) {
            long threads = threadOpt.values[selected];
            ThreadPool::instance().start(threads);
            AppConfig::instance().setItem(AppConfig::REQUEST_THREADS, threads);
        });

    auto& timeoutOption = conf.getOptions(AppConfig::REQUEST_TIMEOUT);
    selectorTimeout->init("main/setting/network/timeout"_i18n, timeoutOption.options,
        conf.getValueIndex(AppConfig::REQUEST_TIMEOUT), [&timeoutOption](int selected) {
            HTTP::TIMEOUT = timeoutOption.values[selected];
            AppConfig::instance().setItem(AppConfig::REQUEST_TIMEOUT, HTTP::TIMEOUT);
        });

    btnProxy->init("main/setting/network/proxy"_i18n, HTTP::PROXY_STATUS, [this](bool value) {
        inputProxy->setVisibility(value ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        HTTP::PROXY_STATUS = value;
        AppConfig::instance().setItem(AppConfig::HTTP_PROXY_STATUS, value);
    });

    inputProxy->init("main/setting/network/host"_i18n, HTTP::PROXY, [](std::string value) {
        if (value.find_first_of("://") == std::string::npos) value = "http://" + value;
        HTTP::PROXY = value;
        AppConfig::instance().setItem(AppConfig::HTTP_PROXY, value);
    });
    inputProxy->setVisibility(HTTP::PROXY_STATUS ? brls::Visibility::VISIBLE : brls::Visibility::GONE);

    btnSync->init("main/setting/others/sync"_i18n, AppConfig::SYNC, [](bool value) {
        AppConfig::SYNC = value;
        AppConfig::instance().setItem(AppConfig::SYNC_SETTING, value);
    });

    btnDebug->init("main/setting/others/debug"_i18n, brls::Application::isDebuggingViewEnabled(), [](bool value) {
        // On-screen overlay only. File logging is no longer gated on this:
        // it is always on and rewritten each launch (see main.cpp), because
        // the launches worth diagnosing are the ones that never reach here.
        brls::Application::enableDebuggingView(value);
        MPVCore::instance().restart();
    });

    // ---- Advanced / Performance & navigation, and Advanced / Diagnostics,
    // the reference's own two groups (AdvancedSettingsContent).

    // fastHorizontalNavigationEnabled. The gate is read once, at startup, the
    // way the reference reads it into its composition local — so this lands on
    // the next launch, like poster titles and card width.
    btnFastHorizontalNav->init("main/setting/advanced/fast_horizontal_nav"_i18n,
        conf.getItem(AppConfig::FAST_HORIZONTAL_NAV, false), [&conf](bool value) {
            conf.setItem(AppConfig::FAST_HORIZONTAL_NAV, value);
            Dialog::quitApp();
        });

    // startupSplashEnabled. Read by LoadingActivity as it is built, so no
    // restart is needed — but the screen it affects is only shown at startup.
    btnStartupSplash->init("main/setting/advanced/startup_splash"_i18n,
        conf.getItem(AppConfig::STARTUP_SPLASH, true),
        [&conf](bool value) { conf.setItem(AppConfig::STARTUP_SPLASH, value); });

    // playerStatsHudEnabled — the row in Stream Information, not the overlay
    // itself, which keeps its own keybind either way.
    btnPlayerStatsHud->init("main/setting/advanced/player_stats_hud"_i18n, MPVCore::PLAYER_STATS_HUD,
        [&conf](bool value) {
            MPVCore::PLAYER_STATS_HUD = value;
            conf.setItem(AppConfig::PLAYER_STATS_HUD, value);
        });

    btnReleaseChecker->title->setText(
        fmt::format("{} ({}: {})", "main/setting/others/release"_i18n, "hints/current"_i18n, AppVersion::getVersion()));
    btnReleaseChecker->registerClickAction([](...) -> bool {
        AppVersion::checkUpdate(0, true);
        return true;
    });

    btnChangelog->registerClickAction([](...) -> bool {
        brls::Application::pushActivity(new Changelog());
        return true;
    });

    // DisclosureCell renders its own (SVG) chevron — see disclosure_cell.hpp.
    btnAbout->registerClickAction([](...) {
        brls::Dialog* dialog = new brls::Dialog(new SettingAbout());
        dialog->addButton("hints/ok"_i18n, []() {});
        dialog->open();
        return true;
    });

    // Reorder / hide the sidebar tabs (libraries + Playlists + Watchlist).
    // Presented as a detail view so the sidebar stays visible and previews the
    // changes live; needs the MainTabFrame (outermost AutoTabFrame) to rebuild.
    // Connections (accounts/servers): the sidebar avatar tab's old content,
    // presented as a detail view like the other managers here.
    btnConnections->registerClickAction([](brls::View* view) -> bool {
        ui::presentDetail(view, new ConnectionSwitcher());
        return true;
    });

    btnLibraries->registerClickAction([](brls::View* view) -> bool {
        MainTabFrame* frame = nullptr;
        for (brls::View* v = view; v; v = v->getParent())
            if (auto* f = dynamic_cast<MainTabFrame*>(v)) frame = f;
        if (frame) ui::presentDetail(view, new LibraryManager(frame));
        return true;
    });

    // Show/hide hub rows (Home + Movie/Show suggestions) — global, not tied
    // to a MainTabFrame like LibraryManager (no sidebar to live-preview into).
    btnHiddenRows->registerClickAction([](brls::View* view) -> bool {
        ui::presentDetail(view, new HubVisibilityManager());
        return true;
    });

    // Poster labels. NuvioTV makes this a Layout setting rather than a fixed
    // behaviour; the rows budget their height for the title block at startup
    // (AppConfig::initThemes), so it only takes effect on the next launch.
    btnPosterLabels->init("main/setting/layout/poster_labels"_i18n,
        conf.getItem(AppConfig::POSTER_LABELS, false), [](bool value) {
            AppConfig::instance().setItem(AppConfig::POSTER_LABELS, value);
            Dialog::quitApp();
        });

    // The rest of NuvioTV's Layout toggles that GMCA had as fixed behaviour.
    // Each takes effect the next time the screen it affects is built, so they
    // are plain setItem calls — no restart, unlike poster titles, whose row
    // heights are computed once at startup.
    struct LayoutToggle {
        brls::BooleanCell* cell;
        AppConfig::Item key;
        const char* label;
    };
    for (const LayoutToggle& t : std::vector<LayoutToggle>{
             {btnShowHero, AppConfig::SHOW_HERO, "main/setting/layout/show_hero"},
             {btnShowContinue, AppConfig::SHOW_CONTINUE, "main/setting/layout/show_continue"},
             {btnAddonName, AppConfig::CATALOG_ADDON_NAME, "main/setting/layout/catalog_addon_name"},
             {btnCatalogType, AppConfig::CATALOG_TYPE_SUFFIX, "main/setting/layout/catalog_type"},
             {btnStreamLogo, AppConfig::STREAM_ADDON_LOGO, "main/setting/layout/stream_addon_logo"},
             {btnEpisodeArt, AppConfig::EPISODE_OVERLAY_ART, "main/setting/layout/episode_overlay_art"},
         }) {
        AppConfig::Item key = t.key;
        t.cell->init(brls::getStr(t.label), conf.getItem(key, true),
            [key](bool value) { AppConfig::instance().setItem(key, value); });
    }

    // hide_unreleased_content, which the reference starts OFF — so it cannot
    // join the loop above, whose toggles all default on.
    btnHideUnreleased->init("main/setting/layout/hide_unreleased"_i18n,
        conf.getItem(AppConfig::LAYOUT_HIDE_UNRELEASED, false),
        [&conf](bool value) { conf.setItem(AppConfig::LAYOUT_HIDE_UNRELEASED, value); });

    // modernLandscapePostersEnabled. The row geometry behind it is computed
    // once, in initThemes, so it lands on the next launch.
    btnLandscapePosters->init("main/setting/layout/landscape_posters"_i18n,
        conf.getItem(AppConfig::LAYOUT_LANDSCAPE_POSTERS, false), [&conf](bool value) {
            conf.setItem(AppConfig::LAYOUT_LANDSCAPE_POSTERS, value);
            Dialog::quitApp();
        });

    // homeImdbRatingsVisibility, as a switch rather than its two-value enum:
    // the reference only ever offers SHOW_ALL and HIDE_ALL from this row.
    btnShowRatings->init("main/setting/layout/show_ratings"_i18n,
        conf.getItem(AppConfig::LAYOUT_SHOW_RATINGS, true),
        [&conf](bool value) { conf.setItem(AppConfig::LAYOUT_SHOW_RATINGS, value); });

    // modernHeroFullScreenBackdropEnabled. Off there, and read as the home
    // screen is built, so it lands on the next launch.
    btnFullscreenHero->init("main/setting/layout/fullscreen_hero"_i18n,
        conf.getItem(AppConfig::LAYOUT_FULLSCREEN_HERO, false), [&conf](bool value) {
            conf.setItem(AppConfig::LAYOUT_FULLSCREEN_HERO, value);
            Dialog::quitApp();
        });

    // Two more of the reference's Layout toggles, both on as it has them.
    btnFullReleaseDate->init("main/setting/layout/full_release_date"_i18n,
        conf.getItem(AppConfig::LAYOUT_FULL_RELEASE_DATE, true),
        [&conf](bool value) { conf.setItem(AppConfig::LAYOUT_FULL_RELEASE_DATE, value); });
    btnCwEpisodeThumbs->init("main/setting/layout/cw_episode_thumbs"_i18n,
        conf.getItem(AppConfig::LAYOUT_CW_EPISODE_THUMBS, true),
        [&conf](bool value) { conf.setItem(AppConfig::LAYOUT_CW_EPISODE_THUMBS, value); });

    // The reference's three Continue Watching rules. All of them are about the
    // episode the row offers once the one before it is finished, which is why
    // they sit together.
    btnCwNextUpFurthest->init("main/setting/layout/cw_next_up_furthest"_i18n,
        conf.getItem(AppConfig::CW_NEXT_UP_FURTHEST, true),
        [&conf](bool value) { conf.setItem(AppConfig::CW_NEXT_UP_FURTHEST, value); });
    btnCwShowUnaired->init("main/setting/layout/cw_show_unaired"_i18n,
        conf.getItem(AppConfig::CW_SHOW_UNAIRED, true),
        [&conf](bool value) { conf.setItem(AppConfig::CW_SHOW_UNAIRED, value); });
    selectorCwSortMode->init("main/setting/layout/cw_sort_mode"_i18n,
        {
            "main/setting/layout/cw_sort/default"_i18n,
            "main/setting/layout/cw_sort/streaming"_i18n,
            "main/setting/layout/cw_sort/split"_i18n,
        },
        std::clamp(conf.getItem(AppConfig::CW_SORT_MODE, 0), 0, 2),
        [&conf](int selected) { conf.setItem(AppConfig::CW_SORT_MODE, selected); });

    // Poster Card Style. The reference offers named presets rather than a free
    // slider, and these are its own six widths and five radii, in its dp.
    struct Preset {
        const char* label;
        int dp;
    };
    static const std::vector<Preset> kWidths = {
        {"main/setting/layout/preset/compact", 104}, {"main/setting/layout/preset/dense", 112},
        {"main/setting/layout/preset/standard", 120}, {"main/setting/layout/preset/balanced", 126},
        {"main/setting/layout/preset/comfort", 134}, {"main/setting/layout/preset/large", 140},
    };
    static const std::vector<Preset> kRadii = {
        {"main/setting/layout/preset/sharp", 0}, {"main/setting/layout/preset/subtle", 4},
        {"main/setting/layout/preset/classic", 8}, {"main/setting/layout/preset/rounded", 12},
        {"main/setting/layout/preset/pill", 16},
    };
    // Generic in the cell so it takes the BRLS_BIND wrapper directly.
    auto initPresets = [&conf](auto&& cell, const std::vector<Preset>& presets, AppConfig::Item key, int fallback,
                           const char* label) {
        std::vector<std::string> labels;
        int index = 0;
        int current = conf.getItem(key, fallback);
        for (size_t i = 0; i < presets.size(); i++) {
            labels.push_back(brls::getStr(presets[i].label));
            if (presets[i].dp == current) index = (int)i;
        }
        cell->init(brls::getStr(label), labels, index, [&presets, key](int selected) {
            AppConfig::instance().setItem(key, presets[selected].dp);
            // Card widths and row heights are computed once, in initThemes, so
            // this lands on the next launch — the same deal poster titles get.
            Dialog::quitApp();
        });
    };
    initPresets(selectorCardWidth, kWidths, AppConfig::LAYOUT_POSTER_WIDTH, 126, "main/setting/layout/card_width");
    initPresets(selectorCardRadius, kRadii, AppConfig::LAYOUT_POSTER_RADIUS, 12, "main/setting/layout/card_radius");

    this->buildCategories();
}

/// Material icon paths (24x24), inlined the way DisclosureCell inlines its
/// chevron so the rail needs no new asset files. In order: person, palette,
/// grid_view, play_arrow, build, info — the icons NuvioTV gives the same
/// categories in SettingsScreen.kt's section list.
static const char* kCategoryIcons[] = {
    "M12 12c2.21 0 4-1.79 4-4s-1.79-4-4-4-4 1.79-4 4 1.79 4 4 4zm0 2c-2.67 0-8 1.34-8 4v2h16v-2c0-2.66-5.33-4-8-4z",
    "M12 3c-4.97 0-9 4.03-9 9s4.03 9 9 9c.83 0 1.5-.67 1.5-1.5 0-.39-.15-.74-.39-1.01-.23-.26-.38-.61-.38-.99 "
    "0-.83.67-1.5 1.5-1.5H16c2.76 0 5-2.24 5-5 0-4.42-4.03-8-9-8zm-5.5 9c-.83 0-1.5-.67-1.5-1.5S5.67 9 6.5 9 8 "
    "9.67 8 10.5 7.33 12 6.5 12zm3-4C8.67 8 8 7.33 8 6.5S8.67 5 9.5 5s1.5.67 1.5 1.5S10.33 8 9.5 8zm5 0c-.83 "
    "0-1.5-.67-1.5-1.5S13.67 5 14.5 5s1.5.67 1.5 1.5S15.33 8 14.5 8zm3 4c-.83 0-1.5-.67-1.5-1.5S16.67 9 17.5 "
    "9s1.5.67 1.5 1.5-.67 1.5-1.5 1.5z",
    "M3 3h8v8H3V3zm10 0h8v8h-8V3zM3 13h8v8H3v-8zm10 0h8v8h-8v-8z",
    "M8 5v14l11-7z",
    "M11 7h2v2h-2zm0 4h2v6h-2zm1-9C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm0 18c-4.41 "
    "0-8-3.59-8-8s3.59-8 8-8 8 3.59 8 8-3.59 8-8 8z",
    "M22.7 19l-9.1-9.1c.9-2.3.4-5-1.5-6.9-2-2-5-2.4-7.4-1.3L9 6 6 9 1.6 4.7C.4 7.1.9 10.1 2.9 12.1c1.9 1.9 4.6 "
    "2.4 6.9 1.5l9.1 9.1c.4.4 1 .4 1.4 0l2.3-2.3c.5-.4.5-1.1.1-1.4z",
};

void SettingTab::buildCategories() {
    // SettingsScreen.kt's own order, minus the categories this app has no
    // counterpart for: About comes BEFORE Advanced there.
    static const char* kPages[] = {
        "setting/page/account",
        "setting/page/appearance",
        "setting/page/layout",
        "setting/page/playback",
        "setting/page/about",
        "setting/page/advanced",
    };
    static const char* kNames[] = {"account", "appearance", "layout", "playback", "about", "advanced"};

    this->categories.clear();
    this->boxNav->clearViews();

    for (size_t i = 0; i < sizeof(kPages) / sizeof(kPages[0]); i++) {
        brls::View* page = this->getView(kPages[i]);
        if (!page) continue;
        Category c;
        c.pageId = kPages[i];
        const std::string base = std::string("main/setting/category/") + kNames[i];
        c.title = brls::getStr(base + "/title");
        c.subtitle = brls::getStr(base + "/subtitle");
        // A category with a screen of its own in the reference names the pane
        // differently from the rail: the rail says "Playback", the pane says
        // "Playback Settings" (settings_playback vs playback_title).
        c.paneTitle = brls::getStr(base + "/pane_title");
        c.paneSubtitle = brls::getStr(base + "/pane_subtitle");
        // getStr hands back the key itself when there is no such string.
        if (c.paneTitle.empty() || c.paneTitle == base + "/pane_title") c.paneTitle = c.title;
        if (c.paneSubtitle.empty() || c.paneSubtitle == base + "/pane_subtitle") c.paneSubtitle = c.subtitle;
        c.page = page;
        size_t index = this->categories.size();
        c.item = new SettingsNavItem(
            kCategoryIcons[i], c.title, [this, index]() { this->selectCategory(index, true); });
        this->boxNav->addView(c.item);
        this->categories.push_back(c);
    }
    // false: the tab is still being built, nothing should steal focus yet
    if (!this->categories.empty()) this->selectCategory(0, false);
}

void SettingTab::selectCategory(size_t index, bool moveFocus) {
    if (index >= this->categories.size()) return;
    this->activeCategory = index;
    for (size_t i = 0; i < this->categories.size(); i++) {
        Category& c = this->categories[i];
        bool on = i == index;
        c.item->setActive(on);
        c.page->setVisibility(on ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }
    const Category& c = this->categories[index];
    this->labelPageTitle->setText(c.paneTitle);
    this->labelPageSubtitle->setText(c.paneSubtitle);
    this->labelPageSubtitle->setVisibility(
        c.paneSubtitle.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);

    // Picking a category hands focus to its first setting rather than leaving
    // it on the rail — you chose the category to get at what is in it. Next
    // frame: the page has only just become visible, so its cells are not
    // focusable (nor laid out) until this one has been through layout.
    if (!moveFocus) return;
    brls::View* page = c.page;
    brls::sync([page]() {
        if (brls::View* first = page->getDefaultFocus()) brls::Application::giveFocus(first);
    });
}

brls::View* SettingTab::create() { return new SettingTab(); }
