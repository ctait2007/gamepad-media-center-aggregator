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
    BRLS_BIND(brls::BooleanCell, btnOSDOnToggle, "setting/player/osd_on_toggle");
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
