/*
    GMCA — Stremio addon engine implementation (see stremio/addons.hpp).
    Re-syncs the account's addon collection (via the resyncAddons hook), then
    loads the configured transportUrls' manifests once and routes resource
    queries across them.
*/

#include "api/stremio/addons.hpp"
#include "utils/config.hpp"
#include <borealis/core/logger.hpp>
#include <algorithm>

namespace stremio {

void AddonEngine::ensureLoaded() {
    std::lock_guard<std::mutex> lock(mtx);
    if (loaded) return;

    // A previous attempt that resolved ZERO addons is NOT a loaded state.
    // Latching `loaded` on an empty result (a resync that failed, an expired
    // token, an account query that matched no rows, every manifest
    // unreachable) stranded the whole session with no catalogs at all: no
    // library sections in the sidebar, and a blank Home — with no retry short
    // of restarting the app, and no way to tell that from a genuine "this
    // account has nothing". Retry instead, throttled so a collection that
    // really is empty doesn't re-hit the network on every navigation.
    auto now = std::chrono::steady_clock::now();
    if (emptyAttempt.time_since_epoch().count() != 0 && now - emptyAttempt < std::chrono::seconds(15)) return;

    // Re-sync the account's addon collection before loading manifests (see
    // resyncAddons — StremioBackend/NuvioBackend each set this to their own
    // account API in their constructor).
    if (resyncAddons) resyncAddons();

    // AppConfig::instance().getStremioAddons() returns the configured list of
    // transportUrls (each ending in /manifest.json). Provided by the config layer.
    const std::vector<std::string>& transports = AppConfig::instance().getStremioAddons();
    addons.clear();
    addons.reserve(transports.size());
    for (const auto& transport : transports) {
        try {
            nlohmann::json j = getSync(transport);
            if (j.empty()) {
                brls::Logger::warning("stremio: empty manifest from {}", transport);
                continue;
            }
            Addon a;
            a.transportUrl = transport;
            a.base = baseFromTransport(transport);
            a.manifest = parseManifest(j);
            addons.push_back(std::move(a));
        } catch (const std::exception& ex) {
            brls::Logger::warning("stremio: manifest load failed {}: {}", transport, ex.what());
        }
    }

    if (addons.empty()) {
        emptyAttempt = now;
        brls::Logger::warning(
            "stremio: no addons loaded ({} configured) — not latching, will retry", transports.size());
        return;
    }
    emptyAttempt = {};
    loaded = true;
}

void AddonEngine::invalidate() {
    std::lock_guard<std::mutex> lock(mtx);
    loaded = false;
}

std::vector<Addon> AddonEngine::addonsFor(
    const std::string& resource, const std::string& type, const std::string& id) {
    std::lock_guard<std::mutex> lock(mtx);
    std::vector<Addon> out;
    for (const auto& a : addons)
        if (a.supports(resource, type, id)) out.push_back(a);
    return out;
}

bool AddonEngine::hasResource(const std::string& resource) const {
    // Called from the UI thread (subtitle menu). ensureLoaded() holds mtx across
    // its network fetches, so a blocking lock here could freeze the UI for
    // seconds. Try the lock instead and treat contention (worker still loading)
    // like "not loaded yet" -> return true, so no misleading "install an addon"
    // hint is shown while we can't actually inspect the collection.
    std::unique_lock<std::mutex> lock(mtx, std::try_to_lock);
    if (!lock.owns_lock() || !loaded) return true;
    for (const auto& a : addons)
        if (a.manifest.resources.count(resource)) return true;
    return false;
}

std::vector<std::pair<Addon, Catalog>> AddonEngine::allCatalogs() {
    std::lock_guard<std::mutex> lock(mtx);
    std::vector<std::pair<Addon, Catalog>> out;
    for (const auto& a : addons) {
        if (a.manifest.resources.count("catalog") == 0) continue;
        for (const auto& c : a.manifest.catalogs) out.emplace_back(a, c);
    }
    return out;
}

std::vector<std::pair<Addon, Catalog>> AddonEngine::catalogsForType(const std::string& stremioType) {
    std::lock_guard<std::mutex> lock(mtx);
    std::vector<std::pair<Addon, Catalog>> out;
    for (const auto& a : addons) {
        if (a.manifest.resources.count("catalog") == 0) continue;
        for (const auto& c : a.manifest.catalogs)
            if (c.browsable && c.type == stremioType) out.emplace_back(a, c);
    }
    return out;
}

std::vector<std::string> AddonEngine::browsableTypes() {
    std::lock_guard<std::mutex> lock(mtx);
    std::vector<std::string> out;
    for (const auto& a : addons) {
        if (a.manifest.resources.count("catalog") == 0) continue;
        for (const auto& c : a.manifest.catalogs) {
            if (!c.browsable) continue;
            if (std::find(out.begin(), out.end(), c.type) == out.end()) out.push_back(c.type);
        }
    }
    return out;
}

std::string AddonEngine::resourceUrl(const Addon& addon, const std::string& resource, const std::string& type,
    const std::string& id, const std::vector<std::pair<std::string, std::string>>& extra) const {
    std::string url = addon.base + "/" + resource + "/" + type + "/" + encodeURIComponent(id);
    if (!extra.empty()) {
        std::string joined;
        for (size_t i = 0; i < extra.size(); ++i) {
            if (i) joined += "&";
            joined += extra[i].first + "=" + encodeURIComponent(extra[i].second);
        }
        url += "/" + joined;
    }
    url += ".json";
    return url;
}

}  // namespace stremio
