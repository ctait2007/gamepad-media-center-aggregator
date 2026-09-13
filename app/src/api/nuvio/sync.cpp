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
#include <cctype>
#include <mutex>
#include <random>
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

bool isValidClientId(const std::string& id) {
    // NuvioTV's own acceptance test for a stored id (SyncClientIdentity.
    // isValidSyncClientId): length 16..96, alphanumerics plus - and _.
    if (id.size() < 16 || id.size() > 96) return false;
    for (char c : id)
        if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') return false;
    return true;
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

std::string syncClientId() {
    static std::mutex idMtx;
    static std::string cached;
    std::lock_guard<std::mutex> lock(idMtx);
    if (!cached.empty()) return cached;

    auto& cfg = AppConfig::instance();
    std::string stored = cfg.getItem(AppConfig::SYNC_CLIENT_ID, std::string{});
    if (isValidClientId(stored)) {
        cached = stored;
        return cached;
    }

    // Same shape as NuvioTV's SyncClientIdentity.generateClientId(): the
    // prefix plus 32 characters of [a-z0-9]. random_device rather than a
    // time seed — two consoles first launched in the same second must not
    // end up claiming the same origin.
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> pick(0, (int)sizeof(alphabet) - 2);
    std::string id = "nuvio-tv-";
    for (int i = 0; i < 32; i++) id.push_back(alphabet[pick(gen)]);

    cfg.setItem(AppConfig::SYNC_CLIENT_ID, id);
    cached = id;
    return cached;
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

namespace {

/// WHICH profile's addon rows to read — not always the active one. Mirrors
/// NuvioTV's own AddonSyncService.getRemoteAddonUrls(): a NON-primary profile
/// flagged `uses_primary_addons` shares the PRIMARY profile's collection, so
/// it owns no `addons` rows of its own. Querying its own profile_id then
/// matches nothing, and the app comes up with zero addons — no catalogs, no
/// library sections, an empty Home — on an account that visibly has addons.
int addonProfileIndex(int activeIndex) {
    if (activeIndex == 1) return 1;  // primary owns its rows
    try {
        nlohmann::json rows = rpc("sync_pull_profiles", nlohmann::json::object());
        if (rows.is_array()) {
            for (auto& p : rows) {
                if ((int)media::jint(p, "profile_index") != activeIndex) continue;
                if (media::jbool(p, "uses_primary_addons", false)) {
                    brls::Logger::info("nuvio: profile {} inherits the primary profile's addons", activeIndex);
                    return 1;
                }
                return activeIndex;
            }
        }
    } catch (const std::exception& ex) {
        // Unknown flag: fall through to the active profile, and let the
        // empty-result fallback in resyncAddons() cover us.
        brls::Logger::warning("nuvio: could not read profile {} flags: {}", activeIndex, ex.what());
    }
    return activeIndex;
}

}  // namespace

void resyncAddons() {
    try {
        int activeIndex = AppConfig::instance().getNuvioProfileIndex();
        int profileIndex = addonProfileIndex(activeIndex);
        auto fetch = [](int idx) {
            return restGet(fmt::format("addons?profile_id=eq.{}&order=sort_order.asc&select=url,enabled", idx));
        };

        nlohmann::json rows = fetch(profileIndex);
        // Last resort: no rows for this profile still must not leave the app
        // with NO addons at all — that presents as a broken app rather than as
        // an empty profile. Borrow the primary profile's collection.
        if ((!rows.is_array() || rows.empty()) && profileIndex != 1) {
            brls::Logger::warning(
                "nuvio: profile {} returned no addon rows, falling back to the primary profile", profileIndex);
            rows = fetch(1);
        }
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
