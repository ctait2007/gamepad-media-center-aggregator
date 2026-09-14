#include "view/video_view.hpp"
#include "utils/config.hpp"
#include "utils/dialog.hpp"
#include "utils/gesture.hpp"
#include "utils/keybind.hpp"
#include "utils/misc.hpp"
#include "view/mpv_core.hpp"
#include "view/svg_image.hpp"
#include "view/video_profile.hpp"
#include "view/video_progress_slider.hpp"

const int VIDEO_SEEK_NODELAY = 0;

using namespace brls::literals;

static int getSeekRange(int current) {
    current = abs(current);
    // 10 s for the first jumps, not 5: a single press should move far enough
    // to be worth pressing. The tiers above it are unchanged — holding the
    // button still accelerates through a long file.
    if (current < 300) return 10;
    if (current < 600) return 20;
    if (current < 1200) return 60;
    return current / 15;
}

VideoView::VideoView() {
    this->inflateFromXMLRes("xml/view/video_view.xml");
    brls::Logger::debug("VideoView: created");
    this->setHideHighlightBorder(true);
    this->setHideHighlightBackground(true);
    this->setHideClickAnimation(true);

    auto& mpv = MPVCore::instance();
    // 停止正在播放的音乐
    mpv.reset();
    mpv.enableVO(true);

    if (MPVCore::OSD_TV_MODE) this->setTvMode(true);

    this->input = brls::Application::getPlatform()->getInputManager();

    this->registerAction(
        "hints/back"_i18n, brls::BUTTON_B,
        [this](brls::View* view) {
            // Circle on the pause screen is "put that away", not "leave" —
            // it hands you back the paused OSD. Checked before everything
            // else, since the screen is up over all of it.
            if (this->nextCard && this->nextCard->shown()) {
                this->dismissNextEpisodeCard();
                return true;
            }
            if (this->pauseScreenShown()) {
                this->cancelPauseScreen();
                // Still paused, so the bar it hands back is pinned open too.
                this->showOSD(false);
                return true;
            }
            if (MPVCore::OSD_TV_MODE && this->isOsdShown) {
                this->toggleOSD();
                return true;
            }
            return close();
        },
        true);

    this->registerActions(
        "\uE08F", brls::BUTTON_LB, KeyBind::getRewind(),
        [this](brls::View* view) -> bool {
            this->seekingRange -= getSeekRange(this->seekingRange);
            this->requestSeeking(seekingRange);
            return true;
        },
        false, true);

    this->registerActions(
        "\uE08E", brls::BUTTON_RB, KeyBind::getForward(),
        [this](brls::View* view) -> bool {
            this->seekingRange += getSeekRange(this->seekingRange);
            this->requestSeeking(seekingRange);
            return true;
        },
        false, true);

    // d-pad on the focused progress bar: left/right seek (same steps as
    // LB/RB), up/down leave towards the OSD controls
    this->osdSlider->registerAction(
        "\uE08F", brls::BUTTON_NAV_LEFT,
        [this](brls::View* view) -> bool {
            this->showOSD(true);
            this->seekingRange -= getSeekRange(this->seekingRange);
            this->requestSeeking(seekingRange);
            return true;
        },
        true, true);
    this->osdSlider->registerAction(
        "\uE08E", brls::BUTTON_NAV_RIGHT,
        [this](brls::View* view) -> bool {
            this->showOSD(true);
            this->seekingRange += getSeekRange(this->seekingRange);
            this->requestSeeking(seekingRange);
            return true;
        },
        true, true);
    // The bar sits directly above the control row, so DOWN goes to play/pause
    // and there is nothing above it to go UP to — the reference wires exactly
    // this pair (its progress bar's downFocusRequester is the play button).
    // Both routes used to name views that no longer exist, which is why DOWN
    // did nothing at all and UP reached the lock button.
    brls::View* sliderPointer = this->osdSlider->getDefaultFocus();
    sliderPointer->setCustomNavigationRoute(brls::FocusDirection::DOWN, (brls::View*)this->btnToggle);

    this->registerActions(
        "toggleOSD", brls::BUTTON_Y, KeyBind::getVideoOsd(),
        [this](brls::View* view) -> bool {
            // 拖拽进度时不要影响显示 OSD
            if (!this->seekingRange) this->toggleOSD();
            return true;
        },
        true);

    // NO settings shortcut. Square used to open the playback side panel, and
    // everything worth reaching from the player is already on the control row
    // (settingEvent is still here for any caller that wants to open it another
    // way — nothing on the player fires it).

    this->registerActions(
        "volumeUp", brls::BUTTON_NAV_UP, KeyBind::getVolumeUp(),
        [this](brls::View* view) -> bool {
            auto& state = brls::Application::getControllerState();
            if (state.buttons[brls::BUTTON_RT]) {
                this->requestVolume((int)MPVCore::instance().volume + 5, 400);
                return true;
            }
            // d-pad with the OSD hidden: wake it and focus the controls
            if (!this->isOsdShown) {
                this->showOSD(true);
                brls::Application::giveFocus(this->btnToggle);
                return true;
            }
            if (!this->isChildFocused()) {
                brls::Application::giveFocus(this->btnToggle);
                return true;
            }
            return false;
        },
        true, true);

    this->registerActions(
        "volumeDown", brls::BUTTON_NAV_DOWN, KeyBind::getVolumeDown(),
        [this](brls::View* view) -> bool {
            auto& state = brls::Application::getControllerState();
            if (state.buttons[brls::BUTTON_RT]) {
                this->requestVolume((int)MPVCore::instance().volume - 5, 400);
                return true;
            }
            // d-pad with the OSD hidden: wake it and focus the controls
            if (!this->isOsdShown) {
                this->showOSD(true);
                brls::Application::giveFocus(this->btnToggle);
                return true;
            }
            if (!this->isChildFocused()) {
                brls::Application::giveFocus(this->btnToggle);
                return true;
            }
            return false;
        },
        true, true);

    // Left/right with the OSD DOWN seek, which is what every other player on
    // this hardware does and what these two did here: nothing. Same steps as
    // LB/RB and the progress bar, so holding accumulates through getSeekRange's
    // tiers. Registered on the button only, not the key — the keyboard bindings
    // for seeking are already on LB/RB above, and the same key twice on one
    // view is one registration too many. With the OSD up and focus on the
    // control row these must stay navigation, so that case falls through.
    this->registerAction(
        "\uE08F", brls::BUTTON_NAV_LEFT,
        [this](brls::View* view) -> bool {
            if (this->isChildFocused()) return false;
            this->showOSD(true);
            this->seekingRange -= getSeekRange(this->seekingRange);
            this->requestSeeking(seekingRange);
            return true;
        },
        true, true);
    this->registerAction(
        "\uE08E", brls::BUTTON_NAV_RIGHT,
        [this](brls::View* view) -> bool {
            if (this->isChildFocused()) return false;
            this->showOSD(true);
            this->seekingRange += getSeekRange(this->seekingRange);
            this->requestSeeking(seekingRange);
            return true;
        },
        true, true);

    /// 音量按钮

    this->registerMpvEvent();

    osdSlider->getProgressSetEvent().subscribe([this](float progress) {
        brls::Logger::verbose("Set progress: {}", progress);
        this->showOSD(true);
        MPVCore::instance().seek(progress * 100, "absolute-percent");
    });

    osdSlider->getProgressEvent().subscribe([this](float progress) { this->showOSD(false); });

    /// 组件触摸事件
    /// 单击控制 OSD
    /// 双击控制播放与暂停
    /// 长按加速
    /// 滑动调整进度
    /// 左右侧滑动调整音量，在支持调节背光的设备上左侧滑动调节背光亮度，右侧调节音量
    this->addGestureRecognizer(new OsdGestureRecognizer([this](OsdGestureStatus status) {
        auto& mpv = MPVCore::instance();
        if (status.osdGestureType == OsdGestureType::TAP) {
            this->toggleOSD();
            return;
        }
        if (!MPVCore::TOUCH_GESTURE) return;

        switch (status.osdGestureType) {
        case OsdGestureType::DOUBLE_TAP_END:
            mpv.togglePlay();
            break;
        case OsdGestureType::LONG_PRESS_START: {
            float cur = MPVCore::VIDEO_SPEED == 100 ? 2.0 : MPVCore::VIDEO_SPEED * 0.01f;
            MPVCore::instance().setSpeed(cur);
            // 绘制临时加速标识
            this->speedHintLabel->setText(fmt::format(fmt::runtime("main/player/speed_up"_i18n), cur));
            this->speedHintBox->setVisibility(brls::Visibility::VISIBLE);
            break;
        }
        case OsdGestureType::LONG_PRESS_CANCEL:
        case OsdGestureType::LONG_PRESS_END:
            mpv.setSpeed(1.0f);
            this->speedHintBox->setVisibility(brls::Visibility::GONE);
            break;
        case OsdGestureType::HORIZONTAL_PAN_START:
            infoIcon->setImageFromSVGRes("icon/ico-seeking.svg");
            osdInfoBox->setVisibility(brls::Visibility::VISIBLE);
            break;
        case OsdGestureType::HORIZONTAL_PAN_UPDATE:
            this->requestSeeking(fmin(120.0f, mpv.duration) * status.deltaX);
            break;
        case OsdGestureType::HORIZONTAL_PAN_CANCEL:
            // 立即取消
            this->requestSeeking(0, VIDEO_SEEK_NODELAY);
            break;
        case OsdGestureType::HORIZONTAL_PAN_END:
            // 立即跳转
            this->requestSeeking(fmin(120.0f, mpv.duration) * status.deltaX, VIDEO_SEEK_NODELAY);
            break;
        case OsdGestureType::LEFT_VERTICAL_PAN_START:
            if (brls::Application::getPlatform()->canSetBacklightBrightness()) {
                this->brightnessInit = brls::Application::getPlatform()->getBacklightBrightness();
                infoIcon->setImageFromSVGRes("icon/ico-sun-fill.svg");
                osdInfoBox->setVisibility(brls::Visibility::VISIBLE);
                break;
            }
        case OsdGestureType::RIGHT_VERTICAL_PAN_START:
            this->volumeInit = mpv.volume;
            infoIcon->setImageFromSVGRes("icon/ico-volume.svg");
            osdInfoBox->setVisibility(brls::Visibility::VISIBLE);
            break;
        case OsdGestureType::LEFT_VERTICAL_PAN_UPDATE:
            if (brls::Application::getPlatform()->canSetBacklightBrightness()) {
                this->requestBrightness(this->brightnessInit + status.deltaY);
                break;
            }
        case OsdGestureType::RIGHT_VERTICAL_PAN_UPDATE:
            this->requestVolume(this->volumeInit + status.deltaY * 100);
            break;
        case OsdGestureType::LEFT_VERTICAL_PAN_CANCEL:
        case OsdGestureType::LEFT_VERTICAL_PAN_END:
            if (brls::Application::getPlatform()->canSetBacklightBrightness()) {
                osdInfoBox->setVisibility(brls::Visibility::GONE);
                break;
            }
        case OsdGestureType::RIGHT_VERTICAL_PAN_CANCEL:
        case OsdGestureType::RIGHT_VERTICAL_PAN_END:
            osdInfoBox->setVisibility(brls::Visibility::GONE);
            break;
        default:
            break;
        }
    }));

    /// 播放/暂停 按钮
    this->btnToggle->setOnClick([]() { MPVCore::instance().togglePlay(); });
    this->btnToggle->setIconPath(player_icon::PLAY);

    // Skip to the next episode — the reference's second control. Revealed by
    // setList() once there IS one; closing the file is what the old forward
    // arrow did, and it lives here now.
    this->btnNext->setIconPath(player_icon::SKIP_NEXT);
    this->btnNext->setOnClick([this]() { this->playIndexEvent.fire(this->playIndex + 1); });

    this->registerActions(
        "main/player/toggle"_i18n, brls::BUTTON_A, KeyBind::getVideoPause(), [this](brls::View* view) {
            // "Up next" is up: cross takes it, the way the reference's card
            // answers select. Circle below puts it away instead.
            if (this->nextCard && this->nextCard->shown()) {
                this->dismissNextEpisodeCard();
                this->playIndexEvent.fire(this->playIndex + 1);
                return true;
            }
            MPVCore::instance().togglePlay();
            if (MPVCore::OSD_ON_TOGGLE) {
                this->showOSD(true);
            }
            return true;
        });

    /// 视频详情信息
    this->profile = new VideoProfile();
    this->addView(this->profile);
    this->registerActions(
        "profile", brls::BUTTON_BACK, KeyBind::getVideoProfile(),
        [this](brls::View* view) { return this->toggleProfile(); }, true);
    // The three pickers carry their glyphs from the start; whether they are on
    // the row at all is decided by registerVideoSubtitle/Audio/Quality, which
    // only fire when there is something to pick.
    this->btnVideoSubtitle->setIconPath(player_icon::CLOSED_CAPTION);
    this->btnVideoAudio->setIconPath(player_icon::AUDIO);
    this->btnSources->setIconPath(player_icon::SOURCES);
    this->btnVideoQuality->setIconPath(player_icon::QUALITY);
    this->btnEpisode->setIconPath(player_icon::EPISODES);

    // Stream information, on the row rather than behind a "more" menu: it is
    // the only entry of the reference's overflow that has any meaning here.
    // What it opens is registered by the owner (registerStreamInfo), which is
    // the only thing that knows which addon served the stream.
    this->btnCast->setIconPath(player_icon::INFO);

    // NO stick-click shortcuts. L3 opened a playback-speed dropdown and R3 a
    // bitrate one (see registerVideoQuality); both were a surprise under the
    // thumb mid-film and neither is worth a dedicated button.

    // The two full-screen states, added LAST so they draw over the whole OSD.
    // Neither is focusable: the player's own actions keep working underneath,
    // which is what lets circle and cross mean something on the pause screen.
    this->loadingScreen = new LoadingScreen();
    this->addView(this->loadingScreen);
    this->pauseScreen = new PauseScreen();
    this->addView(this->pauseScreen);
    this->nextCard = new NextEpisodeCard();
    this->addView(this->nextCard);

    // Paint the clock and a 0:00 / 0:00 straight away rather than leaving three
    // blank lines until mpv reports its first duration.
    this->updateTime(0, 0);
}

