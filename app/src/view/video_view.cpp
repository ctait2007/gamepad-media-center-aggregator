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

#include <algorithm>

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
            // Circle on a screen that is up is "put that away", not "leave".
            // Checked before everything else, since they are up over all of it.
            if (this->nextCard && this->nextCard->shown()) {
                // While the card is ASKING, circle is one of the two answers —
                // onDismissStillWatchingPrompt, which exits exactly as the
                // countdown running out does.
                if (this->nextCard->isStillWatching())
                    this->leaveStillWatching(false);
                else
                    this->dismissNextEpisodeCard();
                return true;
            }
            if (this->skipButton && this->skipButton->shown()) {
                this->skipDismissed = true;
                this->hideSkipButton();
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
            // "Up next" is up: up puts focus on it, the reference's
            // progressBarUpFocusRequester. Already on it, up does nothing —
            // there is nothing above the card, as there is nothing above its
            // Card — which is also why this comes before waking the bar.
            if (this->nextCard && this->nextCard->shown()) {
                if (!this->nextCardHasFocus())
                    brls::Application::giveFocus(this->nextCard->getDefaultFocus());
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
            // NO "up next" case here. The card answers select itself, as the
            // reference's Card does, and only while it holds focus — hijacking
            // cross globally instead left no way to pause over the last of an
            // episode and no way to see what cross was about to do.
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
    this->skipButton = new SkipButton();
    this->addView(this->skipButton);
    this->skipButton->onSkip([this]() { this->takeSkipInterval(); });
    // Select ON THE CARD plays the next episode: registered on the card, so it
    // only ever fires while the card holds focus.
    this->nextCard->onPlay([this]() {
        if (this->nextCard->isStillWatching()) {
            this->leaveStillWatching(true);
            return;
        }
        this->dismissNextEpisodeCard();
        this->playIndexEvent.fire(this->playIndex + 1);
    });
    // "Exit" only exists while the card is asking, and means the same as
    // letting its minute run out.
    this->nextCard->onExit([this]() { this->leaveStillWatching(false); });
    // Down off the card is the volumeDown handler's own "d-pad with the OSD
    // hidden" case, and up back onto it is the volumeUp one's — both below,
    // where the player already decides what the d-pad means.

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

    // The "ends at" line below shares `now` and `buf`, so they stay out here;
    // only the wall clock itself answers to the setting.
    std::time_t now = std::time(nullptr);
    std::tm lt {};
    localtime_r(&now, &lt);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M", &lt);
    this->clockLabel->setText(buf);
    this->clockLabel->setVisibility(MPVCore::OSD_CLOCK ? brls::Visibility::VISIBLE : brls::Visibility::GONE);

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
    // nextConsecutiveAutoPlayCount: one more when this view advanced by itself,
    // zero the moment the viewer picks anything. The still-watching prompt only
    // ever asks about a run nobody asked for.
    this->consecutiveAutoPlay = this->advancingAutomatically ? this->consecutiveAutoPlay + 1 : 0;
    this->advancingAutomatically = false;
    this->stillWatchingUntil = 0;
    this->stillWatchingShown = -1;
    if (this->nextCard) this->nextCard->setStillWatching(false);

    this->playIndex = index;
    // New episode: the card owes it a fresh offer, and the arming has to see a
    // position away from the end again before it will make one.
    this->nextCardArmed = false;
    this->nextCardDismissed = false;
    if (this->nextCard) this->nextCard->hide();
    // The markers belong to the file that was playing; PlayerView hands over
    // the new ones when its lookup lands.
    this->skipIntervals.clear();
    this->activeSkipIndex = -1;
    this->skipDismissed = false;
    this->autoSkipped.clear();
    if (this->skipButton) this->skipButton->hide();
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
        if (!this->isOsdShown) this->isOsdShown = true;

        osdTopBox->setVisibility(brls::Visibility::VISIBLE);
        osdBottomBox->setVisibility(brls::Visibility::VISIBLE);
        osdBottomBox->frame(ctx);
        osdTopBox->frame(ctx);

    } else if (this->isOsdShown) {
        // The bar timing out. Everything the deliberate hideOSD() does, since
        // the card cannot tell the two apart and neither can the viewer.
        this->isOsdShown = false;
        if (this->nextCard) this->nextCard->setOsdVisible(false);
        if (this->skipButton) this->skipButton->setOsdVisible(false);
        // 当焦点位于video组件内部重新赋予焦点，用来隐藏屏幕上的高亮框
        if (isChildFocused()) {
            // "Up next" first: the reference suppresses the skip button's own
            // focus request whenever the post-play card is up (its suppressFocus).
            if (this->nextCard && this->nextCard->shown())
                brls::Application::giveFocus(this->nextCard);
            else if (this->skipButton && this->skipButton->shown())
                brls::Application::giveFocus(this->skipButton);
            else
                brls::Application::giveFocus(this);
        }
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
    // Playback is stopped while the still-watching prompt is up, so its
    // countdown cannot be driven off mpv's progress — it runs on the frame
    // clock, like the skip button's.
    this->tickStillWatching();
    if (nextCard->getVisibility() == brls::Visibility::VISIBLE) nextCard->frame(ctx);
    // The countdown is redrawn EVERY frame, not on mpv's once-a-second progress
    // tick -- off that it advanced in ten visible steps instead of sweeping.
    this->updateSkipCountdown();
    if (skipButton->getVisibility() == brls::Visibility::VISIBLE) skipButton->frame(ctx);
}

void VideoView::invalidate() { View::invalidate(); }

void VideoView::onChildFocusGained(View* directChild, View* focusedView) {
    Box::onChildFocusGained(directChild, focusedView);
    // "Up next" is the one child allowed focus with the OSD down — it IS the
    // control at that point, as the reference's Card is. Without this the
    // bounce below takes the focus straight back off it and the card lights up
    // for a frame and then goes dead.
    if (directChild == this->nextCard || directChild == this->skipButton) return;
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
            this->tickSkipButton(mpv.playback_time, mpv.duration);
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
    // The still-watching prompt owns the screen while it is asking, as the
    // reference's own showControls = false says. It stops playback itself, and
    // the pause that follows would otherwise bring the bar up over it.
    if (this->stillWatchingUntil > 0) return;
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
    // "Up next" rides above the controls when they are out, as the reference's
    // 122 dp bottom padding does. Set HERE rather than in draw(): showOSD marks
    // the bar shown itself, so draw's "it just came up" branch never runs.
    if (this->nextCard) this->nextCard->setOsdVisible(true);
    if (this->skipButton) this->skipButton->setOsdVisible(true);
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
/// The reference's rule has two halves. When it has outro markers for the
/// episode it fires at the outro, and when it has none it falls back to a
/// threshold the viewer sets — a percentage of the episode, or a number of
/// minutes before the end. Only the second half is reachable here, and it is
/// reachable in the reference too: the markers come from an intro database
/// whose endpoint is a build-time key (INTRODB_API_URL, read from a private
/// localProperties and absent from the example one), and its own repository
/// gates every lookup on that key being set. A NuvioTV built without it runs
/// this same path, which is why its settings screen calls the threshold
/// "Fallback when no outro timestamp exists" and still ships it as the
/// default-on behaviour.
///
/// So: shouldShowNextEpisodeCard's no-marker branch, at its own clamps, off
/// its own three settings.

namespace {
/// PlayerNextEpisodeRules.NEAR_END_MS and END_OF_VIDEO_EPSILON_MS.
constexpr double kNextNearEndSec = 0.5;
constexpr double kNextEndEpsilonSec = 1.0;

/// OUTRO_SEGMENT_TYPES. introdb.cpp normalises v3's "credits" to "outro", so
/// the reference's own set is the whole membership test.
bool isOutro(const std::string& type) { return type == "outro" || type == "ed" || type == "mixed-ed"; }

/// A span's end, with "runs to the end of the file" closed against the length
/// mpv is reporting. theintrodb.org records most credits that way and nothing
/// before the file is open knows the number — a Stremio episode carries no
/// duration in its metadata at all.
double skipEnd(const introdb::SkipInterval& iv, double durationSec) {
    return iv.endTime < 0 ? durationSec : iv.endTime;
}

/// The threshold the viewer set, as seconds from the end.
double userThresholdSec(double durationSec) {
    if (MPVCore::NEXT_EPISODE_MODE == 1) return std::clamp(MPVCore::NEXT_EPISODE_MINUTES / 2.0, 0.0, 3.5) * 60.0;
    double percent = std::clamp(MPVCore::NEXT_EPISODE_PERCENT / 2.0, 97.0, 100.0);
    return (1.0 - percent / 100.0) * durationSec;
}

/// Whether the threshold alone says the card is due.
bool thresholdMet(double positionSec, double durationSec) {
    if (MPVCore::NEXT_EPISODE_MODE == 1) {
        return durationSec - positionSec <= std::clamp(MPVCore::NEXT_EPISODE_MINUTES / 2.0, 0.0, 3.5) * 60.0;
    }
    double percent = std::clamp(MPVCore::NEXT_EPISODE_PERCENT / 2.0, 97.0, 100.0);
    return positionSec / durationSec >= percent / 100.0;
}

/// shouldShowNextEpisodeCard, in full.
///
/// With outro markers it fires at the outro; without them it falls back to the
/// threshold. The two-step inside the outro branch is the reference's own and
/// is the point of the whole thing: when the credits end WELL BEFORE the file
/// does (a stinger, a "next time on"), the gap after them is wider than the
/// threshold and the card waits for the threshold as usual; when they run to
/// the end of the file — which is what theintrodb.org records for most
/// episodes, as an open-ended span — the gap is nothing and the card comes up
/// the moment the credits START.
bool nextEpisodeDue(
    double positionSec, double durationSec, const std::vector<introdb::SkipInterval>& intervals) {
    if (durationSec <= 0) return false;
    // "A duration below the current position is not a valid end-of-video
    // signal" — mpv reports one every time a file is opened, before it knows
    // how long the new one is.
    if (positionSec > durationSec + kNextEndEpsilonSec) return false;

    double latestOutroEnd = -1, earliestOutroStart = 0;
    for (const auto& iv : intervals) {
        if (!isOutro(iv.type)) continue;
        if (latestOutroEnd < 0 || iv.startTime < earliestOutroStart) earliestOutroStart = iv.startTime;
        latestOutroEnd = std::max(latestOutroEnd, skipEnd(iv, durationSec));
    }

    if (latestOutroEnd >= 0) {
        double postOutroGap = durationSec - latestOutroEnd;
        if (postOutroGap > userThresholdSec(durationSec)) return thresholdMet(positionSec, durationSec);
        return positionSec >= earliestOutroStart;
    }
    return thresholdMet(positionSec, durationSec);
}

/// isAwayFromEnd: a reading clearly before the end AND outside the window.
bool nextEpisodeAwayFromEnd(
    double positionSec, double durationSec, const std::vector<introdb::SkipInterval>& intervals) {
    return durationSec > 0 && positionSec < durationSec - kNextNearEndSec &&
           !nextEpisodeDue(positionSec, durationSec, intervals);
}
}  // namespace

void VideoView::tickNextEpisodeCard(double positionSec, double durationSec) {
    if (!this->nextCard) return;
    if (!MPVCore::NEXT_EPISODE_CARD) return;

    // Nothing to offer: a film, the last episode, or a list we were never given.
    if (this->playIndex < 0 || this->playIndex + 1 >= this->playListSize) return;
    if (this->nextCardDismissed) return;

    // Arming, the reference's isAwayFromEnd: the card cannot come up until the
    // stream has first reported a position clearly outside the window. Without
    // it a file whose first report lands at the duration — a resume at the very
    // end, an open before mpv knows the length — puts the card up as it opens.
    if (!this->nextCardArmed) {
        if (nextEpisodeAwayFromEnd(positionSec, durationSec, this->skipIntervals)) {
            this->nextCardArmed = true;
            brls::Logger::debug("VideoView: up next armed at {:.0f}/{:.0f}s", positionSec, durationSec);
        }
        return;
    }

    if (!nextEpisodeDue(positionSec, durationSec, this->skipIntervals)) {
        // Seeking back out of the window takes the card away again — and hands
        // focus back, since it was holding it.
        this->hideNextEpisodeCard();
        return;
    }
    if (this->nextCard->shown()) return;
    // Anything full-screen owns the screen while it is up.
    if (this->pauseScreenShown() || this->loadingScreen->shown()) return;

    bool byOutro = false;
    for (const auto& iv : this->skipIntervals) byOutro = byOutro || isOutro(iv.type);
    brls::Logger::debug("VideoView: up next at {:.0f}/{:.0f}s ({})", positionSec, durationSec,
        byOutro ? std::string("outro marker")
        : MPVCore::NEXT_EPISODE_MODE == 1
            ? fmt::format("{:.1f} min before end", MPVCore::NEXT_EPISODE_MINUTES / 2.0)
            : fmt::format("{:.1f}%", MPVCore::NEXT_EPISODE_PERCENT / 2.0));

    // shouldEnterStillWatchingPrompt: only with auto-play on, and only once a
    // run of episodes nobody asked for is long enough. Otherwise the card comes
    // up as it always has — and, with auto-play on, the next episode starts
    // behind it rather than waiting to be told.
    if (MPVCore::STILL_WATCHING && MPVCore::NEXT_EPISODE_AUTOPLAY &&
        this->consecutiveAutoPlay >= MPVCore::STILL_WATCHING_THRESHOLD) {
        this->enterStillWatching();
        return;
    }

    this->nextCard->setStillWatching(false);
    this->nextCard->setOsdVisible(this->isOsdShown);
    this->nextCard->show();
    // The reference's onPlaced: the card takes focus as it appears, but only
    // with the controls down. With them up it waits to be navigated to, so
    // that the card arriving cannot pull focus off a control mid-press.
    if (!this->isOsdShown) brls::Application::giveFocus(this->nextCard);

    if (MPVCore::NEXT_EPISODE_AUTOPLAY) {
        brls::Logger::info("VideoView: auto-play, episode {} of {}", this->playIndex + 2, this->playListSize);
        this->nextCardDismissed = true;
        this->advancingAutomatically = true;
        this->playIndexEvent.fire(this->playIndex + 1);
    }
}

/// PostPlayMode.StillWatching. The card keeps its place and its artwork and
/// asks instead of offering; playback stops behind it, as the reference's
/// pauseForStillWatchingPrompt does, and after its minute with no answer the
/// player closes.
void VideoView::enterStillWatching() {
    if (this->stillWatchingUntil > 0) return;
    constexpr int kCountdownSec = 60;  // STILL_WATCHING_COUNTDOWN_SECONDS
    brls::Logger::info("VideoView: still watching? after {} auto-played episode(s)", this->consecutiveAutoPlay);

    auto& mpv = MPVCore::instance();
    if (!mpv.isPaused()) mpv.togglePlay();
    this->hideOSD();

    this->stillWatchingUntil = brls::getCPUTimeUsec() + (brls::Time)kCountdownSec * 1000000;
    this->stillWatchingShown = -1;
    this->nextCard->setStillWatching(true);
    this->nextCard->setCountdown(kCountdownSec);
    this->nextCard->setOsdVisible(false);
    this->nextCard->show();
    brls::Application::giveFocus(this->nextCard->getDefaultFocus());
}

void VideoView::tickStillWatching() {
    if (this->stillWatchingUntil == 0) return;
    brls::Time now = brls::getCPUTimeUsec();
    if (now >= this->stillWatchingUntil) {
        this->leaveStillWatching(false);
        return;
    }
    // Repainted only when the whole second changes — the label is rebuilt from
    // a format string and this runs on every frame.
    int remaining = (int)((this->stillWatchingUntil - now) / 1000000);
    if (remaining == this->stillWatchingShown) return;
    this->stillWatchingShown = remaining;
    this->nextCard->setCountdown(remaining);
}

void VideoView::leaveStillWatching(bool play) {
    if (this->stillWatchingUntil == 0) return;
    this->stillWatchingUntil = 0;
    this->stillWatchingShown = -1;
    // Either answer ends the run: onStillWatchingContinue and exitFromStillWatching
    // both zero the count there.
    this->consecutiveAutoPlay = 0;
    this->nextCard->setStillWatching(false);
    this->dismissNextEpisodeCard();
    if (play) {
        this->playIndexEvent.fire(this->playIndex + 1);
        return;
    }
    brls::Logger::info("VideoView: still watching? answered no (or not at all) — closing the player");
    VideoView::close();
}

void VideoView::setNextEpisode(const plex::Item& ep) { this->nextCard->setEpisode(ep); }

bool VideoView::nextCardShown() { return this->nextCard && this->nextCard->shown(); }

/// ---- the skip button ------------------------------------------------------
///
/// SkipIntroButton.kt's rules: it appears while the position is inside a span,
/// it takes itself away again after 10 seconds, circle puts it away for that
/// span, and the controls coming up bring it back (and pause its countdown).

void VideoView::setSkipIntervals(std::vector<introdb::SkipInterval> intervals) {
    for (const auto& iv : intervals)
        brls::Logger::debug("VideoView: marker {} {:.0f}s..{}", iv.type, iv.startTime,
            iv.endTime < 0 ? std::string("end") : fmt::format("{:.0f}s", iv.endTime));
    this->skipIntervals = std::move(intervals);
    this->activeSkipIndex = -1;
    this->skipDismissed = false;
    this->autoSkipped.clear();
    this->hideSkipButton();
}

bool VideoView::skipButtonHasFocus() {
    if (!this->skipButton) return false;
    for (brls::View* v = brls::Application::getCurrentFocus(); v; v = v->getParent())
        if (v == this->skipButton) return true;
    return false;
}

void VideoView::hideSkipButton() {
    if (!this->skipButton || !this->skipButton->shown()) return;
    bool hadFocus = this->skipButtonHasFocus();
    this->skipButton->hide();
    if (hadFocus) brls::Application::giveFocus(this);
}

/// Take the offer: seek to the end of the span, as the reference's skipInterval
/// does, and put the button away without offering it again.
void VideoView::takeSkipInterval() {
    if (this->activeSkipIndex < 0 || this->activeSkipIndex >= (int)this->skipIntervals.size()) return;
    const auto& iv = this->skipIntervals[this->activeSkipIndex];
    double end = skipEnd(iv, MPVCore::instance().duration);
    brls::Logger::debug("VideoView: skip {} -> {:.0f}s", iv.type, end);
    this->autoSkipped.insert(this->activeSkipIndex);
    this->skipDismissed = true;
    this->hideSkipButton();
    MPVCore::instance().seek((int64_t)end, "absolute");
}

void VideoView::tickSkipButton(double positionSec, double durationSec) {
    if (!this->skipButton) return;
    // The markers still drive "up next" with the button switched off — the
    // reference keeps skipIntroEnabled separate from having the data for
    // exactly that reason.
    if (!MPVCore::SKIP_INTRO_ENABLED) {
        this->hideSkipButton();
        return;
    }
    if (this->skipIntervals.empty()) {
        this->hideSkipButton();
        return;
    }

    // "Up next" already owns the end of an episode that HAS a next one: it
    // comes up on the same outro marker, offers the same thing and takes the
    // focus, so a Skip Ending beside it is a second button for one decision.
    // With nothing to go on to — the last episode, a film — the outro is worth
    // skipping on its own and the button is the only thing offering it.
    bool nextEpisodeCovers = MPVCore::NEXT_EPISODE_CARD && this->playIndex >= 0 &&
                             this->playIndex + 1 < this->playListSize;

    // findActiveSkipInterval: the span the position is inside, if any.
    int active = -1;
    for (size_t i = 0; i < this->skipIntervals.size(); i++) {
        const auto& iv = this->skipIntervals[i];
        if (positionSec < iv.startTime || positionSec >= skipEnd(iv, durationSec)) continue;
        if (nextEpisodeCovers && isOutro(iv.type)) continue;
        active = (int)i;
        break;
    }

    if (active != this->activeSkipIndex) {
        // A new span resets the dismissal and the countdown, as the reference's
        // LaunchedEffect(interval.startTime, interval.type) does.
        this->activeSkipIndex = active;
        this->skipDismissed = false;
        this->skipShownAt = brls::getCPUTimeUsec();
        this->hideSkipButton();
        if (active >= 0) this->skipButton->setSegmentType(this->skipIntervals[active].type);
    }
    if (active < 0) return;

    // Skip it outright when the viewer asked for that — autoSkipSegmentTypes,
    // as one switch rather than a set, and once per span so that seeking back
    // into one does not fight them.
    const std::string& activeType = this->skipIntervals[active].type;
    bool autoSkip = (activeType == "intro" && MPVCore::AUTO_SKIP_INTRO) ||
                    (activeType == "recap" && MPVCore::AUTO_SKIP_RECAP) ||
                    (isOutro(activeType) && MPVCore::AUTO_SKIP_OUTRO);
    if (autoSkip && !this->autoSkipped.count(active)) {
        this->takeSkipInterval();
        return;
    }

    if (this->skipDismissed && !this->isOsdShown) {
        this->hideSkipButton();
        return;
    }
    if (this->skipButton->shown()) return;
    if (this->pauseScreenShown() || this->loadingScreen->shown()) return;

    this->skipButton->setOsdVisible(this->isOsdShown);
    this->skipButton->show();
    // Focus, unless "up next" has it — the reference's suppressFocus is exactly
    // "the post-play card is up".
    if (!this->isOsdShown && !this->nextCardShown()) brls::Application::giveFocus(this->skipButton);
}

/// The 10 s auto-hide and the bar that shows it running out. Per FRAME, from
/// draw(): mpv reports a position once a second, and a bar stepping ten times
/// is not the sweep the reference animates.
///
/// The countdown does not run while the controls are up — the reference gates
/// its animation on !controlsVisible — and the controls coming up bring an
/// auto-hidden button back.
void VideoView::updateSkipCountdown() {
    if (!this->skipButton || !this->skipButton->shown()) return;

    constexpr int64_t kAutoHideUs = 10 * 1000000;
    brls::Time now = brls::getCPUTimeUsec();
    int64_t elapsed = now - this->skipShownAt;

    if (this->isOsdShown) {
        // Pinned open: hold the countdown where it is rather than letting it
        // run out behind the bar.
        this->skipShownAt = now - std::min(elapsed, kAutoHideUs);
        this->skipButton->setCountdownVisible(false);
        return;
    }
    if (this->skipDismissed || elapsed >= kAutoHideUs) {
        this->hideSkipButton();
        return;
    }
    this->skipButton->setCountdownVisible(true);
    this->skipButton->setProgress((float)elapsed / (float)kAutoHideUs);
}

/// Take the card down, and give focus back to the video if the card had it —
/// leaving it on a hidden view is how the player stops answering buttons.
bool VideoView::nextCardHasFocus() {
    if (!this->nextCard) return false;
    for (brls::View* v = brls::Application::getCurrentFocus(); v; v = v->getParent())
        if (v == this->nextCard) return true;
    return false;
}

void VideoView::hideNextEpisodeCard() {
    if (!this->nextCard || !this->nextCard->shown()) return;
    bool hadFocus = this->nextCardHasFocus();
    this->nextCard->hide();
    if (hadFocus) brls::Application::giveFocus(this);
}

/// Circle, or having taken the offer. Stays down for the rest of the episode.
void VideoView::dismissNextEpisodeCard() {
    this->nextCardDismissed = true;
    this->hideNextEpisodeCard();
}

void VideoView::schedulePauseScreen() {
    this->pausedSince = 0;
    if (!MPVCore::PAUSE_SCREEN || !this->firstFrameSeen) return;
    // The still-watching prompt stopped playback itself, and owns the screen
    // while it is asking. This is not a pause the viewer made.
    if (this->stillWatchingUntil > 0) return;
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
    this->osdLastShowTime = 0;
    this->osdState = OSDState::HIDDEN;
    if (this->skipButton) {
        this->skipButton->setOsdVisible(false);
        // The countdown is paused while the controls are up, as the reference
        // pauses its animation; it restarts from where it stopped.
        if (this->skipButton->shown() && !this->nextCardShown()) {
            this->skipShownAt = brls::getCPUTimeUsec();
            brls::Application::giveFocus(this->skipButton);
        }
    }
    if (!this->nextCard) return;
    this->nextCard->setOsdVisible(false);
    // The bar going away is the reference's controlsVisible turning false, and
    // its card takes focus the moment that is true. Ours has to, too: the OSD
    // is where focus was, and there is nothing else left to hold it.
    if (this->nextCard->shown()) brls::Application::giveFocus(this->nextCard);
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