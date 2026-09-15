#ifdef __SWITCH__
#include <switch.h>
#include "utils/overclock.hpp"
#elif defined(__PSV__)
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr/thread.h>
#include <psp2/appmgr.h>
#include <psp2/vshbridge.h>
#include <borealis/platforms/desktop/desktop_platform.hpp>

extern "C" {
unsigned int _newlib_heap_size_user = 220 * 1024 * 1024;
unsigned int _pthread_stack_default_user = 2 * 1024 * 1024;
}
#elif defined(__PS4__)
#include <orbis/SystemService.h>
#include <orbis/Sysmodule.h>
#include <arpa/inet.h>

extern "C" {
extern int ps4_mpv_use_precompiled_shaders;
extern int ps4_mpv_dump_shaders;
extern in_addr_t primary_dns;
extern in_addr_t secondary_dns;
}
#elif defined(ANDROID)
#include <SDL2/SDL_system.h>
#include <jni.h>
#elif defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
#include <unistd.h>
#include <borealis/platforms/desktop/desktop_platform.hpp>
#if defined(_WIN32)
#include <shlobj.h>
#endif

constexpr uint32_t MINIMUM_WINDOW_WIDTH = 640;
constexpr uint32_t MINIMUM_WINDOW_HEIGHT = 360;
#endif

#include <borealis.hpp>
#include <borealis/core/cache_helper.hpp>
#include <algorithm>
#include <borealis/views/edit_text_dialog.hpp>
#include "api/plex/auth.hpp"
#include "api/backend.hpp"
#include "api/plex/backend.hpp"
#include "api/jellyfin/backend.hpp"
#include "api/stremio/backend.hpp"
#include "api/nuvio/backend.hpp"
#include "api/http.hpp"
#include "utils/config.hpp"

#include <cmath>
#include "utils/theme_palette.hpp"
#include "utils/keybind.hpp"
#include "utils/misc.hpp"
#include "utils/ums.hpp"
#include "utils/thread.hpp"
#include "view/mpv_core.hpp"
#include "view/video_view.hpp"

std::unordered_map<AppConfig::Item, AppConfig::Option> AppConfig::settingMap = {
    {APP_THEME, {"app_theme", {"auto", "light", "dark"}}},
    // options filled from plenx::namedThemes() at startup (see initThemeOptions)
    {ACCENT_THEME, {"accent_theme", {"auto"}}},
    {APP_LANG, {"app_lang", {brls::LOCALE_AUTO, brls::LOCALE_EN_US, brls::LOCALE_ZH_HANS, brls::LOCALE_ZH_HANT,
                                brls::LOCALE_JA, brls::LOCALE_Ko, brls::LOCALE_RU, brls::LOCALE_DE, brls::LOCALE_FR,
                                brls::LOCALE_ES, brls::LOCALE_PT, "cs", "uk", "tr", "vi"}}},
    {APP_UPDATE, {"app_update"}},
    {APP_UI_SCALE, {"app_ui_scale", {"544p", "720p", "900p", "1080p"}}},
    {SCROLLBAR, {"scrollbar"}},
    {POSTER_LABELS, {"poster_labels"}},
    {SYNC_CLIENT_ID, {"sync_client_id"}},
    {SHOW_HERO, {"show_hero"}},
    {SHOW_CONTINUE, {"show_continue"}},
    {CATALOG_ADDON_NAME, {"catalog_addon_name"}},
    {CATALOG_TYPE_SUFFIX, {"catalog_type_suffix"}},
    {STREAM_ADDON_LOGO, {"stream_addon_logo"}},
    {EPISODE_OVERLAY_ART, {"episode_overlay_art"}},
    {DISCOVER_SELECTION, {"discover_selection"}},
    {AUDIO_CHANNELS, {"audio-channels", {"auto-safe", "stereo", "mono"}}},
    {KEYMAP, {"keymap", {"xbox", "ps", "keyboard"}}},
    {WINDOW_STATE, {"window_state"}},
    {TRANSCODEC, {"transcodec", {"h264", "hevc", "av1"}}},
    {FORCE_DIRECTPLAY, {"force_directplay"}},
    {PLAYER_VIDEO_QUALITY, {"player_video_quality"}},
    {FULLSCREEN, {"fullscreen"}},
    {OSD_ON_TOGGLE, {"osd_on_toggle"}},
    {LOADING_SCREEN, {"loading_screen"}},
    {LOADING_STAGES, {"loading_stages"}},
    {PAUSE_SCREEN, {"pause_screen"}},
    {NEXT_EPISODE_CARD, {"next_episode_card"}},
    {STREAM_AUTOPLAY_MODE, {"stream_autoplay_mode"}},
    {STREAM_AUTOPLAY_REGEX, {"stream_autoplay_regex"}},
    {PREFER_BINGE_GROUP, {"prefer_binge_group"}},
    {NEXT_EPISODE_AUTOPLAY, {"next_episode_autoplay"}},
    {STILL_WATCHING, {"still_watching"}},
    {STILL_WATCHING_THRESHOLD, {"still_watching_threshold"}},
    {INTRODB, {"introdb"}},
    {INTRODB_AUTO_SKIP, {"introdb_auto_skip"}},
    {LAYOUT_FULL_RELEASE_DATE, {"layout_full_release_date"}},
    {LAYOUT_SHOW_RATINGS, {"layout_show_ratings"}},
    {LAYOUT_FULLSCREEN_HERO, {"layout_fullscreen_hero"}},
    {CW_NEXT_UP_FURTHEST, {"cw_next_up_furthest"}},
    {CW_SHOW_UNAIRED, {"cw_show_unaired"}},
    {CW_SORT_MODE, {"cw_sort_mode"}},
    {LAYOUT_CW_EPISODE_THUMBS, {"layout_cw_episode_thumbs"}},
    {PARENTAL_GUIDE, {"parental_guide"}},
    {AUDIO_DELAY_REMEMBER, {"audio_delay_remember"}},
    {AUDIO_DELAY_MS, {"audio_delay_ms"}},
    {PLAYER_AUDIO_LANG, {"player_audio_lang", {"default", "device"}}},
    {SKIP_INTRO_ENABLED, {"skip_intro_enabled"}},
    {SUB_STRIP_SDH, {"sub_strip_sdh"}},
    {SUB_SECONDARY_LANG, {"sub_secondary_lang"}},
    {SUB_ONLY_PREFERRED_LANGS, {"sub_only_preferred_langs"}},
    {SUB_USE_FORCED, {"sub_use_forced"}},
    {OSD_CLOCK, {"osd_clock"}},
    {PLAYER_STATS_HUD, {"player_stats_hud"}},
    {STARTUP_SPLASH, {"startup_splash"}},
    {FAST_HORIZONTAL_NAV, {"fast_horizontal_nav"}},
    {AMOLED_MODE, {"amoled_mode"}},
    {AMOLED_SURFACES, {"amoled_surfaces"}},
    {SUB_SIZE, {"sub_size"}},
    {SUB_OFFSET, {"sub_offset"}},
    {SUB_BOLD, {"sub_bold"}},
    {SUB_OUTLINE, {"sub_outline"}},
    {SUB_OUTLINE_WIDTH, {"sub_outline_width"}},
    {SUB_TEXT_COLOR, {"sub_text_color"}},
    {SUB_BG_COLOR, {"sub_bg_color"}},
    {SUB_OUTLINE_COLOR, {"sub_outline_color"}},
    {AUTO_SKIP_INTRO, {"auto_skip_intro"}},
    {AUTO_SKIP_RECAP, {"auto_skip_recap"}},
    {AUTO_SKIP_OUTRO, {"auto_skip_outro"}},
    {LAYOUT_POSTER_WIDTH, {"layout_poster_width"}},
    {LAYOUT_POSTER_RADIUS, {"layout_poster_radius"}},
    {APPEARANCE_FONT, {"appearance_font"}},
    {LAYOUT_LANDSCAPE_POSTERS, {"layout_landscape_posters"}},
    {LAYOUT_HIDE_UNRELEASED, {"layout_hide_unreleased"}},
    {NEXT_EPISODE_MODE, {"next_episode_mode"}},
    {NEXT_EPISODE_PERCENT, {"next_episode_percent"}},
    {NEXT_EPISODE_MINUTES, {"next_episode_minutes"}},
    {PAUSE_SCREEN_DELAY, {"pause_screen_delay"}},
    {TOUCH_GESTURE, {"touch_gesture"}},
    {CLIP_POINT, {"clip_point"}},
    {SYNC_SETTING, {"sync_setting"}},
    {OVERCLOCK, {"overclock"}},
    {MPV_VO, {"mpv_vo", {"gpu", "gpu-next", "mediacodec_embed"}}},
    {PLAYER_LOW_QUALITY, {"player_low_quality"}},
    {PLAYER_SUBS_FALLBACK, {"player_subs_fallback"}},
    // options/labels are built at runtime from media::subtitleLangCatalog() in the
    // settings tab (value stored as-is: "auto" | "off" | 2-letter code)
    {PLAYER_SUBTITLE_LANG, {"player_subtitle_lang"}},
    {PLAYER_INMEMORY_CACHE,
        {
            "player_inmemory_cache",
            // 64 is the reference's own (NuvioMpvSurfaceView's 64 MiB).
            {"0MB", "10MB", "20MB", "50MB", "64MB", "100MB", "200MB", "500MB"},
            {0, 10, 20, 50, 64, 100, 200, 500},
        }},
    {PLAYER_SPEED,
        {
            "player_speed",
            {"4x", "3x", "2x"},
            {400, 300, 200},
        }},
    {PLAYER_HWDEC, {"player_hwdec"}},
    {PLAYER_HWDEC_CUSTOM, {"player_hwdec_custom"}},
    {PLAYER_ASPECT, {"player_aspect", {"auto", "stretch", "crop", "4:3", "16:9"}}},
    {PLAYER_TV_MODE, {"player_tv_mode"}},
    {ALWAYS_ON_TOP, {"always_on_top"}},
    {SINGLE, {"single"}},
    {SHOW_FPS, {"show_fps"}},
    {SWAP_INTERVAL, {"swap_interval"}},
    {APP_SWAP_ABXY, {"app_swap_abxy"}},
    {TEXTURE_CACHE_NUM, {"texture_cache_num"}},
    {REQUEST_THREADS, {"request_threads", {"1", "2", "4", "8"}, {1, 2, 4, 8}}},
    {REQUEST_TIMEOUT,
        {"request_timeout", {"3000", "5000", "10000", "20000", "30000"}, {3000, 5000, 10000, 20000, 30000}}},
    {HTTP_PROXY_STATUS, {"http_proxy_status"}},
    {HTTP_PROXY, {"http_proxy"}},

    {LIBRARY_SORT, {"library_sort"}},
    {SIDEBAR_LAYOUT, {"sidebar_layout"}},
    {HIDDEN_HUBS, {"hidden_hubs"}},
    {HUB_ORDER, {"hub_order"}},
    {DEBUG_LOG, {"debug_log"}},

    {HINT_FORWARDER, {"hint_forwarder"}},
    {HINT_FORWARDER_GMCA, {"hint_forwarder_gmca"}},
    {RENAME_NOTICE_SHOWN, {"rename_notice_shown"}},

    {KEY_REFRESH, {"key_refresh"}},
    {KEY_LAST, {"key_last"}},
    {KEY_NEXT, {"key_next"}},
    {KEY_VOLUME_UP, {"key_volume_up"}},
    {KEY_VOLUME_DOWN, {"key_volume_down"}},
    {KEY_VIDEO_PROFILE, {"key_video_profile"}},
    {KEY_FORWARD, {"key_forward"}},
    {KEY_REWIND, {"key_rewind"}},
    {KEY_SETTING, {"key_setting"}},
    {KEY_VIDEO_QUALITY, {"key_video_quality"}},
    {KEY_VIDEO_SPEED, {"key_video_speed"}},
    {KEY_VIDEO_OSD, {"key_video_osd"}},
    {KEY_VIDEO_PAUSE, {"key_video_pause"}},
};

