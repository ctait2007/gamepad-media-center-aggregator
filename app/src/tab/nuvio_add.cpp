/*
    GMCA — sign-in to Nuvio (see tab/nuvio_add.hpp).
    Discovery + Supabase password sign-in, then profile pick (Plex Home
    dropdown pattern, PIN via IME for a locked profile).

    Presented as a content view of the ServerList AppletFrame (View::present
    from ServerTypeChoose), so it keeps the footer; Back is governed by the
    frame's hints/back action (no own BUTTON_B handler — that would override it
    and popActivity the whole ServerList).
*/

#include "tab/nuvio_add.hpp"
#include "activity/main_activity.hpp"
#include "api/nuvio/auth.hpp"
#include "utils/config.hpp"
#include "utils/dialog.hpp"

using namespace brls::literals;

static std::string trim(const std::string& in) {
    auto a = in.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    auto b = in.find_last_not_of(" \t\r\n");
    return in.substr(a, b - a + 1);
}

NuvioAdd::NuvioAdd() {
    this->inflateFromXMLRes("xml/view/nuvio_add.xml");
    brls::Logger::debug("NuvioAdd: create");

    this->header->setText("main/server/nuvio/title"_i18n);

    this->cellUrl->init(
        "main/server/nuvio/url"_i18n, "https://api.nuvio.tv", [](std::string) {}, "https://api.nuvio.tv", "", 256);
    this->cellEmail->init("main/server/nuvio/email"_i18n, "", [](std::string) {}, "", "", 128);
    this->cellPasswd->init("main/server/nuvio/password"_i18n, "", [](std::string) {}, "", "", 128);
    this->cellPasswd->setType(brls::InputCellType::PASSWORD);

    this->btnConnect->registerClickAction([this](...) {
        this->submit();
        return true;
    });
    // No BUTTON_B handler here: Back is owned by the ServerList AppletFrame's
    // hints/back action, which dismiss()es this content view back to the type
    // chooser. A local handler would be found first (it sits below the frame)
    // and popActivity the whole ServerList instead.
}

NuvioAdd::~NuvioAdd() { brls::Logger::debug("NuvioAdd: delete"); }

brls::View* NuvioAdd::getDefaultFocus() { return this->cellUrl; }

void NuvioAdd::submit() {
    std::string url = trim(this->cellUrl->getValue());
    while (!url.empty() && url.back() == '/') url.pop_back();
    if (url.empty() || (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0)) {
        this->labelStatus->setText("main/setting/server/invalid"_i18n);
        return;
    }

    std::string email = trim(this->cellEmail->getValue());
    std::string pass = this->cellPasswd->getValue();
    if (email.empty() || pass.empty()) {
        this->labelStatus->setText("main/server/nuvio/invalid"_i18n);
        return;
    }

    this->labelStatus->setText("main/plex/connecting"_i18n);
    this->btnConnect->setVisibility(brls::Visibility::GONE);
    this->spinner->setVisibility(brls::Visibility::VISIBLE);
    brls::Application::blockInputs();

    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, url, email, pass]() {
        std::string error;
        bool ok = false;
        nuvio::Discovery disc;
        nuvio::Session session;
        std::vector<nuvio::Profile> profiles;
        try {
            disc = nuvio::discover(url);
            session = nuvio::signIn(disc.backendUrl, disc.publishableKey, email, pass);
            profiles = nuvio::getProfiles(disc.backendUrl, disc.publishableKey, session.accessToken);
            ok = true;
        } catch (const std::exception& ex) {
            error = ex.what();
        }
        brls::sync([ASYNC_TOKEN, disc, session, profiles, ok, error]() {
            ASYNC_RELEASE
            brls::Application::unblockInputs();
            this->spinner->setVisibility(brls::Visibility::GONE);
            this->btnConnect->setVisibility(brls::Visibility::VISIBLE);
            if (!ok) {
                // Supabase's password grant answers a bad email/password with
                // 400 or 401 — surface a friendlier message than the raw
                // "http status NNN" for that specific, common case.
                bool badCredentials = error.find("400") != std::string::npos || error.find("401") != std::string::npos;
                this->labelStatus->setText(badCredentials ? "main/server/nuvio/bad_credentials"_i18n : error);
                return;
            }
            if (profiles.empty()) {
                this->labelStatus->setText("main/server/nuvio/no_profile"_i18n);
                return;
            }
            this->onProfiles(disc, session, profiles);
        });
    });
}

