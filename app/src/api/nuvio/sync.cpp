/*
    GMCA — Nuvio session upkeep + PostgREST plumbing (see nuvio/sync.hpp).
*/

#include "api/nuvio/sync.hpp"
#include "api/nuvio/auth.hpp"
#include "api/http.hpp"
#include "api/media/types.hpp"
#include "utils/config.hpp"
#include <borealis/core/logger.hpp>
#include <chrono>
#include <fmt/format.h>
#include <stdexcept>

namespace nuvio {

namespace {

// Proactively refresh once fewer than this many seconds remain, so a request
// never races an about-to-expire token.
constexpr int64_t kRefreshMarginSec = 60;
constexpr long kTimeout = 20000L;

int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

bool isUnauthorized(const std::exception& ex) {
    std::string msg = ex.what();
    return msg.find("401") != std::string::npos;
}

HTTP::Header bearerHeaders() {
    auto& cfg = AppConfig::instance();
    return HTTP::Header{"apikey: " + cfg.getNuvioPublishableKey(), "Authorization: Bearer " + cfg.getToken(),
        "Content-Type: application/json"};
}

}  // namespace

void ensureFreshSession() {
    auto& cfg = AppConfig::instance();
    const std::string& refreshToken = cfg.getNuvioRefreshToken();
    if (refreshToken.empty()) throw std::runtime_error("Nuvio: not signed in");
    if (cfg.getNuvioExpiresAt() - nowSeconds() > kRefreshMarginSec) return;  // still fresh

    Session s = refresh(cfg.getUrl(), cfg.getNuvioPublishableKey(), refreshToken);
    // Persisted immediately: the refresh token above is now dead server-side —
    // any failure between here and a later retry must never strand the
    // connection on a token Nuvio will never accept again.
    cfg.setNuvioSession(s.accessToken, s.refreshToken, s.expiresAt);
}

nlohmann::json rpc(const std::string& function, const nlohmann::json& payload) {
    try {
        ensureFreshSession();
        auto& cfg = AppConfig::instance();
        std::string url = cfg.getUrl() + "/rest/v1/rpc/" + function;
        std::string body = payload.dump();
        std::string resp;
        try {
            resp = HTTP::post(url, body, bearerHeaders(), HTTP::Timeout{kTimeout});
        } catch (const std::exception& ex) {
            if (!isUnauthorized(ex)) throw;
            Session s = refresh(cfg.getUrl(), cfg.getNuvioPublishableKey(), cfg.getNuvioRefreshToken());
            cfg.setNuvioSession(s.accessToken, s.refreshToken, s.expiresAt);
            resp = HTTP::post(url, body, bearerHeaders(), HTTP::Timeout{kTimeout});
        }
        if (resp.empty()) return nlohmann::json::array();
        nlohmann::json j = nlohmann::json::parse(resp, nullptr, false);
        if (j.is_discarded()) throw std::runtime_error("invalid response");
        return j;
    } catch (const std::exception& ex) {
        throw std::runtime_error(fmt::format("Nuvio rpc/{} failed: {}", function, ex.what()));
    }
}

nlohmann::json restGet(const std::string& pathAndQuery) {
    try {
        ensureFreshSession();
        auto& cfg = AppConfig::instance();
        std::string url = cfg.getUrl() + "/rest/v1/" + pathAndQuery;
        std::string resp;
        try {
            resp = HTTP::get(url, bearerHeaders(), HTTP::Timeout{kTimeout});
        } catch (const std::exception& ex) {
            if (!isUnauthorized(ex)) throw;
            Session s = refresh(cfg.getUrl(), cfg.getNuvioPublishableKey(), cfg.getNuvioRefreshToken());
            cfg.setNuvioSession(s.accessToken, s.refreshToken, s.expiresAt);
            resp = HTTP::get(url, bearerHeaders(), HTTP::Timeout{kTimeout});
        }
        nlohmann::json j = nlohmann::json::parse(resp, nullptr, false);
        if (j.is_discarded()) throw std::runtime_error("invalid response");
        return j;
    } catch (const std::exception& ex) {
        throw std::runtime_error(fmt::format("Nuvio rest/{} failed: {}", pathAndQuery, ex.what()));
    }
}

void resyncAddons() {
    try {
        int profileIndex = AppConfig::instance().getNuvioProfileIndex();
        nlohmann::json rows =
            restGet(fmt::format("addons?profile_id=eq.{}&order=sort_order.asc&select=url,enabled", profileIndex));
        if (!rows.is_array()) return;

        std::vector<std::string> fresh;
        for (auto& r : rows) {
            if (!media::jbool(r, "enabled", true)) continue;  // skip disabled addons
            std::string url = media::jstr(r, "url");
            if (!url.empty()) fresh.push_back(url);
        }
        // An empty result could mean "no addons" or a transient parse miss;
        // like Stremio's resync, never wipe a working list on an empty read.
        if (!fresh.empty()) AppConfig::instance().setStremioAddons(fresh);
    } catch (const std::exception& ex) {
        brls::Logger::warning("nuvio: addon sync failed: {}", ex.what());
    }
}

}  // namespace nuvio