static std::string generateDeviceId() {
#ifdef __SWITCH__
    AccountUid uid;
    accountInitialize(AccountServiceType_Administrator);
    if (R_FAILED(accountGetPreselectedUser(&uid))) {
        if (R_FAILED(accountTrySelectUserWithoutInteraction(&uid, false))) {
            accountGetLastOpenedUser(&uid);
        }
    }
    accountExit();
    if (accountUidIsValid(&uid)) {
        uint8_t digest[32];
        sha256CalculateHash(digest, &uid, sizeof(uid));
        return misc::hexEncode(digest, sizeof(digest));
    }
#elif defined(__PSV__)
    char cid[0x20];
    if (_vshSblAimgrGetConsoleId(cid) >= 0) {
        char text[0x40];
        sceClibSnprintf(text, sizeof(text) - 1, "%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
            cid[0x0], cid[0x1], cid[0x2], cid[0x3], cid[0x4], cid[0x5], cid[0x6], cid[0x7], cid[0x8], cid[0x9],
            cid[0xA], cid[0xB], cid[0xC], cid[0xD], cid[0xE], cid[0xF]);
        return text;
    }
#elif defined(ANDROID)
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jclass utilsClass = env->FindClass("org/libsdl/app/PlatformUtils");
    if (utilsClass) {
        jmethodID jmethod = env->GetStaticMethodID(utilsClass, "getAndroidId", "()Ljava/lang/String;");
        jstring jname = (jstring)env->CallStaticObjectMethod(utilsClass, jmethod);
        const char* name = env->GetStringUTFChars(jname, nullptr);
        std::string deviceId = name;
        env->ReleaseStringUTFChars(jname, name);
        env->DeleteLocalRef(jname);
        env->DeleteLocalRef(utilsClass);
        return deviceId;
    }
#elif defined(_WIN32)
    HW_PROFILE_INFOW profile;
    if (GetCurrentHwProfileW(&profile)) {
        std::vector<char> deviceId(HW_PROFILE_GUIDLEN);
        WideCharToMultiByte(CP_UTF8, 0, profile.szHwProfileGuid, std::wcslen(profile.szHwProfileGuid), deviceId.data(),
            deviceId.size(), nullptr, nullptr);
        return deviceId.data();
    }
#elif defined(__APPLE__)
    io_registry_entry_t ioRegistryRoot = IORegistryEntryFromPath(kIOMasterPortDefault, "IOService:/");
    if (ioRegistryRoot) {
        CFStringRef uuidCf = (CFStringRef)IORegistryEntryCreateCFProperty(
            ioRegistryRoot, CFSTR(kIOPlatformUUIDKey), kCFAllocatorDefault, 0);
        std::vector<char> deviceId(CFStringGetLength(uuidCf) + 1);
        CFStringGetCString(uuidCf, deviceId.data(), deviceId.size(), kCFStringEncodingMacRoman);
        CFRelease(uuidCf);
        IOObjectRelease(ioRegistryRoot);
        return deviceId.data();
    }
#elif defined(__linux__)
    static const std::vector<std::string> dev_names = {
        "/sys/devices/virtual/dmi/id/board_serial",
        "/proc/device-tree/serial-number",
        "/etc/machine-id",
    };
    for (auto& path : dev_names) {
        std::ifstream f(path.c_str());
        if (f.is_open()) {
            std::string name;
            std::getline(f, name);
            if (name.size() > 0) {
                return misc::hexEncode((uint8_t*)name.data(), name.size());
            }
        }
    }
#endif
    return misc::randHex(16);
}

/// Per-platform data folder for a given application name.
/// Factored out so the migration can compute the old name's path.
static std::string dataDir(const std::string& name) {
#if __SWITCH__
    return fmt::format("sdmc:/switch/{}", name);
#elif defined(__PS4__)
    return fmt::format("/data/{}", name);
#elif defined(__PSV__)
    return fmt::format("ux0:/data/{}", name);
#elif _WIN32
    WCHAR wpath[MAX_PATH];
    std::vector<char> lpath(MAX_PATH);
    SHGetSpecialFolderPathW(0, wpath, CSIDL_LOCAL_APPDATA, false);
    WideCharToMultiByte(CP_UTF8, 0, wpath, std::wcslen(wpath), lpath.data(), lpath.size(), nullptr, nullptr);
    return fmt::format("{}\\{}", lpath.data(), name);
#elif defined(ANDROID)
    return SDL_AndroidGetExternalStoragePath();
#elif __linux__
    char* config_home = getenv("XDG_CONFIG_HOME");
    if (config_home) return fmt::format("{}/{}", config_home, name);
    return fmt::format("{}/.config/{}", getenv("HOME"), name);
#elif __APPLE__
    return fmt::format("{}/Library/Application Support/{}", getenv("HOME"), name);
#endif
}

/// Silent migration of the config folder inherited from a previous name
/// (Switchlex -> pleNx -> GMCA): Plex/Jellyfin session, settings and
/// downloads must survive each rename. Returns true when a legacy dir was
/// actually relocated (the caller uses this to gate the one-time rebrand
/// welcome notice).
static bool migrateLegacyConfigDir(const std::string& legacy, const std::string& current) {
    if (legacy == current) return false;  // e.g. Android: path independent of the name
#if !defined(USE_BOOST_FILESYSTEM) || defined(_WIN32)
    const fs::path from = fs::u8path(legacy), to = fs::u8path(current);
#else
    const fs::path from = legacy, to = current;
#endif
    try {
        if (fs::exists(from) && !fs::exists(to)) {
            fs::rename(from, to);
            brls::Logger::info("AppConfig: migrated config dir {} -> {}", legacy, current);
            return true;
        }
    } catch (const std::exception& ex) {
        brls::Logger::warning("AppConfig: config dir migration {} -> {} failed: {}", legacy, current, ex.what());
    }
    return false;
}

