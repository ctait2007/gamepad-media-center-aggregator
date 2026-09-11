/*
    GMCA — Nuvio account authentication (Supabase auth + PostgREST RPCs).

    Nuvio's backend is Supabase: email/password sign-in is the GoTrue password
    grant, profiles/PIN are plain PostgREST RPCs. The backend URL is
    user-editable (default https://api.nuvio.tv); the publishable (anon) key is
    NEVER hardcoded — it is fetched from the backend's own discovery document
    (discover()), which both the official and self-hosted backends serve.

    All calls are synchronous (call from brls::async) and throw
    std::runtime_error on failure. HTTP::post/get throw before returning the
    body on a non-2xx response (see api/http.hpp), so the message carries only
    "http status NNN" — never a token, per GMCA's existing convention (Plex/
    Jellyfin/Stremio auth do the same).
*/

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace nuvio {

/// Parsed /.well-known/nuvio document.
struct Discovery {
    std::string backendUrl;
    std::string publishableKey;
    bool emailPasswordAuth = true;
    bool tvLogin = false;
};

/// A Supabase auth session. `expiresAt` is epoch seconds, computed from the
/// response's `expires_in` at call time.
struct Session {
    std::string accessToken;
    std::string refreshToken;
    int64_t expiresAt = 0;
    // Only populated by signIn() (the token response's `user` object) — used
    // once to derive a stable AppServer::id. Empty after refresh().
    std::string userId;
};

/// A Nuvio profile (sync_pull_profiles row).
struct Profile {
    int index = 0;  // profile_index, 1-6
    std::string name;
    std::string avatarUrl;
    bool pinEnabled = false;
};

/// GET {url}/.well-known/nuvio. Throws if unreachable, not JSON, or missing a
/// publishable key.
Discovery discover(const std::string& url);

/// POST {backendUrl}/auth/v1/token?grant_type=password.
Session signIn(
    const std::string& backendUrl, const std::string& publishableKey, const std::string& email, const std::string& password);

/// POST {backendUrl}/auth/v1/token?grant_type=refresh_token. Supabase rotates
/// the refresh token on every use — the old one is rejected the instant a new
/// one is issued — so the caller MUST persist the returned pair immediately.
Session refresh(const std::string& backendUrl, const std::string& publishableKey, const std::string& refreshToken);

/// POST {backendUrl}/rest/v1/rpc/sync_pull_profiles.
std::vector<Profile> getProfiles(
    const std::string& backendUrl, const std::string& publishableKey, const std::string& accessToken);

/// POST {backendUrl}/rest/v1/rpc/verify_profile_pin. Returns true if unlocked;
/// on a wrong/locked PIN returns false and sets `retryAfterSeconds` (>0 when
/// temporarily locked out, 0 otherwise).
bool verifyProfilePin(const std::string& backendUrl, const std::string& publishableKey, const std::string& accessToken,
    int profileIndex, const std::string& pin, int& retryAfterSeconds);

}  // namespace nuvio
