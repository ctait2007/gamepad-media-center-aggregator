#pragma once

#include <borealis/core/singleton.hpp>
#include <borealis/core/logger.hpp>
#include <borealis/core/theme.hpp>
#include <nlohmann/json.hpp>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>

namespace media {
class Backend;
// opaque (scoped enums default to an int base, so this is a complete type) —
// avoids pulling api/backend.hpp into every TU that includes config.hpp.
enum class BackendType;
}  // namespace media

namespace plenx {
struct ThemePalette;
}

class AppVersion {
public:
    static std::string getVersion();
    static std::string getPlatform();
    static std::string getDeviceName();
    static std::string getPackageName();
    static std::string getCommit();
    static bool needUpdate(std::string latestVersion);
    static void checkUpdate(int delay = 2000, bool showUpToDateDialog = false);

    inline static std::shared_ptr<std::atomic_bool> updating = std::make_shared<std::atomic_bool>(true);
    inline static std::string git_repo = "ctait2007/gamepad-media-center-aggregator";

    /// Real path of the running NRO (argv[0] provided by hbloader or the
    /// forwarder), filled in main(). THIS is the file the auto-update must
    /// replace: the NRO may live at `sdmc:/switch/GMCA.nro` (forwarder)
    /// or `sdmc:/switch/GMCA/GMCA.nro`. Empty off-Switch or if unavailable.
    inline static std::string nro_path;
};

/// A plex.tv profile (account or Plex Home user).
/// `access_token` is the plex.tv token of THIS profile — it is used for the
/// plex.tv API (resources, home users), NOT for server requests.
struct AppUser {
    std::string id;  // plex.tv uuid
    std::string name;
    std::string access_token;
    std::string server_id;  // clientIdentifier of the last used server
    std::string thumb;      // avatar (absolute plex.tv URL)
    // Nuvio only: profile_index (1-6) this connection selects. All Nuvio data
    // (addons, library, progress) is scoped to a profile, not the account.
    int nuvio_profile_index = 0;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    AppUser, id, name, access_token, server_id, thumb, nuvio_profile_index);

/// A known media server. `access_token` is the server access token (Plex: from
/// /api/v2/resources; Jellyfin: AccessToken from authentication). urls.front()
/// = last reachable connection. `type` discriminates the backend implementation
/// (defaults to "plex" for configs written before multi-backend support).
struct AppServer {
    std::string name;
    std::string id;  // clientIdentifier (machine id) / Jellyfin server Id / Stremio account user id
    std::string access_token;
    std::vector<std::string> urls;
    std::string type = "plex";  // plex | jellyfin | emby | stremio | nuvio
    // Stremio/Nuvio: transport URLs (…/manifest.json) of the installed addons,
    // either entered manually (Stremio) or synced from the account's addon
    // collection (Stremio: addonCollectionGet; Nuvio: the `addons` table).
    std::vector<std::string> addons;
    // Nuvio only: Supabase session. `access_token` (above) is the current
    // short-lived bearer token; `nuvio_refresh_token` rotates on every use (the
    // old one is rejected the moment a new one is issued) and must be persisted
    // immediately after each refresh; `nuvio_expires_at` (epoch seconds) drives
    // proactive refresh; `nuvio_publishable_key` is the anon key discovered
    // from /.well-known/nuvio at sign-in, cached so it needn't be re-fetched.
    std::string nuvio_refresh_token;
    std::string nuvio_publishable_key;
    int64_t nuvio_expires_at = 0;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AppServer, id, name, access_token, urls, type, addons,
    nuvio_refresh_token, nuvio_publishable_key, nuvio_expires_at);

struct AppRemote {
    std::string name;
    std::string url;
    std::string user;
    std::string passwd;
    std::string user_agent;
};
inline void to_json(nlohmann::json& nlohmann_json_j, const AppRemote& nlohmann_json_t) {
    if (!nlohmann_json_t.user.empty()) nlohmann_json_j["user"] = nlohmann_json_t.user;
    if (!nlohmann_json_t.passwd.empty()) nlohmann_json_j["passwd"] = nlohmann_json_t.passwd;
    if (!nlohmann_json_t.user_agent.empty()) nlohmann_json_j["user_agent"] = nlohmann_json_t.user_agent;
    NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_TO, name, url))
}
inline void from_json(const nlohmann::json& nlohmann_json_j, AppRemote& nlohmann_json_t) {
    if (nlohmann_json_j.contains("user")) nlohmann_json_j["user"].get_to(nlohmann_json_t.user);
    if (nlohmann_json_j.contains("passwd")) nlohmann_json_j["passwd"].get_to(nlohmann_json_t.passwd);
    if (nlohmann_json_j.contains("user_agent")) nlohmann_json_j["user_agent"].get_to(nlohmann_json_t.user_agent);
    NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_FROM, name, url))
}