bool AppConfig::init() {
    // Chained most-recent-first; the !exists(to) guard means only the first
    // applicable source migrates. pleNx 0.2.0 already targets the GMCA folder
    // (BUILD_PACKAGE_NAME), so existing pleNx data is relocated here, and the
    // renamed GMCA build then reads it in place.
    // Only one source can migrate (the !exists(to) guard), so at most one of
    // these is true — enough to flag "this user just came from a legacy build".
    this->migratedFromLegacy = migrateLegacyConfigDir(dataDir("pleNx"), this->configDir());
    this->migratedFromLegacy |= migrateLegacyConfigDir(dataDir("Switchlex"), this->configDir());
    const std::string path = this->configDir() + "/config.json";
#if !defined(USE_BOOST_FILESYSTEM) || defined(_WIN32)
    std::ifstream f(fs::u8path(path));
#else
    std::ifstream f(path);
#endif
    if (f.is_open()) {
        try {
            nlohmann::json::parse(f).get_to(*this);
            brls::Logger::info("Load config from: {}", path);
        } catch (const std::exception& ex) {
            brls::Logger::error("AppConfig::load: {}", ex.what());
            return false;
        }
    }

#if defined(_WIN32) && !defined(_WINRT_)
    misc::initCrashDump();
#endif

#if (defined(__APPLE__) || defined(__linux__) || defined(_WIN32)) && !defined(ANDROID) && !defined(TRIMUI)
    brls::DesktopPlatform::GAMEPAD_DB = configDir() + "/gamecontrollerdb.txt";
    if (this->getItem(AppConfig::SINGLE, false) && misc::sendIPC(this->ipcSocket(), "{}")) {
        brls::Logger::warning("AppConfig single instance");
        return false;
    }
    // 加载窗口位置
    auto wstate = this->getItem(AppConfig::WINDOW_STATE, std::string{""});
    if (wstate.size() > 0) {
        int hXPos, hYPos, monitor;
        uint32_t hWidth, hHeight;
        sscanf(wstate.c_str(), "%d,%ux%u,%dx%d", &monitor, &hWidth, &hHeight, &hXPos, &hYPos);
        if (hWidth > 0 && hHeight > 0) {
            VideoContext::sizeH = hHeight;
            VideoContext::sizeW = hWidth;
            VideoContext::posX = (float)hXPos;
            VideoContext::posY = (float)hYPos;
            VideoContext::monitorIndex = monitor;
        }
    }
    // 窗口将要关闭时, 保存窗口状态配置
    brls::Application::getExitEvent()->subscribe([this]() {
        if (std::isnan(VideoContext::posX) || std::isnan(VideoContext::posY)) return;
        if (VideoContext::FULLSCREEN) return;
        auto videoContext = brls::Application::getPlatform()->getVideoContext();
        uint32_t width = VideoContext::sizeW;
        uint32_t height = VideoContext::sizeH;
        if (width == 0) width = brls::Application::ORIGINAL_WINDOW_WIDTH;
        if (height == 0) height = brls::Application::ORIGINAL_WINDOW_HEIGHT;
        this->setItem(AppConfig::WINDOW_STATE, fmt::format("{},{}x{},{}x{}", videoContext->getCurrentMonitorIndex(),
                                                   width, height, (int)VideoContext::posX, (int)VideoContext::posY));
        this->save();
    });
#elif defined(__PSV__)
    int search_unk[2];
    if (_vshKernelSearchModuleByName("CapUnlocker", search_unk) >= 0) {
        brls::sync([]() { brls::Application::notify("CapUnlocker found"); });
        sceKernelChangeThreadPriority(SCE_KERNEL_THREAD_ID_SELF, 64);
        sceKernelChangeThreadCpuAffinityMask(SCE_KERNEL_THREAD_ID_SELF, SCE_KERNEL_CPU_MASK_SYSTEM);
    }
#elif defined(__PS4__)
    if (sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET) < 0) brls::Logger::error("cannot load net module");
    primary_dns = inet_addr("223.5.5.5");
    secondary_dns = inet_addr("1.1.1.1");
    ps4_mpv_use_precompiled_shaders = 1;
    ps4_mpv_dump_shaders = 0;
    // 在加载第一帧之后隐藏启动画面
    brls::sync([]() { sceSystemServiceHideSplashScreen(); });
#endif

    std::string uiScale = this->getItem(APP_UI_SCALE, std::string(""));
    if (uiScale == "544p") {
        brls::Application::ORIGINAL_WINDOW_WIDTH = 960;
        brls::Application::ORIGINAL_WINDOW_HEIGHT = 544;
    } else if (uiScale == "720p") {
        brls::Application::ORIGINAL_WINDOW_WIDTH = 1280;
        brls::Application::ORIGINAL_WINDOW_HEIGHT = 720;
    } else if (uiScale == "900p") {
        brls::Application::ORIGINAL_WINDOW_WIDTH = 1600;
        brls::Application::ORIGINAL_WINDOW_HEIGHT = 900;
    } else if (uiScale == "1080p") {
        brls::Application::ORIGINAL_WINDOW_WIDTH = 1920;
        brls::Application::ORIGINAL_WINDOW_HEIGHT = 1080;
    }

    AppConfig::SYNC = this->getItem(SYNC_SETTING, true);

    HTTP::TIMEOUT = this->getItem(REQUEST_TIMEOUT, 3000L);
    HTTP::PROXY_STATUS = this->getItem(HTTP_PROXY_STATUS, false);
    HTTP::PROXY = this->getItem(HTTP_PROXY, std::string("http://192.168.1.1:1080"));

    // 初始化是否全屏，必须在创建窗口前设置此值
    VideoContext::FULLSCREEN = this->getItem(FULLSCREEN, false);

    MPVCore::OSD_ON_TOGGLE = this->getItem(OSD_ON_TOGGLE, true);
    MPVCore::LOADING_SCREEN = this->getItem(LOADING_SCREEN, true);
    MPVCore::LOADING_STAGES = this->getItem(LOADING_STAGES, true);
    MPVCore::PAUSE_SCREEN = this->getItem(PAUSE_SCREEN, true);
    MPVCore::NEXT_EPISODE_CARD = this->getItem(NEXT_EPISODE_CARD, true);
    MPVCore::INTRODB = this->getItem(INTRODB, true);
    MPVCore::NEXT_EPISODE_AUTOPLAY = this->getItem(NEXT_EPISODE_AUTOPLAY, false);
    MPVCore::STILL_WATCHING = this->getItem(STILL_WATCHING, false);
    MPVCore::STILL_WATCHING_THRESHOLD = this->getItem(STILL_WATCHING_THRESHOLD, 3);
    MPVCore::OSD_CLOCK = this->getItem(OSD_CLOCK, true);
    MPVCore::PLAYER_STATS_HUD = this->getItem(PLAYER_STATS_HUD, false);
    MPVCore::SKIP_INTRO_ENABLED = this->getItem(SKIP_INTRO_ENABLED, true);
    MPVCore::PARENTAL_GUIDE = this->getItem(PARENTAL_GUIDE, true);
    MPVCore::AUDIO_DELAY_MS = this->getItem(AUDIO_DELAY_REMEMBER, true) ? this->getItem(AUDIO_DELAY_MS, 0) : 0;
    MPVCore::AUTO_SKIP_INTRO = this->getItem(AUTO_SKIP_INTRO, false);
    MPVCore::AUTO_SKIP_RECAP = this->getItem(AUTO_SKIP_RECAP, false);
    MPVCore::AUTO_SKIP_OUTRO = this->getItem(AUTO_SKIP_OUTRO, false);
    MPVCore::SUB_SIZE = this->getItem(SUB_SIZE, 100);
    MPVCore::SUB_OFFSET = this->getItem(SUB_OFFSET, 5);
    MPVCore::SUB_BOLD = this->getItem(SUB_BOLD, false);
    MPVCore::SUB_OUTLINE = this->getItem(SUB_OUTLINE, true);
    MPVCore::SUB_OUTLINE_WIDTH = this->getItem(SUB_OUTLINE_WIDTH, 2);
    MPVCore::SUB_TEXT_COLOR = this->getItem(SUB_TEXT_COLOR, std::string("#FFFFFFFF"));
    MPVCore::SUB_BG_COLOR = this->getItem(SUB_BG_COLOR, std::string("#00000000"));
    MPVCore::SUB_OUTLINE_COLOR = this->getItem(SUB_OUTLINE_COLOR, std::string("#FF000000"));
    MPVCore::NEXT_EPISODE_MODE = this->getItem(NEXT_EPISODE_MODE, 0);
    MPVCore::NEXT_EPISODE_PERCENT = this->getItem(NEXT_EPISODE_PERCENT, 198);
    MPVCore::NEXT_EPISODE_MINUTES = this->getItem(NEXT_EPISODE_MINUTES, 4);
    MPVCore::PAUSE_SCREEN_DELAY = this->getItem(PAUSE_SCREEN_DELAY, 15);
    MPVCore::TOUCH_GESTURE = this->getItem(TOUCH_GESTURE, true);
    MPVCore::CLIP_POINT = this->getItem(CLIP_POINT, true);
    // 初始化内存缓存大小
    MPVCore::INMEMORY_CACHE = this->getItem(PLAYER_INMEMORY_CACHE, 10);
    // 是否使用低质量解码
#if defined(__PSV__) || defined(__PS4__) || defined(__SWITCH__)
    MPVCore::LOW_QUALITY = this->getItem(PLAYER_LOW_QUALITY, true);
#else
    MPVCore::LOW_QUALITY = this->getItem(PLAYER_LOW_QUALITY, false);
#endif
    MPVCore::SUBS_FALLBACK = this->getItem(PLAYER_SUBS_FALLBACK, true);

    // 初始化是否使用硬件加速
    MPVCore::VO = this->getItem(MPV_VO, MPVCore::VO);
    MPVCore::HARDWARE_DEC = this->getItem(PLAYER_HWDEC, true);
    MPVCore::FORCE_DIRECTPLAY = this->getItem(FORCE_DIRECTPLAY, false);
    // default transcode bitrate cap. The Vita decoder chokes on heavy direct
    // play (high-bitrate / unsupported codecs -> slideshow), so default to a
    // smooth 4 Mbps H.264 transcode; "Auto" (0 = direct play) stays selectable
    // in the player quality menu. Other platforms default to direct play.
    // Persisted now (it was reset every launch, so the user's lowered choice
    // never survived a restart).
#if defined(__PSV__)
    MPVCore::VIDEO_QUALITY = this->getItem(PLAYER_VIDEO_QUALITY, (int64_t)4000000);
#else
    MPVCore::VIDEO_QUALITY = this->getItem(PLAYER_VIDEO_QUALITY, (int64_t)0);
