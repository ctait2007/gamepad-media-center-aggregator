/*
    GMCA — Nuvio session upkeep + PostgREST plumbing for the active connection.
    See nuvio/sync.cpp. Stage 2 (library/progress) builds on rpc()/restGet().
*/

#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace nuvio {

/// Refreshes the active Nuvio server's access token if it is expired or about
/// to expire, persisting the rotated pair immediately (AppConfig::
/// setNuvioSession). Throws std::runtime_error if there is no active Nuvio
/// connection or the refresh itself fails (dead refresh token -> caller should
/// prompt re-sign-in).
void ensureFreshSession();

/// POST {activeServerUrl}/rest/v1/rpc/{function} with the active connection's
/// bearer token, refreshing proactively first. On a 401 (token invalidated
/// server-side despite our bookkeeping) refreshes once more and retries
/// exactly once. Throws std::runtime_error (message includes the function
/// name and HTTP status, never a token) on any other failure.
nlohmann::json rpc(const std::string& function, const nlohmann::json& payload);

/// This install's sync client id, generated once and persisted. Every PUSH and
/// DELETE rpc carries it as `p_origin_client_id`, exactly as NuvioTV's
/// SyncClientIdentity does: the server uses it to keep a client's own writes
/// out of the delta stream it sends back. PostgREST matches an RPC by its
/// argument NAMES, so omitting a parameter the function declares without a
/// default makes the call 404 rather than merely lose the tagging.
std::string syncClientId();

/// GET {activeServerUrl}/rest/v1/{pathAndQuery} with the active connection's
/// bearer token. Same refresh/retry/error convention as rpc().
nlohmann::json restGet(const std::string& pathAndQuery);

/// Re-syncs the active profile's addon list from the `addons` table (read-
/// only: url/name/enabled/sort_order) and calls AppConfig::setStremioAddons()
/// with the enabled ones in sort_order. On any failure, logs a warning and
/// keeps the stored list — never throws. Set as the delegate StremioBackend's
/// AddonEngine::resyncAddons hook by NuvioBackend's constructor.
void resyncAddons();

}  // namespace nuvio