VideoView::~VideoView() {
    brls::Logger::debug("trying delete VideoView...");
    this->unRegisterMpvEvent();
    disableDimming(false);

    MPVCore::instance().stop();
}

/// Callers hand over ONE joined string ("Show · S1E2 — Name"); the reference
/// splits that across three lines. PlayerView derives the split from the item
/// (setEpisodeLine/setSourceLine) and this is what is left for the top line, so
/// a caller-supplied string is kept only as the fallback for a caller that has
/// no item to derive from.
void VideoView::setTitie(const std::string& title) {
    if (this->titleLocked) return;
    this->titleLabel->setText(title);
}

void VideoView::setMainTitle(const std::string& text) {
    this->titleLocked = true;
    this->mainTitle   = text;
    this->titleLabel->setText(text);
}

void VideoView::setEpisodeLine(const std::string& text) {
    this->episodeLabel->setText(text);
    this->episodeLabel->setVisibility(text.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
}

void VideoView::setSourceLine(const std::string& text) {
    this->sourceName = text;
    this->applySourceLine();
}

/// The reference prints "via <stream>" only while PAUSED — it is there to tell
/// you what you are watching when you have stopped to look, not to sit over the
/// picture the whole time.
void VideoView::applySourceLine() {
    bool show = !this->sourceName.empty() && MPVCore::instance().isPaused();
    this->sourceLabel->setVisibility(show ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    if (show) this->sourceLabel->setText(fmt::format(fmt::runtime("main/player/via"_i18n), this->sourceName));
}

/// "0:11 / 24:06" on the right of the control row, and the wall clock with the
/// finish time top-right — both the reference's.
void VideoView::updateTime(double positionSec, double durationSec) {
    this->timeLabel->setText(fmt::format("{} / {}", misc::sec2Time(positionSec), misc::sec2Time(durationSec)));

    std::time_t now = std::time(nullptr);
    std::tm lt {};
    localtime_r(&now, &lt);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M", &lt);
    this->clockLabel->setText(buf);

    double remaining = durationSec - positionSec;
    if (durationSec <= 0 || remaining < 0) {
        this->endsLabel->setText("");
        return;
    }
    std::time_t end = now + (std::time_t)remaining;
    std::tm et {};
    localtime_r(&end, &et);
    std::strftime(buf, sizeof(buf), "%H:%M", &et);
    this->endsLabel->setText(fmt::format(fmt::runtime("main/player/ends_at"_i18n), buf));
}

void VideoView::setList(const std::vector<std::string>& values, int index) {
    // 选集
    this->playListSize = (int)values.size();
    // "Skip to next episode" appears only when there IS a next one, exactly as
    // the reference gates it on nextEpisode?.hasAired.
    auto event = [this](int index) { this->setPlayIndex(index); };

    this->playIndexEvent.subscribe(event);
    event(index);
}

void VideoView::setPlayIndex(int index) {
    this->playIndex = index;
    // New episode: the card owes it a fresh offer, and the arming has to see a
    // position away from the end again before it will make one.
    this->nextCardArmed = false;
    this->nextCardDismissed = false;
    if (this->nextCard) this->nextCard->hide();
    this->btnNext->setVisibility(
        index + 1 < this->playListSize ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
}

void VideoView::requestSeeking(int seek, int delay) {
    auto& mpv = MPVCore::instance();
    if (mpv.duration <= 0) {
        this->seekingRange = 0;
        return;
    }
    double progress = (mpv.playback_time + seek) / mpv.duration;

    if (progress < 0) {
        progress = 0;
        seek = (int64_t)mpv.playback_time * -1;
    } else if (progress > 1) {
        progress = 1;
        seek = mpv.duration;
    }

    showOSD(false);
    if (osdInfoBox->getVisibility() != brls::Visibility::VISIBLE) {
        osdInfoBox->setVisibility(brls::Visibility::VISIBLE);
        infoIcon->setImageFromSVGRes("icon/ico-seeking.svg");
    }
    infoLabel->setText(fmt::format("{:+d} s", seek));
    osdSlider->setProgress(progress);
    this->updateTime(mpv.duration * progress, mpv.duration);

    // 延迟触发跳转进度
    brls::cancelDelay(this->seekingIter);
    if (delay <= VIDEO_SEEK_NODELAY) {
        osdInfoBox->setVisibility(brls::Visibility::GONE);
        this->seekingRange = 0;
        if (seek == 0) return;
        MPVCore::instance().seek(seek, "relative");
    } else if (delay > 0) {
        ASYNC_RETAIN
        this->seekingIter = brls::delay(delay, [ASYNC_TOKEN, seek]() {
            ASYNC_RELEASE
            osdInfoBox->setVisibility(brls::Visibility::GONE);
            this->seekingRange = 0;
            if (seek == 0) return;
            MPVCore::instance().seek(seek, "relative");
        });
    }
}

void VideoView::requestVolume(int value, int delay) {
    if (value < 0) value = 0;
    if (value > 200) value = 200;
    MPVCore::instance().setInt("volume", value);
    infoLabel->setText(fmt::format("{:+d} %", value));

    if (delay == 0) return;
    if (this->volumeIter == 0) {
        osdInfoBox->setVisibility(brls::Visibility::VISIBLE);
        infoIcon->setImageFromSVGRes("icon/ico-volume.svg");
    } else {
        brls::cancelDelay(this->volumeIter);
    }
    ASYNC_RETAIN
    volumeIter = brls::delay(delay, [ASYNC_TOKEN]() {
        ASYNC_RELEASE
        osdInfoBox->setVisibility(brls::Visibility::GONE);
        this->volumeIter = 0;
    });
}

void VideoView::requestBrightness(float value) {
    if (value < 0) value = 0.0f;
    if (value > 1) value = 1.0f;
    brls::Application::getPlatform()->setBacklightBrightness(value);
    infoLabel->setText(fmt::format("{} %", (int)(value * 100)));
    infoIcon->setImageFromSVGRes("icon/ico-sun-fill.svg");
    osdInfoBox->setVisibility(brls::Visibility::VISIBLE);
}

void VideoView::draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx) {
    auto& mpv = MPVCore::instance();
    if (!mpv.isValid()) return;

    // draw video
    mpv.draw(this->getFrame(), this->getAlpha());

    // draw osd
    brls::Time current = brls::getCPUTimeUsec();
    if (current < this->osdLastShowTime) {
        if (!this->isOsdShown) {
            this->isOsdShown = true;
    // "Up next" rides above the controls when they are out, as the reference's
    // 122 dp bottom padding does.
    if (this->nextCard) this->nextCard->setOsdVisible(true);
        }

        osdTopBox->setVisibility(brls::Visibility::VISIBLE);
        osdBottomBox->setVisibility(brls::Visibility::VISIBLE);
        osdBottomBox->frame(ctx);
        osdTopBox->frame(ctx);

    } else if (this->isOsdShown) {
        this->isOsdShown = false;
        // 当焦点位于video组件内部重新赋予焦点，用来隐藏屏幕上的高亮框
        if (isChildFocused()) brls::Application::giveFocus(this);
        osdTopBox->setVisibility(brls::Visibility::INVISIBLE);
        osdBottomBox->setVisibility(brls::Visibility::INVISIBLE);
    }

    if (current > this->hintLastShowTime) {
        this->hintBox->setVisibility(brls::Visibility::GONE);
        this->hintLastShowTime = 0;
    }

    // hot key
    this->buttonProcessing();

    // draw speed hint
    if (speedHintBox->getVisibility() == brls::Visibility::VISIBLE) {
        speedHintBox->frame(ctx);
        brls::Rect frame = speedHintLabel->getFrame();

        // a1-3 周期 800，范围 800 * 0.3 / 2 = 120, 0 - 120 - 0
        int ta1 = ((current >> 10) % 800) * 0.3;
        float tx = frame.getMinX() - 50;
        float ty = frame.getMinY() + 4.5;

        for (int i = 0; i < 3; i++) {
            int offx = tx + i * 15;
            int ta2 = (ta1 + i * 40) % 240;
            if (ta2 > 120) ta2 = 240 - ta2;

            nvgBeginPath(vg);
            nvgMoveTo(vg, offx, ty);
            nvgLineTo(vg, offx, ty + 12);
            nvgLineTo(vg, offx + 12, ty + 6);
            nvgFillColor(vg, a(nvgRGBA(255, 255, 255, ta2 + 80)));
            nvgClosePath(vg);
            nvgFill(vg);
        }
    }

    // cache info
    osdCenterBox->frame(ctx);

    // center hint
    osdInfoBox->frame(ctx);

    // draw video profile
    if (profile->getVisibility() == brls::Visibility::VISIBLE) {
        if (current - this->profileLastShowTime > 2000000) {
            profile->update();
            this->profileLastShowTime = current;
        }
        profile->frame(ctx);
    }

    this->tickPauseScreen(current);

    // LAST, over everything above. draw() never chains to Box::draw — it
    // frames the children it wants, in the order it wants them — so a view
    // added to this box is invisible until it is named here.
    if (loadingScreen->getVisibility() == brls::Visibility::VISIBLE) loadingScreen->frame(ctx);
    if (pauseScreen->getVisibility() == brls::Visibility::VISIBLE) pauseScreen->frame(ctx);
    if (nextCard->getVisibility() == brls::Visibility::VISIBLE) nextCard->frame(ctx);
}

void VideoView::invalidate() { View::invalidate(); }

void VideoView::onChildFocusGained(View* directChild, View* focusedView) {
    Box::onChildFocusGained(directChild, focusedView);
    // 只有在全屏显示OSD时允许OSD组件获取焦点
    if (this->isOsdShown) {
        // 当弹幕按钮隐藏时不可获取焦点
        if (focusedView->getParent()->getVisibility() == brls::Visibility::GONE) {
            brls::Application::giveFocus(this);
        }
        lastFocusedView = focusedView;
        return;
    }
    brls::Application::giveFocus(this);
}

void VideoView::buttonProcessing() {
    // 获取按键数据
    auto& state = brls::Application::getControllerState();
    // 当OSD显示时上下左右切换选择按钮，持续显示OSD
    if (this->isOsdShown) {
        if (state.buttons[brls::BUTTON_NAV_RIGHT] || state.buttons[brls::BUTTON_NAV_LEFT] ||
            state.buttons[brls::BUTTON_NAV_UP] || state.buttons[brls::BUTTON_NAV_DOWN]) {
            if (this->osdState == OSDState::SHOWN) this->showOSD(true);
        }
    }
}

void VideoView::registerMpvEvent() {
    auto& mpv = MPVCore::instance();
    this->eventSubscribeID = mpv.getEvent()->subscribe([this](MpvEventEnum event) {
        auto& mpv = MPVCore::instance();
        // brls::Logger::info("mpv event => : {}", event);
        switch (event) {
        case MpvEventEnum::MPV_RESUME:
            this->cancelPauseScreen();
            // Back to timing out now the picture is moving again — including
            // an OSD that was pinned open by the pause it just ended.
            if (MPVCore::OSD_ON_TOGGLE || this->osdState == OSDState::ALWAYS_ON) {
                this->showOSD(true);
            }
            this->btnToggle->setIconPath(player_icon::PAUSE);
            this->applySourceLine();
            break;
        case MpvEventEnum::MPV_PAUSE:
            // PINNED, not on the 5 s timer: a paused player should keep saying
            // where it is paused. An OSD that is already up gets pinned even
            // when the "show the OSD on play/pause" setting is off — that
            // setting is about the bar APPEARING, not about it staying.
            if (MPVCore::OSD_ON_TOGGLE || this->isOsdShown) {
                this->showOSD(false);
            }
            // AFTER showOSD, which cancels it — arming first would arm and
            // disarm the timer in the same breath.
            this->schedulePauseScreen();
            hideLoading(false);
            this->btnToggle->setIconPath(player_icon::PLAY);
            this->applySourceLine();
            break;
        case MpvEventEnum::START_FILE:
            if (MPVCore::OSD_ON_TOGGLE) {
                this->showOSD(false);
            }
            break;
        case MpvEventEnum::LOADING_START:
            this->showLoading();
            break;
        case MpvEventEnum::LOADING_END:
            this->hideLoading();
            // Something is on the screen now, which is exactly what the
            // startup screen was covering for. It is also the gate on the
            // pause screen: nothing to describe before the first frame.
            this->firstFrameSeen = true;
            this->hideLoadingScreen();
            break;
        case MpvEventEnum::UPDATE_DURATION:
            if (this->seekingRange == 0) {
                this->updateTime(mpv.video_progress, mpv.duration);
                this->osdSlider->setProgress(mpv.playback_time / mpv.duration);
            }
            break;
        case MpvEventEnum::UPDATE_PROGRESS:
            if (this->seekingRange == 0) {
                this->updateTime(mpv.video_progress, mpv.duration);
                this->osdSlider->setProgress(mpv.playback_time / mpv.duration);
            }
            this->tickNextEpisodeCard(mpv.playback_time, mpv.duration);
            break;
        case MpvEventEnum::END_OF_FILE:
            // 播放结束
            disableDimming(false);
            this->btnToggle->setIconPath(player_icon::PLAY);
            this->playIndexEvent.fire(++this->playIndex);
            break;
        case MpvEventEnum::CACHE_SPEED_CHANGE:
            // 仅当加载圈已经开始转起的情况显示缓存
            if (this->osdCenterBox->getVisibility() != brls::Visibility::GONE) {
                if (this->centerLabel->getVisibility() != brls::Visibility::VISIBLE)
                    this->centerLabel->setVisibility(brls::Visibility::VISIBLE);
                this->centerLabel->setText(MPVCore::instance().getCacheSpeed());
            }
            break;
        case MpvEventEnum::VIDEO_MUTE:
        case MpvEventEnum::VIDEO_UNMUTE:
            // no volume control on the reference's row; the mute state shows in
            // the same toast every other volume change already uses
            break;
        case MpvEventEnum::MPV_FILE_ERROR: {
            // Let an owner (PlayerView) try to recover first (e.g. fall back to
            // direct play). If it reports the error handled, show nothing.
            if (this->errorAction && this->errorAction(this)) break;
            // Nothing is going to play: take the startup screen down so the
            // dialog lands on the video rather than on a full-screen poster
            // that is still promising the film is on its way.
            this->hideLoadingScreen();
            // Otherwise surface the concrete mpv reason so bug reports are
            // actionable ("Playback error (mpv -13: unrecognized file format)")
            // instead of an opaque "Playback error".
            std::string reason = MPVCore::instance().getError();
            std::string msg = "main/player/error"_i18n;
            if (!reason.empty()) msg += "\n(" + reason + ")";
            auto dialog = new brls::Dialog(msg);
            dialog->addButton("hints/back"_i18n, []() { VideoView::close(true); });
            dialog->open();
            break;
        }
        default:;
        }
    });
}

void VideoView::unRegisterMpvEvent() {
    auto& mpv = MPVCore::instance();
    mpv.getEvent()->unsubscribe(eventSubscribeID);
}

// Loading
void VideoView::showLoading() {
    this->centerLabel->setVisibility(brls::Visibility::INVISIBLE);
    this->osdCenterBox->setVisibility(brls::Visibility::VISIBLE);
    disableDimming(false);
}

void VideoView::hideLoading(bool dimming) {
    this->osdCenterBox->setVisibility(brls::Visibility::GONE);
    disableDimming(dimming);
}

bool VideoView::toggleProfile() {
    if (profile->getVisibility() == brls::Visibility::VISIBLE) {
        profile->setVisibility(brls::Visibility::INVISIBLE);
        return false;
    }
    profile->setVisibility(brls::Visibility::VISIBLE);
    profile->update();
    return true;
}

/// OSD
void VideoView::toggleOSD() {
    if (this->isOsdShown) {
        this->hideOSD();
    } else {
        this->showOSD(true);
    }
}

void VideoView::showOSD(bool autoHide) {
    // ANY interaction takes the pause screen down — the reference's
    // onUserInteraction does the same. Everything that wakes the bar (a seek,
    // the volume, a button, cross) comes through here, so this is the one
    // place that has to say it.
    this->cancelPauseScreen();
    // Marked shown HERE and not only in frame(). onChildFocusGained bounces
    // focus back to the video whenever the OSD is not shown, so a handler that
    // revealed the OSD and gave focus to a control in the same tick had that
    // focus taken straight back off it — the reason a d-pad press woke the bar
    // but left focus somewhere other than play/pause. frame() still owns the
    // hide, so this only ever runs ahead of it, never against it.
    this->isOsdShown = true;
    if (autoHide) {
        this->osdLastShowTime = brls::getCPUTimeUsec() + VideoView::OSD_SHOW_TIME;
        this->osdState = OSDState::SHOWN;
    } else {
        this->osdLastShowTime = std::numeric_limits<std::time_t>::max();
        this->osdState = OSDState::ALWAYS_ON;
    }
}

/// ---- the two full-screen states ------------------------------------------

void VideoView::showLoadingScreen(
    const std::string& backdropUrl, const std::string& logoUrl, const std::string& title) {
    if (!MPVCore::LOADING_SCREEN) return;
    this->firstFrameSeen = false;
    this->loadingScreen->setArtwork(backdropUrl, logoUrl, title);
    this->loadingScreen->setStage("");
    this->loadingScreen->show();
    this->hideOSD();
}

void VideoView::setLoadingStage(const std::string& text) {
    if (!this->loadingScreen->shown()) return;
    // The step text is the part the reference makes optional; the screen
    // itself stays either way.
    this->loadingScreen->setStage(MPVCore::LOADING_STAGES ? text : "");
}

void VideoView::hideLoadingScreen() { this->loadingScreen->hide(); }

void VideoView::setPauseItem(const plex::Item& item, const std::string& showTitle, const std::string& logoUrl) {
    this->pauseScreen->setItem(item, showTitle, logoUrl);
}

bool VideoView::pauseScreenShown() { return this->pauseScreen && this->pauseScreen->shown(); }

/// "Up next", on NuvioTV's rules (PlayerNextEpisodeRules).
///
/// The card comes up once the position crosses the threshold — the reference's
/// PERCENTAGE mode at its own 99% default, which it clamps to 97..100 — and
/// only after the stream has first reported a position clearly AWAY from the
/// end (its isAwayFromEnd arming). Without that arming a file whose first
/// position report lands at the duration (a resume at the very end, a seek
/// that overshot) puts the card up the instant it opens.
///
/// The reference also reads outro markers from an intro database and fires at
/// the outro start when the credits run to the end of the file. That database
/// is behind a build-time key we do not have, so this is its own no-marker
/// fallback path, which is the same code either way.
void VideoView::tickNextEpisodeCard(double positionSec, double durationSec) {
    if (!this->nextCard) return;
    if (!MPVCore::NEXT_EPISODE_CARD) return;

    // Nothing to offer: a film, the last episode, or a list we were never given.
    if (this->playIndex < 0 || this->playIndex + 1 >= this->playListSize) return;
    if (durationSec <= 0 || this->nextCardDismissed) return;

    constexpr double kThresholdPercent = 99.0;  // the reference's default
    constexpr double kNearEndSec = 0.5;         // its NEAR_END_MS

    bool past = positionSec / durationSec >= kThresholdPercent / 100.0;

    if (!this->nextCardArmed) {
        if (positionSec < durationSec - kNearEndSec && !past) this->nextCardArmed = true;
        return;
    }
    // Seeking back out of the window takes the card away again.
    if (!past) {
        if (this->nextCard->shown()) this->nextCard->hide();
        return;
    }
    if (this->nextCard->shown()) return;
    // Anything full-screen owns the screen while it is up.
    if (this->pauseScreenShown() || this->loadingScreen->shown()) return;

    this->nextCard->setOsdVisible(this->isOsdShown);
    this->nextCard->show();
}

void VideoView::setNextEpisode(const plex::Item& ep) { this->nextCard->setEpisode(ep); }

void VideoView::dismissNextEpisodeCard() {
    this->nextCardDismissed = true;
    if (this->nextCard) this->nextCard->hide();
}

void VideoView::schedulePauseScreen() {
    this->pausedSince = 0;
    if (!MPVCore::PAUSE_SCREEN || !this->firstFrameSeen) return;
    // WHEN THE PAUSE STARTED, not a deadline — tickPauseScreen re-derives how
    // long it has lasted on every frame, and re-checks that it is still
    // running. A stored deadline only has to be wrong once (a stale one left
    // armed, a clock read somewhere it should not have been) to put the screen
    // up the instant playback pauses, which is what kept happening.
    this->pausedSince = brls::getCPUTimeUsec();
    brls::Logger::debug("VideoView: pause screen armed, {} s", MPVCore::PAUSE_SCREEN_DELAY);
}

void VideoView::cancelPauseScreen() {
    this->pausedSince = 0;
    if (this->pauseScreen) this->pauseScreen->hide();
}

/// Called once a frame from draw(). Shows the screen when the pause has lasted
/// long enough and nothing has come along that should keep it away.
void VideoView::tickPauseScreen(brls::Time current) {
    if (!this->pausedSince || this->pauseScreen->shown()) return;
    brls::Time waited = current - this->pausedSince;
    if (waited < (brls::Time)MPVCore::PAUSE_SCREEN_DELAY * 1000000) return;

    // Still paused? A resume cancels this outright, but re-reading mpv costs
    // nothing and covers a pause that ended without the event reaching us.
    if (!MPVCore::instance().isPaused() || this->loadingScreen->shown()) {
        this->pausedSince = 0;
        return;
    }
    // Not over a panel. A panel is a pushed activity, so the focus is no
    // longer anywhere under this view — which is the cheapest way to ask.
    brls::View* focus = brls::Application::getCurrentFocus();
    for (brls::View* v = focus; v; v = v->getParent()) {
        if (v != this) continue;
        brls::Logger::debug("VideoView: pause screen after {} ms", waited / 1000);
        this->hideOSD();
        // Same wall clock the OSD shows, kept in step by updateTime.
        this->pauseScreen->setClock(this->clockLabel->getFullText());
        this->pauseScreen->show();
        return;
    }
}

void VideoView::hideOSD() {
    if (this->nextCard) this->nextCard->setOsdVisible(false);
    this->osdLastShowTime = 0;
    this->osdState = OSDState::HIDDEN;
}

void VideoView::showHint(const std::string& value) {
    brls::Logger::debug("Video hint: {}", value);
    this->hintLabel->setText(value);
    this->hintBox->setVisibility(brls::Visibility::VISIBLE);
    this->hintLastShowTime = brls::getCPUTimeUsec() + VideoView::OSD_SHOW_TIME;
    this->showOSD();
}

void VideoView::setTvMode(bool state) {
    // One row now, with the bar directly above it: every button goes UP to the
    // slider and the slider comes back DOWN to play/pause, which is how the
    // reference wires its controls (upFocusRequester = progressBar on each).
    for (PlayerButton* b : {btnToggle.getView(), btnNext.getView(), btnVideoSubtitle.getView(),
             btnVideoAudio.getView(), btnSources.getView(), btnVideoQuality.getView(), btnEpisode.getView(),
             btnCast.getView()})
        if (state) b->setCustomNavigationRoute(brls::FocusDirection::UP, (brls::View*)osdSlider);
    osdSlider->setFocusable(state);
}

bool VideoView::toggleVolume(brls::View* view) {
    // 一直显示 OSD
    this->showOSD(false);
    auto theme = brls::Application::getTheme();
    auto container = new brls::Box();
    container->setHideClickAnimation(true);
    container->registerAction("hints/back"_i18n, brls::BUTTON_B, [this](brls::View* view) {
        // 几秒后自动关闭 OSD
        this->showOSD(true);
        view->dismiss();
        // 保存结果
        return true;
    });
    container->addGestureRecognizer(new brls::TapGestureRecognizer(container, [this, container]() {
        // 几秒后自动关闭 OSD
        this->showOSD(true);
        container->dismiss();
        // 保存结果
        return true;
    }));
    // 滑动条背景
    auto sliderBox = new brls::Box();
    sliderBox->setAlignItems(brls::AlignItems::CENTER);
    sliderBox->setHeight(40);
    sliderBox->setCornerRadius(4);
    sliderBox->setBackgroundColor(theme.getColor("color/grey_1"));
    float sliderX = view->getX() - 120;
    if (sliderX < 0) sliderX = 20;
    if (sliderX > brls::Application::ORIGINAL_WINDOW_WIDTH - 332)
        sliderX = brls::Application::ORIGINAL_WINDOW_WIDTH - 332;
    sliderBox->setTranslationX(sliderX);
    sliderBox->setTranslationY(view->getY() - 70);
    // 滑动条
    auto slider = new brls::Slider();
    slider->setMargins(8, 16, 8, 16);
    slider->setWidth(300);
    slider->setHeight(20);
    slider->setProgress(MPVCore::instance().getInt("volume") / 200.0f);
    slider->getProgressEvent()->subscribe([](float progress) { MPVCore::instance().setInt("volume", progress * 200); });
    sliderBox->addView(slider);
    container->addView(sliderBox);
    auto frame = new brls::AppletFrame(container);
    frame->setInFadeAnimation(true);
    frame->setHeaderVisibility(brls::Visibility::GONE);
    frame->setFooterVisibility(brls::Visibility::GONE);
    frame->setBackgroundColor(theme.getColor("brls/backdrop"));
    brls::Application::pushActivity(new brls::Activity(frame));
    return true;
}

bool VideoView::close(bool quit) {
    if (brls::Application::getActivitiesStack().size() > 1) {
        return brls::Application::popActivity(brls::TransitionAnimation::NONE);
    }

    if (quit) {
        brls::Application::quit();
        return true;
    }

    auto dialog = new brls::Dialog("hints/exit_hint"_i18n);
    dialog->addButton("hints/cancel"_i18n, []() {});
    dialog->addButton("hints/ok"_i18n, []() { brls::Application::quit(); });
    dialog->open();
    return false;
}

void VideoView::disableDimming(bool disable) {
    brls::Application::getPlatform()->disableScreenDimming(disable, "Playing video", AppVersion::getPackageName());
    brls::Application::setAutomaticDeactivation(!disable);
}

void VideoView::setClipPoint(const std::vector<float>& clips) {
    if (clips.empty()) {
        this->osdSlider->clearClipPoint();
        return;
    }
    if (MPVCore::CLIP_POINT) {
        this->osdSlider->setClipPoint(clips);
    }
}

void VideoView::playNext(int offset) { this->playIndexEvent.fire(this->playIndex + offset); }

void VideoView::hideVideoProgressSlider() { this->osdSlider->setVisibility(brls::Visibility::GONE); }

// Each of the three pickers is GONE until someone registers it, and comes back
// when they do — the reference gates the same three on whether there is
// anything to pick (hasSubtitleControl / hasAudioControl / the sources panel).
void VideoView::hideVideoQuality() { this->btnVideoQuality->setVisibility(brls::Visibility::GONE); }

/// Opening a panel dismisses the transport controls first. The reference does
/// the same, and without it a bottom-anchored overlay lands on top of our
/// control row rather than on the video.
static brls::ActionListener dismissing(VideoView* view, brls::ActionListener action) {
    return [view, action](brls::View* v) {
        view->hideOSD();
        return action(v);
    };
}

void VideoView::registerVideoQuality(brls::ActionListener action) {
    action = dismissing(this, action);
    this->btnVideoQuality->registerClickAction(action);
    this->btnVideoQuality->setVisibility(brls::Visibility::VISIBLE);
}

void VideoView::registerVideoSubtitle(brls::ActionListener action) {
    action = dismissing(this, action);
    this->btnVideoSubtitle->registerClickAction(action);
    this->btnVideoSubtitle->setVisibility(brls::Visibility::VISIBLE);
}

void VideoView::registerVideoAudio(brls::ActionListener action) {
    action = dismissing(this, action);
    this->btnVideoAudio->registerClickAction(action);
    this->btnVideoAudio->setVisibility(brls::Visibility::VISIBLE);
}

void VideoView::registerEpisodes(brls::ActionListener action) {
    action = dismissing(this, action);
    this->btnEpisode->registerClickAction(action);
    this->btnEpisode->setVisibility(brls::Visibility::VISIBLE);
}

void VideoView::registerSources(brls::ActionListener action) {
    action = dismissing(this, action);
    this->btnSources->registerClickAction(action);
    this->btnSources->setVisibility(brls::Visibility::VISIBLE);
}

void VideoView::setSourcesVisible(bool visible) {
    this->btnSources->setVisibility(visible ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
}

void VideoView::setVideoQualityVisible(bool visible) {
    this->btnVideoQuality->setVisibility(visible ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
}

void VideoView::registerStreamInfo(brls::ActionListener action) {
    action = dismissing(this, action);
    this->btnCast->registerClickAction(action);
    this->btnCast->setVisibility(brls::Visibility::VISIBLE);
}

void VideoView::registerError(brls::ActionListener action) { this->errorAction = action; }

void VideoView::registerActions(const std::string& hintText, const brls::ControllerButton button,
    const brls::BrlsKeyCombination key, const brls::ActionListener& actionListener, bool hidden, bool allowRepeating) {
    this->registerAction(hintText, button, actionListener, hidden, allowRepeating);
    this->registerAction(key, actionListener, allowRepeating);
}