#endif
    MPVCore::VIDEO_CODEC = this->getItem(TRANSCODEC, MPVCore::VIDEO_CODEC);
    MPVCore::AUDIO_CHANNELS = this->getItem(AUDIO_CHANNELS, MPVCore::AUDIO_CHANNELS);
    // 初始化自定义的硬件加速方案
    MPVCore::PLAYER_HWDEC_METHOD = this->getItem(PLAYER_HWDEC_CUSTOM, MPVCore::PLAYER_HWDEC_METHOD);
    // 初始化默认的倍速设定
    MPVCore::VIDEO_SPEED = this->getItem(PLAYER_SPEED, MPVCore::VIDEO_SPEED);
    // 初始化视频比例
    MPVCore::VIDEO_ASPECT = this->getItem(PLAYER_ASPECT, MPVCore::VIDEO_ASPECT);
    // TV-style OSD by default: the progress bar is focusable and left/right
    // seek from it — the natural behaviour on a controller-driven device
    MPVCore::OSD_TV_MODE = this->getItem(PLAYER_TV_MODE, true);

    ThreadPool::max_thread_num = this->getItem(REQUEST_THREADS, ThreadPool::max_thread_num);

    // 初始化 deviceId
    if (this->device.empty()) this->device = generateDeviceId();

    // 初始化i18n
    brls::Platform::APP_LOCALE_DEFAULT = this->getItem(APP_LANG, brls::LOCALE_AUTO);

    brls::Application::setFPSStatus(this->getItem(SHOW_FPS, false));
    VideoContext::swapInterval = this->getItem(SWAP_INTERVAL, 1);

    // 初始化 KeyBind
    KeyBind::setLast(this->getItem(KEY_LAST, std::string{"pgup"}));
    KeyBind::setNext(this->getItem(KEY_NEXT, std::string{"pgdn"}));
    KeyBind::setVolumeUp(this->getItem(KEY_VOLUME_UP, std::string{"0"}));
    KeyBind::setVolumeDown(this->getItem(KEY_VOLUME_DOWN, std::string{"9"}));
    KeyBind::setVideoProfile(this->getItem(KEY_VIDEO_PROFILE, std::string{"f1"}));
    KeyBind::setVideoQuality(this->getItem(KEY_VIDEO_QUALITY, std::string{"f2"}));
    KeyBind::setVideoSpeed(this->getItem(KEY_VIDEO_SPEED, std::string{"f3"}));
    KeyBind::setSetting(this->getItem(KEY_SETTING, std::string{"f4"}));
    KeyBind::setRefresh(this->getItem(KEY_REFRESH, std::string{"f5"}));
    KeyBind::setForward(this->getItem(KEY_FORWARD, std::string{"]"}));
    KeyBind::setRewind(this->getItem(KEY_REWIND, std::string{"["}));
    KeyBind::setVideoOsd(this->getItem(KEY_VIDEO_OSD, std::string{"o"}));
    KeyBind::setVideoPause(this->getItem(KEY_VIDEO_PAUSE, std::string{"space"}));

    // 初始化一些在创建窗口之后才能初始化的内容
    brls::Application::getWindowCreationDoneEvent()->subscribe([this]() {
#if defined(PS4)
        // The console tells us its own Enter Button Assignment and the app
        // follows it (ps4_platform.cpp); inverting that only ever made the app
        // disagree with the console, so the toggle that did is gone. Clear a
        // value left behind by the build that briefly offered it, otherwise
        // the swap would persist with nothing left to switch it back.
        if (this->getItem(APP_SWAP_ABXY, false)) this->setItem(APP_SWAP_ABXY, false);
#endif
#if defined(TRIMUI)
        if (this->getItem(APP_SWAP_ABXY, true))
#else
        if (this->getItem(APP_SWAP_ABXY, false))
#endif
        {
            // 对于 PSV/PS4 来说，初始化时会加载系统设置，可能在那时已经交换过按键
            // 所以这里需要读取 isSwapInputKeys 的值，而不是直接设置为 true
            brls::Application::setSwapInputKeys(!brls::Application::isSwapInputKeys());
        }

        // 初始化主题
        std::string appTheme = this->getItem(APP_THEME, std::string("auto"));
        if (appTheme == "light") {
            brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::LIGHT);
        } else if (appTheme == "dark") {
            brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);
        }

        // 初始化纹理缓存数量
#if defined(__PSV__) || defined(__PS4__)
        brls::TextureCache::instance().cache.setCapacity(1);
#ifdef __PSV__
        // The entry-count cap alone (401 effective: setCapacity ADDS
        // DEFAULT_CAPACITY) lets artwork pin ~100+ MB and starve mpv of
        // LPDDR/CDRAM. Cap the BYTES too: 48 MB of DXT posters is ~200-400
        // covers, plenty for browsing, and leaves the video allocations room.
        // (48, not 64: the music-view crash log peaked near ~54 MB of artwork
        // before the blue light — the budget must sit safely below that.)
        brls::TextureCache::instance().cache.setByteCapacity(48 * 1024 * 1024);
#endif
#else
        brls::TextureCache::instance().cache.setCapacity(getItem(TEXTURE_CACHE_NUM, 200));
#endif

#if (defined(__APPLE__) || defined(__linux__) || defined(_WIN32)) && !defined(ANDROID)
        // 设置窗口最小尺寸
        brls::Application::getPlatform()->setWindowSizeLimits(MINIMUM_WINDOW_WIDTH, MINIMUM_WINDOW_HEIGHT, 0, 0);
        if (this->getItem(ALWAYS_ON_TOP, false)) {
            brls::Application::getPlatform()->setWindowAlwaysOnTop(true);
        }
#endif

        // Init keyboard shortcut (F11 fullscreen toggle — non-Apple platforms only;
        // on macOS the handler had no live case, so it's not registered there)
#ifndef __APPLE__
        brls::Application::getPlatform()->getInputManager()->getKeyboardKeyStateChanged()->subscribe(
            [this](brls::KeyState state) {
                if (!state.pressed) return;
                switch (state.key) {
                case brls::BRLS_KBD_KEY_F11:
                    VideoContext::FULLSCREEN = !this->getItem(AppConfig::FULLSCREEN, VideoContext::FULLSCREEN);
                    this->setItem(AppConfig::FULLSCREEN, VideoContext::FULLSCREEN);
                    brls::Application::getPlatform()->getVideoContext()->fullScreen(VideoContext::FULLSCREEN);
                    break;
                default:;
                }
            });
#endif
    });

#ifdef __SWITCH__
    /// Set Overclock
    if (getItem(AppConfig::OVERCLOCK, false)) {
        SwitchSys::setClock(true);
    };
#endif
    Ums::instance().init();

    // init custom font path
    brls::FontLoader::USER_FONT_PATH = configDir() + "/font.ttf";
    brls::FontLoader::USER_ICON_PATH = configDir() + "/icon.ttf";
    if (access(brls::FontLoader::USER_ICON_PATH.c_str(), F_OK) == -1) {
        // 自定义字体不存在，使用内置字体
#if defined(__PSV__) || defined(__PS4__)
        brls::FontLoader::USER_ICON_PATH = BRLS_ASSET("font/keymap_ps.ttf");
#else
        std::string icon = getItem(KEYMAP, std::string("xbox"));
        if (icon == "xbox") {
            brls::FontLoader::USER_ICON_PATH = BRLS_ASSET("font/keymap_xbox.ttf");
        } else if (icon == "ps") {
            brls::FontLoader::USER_ICON_PATH = BRLS_ASSET("font/keymap_ps.ttf");
        } else if (brls::Application::isSwapInputKeys()) {
            brls::FontLoader::USER_ICON_PATH = BRLS_ASSET("font/keymap_keyboard_swap.ttf");
        } else {
            brls::FontLoader::USER_ICON_PATH = BRLS_ASSET("font/keymap_keyboard.ttf");
        }
#endif
    }

    brls::FontLoader::USER_EMOJI_PATH = configDir() + "/emoji.ttf";
    if (access(brls::FontLoader::USER_EMOJI_PATH.c_str(), F_OK) == -1) {
        // 自定义emoji不存在，使用内置emoji
        brls::FontLoader::USER_EMOJI_PATH = BRLS_ASSET("font/emoji.ttf");
    }

    brls::Logger::info("init {} v{}-{} device {} from {}", AppVersion::getPlatform(), AppVersion::getVersion(),
        AppVersion::getCommit(), this->device, path);
    return true;
}

void AppConfig::save() {
    try {
        std::string dir = this->configDir();
        fs::create_directories(dir);
#if !defined(USE_BOOST_FILESYSTEM) || defined(_WIN32)
        std::ofstream f(fs::u8path(dir + "/config.json"));
#else
        std::ofstream f(dir + "/config.json");
#endif
        if (f.is_open()) {
            nlohmann::json j(*this);
            f << j.dump(2);
            f.close();
        }
    } catch (const std::exception& ex) {
        brls::Logger::warning("AppConfig save: {}", ex.what());
    }
}

AppConfig::~AppConfig() { delete this->activeBackend; }

void AppConfig::resetBackend() {
    delete this->activeBackend;
    this->activeBackend = nullptr;
}

media::Backend& AppConfig::backend() {
    if (!this->activeBackend) {
        // active server type = the one whose front URL is the active server_url
        std::string type = "plex";
        for (auto& s : this->servers) {
            if (!this->server_url.empty() && !s.urls.empty() && s.urls.front() == this->server_url) {
                type = s.type;
                break;
            }
        }
        switch (backendTypeFromString(type)) {
            case media::BackendType::Jellyfin:
                this->activeBackend = new jellyfin::JellyfinBackend(media::BackendType::Jellyfin);
                break;
            case media::BackendType::Emby:
                this->activeBackend = new jellyfin::JellyfinBackend(media::BackendType::Emby);
                break;
            case media::BackendType::Stremio:
                this->activeBackend = new stremio::StremioBackend();
                break;
            case media::BackendType::Nuvio:
                this->activeBackend = new nuvio::NuvioBackend();
                break;
            case media::BackendType::Plex:
                this->activeBackend = new plex::PlexBackend();
                break;
        }
        // Which backend the session is actually running on, printed once. The
        // log otherwise cannot tell a Nuvio connection from a Stremio one —
        // both drive the same addons — so a report of "the library is not
        // syncing" had no way to say whether the sync path was even reached.
        brls::Logger::info("backend: type={} url={} account={} library={}", type, this->server_url,
            this->getToken().empty() ? "anonymous" : "signed in",
            this->activeBackend->caps().listKind == media::ListKind::None ? "on-device" : "backend");
    }
    return *this->activeBackend;
}

