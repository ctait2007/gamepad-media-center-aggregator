#include "activity/loading_activity.hpp"
#include "utils/config.hpp"

LoadingActivity::LoadingActivity() { brls::Logger::debug("LoadingActivity: create"); }

void LoadingActivity::onContentAvailable() {
    // startupSplashEnabled. The reference calls this "show logo and loading
    // indicator", and that is all that goes: the screen itself stays, because
    // here it is what covers the server probe and holds input off while it
    // runs, rather than decoration over an already-built UI.
    if (AppConfig::instance().getItem(AppConfig::STARTUP_SPLASH, true)) return;
    if (brls::View* splash = this->getView("loading/splash"))
        splash->setVisibility(brls::Visibility::INVISIBLE);
}

LoadingActivity::~LoadingActivity() { brls::Logger::debug("LoadingActivity: delete"); }