class AppConfig : public brls::Singleton<AppConfig> {
    using UserIter = std::vector<AppUser>::iterator;

public:
    enum Item {
        FULLSCREEN,
        OVERCLOCK,
        APP_THEME,
        ACCENT_THEME,  // colour theme (NuvioTV's list); "auto" follows the backend
        APP_LANG,
        APP_UPDATE,
        APP_UI_SCALE,
        SCROLLBAR,  // show the scroll indicator (scrollbar); default true
        /// Print each poster's title and year under it. NuvioTV makes this a
        /// Layout setting (poster_labels_enabled) rather than a fixed
        /// behaviour; off here, which is the look its reference screens have.
        POSTER_LABELS,

        /// Per-install id sent as `p_origin_client_id` on every Nuvio sync
        /// push, so the server can tell this client's own writes apart from
        /// another device's. Generated once and kept (NuvioTV's
        /// SyncClientIdentity does exactly this, in a SharedPreferences file).
        SYNC_CLIENT_ID,

        // --- Layout toggles the reference exposes and we did not (NuvioTV
        // LayoutSettingsScreen.kt). All default to what the app already did, so
        // an existing install sees no change until it asks for one.
        /// Hero carousel at the top of Home (layout_show_hero).
        SHOW_HERO,
        /// The Continue Watching row on Home (layout_cw_enabled).
        SHOW_CONTINUE,
        /// "from <addon>" under a row title (layout_addon_name).
        CATALOG_ADDON_NAME,
        /// The type half of a row title — "Movies · Popular" vs "Popular"
        /// (layout_catalog_type).
        CATALOG_TYPE_SUFFIX,
        /// The addon's logo and name on a stream card
        /// (settings_stream_addon_logo_title).
        STREAM_ADDON_LOGO,
        /// Episode long-press overlay: its own artwork behind it, or the plain
        /// gradient (layout_episode_options_overlay ARTWORK vs NONE).
        EPISODE_OVERLAY_ART,