bool AppConfig::isHubHidden(const std::string& hubIdentifier) {
    if (hubIdentifier.empty()) return false;
    auto hidden = this->getItem<std::vector<std::string>>(HIDDEN_HUBS, {});
    return std::find(hidden.begin(), hidden.end(), hubIdentifier) != hidden.end();
}

void AppConfig::setHubHidden(const std::string& hubIdentifier, bool hidden) {
    if (hubIdentifier.empty()) return;
    auto list = this->getItem<std::vector<std::string>>(HIDDEN_HUBS, {});
    bool has = std::find(list.begin(), list.end(), hubIdentifier) != list.end();
    if (hidden == has) return;  // no disk write when nothing changed
    if (hidden)
        list.push_back(hubIdentifier);
    else
        list.erase(std::remove(list.begin(), list.end(), hubIdentifier), list.end());
    this->setItem(HIDDEN_HUBS, list);
}

std::vector<std::string> AppConfig::getHubOrder() { return this->getItem<std::vector<std::string>>(HUB_ORDER, {}); }

void AppConfig::setHubOrder(const std::vector<std::string>& order) { this->setItem(HUB_ORDER, order); }

media::BackendType AppConfig::backendTypeFromString(const std::string& type) {
    if (type == "jellyfin") return media::BackendType::Jellyfin;
    if (type == "emby") return media::BackendType::Emby;
    if (type == "stremio") return media::BackendType::Stremio;
    if (type == "nuvio") return media::BackendType::Nuvio;
    return media::BackendType::Plex;
}

const std::vector<std::string>& AppConfig::getStremioAddons() const {
    static const std::vector<std::string> empty;
    if (this->user == this->users.end()) return empty;
    for (auto& s : this->servers)
        if (s.id == this->user->server_id) return s.addons;
    return empty;
}

void AppConfig::setStremioAddons(const std::vector<std::string>& addons) {
    if (this->user == this->users.end()) return;
    for (auto& s : this->servers) {
        if (s.id != this->user->server_id) continue;
        if (s.addons == addons) return;  // no disk write when nothing changed
        s.addons = addons;
        this->save();
        return;
    }
}

const std::string& AppConfig::getNuvioRefreshToken() const {
    static const std::string empty;
    if (this->user == this->users.end()) return empty;
    for (auto& s : this->servers)
        if (s.id == this->user->server_id) return s.nuvio_refresh_token;
    return empty;
}

const std::string& AppConfig::getNuvioPublishableKey() const {
    static const std::string empty;
    if (this->user == this->users.end()) return empty;
    for (auto& s : this->servers)
        if (s.id == this->user->server_id) return s.nuvio_publishable_key;
    return empty;
}

int64_t AppConfig::getNuvioExpiresAt() const {
    if (this->user == this->users.end()) return 0;
    for (auto& s : this->servers)
        if (s.id == this->user->server_id) return s.nuvio_expires_at;
    return 0;
}

int AppConfig::getNuvioProfileIndex() const {
    if (this->user == this->users.end()) return 1;
    return this->user->nuvio_profile_index > 0 ? this->user->nuvio_profile_index : 1;
}

void AppConfig::setNuvioSession(const std::string& accessToken, const std::string& refreshToken, int64_t expiresAt) {
    if (this->user == this->users.end()) return;
    for (auto& s : this->servers) {
        if (s.id != this->user->server_id) continue;
        s.access_token = accessToken;
        s.nuvio_refresh_token = refreshToken;
        s.nuvio_expires_at = expiresAt;
        this->server_token = accessToken;  // keep the in-memory active token in sync
        this->save();
        return;
    }
}

bool AppConfig::checkLogin() {
    auto is_user = [this](const AppUser& u) { return u.id == this->user_id; };
    this->user = std::find_if(this->users.begin(), this->users.end(), is_user);
    if (this->user == this->users.end()) return false;

    auto is_server = [this](const AppServer& s) { return s.id == this->user->server_id; };
    auto it = std::find_if(this->servers.begin(), this->servers.end(), is_server);
    if (it == this->servers.end() || it->urls.empty()) return false;

    // Reconnect to a remembered endpoint. No dependency on plex.tv here: a
    // reachable stored URL is enough. The candidates are raced in parallel so an
    // unreachable LAN address no longer blocks a reachable remote/relay one while
    // roaming (GH #36). Stremio has no single reachable "server" (it aggregates
    // remote addons + an optional account); skip the probe, accept the stored one.
    std::string url = (it->type == "stremio" || it->type == "nuvio")
                           ? (it->urls.empty() ? std::string() : it->urls.front())
                           : plex::raceConnections(it->urls, it->access_token);
    if (url.empty()) {
        brls::Logger::warning("AppConfig checkLogin: aucun endpoint joignable pour {}", it->name);
        return false;
    }
    this->server_url = url;
    this->server_token = it->access_token;
    this->resetBackend();
    this->applyTheme(backendTypeFromString(it->type));
    if (url != it->urls.front()) {
        AppServer front = *it;
        front.urls = {url};
        this->addServer(front);
    }
    return true;
}

std::string AppConfig::configDir() { return dataDir(AppVersion::getPackageName()); }

std::string AppConfig::ipcSocket() {
#ifdef _WIN32
    return "\\\\.\\pipe\\" + AppVersion::getPackageName();
#else
    return fmt::format("{}/{}.sock", configDir(), AppVersion::getPackageName());
#endif
}

void AppConfig::checkRestart(char* argv[]) {
#if !defined(__PS4__) && !defined(__SWITCH__) && !defined(ANDROID)
    if (brls::DesktopPlatform::RESTART_APP) {
        brls::Logger::info("Restart app {}", argv[0]);

#if defined(__PSV__)
        sceAppMgrLoadExec(argv[0], argv, nullptr);
#else
        execv(argv[0], argv);
#endif
    }
#endif
}

int AppConfig::getOptionIndex(const Item item, int default_index) const {
    auto it = settingMap.find(item);
    if (setting.contains(it->second.key)) {
        try {
            std::string value = this->setting.at(it->second.key);
            for (size_t i = 0; i < it->second.options.size(); ++i)
                if (it->second.options[i] == value) return i;
        } catch (const std::exception& e) {
            brls::Logger::error("Damaged config found: {}/{}", it->second.key, e.what());
        }
    }
    return default_index;
}

int AppConfig::getValueIndex(const Item item, int default_index) const {
    auto it = settingMap.find(item);
    if (setting.contains(it->second.key)) {
        try {
            long value = this->setting.at(it->second.key);
            for (size_t i = 0; i < it->second.values.size(); ++i)
                if (it->second.values[i] == value) return i;
        } catch (const std::exception& e) {
            brls::Logger::error("Damaged config found: {}/{}", it->second.key, e.what());
        }
    }
    return default_index;
}

bool AppConfig::addServer(const AppServer& s) {
    if (s.urls.size() > 0) {
        this->server_url = s.urls.front();
    }

    for (auto& o : this->servers) {
        if (s.id == o.id) {
            if (!s.name.empty()) o.name = s.name;
            if (!s.access_token.empty()) o.access_token = s.access_token;
            // Nuvio: a fresh sign-in issues a brand-new refresh token — an old
            // stored one left in place here would be a stale, unusable session.
            if (!s.nuvio_refresh_token.empty()) o.nuvio_refresh_token = s.nuvio_refresh_token;
            if (!s.nuvio_publishable_key.empty()) o.nuvio_publishable_key = s.nuvio_publishable_key;
            if (s.nuvio_expires_at > 0) o.nuvio_expires_at = s.nuvio_expires_at;
            this->server_token = o.access_token;
            // remove old url
            for (auto it = o.urls.begin(); it != o.urls.end(); ++it) {
                if (it->compare(this->server_url) == 0) {
                    it = o.urls.erase(it);
                    break;
                }
            }
            o.urls.insert(o.urls.begin(), this->server_url);
            this->save();
            return true;
        }
    }
    this->server_token = s.access_token;
    this->servers.push_back(s);
    this->save();
    return false;
}

void AppConfig::addUser(const AppUser& u, const std::string& url) {
    auto is_user = [u](const AppUser& o) { return o.id == u.id; };
    auto it = std::find_if(this->users.begin(), this->users.end(), is_user);
    if (it != this->users.end()) {
        it->name = u.name;
        it->access_token = u.access_token;
        it->server_id = u.server_id;
        it->thumb = u.thumb;
        it->nuvio_profile_index = u.nuvio_profile_index;
    } else {
        it = this->users.insert(it, u);
    }
    this->server_url = url;
    this->user_id = u.id;
    this->user = it;
    // keeps the active server token in sync with the active user
    std::string activeType = "plex";
    for (auto& s : this->servers) {
        if (s.id == u.server_id) {
            this->server_token = s.access_token;
            activeType = s.type;
        }
    }
    this->resetBackend();
    this->applyTheme(backendTypeFromString(activeType));
    this->save();
}

void AppConfig::upsertServer(const AppServer& s) {
    // Like addServer but never touches the active server_url/server_token: this
    // registers a server we are NOT switching to. On an existing entry, refresh
    // name/token and merge any new candidate urls, keeping the current ordering
    // so a previously resolved (reachable) front url survives.
    for (auto& o : this->servers) {
        if (s.id == o.id) {
            if (!s.name.empty()) o.name = s.name;
            if (!s.access_token.empty()) o.access_token = s.access_token;
            for (auto& u : s.urls) {
                if (std::find(o.urls.begin(), o.urls.end(), u) == o.urls.end()) o.urls.push_back(u);
            }
            this->save();
            return;
        }
    }
    this->servers.push_back(s);
    this->save();
}

