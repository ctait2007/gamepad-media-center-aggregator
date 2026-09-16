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
#include <view/settings_cells.hpp>

#include <string>
#include <vector>

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
        std::string paneTitle;
        std::string paneSubtitle;
        SettingsNavItem* item = nullptr;
        brls::View* page = nullptr;
    };
    std::vector<Category> categories;
    size_t activeCategory = 0;

    BRLS_BIND(ActionCell, btnTutorialOpenApp, "tools/tutorial_open");
    BRLS_BIND(ActionCell, btnTutorialError, "tools/tutorial_error");
    BRLS_BIND(ActionCell, btnTutorialFont, "tools/tutorial_font");
    BRLS_BIND(BooleanCell, btnHWDEC, "setting/video/hwdec");
    BRLS_BIND(BooleanCell, btnQuality, "setting/video/low_quality");
    BRLS_BIND(BooleanCell, btnSubFallback, "setting/video/subs_fallback");
    BRLS_BIND(SelectorCell, selectorSubLang, "setting/playback/subtitle_lang");
    BRLS_BIND(BooleanCell, btnDirectPlay, "setting/video/directplay");
    BRLS_BIND(SelectorCell, selectorVO, "setting/mpv/vo");
    BRLS_BIND(SelectorCell, selectorCodec, "setting/transcode/codec");
    BRLS_BIND(SelectorCell, selectorAudioChannels, "setting/playback/audio_channels");
    BRLS_BIND(SelectorCell, selectorInmemory, "setting/video/inmemory");
    BRLS_BIND(SelectorCell, selectorScale, "setting/ui/scale");
    BRLS_BIND(SelectorCell, selectorVSync, "setting/ui/vsync");
    BRLS_BIND(BooleanCell, btnShowFPS, "setting/ui/show_fps");
    BRLS_BIND(BooleanCell, btnPosterLabels, "setting/ui/poster_labels");
    BRLS_BIND(BooleanCell, btnShowHero, "setting/layout/show_hero");
    BRLS_BIND(BooleanCell, btnShowContinue, "setting/layout/show_continue");
    BRLS_BIND(BooleanCell, btnAddonName, "setting/layout/catalog_addon_name");
    BRLS_BIND(BooleanCell, btnCatalogType, "setting/layout/catalog_type");
    BRLS_BIND(BooleanCell, btnStreamLogo, "setting/layout/stream_addon_logo");
    BRLS_BIND(BooleanCell, btnEpisodeArt, "setting/layout/episode_overlay_art");
    BRLS_BIND(BooleanCell, btnOSDOnToggle, "setting/player/osd_on_toggle");
    BRLS_BIND(BooleanCell, btnLoadingScreen, "setting/player/loading_screen");
    BRLS_BIND(BooleanCell, btnLoadingStages, "setting/player/loading_stages");
    BRLS_BIND(BooleanCell, btnPauseScreen, "setting/player/pause_screen");
    BRLS_BIND(BooleanCell, btnNextEpisode, "setting/player/next_episode_card");
    BRLS_BIND(BooleanCell, btnHideUnreleased, "setting/layout/hide_unreleased");
    BRLS_BIND(BooleanCell, btnLandscapePosters, "setting/layout/landscape_posters");
    BRLS_BIND(BooleanCell, btnShowRatings, "setting/layout/show_ratings");
    BRLS_BIND(BooleanCell, btnFullscreenHero, "setting/layout/fullscreen_hero");
    BRLS_BIND(BooleanCell, btnFullReleaseDate, "setting/layout/full_release_date");
    BRLS_BIND(BooleanCell, btnCwEpisodeThumbs, "setting/layout/cw_episode_thumbs");
    BRLS_BIND(BooleanCell, btnCwNextUpFurthest, "setting/layout/cw_next_up_furthest");
    BRLS_BIND(BooleanCell, btnCwShowUnaired, "setting/layout/cw_show_unaired");
    BRLS_BIND(SelectorCell, selectorCwSortMode, "setting/layout/cw_sort_mode");
    BRLS_BIND(SelectorCell, selectorCardWidth, "setting/layout/card_width");
    BRLS_BIND(SelectorCell, selectorCardRadius, "setting/layout/card_radius");
    BRLS_BIND(BooleanCell, btnIntroDb, "setting/player/introdb");
    BRLS_BIND(SelectorCell, selectorAudioLang, "setting/playback/audio_lang");
    BRLS_BIND(BooleanCell, btnAudioDelayRemember, "setting/player/audio_delay_remember");
    BRLS_BIND(BooleanCell, btnParentalGuide, "setting/player/parental_guide");
    BRLS_BIND(BooleanCell, btnOsdClock, "setting/player/osd_clock");
    BRLS_BIND(BooleanCell, btnAmoled, "setting/ui/amoled");
    BRLS_BIND(BooleanCell, btnAmoledSurfaces, "setting/ui/amoled_surfaces");
    BRLS_BIND(BooleanCell, btnSkipIntroEnabled, "setting/player/skip_intro_enabled");
    BRLS_BIND(BooleanCell, btnSubStripSdh, "setting/player/sub_strip_sdh");
    BRLS_BIND(SelectorCell, selectorSubSecondaryLang, "setting/playback/sub_secondary_lang");
    BRLS_BIND(BooleanCell, btnSubOnlyPreferred, "setting/player/sub_only_preferred");
    BRLS_BIND(BooleanCell, btnSubUseForced, "setting/player/sub_use_forced");
    BRLS_BIND(BooleanCell, btnAutoSkipIntro, "setting/player/auto_skip_intro");
    BRLS_BIND(BooleanCell, btnAutoSkipRecap, "setting/player/auto_skip_recap");
    BRLS_BIND(BooleanCell, btnAutoSkipOutro, "setting/player/auto_skip_outro");
    BRLS_BIND(SelectorCell, selectorSubSize, "setting/player/sub_size");
    BRLS_BIND(SelectorCell, selectorSubOffset, "setting/player/sub_offset");
    BRLS_BIND(BooleanCell, btnSubBold, "setting/player/sub_bold");
    BRLS_BIND(BooleanCell, btnSubOutline, "setting/player/sub_outline");
    BRLS_BIND(SelectorCell, selectorSubOutlineWidth, "setting/player/sub_outline_width");
    BRLS_BIND(SelectorCell, selectorSubTextColor, "setting/player/sub_text_color");
    BRLS_BIND(SelectorCell, selectorSubOpacity, "setting/player/sub_opacity");
    BRLS_BIND(SelectorCell, selectorSubBgColor, "setting/player/sub_bg_color");
    BRLS_BIND(SelectorCell, selectorSubOutlineColor, "setting/player/sub_outline_color");
    BRLS_BIND(SelectorCell, selectorNextEpisodeMode, "setting/player/next_episode_mode");
    BRLS_BIND(SelectorCell, selectorNextEpisodePercent, "setting/player/next_episode_percent");
    BRLS_BIND(SelectorCell, selectorNextEpisodeMinutes, "setting/player/next_episode_minutes");
    BRLS_BIND(SelectorCell, selectorStreamAutoplayMode, "setting/player/stream_autoplay_mode");
    BRLS_BIND(InputCell, inputStreamAutoplayRegex, "setting/player/stream_autoplay_regex");
    BRLS_BIND(BooleanCell, btnPreferBingeGroup, "setting/player/prefer_binge_group");
    BRLS_BIND(BooleanCell, btnNextEpisodeAutoplay, "setting/player/next_episode_autoplay");
    BRLS_BIND(BooleanCell, btnStillWatching, "setting/player/still_watching");
    BRLS_BIND(SelectorCell, selectorStillWatchingThreshold, "setting/player/still_watching_threshold");
    BRLS_BIND(BooleanCell, btnTouchGesture, "setting/player/touch_gesture");
    BRLS_BIND(BooleanCell, btnTvOsdMode, "setting/player/tv_model");
    BRLS_BIND(BooleanCell, btnClipPoint, "setting/player/clip_point");
    BRLS_BIND(BooleanCell, btnFullscreen, "setting/fullscreen");
    BRLS_BIND(BooleanCell, btnAlwaysOnTop, "setting/always_on_top");
    BRLS_BIND(BooleanCell, btnSingle, "setting/single");
    BRLS_BIND(BooleanCell, btnOverClock, "setting/overclock");
    BRLS_BIND(BooleanCell, btnDebug, "setting/debug");
    BRLS_BIND(BooleanCell, btnFastHorizontalNav, "setting/advanced/fast_horizontal_nav");
    BRLS_BIND(BooleanCell, btnStartupSplash, "setting/advanced/startup_splash");
    BRLS_BIND(BooleanCell, btnPlayerStatsHud, "setting/advanced/player_stats_hud");
    BRLS_BIND(BooleanCell, btnSync, "setting/sync");
    BRLS_BIND(SelectorCell, inputThreads, "setting/network/threads");
    BRLS_BIND(SelectorCell, selectorTimeout, "setting/network/timeout");
    BRLS_BIND(BooleanCell, btnTls, "setting/network/tls");
    BRLS_BIND(BooleanCell, btnProxy, "setting/network/proxy_status");
    BRLS_BIND(InputCell, inputProxy, "setting/network/proxy");
    BRLS_BIND(SelectorCell, selectorKeymap, "setting/keymap");
    BRLS_BIND(SelectorCell, selectorLang, "setting/language");
    BRLS_BIND(SelectorCell, selectorTheme, "setting/ui/theme");
    BRLS_BIND(SelectorCell, selectorAccent, "setting/ui/accent");
    BRLS_BIND(SelectorCell, selectorFont, "setting/ui/font");
    BRLS_BIND(ActionCell, btnOpenConfig, "tools/config_dir");
    BRLS_BIND(ActionCell, btnReleaseChecker, "setting/release_checker");
    BRLS_BIND(ActionCell, btnChangelog, "setting/changelog");
    BRLS_BIND(DisclosureCell, btnAbout, "setting/about");
    BRLS_BIND(DisclosureCell, btnConnections, "setting/connections");
    BRLS_BIND(DisclosureCell, btnLibraries, "setting/libraries");
    BRLS_BIND(DisclosureCell, btnHiddenRows, "setting/hidden_rows");
    BRLS_BIND(brls::Box, boxNav, "setting/nav");
    BRLS_BIND(brls::Label, labelPageTitle, "setting/page/title");
    BRLS_BIND(brls::Label, labelPageSubtitle, "setting/page/subtitle");
};
