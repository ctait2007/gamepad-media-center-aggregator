/*
    GMCA — Nuvio account authentication (see nuvio/auth.hpp).

    Verified against NuvioMedia/self-host (database/migrations,
    docs/client-configuration.md) and the official backend's own
    /.well-known/nuvio document (fetched live, 2026-09-11):
      - discovery: GET {url}/.well-known/nuvio -> {backend_url,
        publishable_key, capabilities:{email_password_auth, tv_login}}.
      - auth: POST {url}/auth/v1/token?grant_type=password|refresh_token,
        header apikey: <publishable_key>, body {email,password} or
        {refresh_token} -> {access_token, refresh_token, expires_in, user}.
      - profiles: POST {url}/rest/v1/rpc/sync_pull_profiles (Authorization:
        Bearer <access_token>) -> array of {profile_index, name, avatar_url,
        pin_enabled, pin_locked_until, ...}.
      - PIN: POST {url}/rest/v1/rpc/verify_profile_pin
        {p_profile_id, p_pin} -> [{unlocked, retry_after_seconds}].
*/

#include "api/nuvio/auth.hpp"
#include "api/http.hpp"
#include "api/media/types.hpp"
#include <chrono>
#include <stdexcept>

namespace nuvio {

using media::jbool;
using media::jint;
using media::jstr;

namespace {

constexpr long kTimeout = 15000L;

int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string trimSlash(const std::string& url) {
    std::string out = url;
    while (!out.empty() && out.back() == '/') out.pop_back();
    return out;
}

nlohmann::json parseJson(const std::string& resp, const char* what) {
    nlohmann::json j = nlohmann::json::parse(resp, nullptr, false);
    if (j.is_discarded()) throw std::runtime_error(std::string("Nuvio: invalid ") + what + " response");
    return j;
}

HTTP::Header publicHeaders(const std::string& publishableKey) {
    return HTTP::Header{"apikey: " + publishableKey, "Content-Type: application/json"};
}

HTTP::Header bearerHeaders(const std::string& publishableKey, const std::string& accessToken) {
    return HTTP::Header{
        "apikey: " + publishableKey, "Authorization: Bearer " + accessToken, "Content-Type: application/json"};
}

Session parseSession(const nlohmann::json& j) {
    Session s;
    s.accessToken = jstr(j, "access_token");
    s.refreshToken = jstr(j, "refresh_token");
    if (s.accessToken.empty() || s.refreshToken.empty())
        throw std::runtime_error("Nuvio: authentication returned an incomplete session");
    s.expiresAt = nowSeconds() + jint(j, "expires_in", 3600);
    if (j.contains("user") && j["user"].is_object()) s.userId = jstr(j["user"], "id");
    return s;
}

}  // namespace

Discovery discover(const std::string& url) {
    std::string base = trimSlash(url);
    if (base.empty()) throw std::runtime_error("Nuvio: empty backend URL");
    std::string resp = HTTP::get(base + "/.well-known/nuvio", HTTP::Timeout{kTimeout});
    nlohmann::json j = parseJson(resp, "discovery");
    if (!j.is_object()) throw std::runtime_error("Nuvio: invalid discovery response");

    Discovery d;
    d.backendUrl = trimSlash(jstr(j, "backend_url", base));
    if (d.backendUrl.empty()) d.backendUrl = base;
    d.publishableKey = jstr(j, "publishable_key");
    if (d.publishableKey.empty()) throw std::runtime_error("Nuvio: discovery document has no publishable key");
    if (j.contains("capabilities") && j["capabilities"].is_object()) {
        d.emailPasswordAuth = jbool(j["capabilities"], "email_password_auth", true);
        d.tvLogin = jbool(j["capabilities"], "tv_login");
    }
    return d;
}

Session signIn(const std::string& backendUrl, const std::string& publishableKey, const std::string& email,
    const std::string& password) {
    nlohmann::json body = {{"email", email}, {"password", password}};
    std::string resp = HTTP::post(trimSlash(backendUrl) + "/auth/v1/token?grant_type=password", body.dump(),
        publicHeaders(publishableKey), HTTP::Timeout{kTimeout});
    return parseSession(parseJson(resp, "sign-in"));
}

Session refresh(const std::string& backendUrl, const std::string& publishableKey, const std::string& refreshToken) {
    nlohmann::json body = {{"refresh_token", refreshToken}};
    std::string resp = HTTP::post(trimSlash(backendUrl) + "/auth/v1/token?grant_type=refresh_token", body.dump(),
        publicHeaders(publishableKey), HTTP::Timeout{kTimeout});
    return parseSession(parseJson(resp, "token refresh"));
}

std::vector<Profile> getProfiles(
    const std::string& backendUrl, const std::string& publishableKey, const std::string& accessToken) {
    std::string resp = HTTP::post(trimSlash(backendUrl) + "/rest/v1/rpc/sync_pull_profiles", std::string("{}"),
        bearerHeaders(publishableKey, accessToken), HTTP::Timeout{kTimeout});
    nlohmann::json j = parseJson(resp, "profiles");
    if (!j.is_array()) throw std::runtime_error("Nuvio: invalid profiles response");

    std::vector<Profile> out;
    for (auto& p : j) {
        Profile pr;
        pr.index = (int)jint(p, "profile_index");
        pr.name = jstr(p, "name");
        pr.avatarUrl = jstr(p, "avatar_url");
        pr.pinEnabled = jbool(p, "pin_enabled");
        if (pr.index > 0) out.push_back(std::move(pr));
    }
    return out;
}

bool verifyProfilePin(const std::string& backendUrl, const std::string& publishableKey, const std::string& accessToken,
    int profileIndex, const std::string& pin, int& retryAfterSeconds) {
    retryAfterSeconds = 0;
    nlohmann::json body = {{"p_profile_id", profileIndex}, {"p_pin", pin}};
    std::string resp = HTTP::post(trimSlash(backendUrl) + "/rest/v1/rpc/verify_profile_pin", body.dump(),
        bearerHeaders(publishableKey, accessToken), HTTP::Timeout{kTimeout});
    nlohmann::json j = parseJson(resp, "profile PIN check");

    // RETURNS TABLE(...) comes back as a one-row array via PostgREST.
    const nlohmann::json* row = nullptr;
    if (j.is_array() && !j.empty())
        row = &j.front();
    else if (j.is_object())
        row = &j;
    if (!row) throw std::runtime_error("Nuvio: invalid profile PIN response");

    bool unlocked = jbool(*row, "unlocked");
    if (!unlocked) retryAfterSeconds = (int)jint(*row, "retry_after_seconds");
    return unlocked;
}

}  // namespace nuvio