void AppConfig::upsertUser(const AppUser& u) {
    // Like addUser but never sets the active profile: registers a connection
    // tile without switching to it.
    auto is_user = [u](const AppUser& o) { return o.id == u.id; };
    auto it = std::find_if(this->users.begin(), this->users.end(), is_user);
    if (it != this->users.end()) {
        it->name = u.name;
        it->access_token = u.access_token;
        it->server_id = u.server_id;
        it->thumb = u.thumb;
    } else {
        this->users.push_back(u);
    }
    this->save();
}

bool AppConfig::removeServer(const std::string& id) {
    for (auto it = this->servers.begin(); it != this->servers.end(); ++it) {
        if (it->id == id) {
            this->servers.erase(it);
            this->save();
            return this->servers.empty();
        }
    }
    return false;
}

void AppConfig::addRemote(const AppRemote& r) {
    this->remotes.push_back(r);
    this->save();
}

void AppConfig::updateRemote(size_t index, const AppRemote& r) {
    if (index >= this->remotes.size()) return;
    this->remotes[index] = r;
    this->save();
}

void AppConfig::removeRemote(size_t index) {
    if (index >= this->remotes.size()) return;
    this->remotes.erase(this->remotes.begin() + index);
    this->save();
}

bool AppConfig::isPinned(const std::string& path) const {
    return std::find(this->pins.begin(), this->pins.end(), path) != this->pins.end();
}

void AppConfig::addPin(const std::string& path) {
    if (path.empty() || this->isPinned(path)) return;
    this->pins.push_back(path);
    this->save();
}

void AppConfig::removePin(const std::string& path) {
    auto it = std::find(this->pins.begin(), this->pins.end(), path);
    if (it == this->pins.end()) return;
    this->pins.erase(it);
    this->save();
}

bool AppConfig::removeUser(const std::string& id) {
    for (auto it = this->users.begin(); it != this->users.end(); ++it) {
        if (it->id == id) {
            this->users.erase(it);
            this->save();
            return true;
        }
    }
    return false;
}

const std::vector<AppUser> AppConfig::getUsers(const std::string& id) const {
    std::vector<AppUser> users;
    for (auto& u : this->users) {
        if (u.server_id == id) {
            users.push_back(u);
        }
    }
    return users;
}

void AppConfig::addColor(const brls::ThemeVariant tv, const std::string& name, NVGcolor defaultColor) {
    auto& theme = (tv == brls::ThemeVariant::LIGHT) ? brls::Theme::getLightTheme() : brls::Theme::getDarkTheme();

    if (!setting.contains(name)) {
        theme.addColor(name, defaultColor);
    } else {
        unsigned int r = 0, g = 0, b = 0;
        std::string s = setting.at(name).get<std::string>();

        std::stringstream sr{s.substr(1, 2)};
        sr >> std::hex >> r;
        std::stringstream sg{s.substr(3, 2)};
        sg >> std::hex >> g;
        std::stringstream sb{s.substr(5, 2)};
        sb >> std::hex >> b;
        theme.addColor(name, nvgRGB(r, g, b));
    }
}

void AppConfig::applyTheme(std::optional<media::BackendType> type) {
    // A colour theme the user picked wins over the backend's brand palette:
    // NuvioTV lets you choose one, and choosing one should mean it sticks
    // whichever account you are connected to.
    const plenx::ThemeColors* chosen = plenx::namedPalette(this->getItem(ACCENT_THEME, std::string("auto")));
    const plenx::ThemeColors& tc =
        chosen ? *chosen : (type ? plenx::backendPalette(*type) : plenx::defaultPalette());
    this->applyThemeVariant(brls::ThemeVariant::DARK, tc.dark);
    this->applyThemeVariant(brls::ThemeVariant::LIGHT, tc.light);
    // LAST, so it wins over both the structural pass and the accent one.
    this->applyAmoled();
}

/// NuvioColorScheme's amoledMode / amoledSurfacesMode.
///
/// Its rule, exactly: amoledMode alone takes the BACKGROUND to pure black,
/// and only with amoledSurfacesMode as well do the cards, panels, fields and
/// menus follow. The overlays and the divider are left alone in both cases —
/// a scrim over video has nothing to do with the panel behind the UI.
///
/// Dark only, which is what the reference means by it too.
void AppConfig::applyAmoled() {
    if (!this->getItem(AMOLED_MODE, false)) return;
    auto& dark = brls::Theme::getDarkTheme();
    const NVGcolor black = nvgRGB(0, 0, 0);

    for (const char* name : {"brls/clear", "brls/background", "brls/sidebar/background", "color/nav_bg",
             "color/fade_1"})
        dark.addColor(name, black);
    dark.addColor("color/fade_0", nvgRGBA(0, 0, 0, 0));
    // The hero's own fade keeps its opacity and only loses its tint.
    dark.addColor("color/hero_scrim", nvgRGBA(0, 0, 0, 216));

    if (!this->getItem(AMOLED_SURFACES, false)) return;
    for (const char* name : {"color/surface", "color/grey_1", "brls/dropdown/background"}) dark.addColor(name, black);
}

void AppConfig::applyThemeVariant(brls::ThemeVariant tv, const plenx::ThemePalette& p) {
    auto& theme = (tv == brls::ThemeVariant::LIGHT) ? brls::Theme::getLightTheme() : brls::Theme::getDarkTheme();
    const NVGcolor accent = nvgRGB(p.accent.r, p.accent.g, p.accent.b);
    const NVGcolor glow = nvgRGB(p.accentGlowTop.r, p.accentGlowTop.g, p.accentGlowTop.b);
    const NVGcolor onAccent = nvgRGB(p.onAccentText.r, p.onAccentText.g, p.onAccentText.b);
    const NVGcolor listValue = nvgRGB(p.listValue.r, p.listValue.g, p.listValue.b);

    // VARIANT tokens (the per-backend accent surface).
    theme.addColor("brls/accent", accent);
    // The focus ring is its own colour in the reference, NOT the accent fill:
    // every ThemeColorPalette carries `secondary` for fills and a separate
    // `focusRing` one step lighter for the ring (White: neutral100 vs pure
    // white; Ocean: blue500 vs blue300; Emerald: green500 vs green300). Our
    // accentGlowTop is already that lighter step for every palette, so the ring
    // routes to it. BOTH stops: the reference's focusRingGradient defaults to
    // listOf(focusRing), i.e. a flat ring, and color2 is only the sheen
    // borealis lays over color1.
    theme.addColor("brls/highlight/color1", glow);
    theme.addColor("brls/highlight/color2", glow);
    theme.addColor("brls/sidebar/active_item", accent);
    theme.addColor("brls/button/primary_enabled_background", accent);
    theme.addColor("brls/button/primary_enabled_text", onAccent);
    theme.addColor("brls/button/highlight_enabled_text", accent);
    theme.addColor("brls/button/highlight_disabled_text", accent);
    theme.addColor("brls/list/listItem_value_color", listValue);
    theme.addColor("brls/slider/line_filled", accent);

    // app tokens routed through AppConfig::addColor() so a user color override in
    // the settings JSON still wins (addColor reads `setting` before this default).
    this->addColor(tv, "color/app", accent);
    this->addColor(tv, "color/focus/bg", nvgRGBA(p.accent.r, p.accent.g, p.accent.b, 115));
}