        /// Discover's last filter, as "<type>\t<catalogKey>\t<genre>". The
        /// reference keeps its own (DiscoverSelectionDataStore) so the screen
        /// reopens where you left it. Matched by value, not by index, so an
        /// addon appearing or disappearing cannot silently move the selection.
        DISCOVER_SELECTION,
        AUDIO_CHANNELS,
        KEYMAP,
        WINDOW_STATE,
        TRANSCODEC,
        FORCE_DIRECTPLAY,
        PLAYER_VIDEO_QUALITY,  // transcode bitrate cap (bps); 0 = auto/direct play
        OSD_ON_TOGGLE,
        LOADING_SCREEN,
        LOADING_STAGES,
        PAUSE_SCREEN,
        /// "Up next" card at the end of an episode (layout_next_episode_card).
        NEXT_EPISODE_CARD,
        /// When it comes up (nextEpisodeThresholdMode / ...Percent /
        /// ...MinutesBeforeEnd). See MPVCore for the doubled-integer encoding.
        /// theintrodb.org markers, and whether they skip themselves.
        INTRODB,
        INTRODB_AUTO_SKIP,
        /// Poster Card Style (posterCardWidthDp / posterCardCornerRadiusDp),
        /// both in the reference's dp. Read into the style metrics at startup.
        LAYOUT_POSTER_WIDTH,
        LAYOUT_POSTER_RADIUS,
        /// hide_unreleased_content: drop anything not out yet from the rows.
        LAYOUT_HIDE_UNRELEASED,
        /// Subtitle face, persisted the way PlayerSettingsDataStore persists
        /// it: the player panel's steppers only ever moved mpv, so every one
        /// of these went back to its default on the next launch.
        ///   SUB_SIZE      percent, 50..200 (sub-scale)
        ///   SUB_OFFSET    percent up from the bottom, -20..50 (sub-pos)
        ///   SUB_OUTLINE_W 1..5 (sub-border-size)
        /// amoledMode / amoledSurfacesMode: pure black backgrounds, and
        /// optionally pure black cards and panels with them.
        /// playback_osd_clock: the wall clock on the player's bar.
        /// show_full_release_date: a movie's full date instead of its year.
        LAYOUT_FULL_RELEASE_DATE,
        /// use_episode_thumbnails_in_cw: an episode's own still on the
        /// Continue Watching tile, or the show's backdrop when off.
        LAYOUT_CW_EPISODE_THUMBS,
        /// AudioLanguageOption: "default" (the file's own) or "device" (the
        /// app's language). Its third, "original", wants TMDB's
        /// original_language, which no backend here carries.
        PLAYER_AUDIO_LANG,
        /// skipIntroEnabled: the skip button itself, separate from whether the
        /// markers are fetched at all (they also drive "up next").
        SKIP_INTRO_ENABLED,
        /// subtitleStripSdh: drop sound descriptions and speaker labels.
        SUB_STRIP_SDH,
        OSD_CLOCK,
        AMOLED_MODE,
        AMOLED_SURFACES,
        SUB_SIZE,
        SUB_OFFSET,
        SUB_BOLD,
        SUB_OUTLINE,
        SUB_OUTLINE_WIDTH,
        SUB_TEXT_COLOR,
        SUB_BG_COLOR,
        SUB_OUTLINE_COLOR,
        /// autoSkipSegmentTypes, one flag per AutoSkipSegmentType.
        AUTO_SKIP_INTRO,
        AUTO_SKIP_RECAP,
        AUTO_SKIP_OUTRO,
        NEXT_EPISODE_MODE,
        NEXT_EPISODE_PERCENT,
        NEXT_EPISODE_MINUTES,
        PAUSE_SCREEN_DELAY,
        TOUCH_GESTURE,
        CLIP_POINT,
        SYNC_SETTING,
        MPV_VO,
        PLAYER_LOW_QUALITY,
        PLAYER_INMEMORY_CACHE,
        PLAYER_SPEED,
        PLAYER_HWDEC,
        PLAYER_HWDEC_CUSTOM,
        PLAYER_ASPECT,
        PLAYER_SUBS_FALLBACK,
        PLAYER_SUBTITLE_LANG,  // preferred external-subtitle language: "auto" (= app locale), "off", or a 2-letter code
        PLAYER_TV_MODE,
        ALWAYS_ON_TOP,
        SINGLE,
        SHOW_FPS,
        SWAP_INTERVAL,
        APP_SWAP_ABXY,  // A-B 交换 和 X-Y 交换
        TEXTURE_CACHE_NUM,
        REQUEST_THREADS,
        REQUEST_TIMEOUT,
        HTTP_PROXY_STATUS,
        HTTP_PROXY,

        /// Library sorts/filters, persisted locally (json object
        /// {itemId: "sortBy,sortOrder,filter"} — /DisplayPreferences does not
        /// exist in Plex, cf. PLEX_MIGRATION.md §2.5)
        LIBRARY_SORT,

        /// Per-server sidebar layout: order + hidden state of the reorderable
        /// tabs (libraries + Playlists + Watchlist), keyed by the active server
        /// id (getUser().server_id). Section keys collide across servers, so
        /// this MUST stay server-scoped. JSON:
        /// { "<serverId>": { "order": [ids...], "hidden": [ids...] } }
        SIDEBAR_LAYOUT,

        /// Global (not per-server) set of hub identifiers hidden from every
        /// hub-rendering tab (Home, Movie/Show suggestions) — see
        /// AppConfig::isHubHidden/setHubHidden. JSON: [hubIdentifier, ...]
        HIDDEN_HUBS,

        /// Global (not per-server) display order of non-hidden hub identifiers
        /// — see AppConfig::getHubOrder/setHubOrder. Same identifier space as
        /// HIDDEN_HUBS; an identifier absent from this list (e.g. a newly
        /// discovered catalog) sorts after every listed one, in discovery
        /// order. JSON: [hubIdentifier, ...]
        HUB_ORDER,

        /// Write a debug log to <configDir>/gmca.log from the next launch on.
        /// Consoles have no console to read stdout from (PS4 logs to klog over
        /// the network, which most users can't capture), so without this a
        /// field bug report has nothing to attach. Toggled in Settings.
        DEBUG_LOG,

        /// HOME tile install prompt (forwarder NSP) already shown at first
        /// launch in application mode (Switch).
        HINT_FORWARDER,

