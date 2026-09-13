//
// Created by fang on 2022/4/22.
//

#pragma once

#include <borealis.hpp>
#include <utils/event.hpp>

#include "view/player_button.hpp"

class VideoProgressSlider;
class SVGImage;
class VideoProfile;

enum class OSDState {
    HIDDEN = 0,
    SHOWN = 1,
    ALWAYS_ON = 2,
};

enum class ClickState {
    IDLE = 0,
    PRESS = 1,
    FAST_RELEASE = 3,
    FAST_PRESS = 4,
    CLICK_DOUBLE = 5,
};

class VideoView : public brls::Box {
public:
    VideoView();

    ~VideoView() override;

    void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx) override;

    void invalidate() override;

    View* getDefaultFocus() override { return this->isOsdShown ? (View*)this->btnToggle.getView() : (View*)this; }

    void onChildFocusGained(View* directChild, View* focusedView) override;

    View* getNextFocus(brls::FocusDirection direction, View* currentView) override { return this; }

    void setTitie(const std::string& title);
    /// The top line, derived from the item — takes over from setTitie's joined
    /// string for good once called.
    void setMainTitle(const std::string& text);
    /// "S2 E1 • The Scrub" under the title; empty hides the line.
    void setEpisodeLine(const std::string& text);
    /// The stream's name, shown as "via <name>" while paused.
    void setSourceLine(const std::string& text);

    void setList(const std::vector<std::string>& values, int index = -1);

    void setClipPoint(const std::vector<float>& clips);

    void playNext(int offset);

    brls::Event<int>* getPlayEvent() { return &this->playIndexEvent; }

    brls::VoidEvent* getSettingEvent() { return &this->settingEvent; }

    VideoProfile* getProfile() { return this->profile; }

    void hideVideoProgressSlider();
    void hideVideoQuality();
    void registerVideoQuality(brls::ActionListener action);
    void registerVideoSubtitle(brls::ActionListener action);
    void registerVideoAudio(brls::ActionListener action);
    /// The source re-selector. Like the three above, the button is GONE until
    /// someone registers it, so it only appears for a backend that actually
    /// has more than one stream to offer.
    /// The episode sheet. Registered by the owner rather than built here: the
    /// reference's panel draws each episode's still, title, air date and
    /// synopsis, and the player view is what actually holds those items — this
    /// view only ever had their formatted one-line titles.
    void registerEpisodes(brls::ActionListener action);
    /// Tell the bar which episode is playing WITHOUT starting it. Firing the
    /// play event is what starts playback, so an episode switched in place
    /// (picked from the sheet, then a source chosen for it) has no way to say
    /// "the index moved" — and the Next button went on offering the episode
    /// after the OLD one.
    void setPlayIndex(int index);
    void registerSources(brls::ActionListener action);
    /// Dismiss the transport controls — what opening a panel does first, so a
    /// bottom-anchored overlay lands on the video and not on the control row.
    void hideOSD();
    /// Show or hide it once the owner knows whether there IS a choice — the
    /// action stays registered either way, so this can flip per episode.
    void setSourcesVisible(bool visible);
    void setVideoQualityVisible(bool visible);
    /// Stream information — the one entry of the reference's overflow menu we
    /// kept, promoted onto the row.
    void registerStreamInfo(brls::ActionListener action);
    /// Optional hook fired on MPV_FILE_ERROR. If it returns true the error is
    /// considered handled (e.g. PlayerView fell back to direct play) and no
    /// error dialog is shown. Unset for local/remote players -> dialog as before.
    void registerError(brls::ActionListener action);
    void registerActions(const std::string& hintText, const brls::ControllerButton button,
        const brls::BrlsKeyCombination key, const brls::ActionListener& actionListener, bool hidden = false,
        bool allowRepeating = false);

    void showOSD(bool autoHide = true);

    static bool close(bool quit = false);