void AppConfig::initThemes() {
    // "Dark theater" identity (UI_REDESIGN.md §3): neutral dark chrome. Only the
    // STRUCTURAL tokens are set here (constant across themes). The accent surface
    // (accent, highlight, sidebar active, primary button, slider, color/app...)
    // is VARIANT: set by applyTheme() per connected backend — see the
    // applyTheme(std::nullopt) call at the end of this function and the hooks in
    // checkLogin()/addUser()/ServerList. Theme::addColor overrides the borealis
    // values (theme.cpp), no submodule patch needed.
    // Background/surface/grey values below are snapped to NuvioMedia/NuvioTV's
    // own published dark-theme tokens (PrimitiveTokens.kt/ThemeColorPalette
    // defaults) rather than invented — chosen because those are the reference
    // the "look like Nuvio" reskin is matching against.
    auto& dark = brls::Theme::getDarkTheme();
    // "brls/clear" is the one that actually matters: it is the GL clear colour,
    // i.e. what every pixel not covered by a view shows. Setting only
    // "brls/background" (as the first reskin pass did) changes nothing you can
    // see — borealis' own dark default of 45,45,45 kept painting the screen.
    // Verified by probing the running app, not by reading the code.
    dark.addColor("brls/clear", nvgRGB(13, 13, 13));                 // Nuvio neutral950
    dark.addColor("brls/background", nvgRGB(13, 13, 13));            // Nuvio neutral950
    dark.addColor("brls/sidebar/background", nvgRGB(13, 13, 13));    // flush with background: no distinct rail panel, matching Nuvio's floating icon rail
    dark.addColor("brls/highlight/background", nvgRGB(48, 48, 48));  // Nuvio White theme's focusBackground
    // press pulse: near-transparent light gray (orange suggested a
    // selection, not a press)
    dark.addColor("brls/click_pulse", nvgRGBA(255, 255, 255, 24));
    // brls:Header draws a 1px line under each section title:
    // an artifact in our design (typography is enough)
    dark.addColor("brls/header/border", nvgRGBA(0, 0, 0, 0));
    // even at rectangle_width 0, the nanovg antialiasing fringe of the
    // decorative rectangle leaves a ~1px line: transparency neutralizes it
    dark.addColor("brls/header/rectangle", nvgRGBA(0, 0, 0, 0));
    // pill toast (brls::Application::notify): translucent dark surface
    // slightly above the #0D0E11 background, white text
    dark.addColor("brls/notification/background", nvgRGBA(24, 26, 31, 235));
    dark.addColor("brls/notification/text", nvgRGB(255, 255, 255));

    auto& light = brls::Theme::getLightTheme();
    light.addColor("brls/click_pulse", nvgRGBA(0, 0, 0, 20));
    light.addColor("brls/header/border", nvgRGBA(0, 0, 0, 0));
    light.addColor("brls/header/rectangle", nvgRGBA(0, 0, 0, 0));
    // pill toast: conventional dark pill, readable on a light background
    light.addColor("brls/notification/background", nvgRGBA(45, 45, 45, 230));
    light.addColor("brls/notification/text", nvgRGB(255, 255, 255));

    // color/app (the app accent token) and color/focus/bg (translucent focus
    // background for player OSD controls) are VARIANT: set by applyThemeVariant().
    // dark scrim behind elements placed over an image (bars, badges)
    this->addColor(brls::ThemeVariant::LIGHT, "color/scrim", nvgRGBA(0, 0, 0, 160));
    this->addColor(brls::ThemeVariant::DARK, "color/scrim", nvgRGBA(0, 0, 0, 160));
    // metadata pills (detail pages)
    this->addColor(brls::ThemeVariant::LIGHT, "color/pill", nvgRGBA(0, 0, 0, 18));
    this->addColor(brls::ThemeVariant::DARK, "color/pill", nvgRGBA(255, 255, 255, 22));
    // surfaces placed over the background (content cards, PIN code panel...)
    // Nav rail. Nuvio's sidebar is a floating icon rail over the page, not a
    // panel in a different shade, so this is flush with the background rather
    // than the elevated grey_1 it used to borrow (grey_1 is also the card
    // placeholder fill, so it needed its own token to move independently).
    // Home hero veil. The focused item's backdrop fills the screen behind the
    // rows, so it has to be knocked back far enough for row titles and card
    // text to stay readable over any artwork — NuvioTV runs it very dark.
    this->addColor(brls::ThemeVariant::LIGHT, "color/hero_scrim", nvgRGBA(235, 235, 235, 224));
    this->addColor(brls::ThemeVariant::DARK, "color/hero_scrim", nvgRGBA(13, 13, 13, 216));
    this->addColor(brls::ThemeVariant::LIGHT, "color/nav_bg", nvgRGB(235, 235, 235));
    this->addColor(brls::ThemeVariant::DARK, "color/nav_bg", nvgRGB(13, 13, 13));
    // Idle glyph colour, swapped into the icon set at load (SVGImage). The
    // assets bake #61666D, a dark blue-grey from the Plex-era set that all but
    // disappears against a 13,13,13 rail; NuvioTV's nav icons sit near its
    // textSecondary, plainly visible but clearly not the active one.
    this->addColor(brls::ThemeVariant::LIGHT, "color/icon_idle", nvgRGB(0x61, 0x66, 0x6D));
    this->addColor(brls::ThemeVariant::DARK, "color/icon_idle", nvgRGB(0x9A, 0x9A, 0x9A));
    this->addColor(brls::ThemeVariant::LIGHT, "color/surface", nvgRGB(255, 255, 255));
    this->addColor(brls::ThemeVariant::DARK, "color/surface", nvgRGB(36, 36, 36));  // Nuvio backgroundCard
    // banner fade towards the background (transparent -> background color)
    this->addColor(brls::ThemeVariant::LIGHT, "color/fade_0", nvgRGBA(235, 235, 235, 0));
    this->addColor(brls::ThemeVariant::LIGHT, "color/fade_1", nvgRGB(235, 235, 235));
    this->addColor(brls::ThemeVariant::DARK, "color/fade_0", nvgRGBA(13, 13, 13, 0));
    this->addColor(brls::ThemeVariant::DARK, "color/fade_1", nvgRGB(13, 13, 13));
    // 用于骨架屏背景色
    this->addColor(brls::ThemeVariant::LIGHT, "color/grey_1", nvgRGB(245, 246, 247));
    this->addColor(brls::ThemeVariant::DARK, "color/grey_1", nvgRGB(26, 26, 26));  // Nuvio backgroundElevated
    this->addColor(brls::ThemeVariant::LIGHT, "color/grey_2", nvgRGB(245, 245, 245));
    this->addColor(brls::ThemeVariant::DARK, "color/grey_2", nvgRGB(51, 51, 51));  // Nuvio neutral750
    this->addColor(brls::ThemeVariant::LIGHT, "color/grey_3", nvgRGBA(200, 200, 200, 16));
    this->addColor(brls::ThemeVariant::DARK, "color/grey_3", nvgRGBA(160, 160, 160, 160));
    this->addColor(brls::ThemeVariant::LIGHT, "color/danger", nvgRGB(198, 28, 28));
    this->addColor(brls::ThemeVariant::DARK, "color/danger", nvgRGBA(198, 28, 28, 180));
    this->addColor(brls::ThemeVariant::LIGHT, "color/white", nvgRGB(255, 255, 255));
    this->addColor(brls::ThemeVariant::DARK, "color/white", nvgRGBA(255, 255, 255, 180));
    // 分割线颜色
    this->addColor(brls::ThemeVariant::LIGHT, "color/line", nvgRGB(208, 208, 208));
    this->addColor(brls::ThemeVariant::DARK, "color/line", nvgRGB(100, 100, 100));
    // 深浅配色通用的灰色字体颜色
    this->addColor(brls::ThemeVariant::LIGHT, "font/grey", nvgRGB(148, 153, 160));
    this->addColor(brls::ThemeVariant::DARK, "font/grey", nvgRGB(179, 179, 179));  // Nuvio textSecondary (neutral400)
    // One step dimmer again — Nuvio textTertiary (neutral600), what it sets the
    // "from <addon>" line under a row title in.
    this->addColor(brls::ThemeVariant::LIGHT, "font/tertiary", nvgRGB(168, 173, 180));
    this->addColor(brls::ThemeVariant::DARK, "font/tertiary", nvgRGB(128, 128, 128));
    // A focused pill in a long-press sheet INVERTS rather than picking up a
    // ring: tv-material3's Button defaults its focused colours to onSurface on
    // inverseOnSurface, which in a dark theme is white text-on-panel becoming
    // dark text on white. Not accent-tinted — the reference's is not.
    this->addColor(brls::ThemeVariant::LIGHT, "color/sheet/focus_bg", nvgRGB(26, 26, 26));
    this->addColor(brls::ThemeVariant::LIGHT, "color/sheet/focus_fg", nvgRGB(255, 255, 255));
    this->addColor(brls::ThemeVariant::DARK, "color/sheet/focus_bg", nvgRGB(255, 255, 255));
    this->addColor(brls::ThemeVariant::DARK, "color/sheet/focus_fg", nvgRGB(26, 26, 26));

    // establish the neutral pleNx DEFAULT accent for all pre-connection screens;
    // checkLogin()/addUser() re-apply the connected backend's palette afterwards.
    this->applyTheme(std::nullopt);

    // Card label block + row gap, as every panel below 1080p has always had
    // them; the 1080p branch replaces them with the reference's own.
    brls::getStyle().addMetric("app/card/label/top", 10);
    brls::getStyle().addMetric("app/card/label/title_size", 18);
    brls::getStyle().addMetric("app/card/label/title_height", 25);
    brls::getStyle().addMetric("app/card/label/sub_size", 12);
    brls::getStyle().addMetric("app/card/label/sub_height", 20);
    brls::getStyle().addMetric("app/card/labels", 55);
    brls::getStyle().addMetric("app/card/item_space", 18);
    brls::getStyle().addMetric("app/card/corner_radius", 12);

    if (brls::Application::ORIGINAL_WINDOW_HEIGHT == 544) {
        brls::getStyle().addMetric("app/album/height", 215);
        brls::getStyle().addMetric("app/books/height", 270);
        brls::getStyle().addMetric("app/video/height", 290);
        // row = width x image ratio (poster 2:3 = 1.5, wide 16:9 = 0.5625)
        // + 55 of label area (margin 10 + title 25 + subtitle 20), so the
        // image fill keeps exactly the media's ratio
        brls::getStyle().addMetric("app/card/poster/width", 150);
        brls::getStyle().addMetric("app/card/poster/row", 280);
        brls::getStyle().addMetric("app/card/wide/width", 280);
        brls::getStyle().addMetric("app/card/wide/row", 213);
        brls::getStyle().addMetric("app/grid/6", 5);
        brls::getStyle().addMetric("app/grid/5", 4);
        brls::getStyle().addMetric("app/grid/4", 3);
        brls::getStyle().addMetric("app/grid/3", 2);
        brls::getStyle().addMetric("app/grid/2", 1);
        brls::getStyle().addMetric("brls/tab_frame/content_padding_sides", 30);
        brls::getStyle().addMetric("main/content_padding_sides", 15);
        brls::getStyle().addMetric("main/detail_padding_sides", 24);
        brls::getStyle().addMetric("main/content_padding_top_bottom", 20);
        // the 1080p rail scaled to this panel (a fixed 144 would eat a quarter
        // of a 960-wide screen)
        brls::getStyle().addMetric("main/sidebar/width", 72);
        brls::getStyle().addMetric("main/sidebar/icon", 20);
        brls::getStyle().addMetric("main/sidebar/item_size", 36);
        brls::getStyle().addMetric("main/sidebar/item_spacing", 26);
    } else {
        // Grids lightened by one column compared to Switchfin: bigger
        // posters, readable from the couch (UI_REDESIGN.md §4).
        switch (brls::Application::ORIGINAL_WINDOW_HEIGHT) {
        case 1080:
            // NuvioTV writes its tokens in dp for a 960x540dp screen (its
            // 1080p density is 2.0), so every number below is its dp doubled.
            brls::getStyle().addMetric("app/album/height", 250);
            brls::getStyle().addMetric("app/books/height", 320);
            brls::getStyle().addMetric("app/video/height", 340);
            // Poster: the 126x189dp base card, scaled by the 0.84 x 1.08 the
            // reference's home applies to it (ModernHomeContent) = 114.3 x
            // 171.5dp -> 229 x 343, + the label block under it. The base is the
            // viewer's "Width" preset (Layout > Poster Card Style), which is
            // what posterCardWidthDp is there; 126 = its Balanced default.
            {
                double baseDp = (double)this->getItem(LAYOUT_POSTER_WIDTH, 126);
                int w, h;
                if (this->getItem(LAYOUT_LANDSCAPE_POSTERS, false)) {
                    // modernLandscapePostersEnabled: the reference's own other
                    // pair of factors (1.24 x 1.34) and its 1.77 aspect.
                    w = (int)std::lround(baseDp * 1.24 * 1.34 * 2);
                    h = (int)std::lround(w / 1.77) + 100;
                } else {
                    // ModernHomeContent's own two factors, then dp -> px.
                    w = (int)std::lround(baseDp * 0.84 * 1.08 * 2);
                    // 189/126 = 1.5, the card's aspect, + the 100 label block.
                    h = (int)std::lround(w * 1.5) + 100;
                }
                brls::getStyle().addMetric("app/card/poster/width", w);
                brls::getStyle().addMetric("app/card/poster/row", h);
            }
            // Continue Watching tile ("card" style): 126dp x 1.24 x 1.34
            // across, 16:9. Its text prints over the artwork, so no labels.
            brls::getStyle().addMetric("app/card/wide/width", 419);
            brls::getStyle().addMetric("app/card/wide/row", 336);
            brls::getStyle().addMetric("app/card/item_space", 24);
            // posterCard radius: the viewer's "Corner Radius" preset, 12dp
            // (Rounded) by default — posterCardCornerRadiusDp, which the
            // reference likewise threads into every card's shape.
            brls::getStyle().addMetric("app/card/corner_radius", this->getItem(LAYOUT_POSTER_RADIUS, 12) * 2);
            // The label block: 16 above the title (the reference's 8dp gap),
            // a 24sp line for it, then a 12sp line + its 2dp spacer.
            brls::getStyle().addMetric("app/card/label/top", 16);
            brls::getStyle().addMetric("app/card/label/title_size", 32);
            brls::getStyle().addMetric("app/card/label/title_height", 48);
            brls::getStyle().addMetric("app/card/label/sub_size", 24);
            brls::getStyle().addMetric("app/card/label/sub_height", 36);
            brls::getStyle().addMetric("app/card/labels", 100);
            // A 229-wide poster and its 24 of gap: six across the grid puts
            // the cell within a few px of the reference's own 126dp card.
            brls::getStyle().addMetric("app/grid/6", 6);
            brls::getStyle().addMetric("app/grid/5", 5);
            brls::getStyle().addMetric("app/grid/4", 4);
            brls::getStyle().addMetric("app/grid/3", 3);
            brls::getStyle().addMetric("app/grid/2", 2);
            break;
        case 900:
            brls::getStyle().addMetric("main/sidebar/width", 120);
            brls::getStyle().addMetric("main/sidebar/icon", 30);
            brls::getStyle().addMetric("main/sidebar/item_size", 57);
            brls::getStyle().addMetric("main/sidebar/item_spacing", 45);
            brls::getStyle().addMetric("app/album/height", 240);
            brls::getStyle().addMetric("app/books/height", 305);
            brls::getStyle().addMetric("app/video/height", 325);
            brls::getStyle().addMetric("app/card/poster/width", 205);
            brls::getStyle().addMetric("app/card/poster/row", 363);
            brls::getStyle().addMetric("app/card/wide/width", 375);
            brls::getStyle().addMetric("app/card/wide/row", 266);
            brls::getStyle().addMetric("app/grid/6", 6);
            brls::getStyle().addMetric("app/grid/5", 5);
            brls::getStyle().addMetric("app/grid/4", 4);
            brls::getStyle().addMetric("app/grid/3", 3);
            brls::getStyle().addMetric("app/grid/2", 2);
            break;
        default:  // 720p
            brls::getStyle().addMetric("main/sidebar/width", 96);
            brls::getStyle().addMetric("main/sidebar/icon", 24);
            brls::getStyle().addMetric("main/sidebar/item_size", 46);
            brls::getStyle().addMetric("main/sidebar/item_spacing", 36);
            brls::getStyle().addMetric("app/album/height", 225);
            brls::getStyle().addMetric("app/books/height", 280);
            brls::getStyle().addMetric("app/video/height", 300);
            // row = width x image ratio + 55 of labels (cf. PSV block)
            brls::getStyle().addMetric("app/card/poster/width", 185);
            brls::getStyle().addMetric("app/card/poster/row", 333);
            brls::getStyle().addMetric("app/card/wide/width", 340);
            brls::getStyle().addMetric("app/card/wide/row", 246);
            brls::getStyle().addMetric("app/grid/6", 5);
            brls::getStyle().addMetric("app/grid/5", 4);
            brls::getStyle().addMetric("app/grid/4", 4);
            brls::getStyle().addMetric("app/grid/3", 3);
            brls::getStyle().addMetric("app/grid/2", 2);
        }
        // 1080p: the reference starts its rows 106dp (212 px) in from the
        // screen edge, so 68 on top of the 144 rail lands them exactly there.
        // Every other panel keeps the 40 it had, its rail being narrower.
        brls::getStyle().addMetric(
            "main/content_padding_sides", brls::Application::ORIGINAL_WINDOW_HEIGHT == 1080 ? 68 : 40);
        // The detail page has no icon rail to clear (AutoTabFrame hides it
        // while one is up), so its own inset is the reference's own screen
        // margin rather than the rail's width plus a gutter: HeroSection.kt
        // pads by spacing.xxxl, 48dp, which is 96 here.
        brls::getStyle().addMetric(
            "main/detail_padding_sides", brls::Application::ORIGINAL_WINDOW_HEIGHT == 1080 ? 96 : 56);
        // 44: the reference's screens start their title there (its 24dp safe
        // area, less the leading its own label carries).
        brls::getStyle().addMetric(
            "main/content_padding_top_bottom", brls::Application::ORIGINAL_WINDOW_HEIGHT == 1080 ? 44 : 30);
    }

    // UI redesign (UI_REDESIGN.md §3.2-3.3): bare and larger section titles
    // (the decorative bar of brls::Header disappears), rounded focus halo
    // matching the cards.
    brls::getStyle().addMetric("brls/header/rectangle_width", 0);
    brls::getStyle().addMetric("brls/header/rectangle_margin", 0);
    // 32 = NuvioTV's titleMedium (16sp at its 2.0 density), the size it sets
    // its row headers in. The whole app's type scale is matched to that
    // Material3 scale doubled: label 20/24/28, body 24/28/32, title 28/32/40.
    // Every settings row is a borealis Cell, whose title reads this metric.
    // NuvioTV sets a settings row in its bodyLarge (16sp): 32 here, where the
    // library's own default of 22 left the whole of Settings a size below
    // everything around it.
    brls::getStyle().addMetric("brls/sidebar/item_font_size", 32);
    brls::getStyle().addMetric("brls/header/font_size", 32);
    // Borealis pads a Header 11 above and below its text. The reference pads
    // nothing: the gap under a section title is the title's own margin, so
    // every one of ours would sit 22 too tall with the library default.
    brls::getStyle().addMetric("brls/header/padding_top_bottom", 0);
    // Line height. Borealis ships 1.65, which is far looser than the reference:
    // NuvioTV's Material3 styles pair 14sp text with a 20sp line (1.43), 16/24
    // (1.5), 12/16 (1.33) — clustered around 1.43, never 1.65. Everything in
    // the app reads a shade tighter now, matching it.
    brls::getStyle().addMetric("brls/label/default_line_height", 1.43f);
    // Nav rail, 1080p only -- every other panel sized its own above and this
    // block used to overwrite it.
    brls::getStyle().addMetric("main/sidebar/item_indent", 0);
    if (brls::Application::ORIGINAL_WINDOW_HEIGHT == 1080) {
        brls::getStyle().addMetric("main/sidebar/width", 144);
        brls::getStyle().addMetric("main/sidebar/icon", 36);
        // 68 = the reference's 34dp "leading visual" slot; the gap that follows
        // brings item centres ~123px apart, as measured off its rail.
        brls::getStyle().addMetric("main/sidebar/item_size", 68);
        brls::getStyle().addMetric("main/sidebar/item_spacing", 55);
        // The reference's rail overlays the content and offsets its items by
        // 12dp inside a 72dp column, which puts the glyphs' centre 48dp
        // (96 px) from the screen edge. Ours is a real 144 column, so the
        // items are indented instead of centred to land in the same place.
        brls::getStyle().addMetric("main/sidebar/item_indent", 62);
    }
    brls::getStyle().addMetric("brls/highlight/stroke_width", 4);
    // The halo is drawn ~5 px outside the frame, so its arc has to be wider
    // than the posters' own cornerRadius to hug it. It TRACKS that radius
    // rather than being fixed at the 12dp default's 28: the reference shapes a
    // card's focused border with the very same posterCardStyle.cornerRadius it
    // shapes the card with, so picking Sharp and keeping a rounded ring around
    // a square poster is its own kind of wrong. Squared off stays squared off.
    {
        int cardRadius = (int)brls::getStyle().getMetric("app/card/corner_radius");
        brls::getStyle().addMetric("brls/highlight/corner_radius", cardRadius > 0 ? cardRadius + 4 : 0);
    }

    // Every */row metric above is "poster height + kCardLabelHeight of title
    // block". With the labels off (video_card.xml collapses the block, see
    // BaseCardCell::applyPosterLabels) that allowance would be an empty gap
    // under each poster, so take it back off the row.
    if (!this->getItem(POSTER_LABELS, false)) {
        float labels = brls::getStyle()["app/card/labels"];
        for (const char* m : {"app/card/poster/row", "app/card/wide/row"}) {
            float v = brls::getStyle()[m];
            if (v > labels) brls::getStyle().addMetric(m, v - labels);
        }
    }
}