        /// GMCA-era HOME-tile re-nudge, gated separately from HINT_FORWARDER on
        /// purpose: pleNx users who self-updated to GMCA are past the pleNx-era
        /// HINT_FORWARDER gate but have no GMCA tile (fresh title id), so re-offer
        /// it exactly once so they get a tile that resolves the migrated NRO.
        HINT_FORWARDER_GMCA,

        /// One-time "pleNx is now GMCA" welcome notice already shown. Set the
        /// first time the notice is displayed (only to users migrated from a
        /// legacy pleNx/Switchlex data dir — see AppConfig::migratedFromLegacy).
        RENAME_NOTICE_SHOWN,

        KEY_REFRESH,        // 刷新快捷键
        KEY_LAST,           // 上一个Tab快捷键
        KEY_NEXT,           // 下一个Tab快捷键
        KEY_VOLUME_UP,      // 音量增大快捷键
        KEY_VOLUME_DOWN,    // 音量减小快捷键
        KEY_VIDEO_PROFILE,  // 视频详情快捷键
        KEY_FORWARD,        // 快进快捷键
        KEY_REWIND,         // 快退快捷键
        KEY_SETTING,        // 设置快捷键
        KEY_VIDEO_QUALITY,  // 视频清晰度菜单快捷键
        KEY_VIDEO_SPEED,    // 视频倍速菜单快捷键
        KEY_VIDEO_OSD,      // 切换OSD显示
        KEY_VIDEO_PAUSE,    // 视频播放暂停快捷键
    };

    AppConfig() = default;
    ~AppConfig();  // out-of-line: activeBackend is a unique_ptr to an incomplete type

    bool init();
    void initThemes();
    /// (Re)applies the accent surface for `type` onto BOTH borealis theme
    /// objects (dark + light). std::nullopt = the neutral pleNx DEFAULT theme
    /// used on pre-connection screens. Structural chrome is left untouched.
    /// Must run BEFORE the activity that will read the colors is (re)built.
    void applyTheme(std::optional<media::BackendType> type);
    /// Pure-black overrides, applied after every theme pass. No-op when off.
    void applyAmoled();
    void save();
    bool checkLogin();

    std::string configDir();
    std::string ipcSocket();
    void checkRestart(char* argv[]);

    template <typename T>
    T getItem(const Item item, T defaultValue) {
        auto& o = settingMap[item];
        try {
            if (!setting.contains(o.key)) return defaultValue;
            return this->setting.at(o.key).get<T>();
        } catch (const std::exception& e) {
            brls::Logger::error("Damaged config found: {}/{}", o.key, e.what());
            return defaultValue;
        }
    }

    template <typename T>
    void setItem(const Item item, T data) {
        auto& o = settingMap[item];
        this->setting[o.key] = data;
        this->save();
    }

    struct Option {
        std::string key;
        std::vector<std::string> options;
        std::vector<long> values;
    };

    int getOptionIndex(const Item item, int default_index = 0) const;
    int getValueIndex(const Item item, int default_index = 0) const;
    inline const Option& getOptions(const Item item) const { return settingMap[item]; }

    bool addServer(const AppServer& s);
    void addUser(const AppUser& u, const std::string& url);
    /// Registers a server/connection WITHOUT making it active — no change to the
    /// active url/token/profile or the backend/theme. Used to store the other
    /// servers of a Plex account at link time; addServer/addUser activate one.
    void upsertServer(const AppServer& s);
    void upsertUser(const AppUser& u);
    bool removeServer(const std::string& id);
    bool removeUser(const std::string& id);
    const std::string& getDeviceId() { return this->device; }
    const std::string& getUserId() const { return this->user_id; }
    const std::string& getUserName() const { return this->user->name; }
    /// Active profile (name, avatar...) — valid after init()/checkLogin().
    const AppUser& getUser() const { return *this->user; }
    /// Access token of the active SERVER (X-Plex-Token of PMS requests).
    const std::string& getToken() const { return this->server_token; }
    /// plex.tv token of the active profile (resources, home users).
    const std::string& getAccountToken() const { return this->user->access_token; }
    const std::string& getUrl() const { return this->server_url; }
    /// Stremio only: addon transport URLs of the active server (manifest URLs).
    /// Empty for other backends / when not logged in.
    const std::vector<std::string>& getStremioAddons() const;
    /// Stremio only: replace the active server's addon list (after an account
    /// collection re-sync) and persist. No-op when unchanged or not logged in.
    void setStremioAddons(const std::vector<std::string>& addons);
    /// Nuvio only: refresh token of the active server's Supabase session.
    /// Empty for other backends / when not logged in.
    const std::string& getNuvioRefreshToken() const;
    /// Nuvio only: publishable (anon) key cached at sign-in.
    const std::string& getNuvioPublishableKey() const;
    /// Nuvio only: epoch-seconds expiry of the current access token (getToken()).
    int64_t getNuvioExpiresAt() const;
    /// Nuvio only: profile_index (1-6) of the active connection.
    int getNuvioProfileIndex() const;
    /// Nuvio only: replaces the active server's access/refresh token pair and
    /// expiry (sign-in or a proactive/401 refresh) and persists immediately —
    /// Supabase refresh tokens are single-use, so a delayed persist could
    /// strand the connection on a token Nuvio will never accept again.
    void setNuvioSession(const std::string& accessToken, const std::string& refreshToken, int64_t expiresAt);
    /// Active media backend (built lazily from the active server's type).
    /// The UI talks to this; it never formats a provider URL itself.
    media::Backend& backend();

