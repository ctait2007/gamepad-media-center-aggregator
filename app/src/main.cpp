#include <borealis.hpp>

#include "utils/config.hpp"
#include "utils/download.hpp"
#include "utils/offline_library.hpp"
#include "utils/image_cache.hpp"
#include "utils/network_state.hpp"
#include "utils/thread.hpp"
#include "api/http.hpp"

#include "view/svg_image.hpp"
#include "view/disclosure_cell.hpp"
#include "view/icon_button.hpp"
#include "view/context_menu.hpp"
#include "view/custom_button.hpp"
#include "view/auto_tab_frame.hpp"
#include "view/recycling_grid.hpp"
#include "view/h_recycling.hpp"
#include "view/recyling_video.hpp"
#include "view/video_progress_slider.hpp"
#include "view/gallery_view.hpp"
#include "view/search_list.hpp"
#include "view/video_view.hpp"
#include "view/selector_cell.hpp"
#include "view/button_close.hpp"
#include "view/text_box.hpp"
#include "view/mpv_core.hpp"

#include "activity/main_activity.hpp"
#include "activity/server_list.hpp"
#include "activity/hint_activity.hpp"
#include "activity/loading_activity.hpp"
#include "tab/home_tab.hpp"
#include "tab/search_tab.hpp"
#include "tab/remote_tab.hpp"
#include "tab/remote_view.hpp"
#include "tab/setting_tab.hpp"
#include "tab/playlists_tab.hpp"
#include "tab/watchlist_tab.hpp"

#if defined(__SDL2__)
#include <SDL2/SDL_main.h>
#endif

using namespace brls::literals;  // for _i18n

#if defined(__SWITCH__) && defined(BUILTIN_NSP)
#include <switch.h>

/// Is the HOME tile (forwarder NSP, title id FORWARDER_TITLEID =
/// PROJECT_TITLEID from CMakeLists.txt) already installed? Best effort: if
/// the ns service fails we answer "no" (the prompt is only shown once
/// anyway, cf. AppConfig::HINT_FORWARDER).
static bool isForwarderInstalled() {
    if (R_FAILED(nsInitialize())) return false;
    bool found = false;
    NsApplicationRecord record;
    s32 count = 0;
    for (s32 offset = 0; R_SUCCEEDED(nsListApplicationRecord(&record, 1, offset, &count)) && count > 0; offset++) {
        if (record.application_id == FORWARDER_TITLEID) {
            found = true;
            break;
        }
    }
    nsExit();
    return found;
}

/// First launch in application mode: offers to install the HOME tile
/// (launched from the tile itself or tile already there -> ns sees it -> nothing).
static void proposeForwarderInstall() {
    if (isForwarderInstalled()) return;  // a GMCA HOME tile is already present

    auto& conf = AppConfig::instance();
    // One-time GMCA-era nudge, keyed separately from HINT_FORWARDER on purpose:
    // pleNx users who self-updated to GMCA are past the pleNx-era HINT_FORWARDER
    // gate yet have no GMCA tile (fresh title id), so re-offer it exactly once.
    if (conf.getItem(AppConfig::HINT_FORWARDER_GMCA, false)) return;
    conf.setItem(AppConfig::HINT_FORWARDER_GMCA, true);
    conf.setItem(AppConfig::HINT_FORWARDER, true);

    auto dialog = new brls::Dialog("main/hints/prompt"_i18n);
    dialog->addButton("hints/cancel"_i18n, []() {});
    dialog->addButton("hints/ok"_i18n, []() { brls::Application::pushActivity(new HintActivity()); });
    dialog->open();
}
#endif

