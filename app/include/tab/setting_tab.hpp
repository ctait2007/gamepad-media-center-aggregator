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

#pragma once

#include <view/auto_tab_frame.hpp>
#include <view/disclosure_cell.hpp>

#include <string>
#include <vector>

class SelectorCell;
class SettingsNavItem;

class SettingTab : public AttachedView {
public:
    SettingTab();

    void onCreate() override;

    static brls::View* create();

private:
    /// Build the category rail and show the first category. Every category's
    /// content is already in the XML, one collapsed page each; this reveals
    /// one at a time, the way NuvioTV's settings screen does.
    void buildCategories();
    void selectCategory(size_t index, bool moveFocus);

    /// One rail entry and the page it reveals.
    struct Category {
        const char* pageId;
        std::string title;
        std::string subtitle;
        SettingsNavItem* item = nullptr;
        brls::View* page = nullptr;
    };
    std::vector<Category> categories;
    size_t activeCategory = 0;

    BRLS_BIND(brls::RadioCell, btnTutorialOpenApp, "tools/tutorial_open");
    BRLS_BIND(brls::RadioCell, btnTutorialError, "tools/tutorial_error");
    BRLS_BIND(brls::RadioCell, btnTutorialFont, "tools/tutorial_font");
    BRLS_BIND(brls::BooleanCell, btnHWDEC, "setting/video/hwdec");
    BRLS_BIND(brls::BooleanCell, btnQuality, "setting/video/low_quality");
    BRLS_BIND(brls::BooleanCell, btnSubFallback, "setting/video/subs_fallback");
    BRLS_BIND(brls::SelectorCell, selectorSubLang, "setting/playback/subtitle_lang");
    BRLS_BIND(brls::BooleanCell, btnDirectPlay, "setting/video/directplay");
    BRLS_BIND(brls::SelectorCell, selectorVO, "setting/mpv/vo");
    BRLS_BIND(brls::SelectorCell, selectorCodec, "setting/transcode/codec");
    BRLS_BIND(brls::SelectorCell, selectorAudioChannels, "setting/playback/audio_channels");
    BRLS_BIND(brls::SelectorCell, selectorInmemory, "setting/video/inmemory");
    BRLS_BIND(brls::SelectorCell, selectorScale, "setting/ui/scale");
    BRLS_BIND(brls::SelectorCell, selectorVSync, "setting/ui/vsync");
    BRLS_BIND(brls::BooleanCell, btnShowFPS, "setting/ui/show_fps");
    BRLS_BIND(brls::BooleanCell, btnPosterLabels, "setting/ui/poster_labels");
    BRLS_BIND(brls::BooleanCell, btnShowHero, "setting/layout/show_hero");
    BRLS_BIND(brls::BooleanCell, btnShowContinue, "setting/layout/show_continue");
    BRLS_BIND(brls::BooleanCell, btnAddonName, "setting/layout/catalog_addon_name");
    BRLS_BIND(brls::BooleanCell, btnCatalogType, "setting/layout/catalog_type");
    BRLS_BIND(brls::BooleanCell, btnStreamLogo, "setting/layout/stream_addon_logo");
    BRLS_BIND(brls::BooleanCell, btnEpisodeArt, "setting/layout/episode_overlay_art");
    BRLS_BIND(brls::BooleanCell, btnOSDOnToggle, "setting/player/osd_on_toggle");
    BRLS_BIND(brls::BooleanCell, btnLoadingScreen, "setting/player/loading_screen");
    BRLS_BIND(brls::BooleanCell, btnLoadingStages, "setting/player/loading_stages");
    BRLS_BIND(brls::BooleanCell, btnPauseScreen, "setting/player/pause_screen");
    BRLS_BIND(brls::BooleanCell, btnNextEpisode, "setting/player/next_episode_card");
    BRLS_BIND(brls::BooleanCell, btnHideUnreleased, "setting/layout/hide_unreleased");
    BRLS_BIND(brls::BooleanCell, btnFullReleaseDate, "setting/layout/full_release_date");
    BRLS_BIND(brls::BooleanCell, btnCwEpisodeThumbs, "setting/layout/cw_episode_thumbs");
    BRLS_BIND(brls::SelectorCell, selectorCardWidth, "setting/layout/card_width");
    BRLS_BIND(brls::SelectorCell, selectorCardRadius, "setting/layout/card_radius");
    BRLS_BIND(brls::BooleanCell, btnIntroDb, "setting/player/introdb");
    BRLS_BIND(brls::BooleanCell, btnOsdClock, "setting/player/osd_clock");
    BRLS_BIND(brls::BooleanCell, btnAmoled, "setting/ui/amoled");
    BRLS_BIND(brls::BooleanCell, btnAmoledSurfaces, "setting/ui/amoled_surfaces");
    BRLS_BIND(brls::BooleanCell, btnAutoSkipIntro, "setting/player/auto_skip_intro");
    BRLS_BIND(brls::BooleanCell, btnAutoSkipRecap, "setting/player/auto_skip_recap");
    BRLS_BIND(brls::BooleanCell, btnAutoSkipOutro, "setting/player/auto_skip_outro");
    BRLS_BIND(brls::SelectorCell, selectorSubSize, "setting/player/sub_size");
    BRLS_BIND(brls::SelectorCell, selectorSubOffset, "setting/player/sub_offset");
    BRLS_BIND(brls::BooleanCell, btnSubBold, "setting/player/sub_bold");
    BRLS_BIND(brls::BooleanCell, btnSubOutline, "setting/player/sub_outline");
    BRLS_BIND(brls::SelectorCell, selectorSubOutlineWidth, "setting/player/sub_outline_width");
    BRLS_BIND(brls::SelectorCell, selectorSubTextColor, "setting/player/sub_text_color");
    BRLS_BIND(brls::SelectorCell, selectorSubBgColor, "setting/player/sub_bg_color");
    BRLS_BIND(brls::SelectorCell, selectorSubOutlineColor, "setting/player/sub_outline_color");
    BRLS_BIND(brls::SelectorCell, selectorNextEpisodeMode, "setting/player/next_episode_mode");
    BRLS_BIND(brls::SelectorCell, selectorNextEpisodePercent, "setting/player/next_episode_percent");
    BRLS_BIND(brls::SelectorCell, selectorNextEpisodeMinutes, "setting/player/next_episode_minutes");
    BRLS_BIND(brls::BooleanCell, btnTouchGesture, "setting/player/touch_gesture");
    BRLS_BIND(brls::BooleanCell, btnTvOsdMode, "setting/player/tv_model");
    BRLS_BIND(brls::BooleanCell, btnClipPoint, "setting/player/clip_point");
    BRLS_BIND(brls::BooleanCell, btnFullscreen, "setting/fullscreen");
    BRLS_BIND(brls::BooleanCell, btnAlwaysOnTop, "setting/always_on_top");
    BRLS_BIND(brls::BooleanCell, btnSingle, "setting/single");
    BRLS_BIND(brls::BooleanCell, btnOverClock, "setting/overclock");
    BRLS_BIND(brls::BooleanCell, btnDebug, "setting/debug");
    BRLS_BIND(brls::BooleanCell, btnSync, "setting/sync");
    BRLS_BIND(brls::SelectorCell, inputThreads, "setting/network/threads");
    BRLS_BIND(brls::SelectorCell, selectorTimeout, "setting/network/timeout");
    BRLS_BIND(brls::BooleanCell, btnTls, "setting/network/tls");
    BRLS_BIND(brls::BooleanCell, btnProxy, "setting/network/proxy_status");
    BRLS_BIND(brls::InputCell, inputProxy, "setting/network/proxy");
    BRLS_BIND(SelectorCell, selectorKeymap, "setting/keymap");
    BRLS_BIND(SelectorCell, selectorLang, "setting/language");
    BRLS_BIND(SelectorCell, selectorTheme, "setting/ui/theme");
    BRLS_BIND(SelectorCell, selectorAccent, "setting/ui/accent");
    BRLS_BIND(brls::RadioCell, btnOpenConfig, "tools/config_dir");
    BRLS_BIND(brls::RadioCell, btnReleaseChecker, "setting/release_checker");
    BRLS_BIND(brls::RadioCell, btnChangelog, "setting/changelog");
    BRLS_BIND(DisclosureCell, btnAbout, "setting/about");
    BRLS_BIND(DisclosureCell, btnConnections, "setting/connections");
    BRLS_BIND(DisclosureCell, btnLibraries, "setting/libraries");
    BRLS_BIND(DisclosureCell, btnHiddenRows, "setting/hidden_rows");
    BRLS_BIND(brls::Box, boxNav, "setting/nav");
    BRLS_BIND(brls::Label, labelPageTitle, "setting/page/title");
    BRLS_BIND(brls::Label, labelPageSubtitle, "setting/page/subtitle");
};
