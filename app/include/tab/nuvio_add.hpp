/*
    GMCA — sign-in form for a Nuvio account (Supabase-backed).
    Backend URL (default https://api.nuvio.tv, editable for self-hosted) +
    email/password. On success the profile picker (single profile: skipped:
    several: a dropdown, same pattern as Plex Home) runs, then the account +
    chosen profile are persisted as an AppServer (type "nuvio") + AppUser and
    the app enters MainActivity.

    Reached from ServerTypeChoose (the Nuvio cell present()s this view), so it
    is a content view of the ServerList AppletFrame: it keeps the footer and B
    (the frame's hints/back action) returns to the type chooser.
*/

#pragma once

#include <borealis.hpp>

namespace nuvio {
struct Discovery;
struct Session;
struct Profile;
}  // namespace nuvio

class NuvioAdd : public brls::Box {
public:
    NuvioAdd();
    ~NuvioAdd() override;

    /// Focus the URL field on entry (deterministic, no phantom focus).
    brls::View* getDefaultFocus() override;

private:
    /// Validates the form, discovers + authenticates (async + spinner), then
    /// fetches the account's profiles.
    void submit();
    /// Shows the profile picker (a Dropdown, Plex-Home style) when the account
    /// has more than one profile; skips straight to onProfilePicked() for one.
    void onProfiles(const nuvio::Discovery& disc, const nuvio::Session& session, const std::vector<nuvio::Profile>& profiles);
    /// Prompts for the profile PIN (IME, Plex-Home style) when locked, then
    /// finishes.
    void onProfilePicked(const nuvio::Discovery& disc, const nuvio::Session& session, const nuvio::Profile& profile);
    /// Persists the active server + user and enters the application.
    void finish(const nuvio::Discovery& disc, const nuvio::Session& session, const nuvio::Profile& profile);

    BRLS_BIND(brls::Label, header, "nuvio/add/header");
    BRLS_BIND(brls::InputCell, cellUrl, "nuvio/add/url");
    BRLS_BIND(brls::InputCell, cellEmail, "nuvio/add/email");
    BRLS_BIND(brls::InputCell, cellPasswd, "nuvio/add/passwd");
    BRLS_BIND(brls::Button, btnConnect, "nuvio/add/connect");
    BRLS_BIND(brls::ProgressSpinner, spinner, "nuvio/add/spinner");
    BRLS_BIND(brls::Label, labelStatus, "nuvio/add/status");
};