void NuvioAdd::onProfiles(
    const nuvio::Discovery& disc, const nuvio::Session& session, const std::vector<nuvio::Profile>& profiles) {
    if (profiles.size() == 1) {
        this->onProfilePicked(disc, session, profiles.front());
        return;
    }

    std::vector<std::string> names;
    for (auto& p : profiles) names.push_back(p.pinEnabled ? fmt::format("{} 🔒", p.name) : p.name);
    // The profile pick must run AFTER the dropdown has popped itself: it fires
    // its callback while still on top of the stack, then pops — run from
    // dismissCb (fires in the pop completion) to keep the stack clean, exactly
    // like PlexAdd's Home profile picker.
    auto* dropdown = new brls::Dropdown(
        "main/plex/choose_profile"_i18n, names, [](int) {}, 0, [this, disc, session, profiles](int selected) {
            if (selected < 0) return;
            this->onProfilePicked(disc, session, profiles.at((size_t)selected));
        });
    brls::Application::pushActivity(new brls::Activity(dropdown));
    brls::sync([]() {
        auto stack = brls::Application::getActivitiesStack();
        if (!stack.empty()) brls::Application::giveFocus(stack.back()->getDefaultFocus());
    });
}

void NuvioAdd::onProfilePicked(const nuvio::Discovery& disc, const nuvio::Session& session, const nuvio::Profile& profile) {
    if (!profile.pinEnabled) {
        this->finish(disc, session, profile);
        return;
    }

    brls::Application::getImeManager()->openForText(
        [this, disc, session, profile](const std::string& pin) {
            brls::Application::blockInputs();
            ASYNC_RETAIN
            brls::async([ASYNC_TOKEN, disc, session, profile, pin]() {
                bool unlocked = false;
                int retryAfter = 0;
                std::string error;
                try {
                    unlocked = nuvio::verifyProfilePin(
                        disc.backendUrl, disc.publishableKey, session.accessToken, profile.index, pin, retryAfter);
                } catch (const std::exception& ex) {
                    error = ex.what();
                }
                brls::sync([ASYNC_TOKEN, disc, session, profile, unlocked, error]() {
                    ASYNC_RELEASE
                    brls::Application::unblockInputs();
                    if (unlocked)
                        this->finish(disc, session, profile);
                    else
                        Dialog::show(!error.empty() ? error : "main/server/nuvio/wrong_pin"_i18n);
                });
            });
        },
        "main/plex/profile_pin"_i18n, "", 4, "");
}

void NuvioAdd::finish(const nuvio::Discovery& disc, const nuvio::Session& session, const nuvio::Profile& profile) {
    AppServer s;
    s.type = "nuvio";
    s.id = session.userId.empty() ? disc.backendUrl : session.userId;
    s.name = "Nuvio";
    s.access_token = session.accessToken;
    s.urls = {disc.backendUrl};
    s.nuvio_refresh_token = session.refreshToken;
    s.nuvio_publishable_key = disc.publishableKey;
    s.nuvio_expires_at = session.expiresAt;
    AppConfig::instance().addServer(s);

    AppUser u;
    u.id = fmt::format("{}#{}", s.id, profile.index);
    u.name = profile.name.empty() ? fmt::format("Profile {}", profile.index) : profile.name;
    u.access_token = session.accessToken;
    u.server_id = s.id;
    u.thumb = profile.avatarUrl;
    u.nuvio_profile_index = profile.index;
    AppConfig::instance().addUser(u, disc.backendUrl);

    brls::Application::clear();
    brls::Application::pushActivity(new MainActivity(), brls::TransitionAnimation::NONE);
}