    /// True if this hub (by its stable hubIdentifier, e.g. "home.continue" or
    /// "section.catalog.{id}") is hidden from every hub-rendering tab (Home,
    /// Movie/Show suggestions). Global (not per-server) — see setHubHidden()
    /// and view/hub_visibility_manager.hpp.
    bool isHubHidden(const std::string& hubIdentifier);
    /// Adds/removes a hub identifier from the global hidden set and persists.
    void setHubHidden(const std::string& hubIdentifier, bool hidden);
    /// Saved display order of non-hidden hub identifiers (see HUB_ORDER).
    /// Callers stable_sort their own hub list by an id's position here
    /// (missing ids sort last, in whatever order they were already in).
    std::vector<std::string> getHubOrder();
    /// Replaces the saved hub order and persists.
    void setHubOrder(const std::vector<std::string>& order);
    const std::vector<AppRemote>& getRemotes() const { return this->remotes; }
    void addRemote(const AppRemote& r);
    void updateRemote(size_t index, const AppRemote& r);
    void removeRemote(size_t index);

    /// Local folders pinned to the Files root screen (issue #24). Stored as
    /// absolute browser paths (e.g. "sdmc:/A_Media/").
    const std::vector<std::string>& getPins() const { return this->pins; }
    bool isPinned(const std::string& path) const;
    void addPin(const std::string& path);
    void removePin(const std::string& path);
    const std::vector<AppServer>& getServers() const { return this->servers; }
    const std::vector<AppUser> getUsers(const std::string& id) const;
    /// All known connections (one AppUser = one server+profile pair).
    const std::vector<AppUser>& getUsers() const { return this->users; }
    /// Maps the AppServer::type discriminant to a BackendType (defaults to Plex).
    /// Public so the connection switcher can tint each tile by its backend brand.
    static media::BackendType backendTypeFromString(const std::string& type);

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(AppConfig, user_id, device, users, servers, setting, remotes, pins);

    inline static bool SYNC = true;

    /// True for this session only when init() relocated a legacy data dir
    /// (pleNx/Switchlex -> GMCA). Runtime-only (never serialized): combined with
    /// the persistent RENAME_NOTICE_SHOWN flag it gates the one-time rebrand
    /// welcome notice so it shows exactly once, and only to migrated users.
    bool migratedFromLegacy = false;

private:
    static std::unordered_map<Item, Option> settingMap;

    /// (Re)builds activeBackend from the active server's type on next backend().
    void resetBackend();

    /// Writes one palette variant onto the matching borealis Theme singleton.
    void applyThemeVariant(brls::ThemeVariant tv, const plenx::ThemePalette& p);

    UserIter user;
    // owning raw pointer (forward-declared type): deleted in ~AppConfig/resetBackend
    media::Backend* activeBackend = nullptr;
    std::string user_id;
    std::string server_url;
    std::string server_token;
    std::string device;
    std::string device_name;
    std::vector<AppUser> users;
    std::vector<AppServer> servers;
    std::vector<AppRemote> remotes;
    std::vector<std::string> pins;
    nlohmann::json setting = {};

    void addColor(const brls::ThemeVariant tv, const std::string& name, NVGcolor defaultColor);
};