int main(int argc, char* argv[]) {
#ifdef __SWITCH__
    if (argc > 0 && argv[0]) AppVersion::nro_path = argv[0];
#endif
    std::vector<std::string> items;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-d") == 0) {
            brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
        } else if (std::strcmp(argv[i], "-v") == 0) {
            brls::Application::enableDebuggingView(true);
        } else if (std::strcmp(argv[i], "-t") == 0) {
            MPVCore::DEBUG = true;
        } else if (std::strcmp(argv[i], "-o") == 0) {
            const char* path = (i + 1 < argc) ? argv[++i] : "gmca.log";
            FILE* logFile = std::fopen(path, "w+");
            // line-buffered: without this the last ~16 KB of logs (including
            // the line preceding a crash) stay in the buffer and die with
            // the process — unbearable for diagnosing
            if (logFile) std::setvbuf(logFile, nullptr, _IOLBF, 0);
            brls::Logger::setLogOutput(logFile);
        } else if (std::strcmp(argv[i], "-version") == 0) {
            brls::Logger::info("{} {}", AppVersion::getDeviceName(), AppVersion::getCommit());
            return 0;
        } else {
            items.push_back(argv[i]);
        }
    }

    std::setlocale(LC_ALL, "C.UTF-8");
    // Load cookies and settings
    auto& conf = AppConfig::instance();
    if (!conf.init()) {
        return 0;
    }

    // Debug logging to a FILE, from the very first line of this launch (the
    // startup path is exactly where the interesting failures are). Consoles
    // give the user no way to read stdout — PS4 sends it to klog over the
    // network — so a log on disk next to the config is the only thing a user
    // can actually retrieve and attach to a report.
    //
    // ALWAYS ON, deliberately: it used to sit behind the Settings "debug"
    // toggle, which meant the launches worth diagnosing — the ones that die
    // before you can reach Settings, or on a fresh install — were exactly the
    // ones that produced no log. The cost is a few hundred KB of text; the
    // benefit is that every crash report already has the evidence in it.
    {
        std::string logPath = conf.configDir() + "/gmca.log";
        // "w" truncates: the file holds THIS launch only, so a user who
        // reproduces a bug and grabs the log can't accidentally send a
        // previous session's, and it can't grow without bound.
        if (FILE* logFile = std::fopen(logPath.c_str(), "w")) {
            std::setvbuf(logFile, nullptr, _IOLBF, 0);  // survive a crash, see -o above
            brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
            brls::Logger::setThreadSafeLogging(true);   // backend verbs log from worker threads
            // Subscribe to the log EVENT rather than Logger::setLogOutput():
            // on PS4 (and PSV/Android) borealis writes straight to the platform
            // debug channel — sceKernelDebugOutText — and never touches the
            // FILE* that setLogOutput installs, so a log file opened that way
            // stays empty forever. The event fires on every platform.
            brls::Logger::getLogEvent()->subscribe(
                [logFile](brls::Logger::TimePoint, brls::LogLevel level, const std::string& msg) {
                    static const char* names[] = {"ERROR", "WARNING", "INFO", "DEBUG", "VERBOSE"};
                    int i = (int)level;
                    std::fprintf(logFile, "[%s] %s\n", (i >= 0 && i < 5) ? names[i] : "?", msg.c_str());
                });
            brls::Logger::info("GMCA {} on {} — log started (request_threads={})", AppVersion::getVersion(),
                AppVersion::getPlatform(), ThreadPool::max_thread_num);
        }
    }

    // libcurl's one-time global setup, forced onto the main thread while it is
    // still the only thread. Left to happen lazily it lands in whichever HTTP
    // constructor runs first, which — once Settings > Network > Threads is
    // above 1 — is N pool workers arriving at once, and curl_global_init must
    // not run while other threads do.
    HTTP::globalInit();

    // Init the app and i18n
    if (!brls::Application::init()) {
        brls::Logger::error("Unable to init application");
        return EXIT_FAILURE;
    }

    conf.initThemes();

    // Scroll indicator (scrollbar) visibility — global, driven by config
    // ("scrollbar", default true). Off = clean captures / a quieter chrome.
    brls::ScrollingFrame::setScrollingIndicatorVisibleGlobal(conf.getItem(AppConfig::SCROLLBAR, true));

    // Screenshot/automation harness (GMCA_NAV_PIPE input hook): keep the render
    // + input loop at full speed even while unfocused, so background navigation
    // and captures stay in sync (the default 5 FPS idle throttle desyncs them).
    if (std::getenv("GMCA_NAV_PIPE")) brls::Application::setDeactivatedFPS(60);

    ImageCache::init();
    DownloadManager::instance().init();
    OfflineLibrary::instance().init();

    // Return directly to the desktop when closing the application (only for NX)
    brls::Application::getPlatform()->exitToHomeMode(true);

    brls::Application::createWindow(fmt::format("{} for {}", AppVersion::getPackageName(), AppVersion::getPlatform()));

    // Have the application register an action on every activity that will quit when you press BUTTON_START
    brls::Application::setGlobalQuit(false);

    // D-pad auto-repeat, at NuvioTV's own gates (DpadThrottleModifier): a row
    // repeats faster than a column, and "fast horizontal navigation" takes the
    // row faster still. borealis had one flat 100 ms for every direction, which
    // made a row of posters feel slower than the reference and a settings
    // column faster.
    brls::Application::setRepeatDelay(
        conf.getItem(AppConfig::FAST_HORIZONTAL_NAV, false) ? 48000 : 80000, 112000);

    // Register custom views (including tabs, which are views)
    brls::Application::registerXMLView("SVGImage", SVGImage::create);
    brls::Application::registerXMLView("DisclosureCell", DisclosureCell::create);
    brls::Application::registerXMLView("IconButton", IconButton::create);
    brls::Application::registerXMLView("MenuItem", MenuItem::create);
    brls::Application::registerXMLView("CustomButton", CustomButton::create);
    brls::Application::registerXMLView("SelectorCell", SelectorCell::create);
    brls::Application::registerXMLView("TextBox", TextBox::create);
    brls::Application::registerXMLView("ButtonClose", ButtonClose::create);
    brls::Application::registerXMLView("AutoTabFrame", AutoTabFrame::create);
    brls::Application::registerXMLView("MainTabFrame", MainTabFrame::create);
    brls::Application::registerXMLView("RecyclingGrid", RecyclingGrid::create);
    brls::Application::registerXMLView("HRecyclerFrame", HRecyclerFrame::create);
    brls::Application::registerXMLView("RecylingVideo", RecylingVideo::create);
    brls::Application::registerXMLView("GalleryView", GalleryView::create);
    brls::Application::registerXMLView("SearchList", SearchList::create);
    brls::Application::registerXMLView("VideoProgressSlider", VideoProgressSlider::create);
    brls::Application::registerXMLView("PlayerButton", PlayerButton::create);

    brls::Application::registerXMLView("HomeTab", HomeTab::create);
    brls::Application::registerXMLView("SearchTab", SearchTab::create);
    brls::Application::registerXMLView("RemoteTab", RemoteTab::create);
    brls::Application::registerXMLView("SettingTab", SettingTab::create);
    brls::Application::registerXMLView("PlaylistsTab", PlaylistsTab::create);
    brls::Application::registerXMLView("WatchlistTab", WatchlistTab::create);

    if (!brls::Application::getPlatform()->isApplicationMode()) {
        brls::Application::pushActivity(new HintActivity());
    } else if (items.size() > 0) {
        RemoteView::play(items.front());
    } else {
        // checkLogin() probes the remembered URLs of the active server
        // (config.cpp:checkLogin, now raced in parallel): called here on the
        // main thread it froze the very first frame for several seconds
        // (changed network, server off...). Show the loading screen and probe
        // in the background.
        brls::Application::pushActivity(new LoadingActivity(), brls::TransitionAnimation::NONE);
        brls::Application::blockInputs();
        brls::async([]() {
            const bool logged = AppConfig::instance().checkLogin();
            brls::sync([logged]() {
                brls::Application::unblockInputs();
                brls::Application::clear();
                if (logged) {
                    brls::Application::pushActivity(new MainActivity(), brls::TransitionAnimation::NONE);
#if defined(__SWITCH__) && defined(BUILTIN_NSP)
                    proposeForwarderInstall();
#endif
                } else if (!OfflineLibrary::instance().empty() ||
                           !AppConfig::instance().getServers().empty()) {
                    // a server is remembered (just unreachable) and/or downloads
                    // exist: enter the offline shell (browse downloads + a Retry
                    // to reconnect) instead of the server picker (SPEC §4.4).
                    // A fresh install with no server still goes to ServerList.
                    NetworkState::setOffline(true);
                    brls::Application::pushActivity(new MainActivity(), brls::TransitionAnimation::NONE);
                } else {
                    brls::Application::pushActivity(new ServerList(), brls::TransitionAnimation::NONE);
                }
            });
        });
    }

    std::string v = conf.getItem(AppConfig::APP_UPDATE, std::string("NaN"));
    if (AppVersion::getVersion().compare(v)) AppVersion::checkUpdate();

    // Run the app
    while (brls::Application::mainLoop());

    ThreadPool::instance().stop();

    conf.checkRestart(argv);
    // Exit
    return EXIT_SUCCESS;
}