private:
    /// OSD
    // The control row is NuvioTV's: play/pause, next episode, subtitles, audio,
    // sources, episodes, stream info. Close, seek, volume and player settings
    // are not on it — the reference has no such chrome, and on a pad they are
    // O, the d-pad, RT+d-pad and X respectively.
    BRLS_BIND(brls::Label, titleLabel, "video/osd/title");
    BRLS_BIND(brls::Label, episodeLabel, "video/osd/episode");
    BRLS_BIND(brls::Label, sourceLabel, "video/osd/source");
    BRLS_BIND(brls::Label, timeLabel, "video/osd/time");
    BRLS_BIND(brls::Label, clockLabel, "video/osd/clock");
    BRLS_BIND(brls::Label, endsLabel, "video/osd/ends");
    BRLS_BIND(PlayerButton, btnToggle, "video/osd/toggle");
    BRLS_BIND(PlayerButton, btnNext, "video/osd/next");
    BRLS_BIND(PlayerButton, btnCast, "video/osd/cast");
    BRLS_BIND(PlayerButton, btnSources, "video/osd/sources");
    BRLS_BIND(PlayerButton, btnVideoQuality, "video/quality/box");
    BRLS_BIND(PlayerButton, btnVideoSubtitle, "video/subtitle/box");
    BRLS_BIND(PlayerButton, btnVideoAudio, "video/audio/box");
    BRLS_BIND(PlayerButton, btnEpisode, "show/episode/box");
    BRLS_BIND(brls::Box, osdTopBox, "video/osd/top/box");
    BRLS_BIND(brls::Box, osdBottomBox, "video/osd/bottom/box");
    // 用于显示缓冲组件
    BRLS_BIND(brls::Box, osdCenterBox, "video/osd/center/box");
    BRLS_BIND(brls::Label, centerLabel, "video/osd/center/label");
    BRLS_BIND(brls::ProgressSpinner, osdSpinner, "video/osd/loading");
    // 用于通用的提示信息
    BRLS_BIND(brls::Box, osdInfoBox, "video/osd/info/box");
    BRLS_BIND(brls::Label, infoLabel, "video/osd/info/label");
    BRLS_BIND(SVGImage, infoIcon, "video/osd/info/icon");
    // 用于显示和控制视频时长
    BRLS_BIND(VideoProgressSlider, osdSlider, "video/osd/bottom/progress");
    BRLS_BIND(brls::Label, speedHintLabel, "video/speed/hint/label");
    BRLS_BIND(brls::Box, speedHintBox, "video/speed/hint/box");
    BRLS_BIND(brls::Label, hintLabel, "video/osd/hint/label");
    BRLS_BIND(brls::Box, hintBox, "video/osd/hint/box");

    void registerMpvEvent();
    void unRegisterMpvEvent();

    void showLoading();
    void hideLoading(bool dimming = true);
    bool toggleProfile();
    /// OSD
    void toggleOSD();
    bool toggleSpeed();
    bool toggleVolume(brls::View* view);
    void showHint(const std::string& value);
    /// elapsed/total on the row, wall clock + finish time top-right
    void updateTime(double positionSec, double durationSec);
    void applySourceLine();
    void setTvMode(bool state);

    /// @brief 延迟 200ms 触发进度跳转到 seeking_range
    void requestSeeking(int seek, int delay = 400);
    void requestVolume(int value, int delay = 400);
    void requestBrightness(float value);
    void buttonProcessing();
    /// @brief notify videoview closed
    static void disableDimming(bool disable);

    std::string sourceName;
    bool titleLocked = false;
    /// what setMainTitle was last given — the episode panel prints it under
    /// its own heading, as the reference does with the show name
    std::string mainTitle;
    /// how many episodes setList was given, for the Next button's gate
    int playListSize = 0;
    int playIndex = -1;
    brls::Event<int> playIndexEvent;
    brls::VoidEvent settingEvent;
    View* lastFocusedView = nullptr;

    // OSD
    bool isOsdShown = false;
    brls::Time osdLastShowTime = 0;
    brls::Time hintLastShowTime = 0;
    brls::Time profileLastShowTime = 0;
    const brls::Time OSD_SHOW_TIME = 5000000;  //默认5秒
    OSDState osdState = OSDState::HIDDEN;
    VideoProfile* profile;

    /// fired on MPV_FILE_ERROR; returning true suppresses the error dialog
    brls::ActionListener errorAction = nullptr;

    int64_t seekingRange = 0;
    size_t seekingIter = 0;

    MPVEvent::Subscription eventSubscribeID;
    brls::Rect oldRect = brls::Rect(-1, -1, -1, -1);
    brls::InputManager* input = nullptr;

    size_t volumeIter = 0;  // 音量UI关闭的延迟函数 handle
    int volumeInit = 0;
    float brightnessInit = 0;
};
