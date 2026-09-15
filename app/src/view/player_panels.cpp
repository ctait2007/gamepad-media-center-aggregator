/*
    GMCA — the player's five panels (see player_panels.hpp).
*/

#include "view/player_panels.hpp"

#include "api/backend.hpp"
#include "utils/config.hpp"
#include "view/mpv_core.hpp"
#include "view/player_panel.hpp"
#include "view/player_setting.hpp"
#include "view/svg_image.hpp"
#include "utils/image.hpp"

#include <algorithm>
#include <fmt/format.h>
#include <cctype>
#include <cstdio>
#include <memory>
#include <set>

using namespace brls::literals;

namespace {

// The reference's content padding for each overlay, doubled. They differ per
// panel there and they differ here (SubtitleSelectionOverlay 52/36/76,
// AudioSelectionOverlay 44/28/64, StreamInfoOverlay xxxl/36/36).
constexpr float kSubPadX = 104, kSubPadTop = 72, kSubPadBottom = 152;
constexpr float kAudPadX = 88, kAudPadTop = 56, kAudPadBottom = 128;
constexpr float kInfoPadX = 96, kInfoPadTop = 72, kInfoPadBottom = 72;

// Rail widths, doubled from the reference's RailColumn calls: the subtitle
// overlay is 200 / 300 / 280dp (languages, subtitles, style) and the audio
// overlay 444 / 268dp (tracks, controls).
constexpr float kSubLangWidth = 400;
constexpr float kSubListWidth = 600;
constexpr float kSubStyleWidth = 560;
constexpr float kAudTrackWidth = 888;
constexpr float kAudControlWidth = 536;
constexpr float kRailGap = 28;  // 14dp between the reference's rails

/// ISO 639-1/2 -> English name, for the language rail. The reference asks
/// java.util.Locale for this; we have no such table, so these are the codes
/// subtitle addons and media files actually carry. Anything unlisted falls
/// back to the code itself, which is still more use than hiding it.
const std::pair<const char*, const char*> kLanguages[] = {
    {"en", "English"}, {"eng", "English"}, {"es", "Spanish"}, {"spa", "Spanish"}, {"fr", "French"},
    {"fre", "French"}, {"fra", "French"}, {"de", "German"}, {"ger", "German"}, {"deu", "German"},
    {"it", "Italian"}, {"ita", "Italian"}, {"pt", "Portuguese"}, {"por", "Portuguese"}, {"nl", "Dutch"},
    {"dut", "Dutch"}, {"nld", "Dutch"}, {"sv", "Swedish"}, {"swe", "Swedish"}, {"no", "Norwegian"},
    {"nor", "Norwegian"}, {"da", "Danish"}, {"dan", "Danish"}, {"fi", "Finnish"}, {"fin", "Finnish"},
    {"pl", "Polish"}, {"pol", "Polish"}, {"cs", "Czech"}, {"cze", "Czech"}, {"ces", "Czech"},
    {"ru", "Russian"}, {"rus", "Russian"}, {"uk", "Ukrainian"}, {"ukr", "Ukrainian"}, {"tr", "Turkish"},
    {"tur", "Turkish"}, {"ar", "Arabic"}, {"ara", "Arabic"}, {"he", "Hebrew"}, {"heb", "Hebrew"},
    {"hi", "Hindi"}, {"hin", "Hindi"}, {"ja", "Japanese"}, {"jpn", "Japanese"}, {"ko", "Korean"},
    {"kor", "Korean"}, {"zh", "Chinese"}, {"chi", "Chinese"}, {"zho", "Chinese"}, {"th", "Thai"},
    {"tha", "Thai"}, {"vi", "Vietnamese"}, {"vie", "Vietnamese"}, {"id", "Indonesian"}, {"ind", "Indonesian"},
    {"el", "Greek"}, {"gre", "Greek"}, {"ell", "Greek"}, {"hu", "Hungarian"}, {"hun", "Hungarian"},
    {"ro", "Romanian"}, {"rum", "Romanian"}, {"ron", "Romanian"}, {"bg", "Bulgarian"}, {"bul", "Bulgarian"},
    {"hr", "Croatian"}, {"hrv", "Croatian"}, {"sr", "Serbian"}, {"srp", "Serbian"}, {"sk", "Slovak"},
    {"slo", "Slovak"}, {"slk", "Slovak"}, {"sl", "Slovenian"}, {"slv", "Slovenian"}, {"fa", "Persian"},
    {"per", "Persian"}, {"fas", "Persian"}, {"ms", "Malay"}, {"msa", "Malay"}, {"ta", "Tamil"},
    {"tam", "Tamil"}, {"te", "Telugu"}, {"tel", "Telugu"}, {"bn", "Bengali"}, {"ben", "Bengali"},
};

/// Material "check" and "visibility" — the two badges the reference puts on an
/// episode still.
const char* kCheck = "M9 16.17L4.83 12l-1.42 1.41L9 19 21 7l-1.41-1.41z";
/// Material "refresh" — the same glyph the pre-playback source picker's round
/// chip carries, so the two refreshes look like the same control.
const char* kRefresh =
    "M17.65 6.35C16.2 4.9 14.21 4 12 4c-4.42 0-7.99 3.58-8 8s3.57 8 8 8c3.73 0 6.84-2.55 7.73-6h-2.08c-.82 "
    "2.33-3.04 4-5.65 4-3.31 0-6-2.69-6-6s2.69-6 6-6c1.66 0 3.14.69 4.22 1.78L13 11h7V4l-2.35 2.35z";

const char* kEye =
    "M12 4.5C7 4.5 2.73 7.61 1 12c1.73 4.39 6 7.5 11 7.5s9.27-3.11 11-7.5c-1.73-4.39-6-7.5-11-7.5zM12 17c-2.76 "
    "0-5-2.24-5-5s2.24-5 5-5 5 2.24 5 5-2.24 5-5 5zm0-8c-1.66 0-3 1.34-3 3s1.34 3 3 3 3-1.34 3-3-1.34-3-3-3z";

std::string hexColor(NVGcolor c) {
    auto to8 = [](float f) {
        int v = (int)(f * 255.0f + 0.5f);
        return v < 0 ? 0 : (v > 255 ? 255 : v);
    };
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", to8(c.r), to8(c.g), to8(c.b));
    return buf;
}

/// The side sheet's width less its padding — what a rail inside one gets.
constexpr float kSheetInnerWidth = 944;

std::string languageName(const std::string& code) {
    if (code.empty() || code == "und" || code == "unknown") return "main/player/panel/lang_unknown"_i18n;
    std::string lower;
    for (char c : code) lower.push_back((char)std::tolower((unsigned char)c));
    // "en-US", "pt_BR" -> match on the base tag as well as the whole thing
    std::string base = lower.substr(0, lower.find_first_of("-_"));
    for (auto& [k, v] : kLanguages)
        if (lower == k || base == k) return v;
    for (char& c : lower) c = (char)std::toupper((unsigned char)c);
    return lower;
}

/// The overlay's title, in the reference's headlineMedium above the rails.
brls::Label* overlayTitle(const std::string& text) {
    auto* l = new brls::Label();
    l->setText(text);
    l->setFontSize(48);
    l->setFontWeight("medium");
    l->setTextColor(nvgRGB(255, 255, 255));
    l->setMarginBottom(24);  // spacing.md
    return l;
}

/// A row of rails, bottom-aligned like the reference's.
brls::Box* railRow() {
    auto* row = new brls::Box();
    row->setAxis(brls::Axis::ROW);
    // Each rail is its own height and they all start at the top of the row.
    row->setAlignItems(brls::AlignItems::FLEX_START);
    return row;
}

std::string trackTitle(int64_t n, const char* kind, const std::string& fallback) {
    auto& mpv = MPVCore::instance();
    std::string title = mpv.getString(fmt::format("track-list/{}/title", n));
    if (title.empty()) title = mpv.getString(fmt::format("track-list/{}/lang", n));
    if (title.empty()) title = fallback;
    (void)kind;
    return title;
}

std::string trackDetail(int64_t n) {
    auto& mpv = MPVCore::instance();
    std::vector<std::string> bits;
    std::string lang = mpv.getString(fmt::format("track-list/{}/lang", n));
    if (!lang.empty() && lang != "und") bits.push_back(lang);
    std::string codec = mpv.getString(fmt::format("track-list/{}/codec", n));
    if (!codec.empty()) bits.push_back(codec);
    int64_t channels = mpv.getInt(fmt::format("track-list/{}/demux-channel-count", n));
    if (channels > 0) bits.push_back(fmt::format("{}ch", channels));
    std::string out;
    for (auto& b : bits) out += (out.empty() ? "" : " · ") + b;
    return out;
}

std::string formatSize(int64_t bytes) {
    if (bytes >= 1073741824LL) return fmt::format("{:.1f} GB", bytes / 1073741824.0);
    if (bytes >= 1048576LL) return fmt::format("{:.1f} MB", bytes / 1048576.0);
    if (bytes >= 1024LL) return fmt::format("{:.1f} KB", bytes / 1024.0);
    return fmt::format("{} B", bytes);
}

std::string formatBitrate(int64_t bps) {
    if (bps >= 1000000) return fmt::format("{:.1f} Mbps", bps / 1000000.0);
    if (bps >= 1000) return fmt::format("{:.0f} kbps", bps / 1000.0);
    return fmt::format("{} bps", bps);
}

/// The reference's formatResolution: the pixel count and the name for it.
std::string formatResolution(int w, int h) {
    int maxDim = std::max(w, h);
    const char* label = maxDim >= 3600   ? "4K"
                        : maxDim >= 2400 ? "1440p"
                        : maxDim >= 1800 ? "1080p"
                        : maxDim >= 1200 ? "720p"
                        : maxDim >= 800  ? "480p"
                                         : nullptr;
    if (label) return fmt::format("{} × {} ({})", w, h, label);
    return fmt::format("{} × {} ({}p)", w, h, std::min(w, h));
}

}  // namespace

namespace player_panels {

namespace {

/// One selectable subtitle. Three kinds land here, and the handler differs:
///   - an mpv track that came with the file (`id` is its mpv id)
///   - a sidecar the player can fetch and attach (`sidecar` indexes into the
///     list PlayerView handed us)
///   - a transcode-side stream, which needs a re-transcode to apply
struct SubOption {
    std::string name;
    std::string detail;
    std::string lang;
    int64_t id = 0;
    int sidecar = -1;
    bool transcode = false;
};

/// Every subtitle the player can offer.
///
/// The IN-FILE tracks are read off mpv; the sidecars come from the caller. The
/// distinction matters because an attached sidecar is BOTH — it is an mpv track
/// once it has been fetched — and listing it twice was the obvious trap. mpv
/// marks the ones it did not find inside the file as `external`, so those are
/// skipped here and represented by their sidecar entry instead.
std::vector<SubOption> collectSubtitles(const plex::Media* src, const std::vector<plex::Stream>& sidecars) {
    auto& mpv = MPVCore::instance();
    std::vector<SubOption> out;

    int64_t count = mpv.getInt("track-list/count");
    for (int64_t n = 0; n < count; n++) {
        if (mpv.getString(fmt::format("track-list/{}/type", n)) != "sub") continue;
        // Read as a STRING: `external` is an mpv FLAG property, and mpv will
        // not convert a flag to INT64 — getInt() silently returns its default
        // and every attached sidecar got listed a second time as an in-file
        // track.
        if (mpv.getString(fmt::format("track-list/{}/external", n)) == "yes") continue;  // ours, listed below
        SubOption o;
        o.id = mpv.getInt(fmt::format("track-list/{}/id", n));
        o.lang = mpv.getString(fmt::format("track-list/{}/lang", n));
        o.name = trackTitle(n, "sub", fmt::format("{} {}", "main/player/subtitle"_i18n, o.id));
        o.detail = mpv.getString(fmt::format("track-list/{}/codec", n));
        out.push_back(std::move(o));
    }

    for (size_t i = 0; i < sidecars.size(); i++) {
        const plex::Stream& st = sidecars[i];
        SubOption o;
        o.sidecar = (int)i;
        o.lang = st.languageTag.empty() ? st.language : st.languageTag;
        o.name = st.displayTitle.empty() ? languageName(o.lang) : st.displayTitle;
        o.detail = st.sourceName;
        out.push_back(std::move(o));
    }

    if (!out.empty() || src == nullptr || src->parts.empty()) return out;

    // Transcode-side streams: no embedded subs in the HLS stream, so choosing
    // one re-transcodes with it burned in.
    for (auto& st : src->parts.front().streams) {
        if (st.streamType != plex::streamTypeSubtitle) continue;
        SubOption o;
        o.id = st.id;
        o.name = st.displayTitle;
        o.lang = st.language;
        o.transcode = true;
        out.push_back(std::move(o));
    }
    return out;
}

}  // namespace

void showSubtitles(const plex::Media* src, const std::vector<plex::Stream>& sidecars, int selected,
    std::function<void(int)> onPick) {
    auto& mpv = MPVCore::instance();
    std::vector<SubOption> options = collectSubtitles(src, sidecars);

    // Top-anchored, as the reference's SubtitleSelectionOverlay is: its content
    // Column wraps rather than filling the height, so it sits at the top of the
    // padded box.
    auto* overlay = new PlayerOverlay(kSubPadX, kSubPadTop, kSubPadBottom);
    overlay->content()->addView(overlayTitle("main/player/subtitle"_i18n));

    auto* row = railRow();

    // ---- rail 1: languages ------------------------------------------------
    // Which subtitle is on: a sidecar the player attached (it knows which one,
    // mpv only knows "some external track"), or an in-file track by mpv id.
    int64_t sid = mpv.getInt("sid");
    std::string activeLang;
    bool anyOn = false;
    if (selected >= 0 && selected < (int)sidecars.size()) {
        const plex::Stream& st = sidecars[selected];
        activeLang = st.languageTag.empty() ? st.language : st.languageTag;
        anyOn = true;
    } else {
        for (auto& o : options) {
            if (o.transcode || o.sidecar >= 0 || o.id != sid) continue;
            activeLang = o.lang;
            anyOn = sid > 0;
        }
    }

    // Keyed on the DISPLAY NAME, not the raw code: a file and an addon will
    // happily label the same language "en", "eng" and "en-US", and keying on
    // the code listed English three times with the tracks split between them.
    // One row per language, and every track that resolves to it underneath.
    std::vector<std::string> langs;  // "" is Off, and comes first
    langs.push_back("");
    for (auto& o : options) {
        std::string key = languageName(o.lang);
        if (std::find(langs.begin(), langs.end(), key) == langs.end()) langs.push_back(key);
    }
    // The reference's own order, which was a hardcoded "English first" here:
    // preferredOverlayLanguageOrder is [preferred, secondary] with the blanks
    // and duplicates dropped, everything else falling in behind it
    // alphabetically (its compareBy(preferredIndex, sortLabel)).
    auto& conf = AppConfig::instance();
    auto resolve = [](const std::string& code) -> std::string {
        if (code.empty() || code == "off" || code == "none") return "";
        // "auto" is the app's own language, which is what the picker's
        // preferred entry means when it has not been set to a code.
        if (code == "auto") {
            std::string locale = brls::Application::getLocale();
            return languageName(locale.substr(0, locale.find('-')));
        }
        return languageName(code);
    };
    std::vector<std::string> preferredOrder;
    for (const std::string& code : {conf.getItem(AppConfig::PLAYER_SUBTITLE_LANG, std::string("auto")),
             conf.getItem(AppConfig::SUB_SECONDARY_LANG, std::string(""))}) {
        std::string name = resolve(code);
        if (name.empty()) continue;
        if (std::find(preferredOrder.begin(), preferredOrder.end(), name) != preferredOrder.end()) continue;
        preferredOrder.push_back(name);
    }

    // showOnlyPreferredLanguages: everything else goes, EXCEPT whatever is
    // playing — the reference keeps the current one so the picker can never
    // hide the track you are looking at.
    if (conf.getItem(AppConfig::SUB_ONLY_PREFERRED_LANGS, false) && !preferredOrder.empty()) {
        std::string current = anyOn ? languageName(activeLang) : "";
        langs.erase(std::remove_if(langs.begin() + 1, langs.end(),
                        [&](const std::string& l) {
                            if (l == current) return false;
                            return std::find(preferredOrder.begin(), preferredOrder.end(), l) ==
                                   preferredOrder.end();
                        }),
            langs.end());
    }

    auto rank = [&preferredOrder](const std::string& l) {
        auto it = std::find(preferredOrder.begin(), preferredOrder.end(), l);
        return it == preferredOrder.end() ? (size_t)-1 : (size_t)(it - preferredOrder.begin());
    };
    std::stable_sort(langs.begin() + (langs.empty() ? 0 : 1), langs.end(),
        [&rank](const std::string& a, const std::string& b) {
            size_t ra = rank(a), rb = rank(b);
            if (ra != rb) return ra < rb;
            return a < b;
        });

    // Which language the middle rail is showing. Held by shared_ptr because
    // the language cards rebuild that rail from their own click handlers,
    // which outlive this function.
    auto selectedLang = std::make_shared<std::string>(anyOn ? languageName(activeLang) : "");

    auto* langRail = new PlayerRail("main/player/panel/languages"_i18n, kSubLangWidth, 720, false);
    auto* listRail = new PlayerRail("main/player/subtitle"_i18n, kSubListWidth, 720, false);
    listRail->setMarginLeft(kRailGap);

    // Filling the middle rail is done repeatedly, so it is a function of the
    // language rather than something built once alongside it.
    auto fillList = [listRail, options, selectedLang, selected, onPick](int64_t currentSid) {
        listRail->clear();
        if (selectedLang->empty()) {
            listRail->addCard("main/player/none"_i18n, "", "", selected < 0 && currentSid == 0, [onPick]() {
                PlayerSetting::selectedSubtitle = 0;
                // `sid` is a choice property ("no"/"auto"/an id) — set through
                // the command, which speaks all three, rather than setInt.
                MPVCore::instance().command("set", "sid", "no");
                if (onPick) onPick(-1);
            });
            return;
        }
        for (const SubOption& o : options) {
            if (languageName(o.lang) != *selectedLang) continue;
            int64_t id = o.id;
            int sidecar = o.sidecar;
            bool transcode = o.transcode;
            bool isSelected = sidecar >= 0 ? sidecar == selected
                              : transcode  ? id == PlayerSetting::selectedSubtitle
                                           : selected < 0 && id == currentSid;
            listRail->addCard(o.name, o.detail, "", isSelected, [id, sidecar, transcode, onPick]() {
                if (sidecar >= 0) {
                    // The file is fetched and sub-add'ed by the player; there is
                    // nothing to set here, and no mpv id to set it to yet.
                    if (onPick) onPick(sidecar);
                    return;
                }
                PlayerSetting::selectedSubtitle = id;
                if (transcode) {
                    MPVCore::instance().getCustomEvent()->fire(QUALITY_CHANGE, nullptr);
                } else {
                    std::string sid = std::to_string(id);
                    MPVCore::instance().command("set", "sid", sid.c_str());
                }
            });
        }
    };

    for (const std::string& name : langs) {
        // Selecting a language does NOT close the overlay: it repopulates the
        // rail beside it, which is the entire reason the two are separate.
        PlayerCard* card = langRail->addControl(name.empty() ? "main/player/none"_i18n : name, "", nullptr);
        // No tick here. The accent fill already says which language is open,
        // and a tick beside it reads as "this subtitle is on", which it is not.
        card->setShowTick(false);
    }
    row->addView(langRail);
    row->addView(listRail);

    // ---- rail 3: style ----------------------------------------------------
    auto* styleRail = new PlayerRail("main/player/panel/sub_style"_i18n, kSubStyleWidth, 720, false);
    styleRail->setMarginLeft(kRailGap);

    auto boldCard = std::make_shared<PlayerCard*>(nullptr);

    // Delay is not a stepper in the reference either: it opens the live sync
    // overlay, where the timing is nudged against the picture.
    auto delayCard = std::make_shared<PlayerCard*>(nullptr);
    *delayCard = styleRail->addSetting("main/player/panel/sub_delay"_i18n,
        fmt::format("{:+.1f} s", mpv.getOptionDouble("sub-delay")), [delayCard]() {
            brls::sync([delayCard]() {
                // The row keeps showing whatever the overlay was left at,
                // rather than the value it had when the panel opened.
                PlayerSetting::showSubsync([delayCard](double delay) {
                    if (*delayCard) (*delayCard)->setTrailingText(fmt::format("{:+.1f} s", delay));
                });
            });
        });

    // EVERY stepper below holds its own value.
    //
    // They used to read the property back from mpv, add the step, and write it
    // again — and mpv's property writes are asynchronous, so the read on the
    // second press still returned the value from before the first. Two presses
    // moved one step, three moved one or two depending on timing. The value is
    // seeded from mpv once, stepped locally, and written out; the number on
    // screen and the number mpv has can then never disagree.
    //
    // The writes go through the `set` COMMAND rather than the typed setters:
    // mpv will not convert several of these property types from the format
    // those setters use, and drops the write silently when it will not.
    auto& core = MPVCore::instance();

    // Every stepper below writes the setting as well as mpv: they are the same
    // values PlayerSettingsDataStore keeps, so a change made here has to
    // survive the session exactly as one made in Settings does. Before this
    // the panel moved mpv and nothing else, which is how the face reset itself
    // on every launch.
    auto persist = [](AppConfig::Item key, auto value) { AppConfig::instance().setItem(key, value); };

    auto size = std::make_shared<double>(core.getOptionDouble("sub-scale", 1.0));
    auto setSize = std::make_shared<std::function<void(const std::string&)>>();
    auto sizeText = [size]() { return fmt::format("{:.0f} %", *size * 100); };
    auto bumpSize = [size, setSize, sizeText](double delta) {
        return [size, setSize, sizeText, delta]() {
            *size = std::clamp(*size + delta, 0.5, 2.0);  // the reference's 50..200 %
            MPVCore::instance().setOption("sub-scale", fmt::format("{:.2f}", *size));
            MPVCore::SUB_SIZE = (int)std::lround(*size * 100);
            AppConfig::instance().setItem(AppConfig::SUB_SIZE, MPVCore::SUB_SIZE);
            (*setSize)(sizeText());
        };
    };
    *setSize = styleRail->addStepper(
        "main/player/panel/sub_size"_i18n, sizeText(), bumpSize(-0.1), bumpSize(0.1));

    auto bold = std::make_shared<bool>(core.getString("sub-bold") == "yes");
    *boldCard = styleRail->addSetting("main/player/panel/sub_bold"_i18n,
        *bold ? "main/player/panel/on"_i18n : "main/player/panel/off"_i18n, [boldCard, bold]() {
            auto& m = MPVCore::instance();
            *bold = !*bold;
            m.setOption("sub-bold", *bold ? "yes" : "no");
            MPVCore::SUB_BOLD = *bold;
            AppConfig::instance().setItem(AppConfig::SUB_BOLD, *bold);
            (*boldCard)->setTrailingText(*bold ? "main/player/panel/on"_i18n : "main/player/panel/off"_i18n);
            brls::Logger::debug("subtitles: sub-bold set to {}, mpv reports '{}'", *bold, m.getString("sub-bold"));
        });

    auto outline = std::make_shared<double>(core.getOptionDouble("sub-border-size", 1.0));
    auto setOut = std::make_shared<std::function<void(const std::string&)>>();
    auto outText = [outline]() { return fmt::format("{:.1f}", *outline); };
    auto bumpOut = [outline, setOut, outText](double delta) {
        return [outline, setOut, outText, delta]() {
            *outline = std::clamp(*outline + delta, 0.0, 5.0);  // the reference's 1..5, plus off
            MPVCore::instance().setOption("sub-border-size", fmt::format("{:.1f}", *outline));
            MPVCore::SUB_OUTLINE = *outline > 0;
            if (*outline > 0) MPVCore::SUB_OUTLINE_WIDTH = (int)std::lround(*outline);
            AppConfig::instance().setItem(AppConfig::SUB_OUTLINE, MPVCore::SUB_OUTLINE);
            AppConfig::instance().setItem(AppConfig::SUB_OUTLINE_WIDTH, MPVCore::SUB_OUTLINE_WIDTH);
            (*setOut)(outText());
        };
    };
    *setOut = styleRail->addStepper(
        "main/player/panel/sub_outline"_i18n, outText(), bumpOut(-0.5), bumpOut(0.5));

    // sub-pos counts DOWN from the top of the frame (100 = the bottom), so
    // "further up the screen" is a smaller number. Shown the way the reference
    // states it — a percentage UP FROM THE BOTTOM — which is 100 - sub-pos, and
    // is the same number the Playback setting shows. It used to count from a
    // hardcoded 98, so the panel and the setting disagreed by three.
    constexpr double kSubPosBase = 100;
    auto pos = std::make_shared<double>(core.getOptionDouble("sub-pos", kSubPosBase));
    auto setPos = std::make_shared<std::function<void(const std::string&)>>();
    auto posText = [pos]() { return fmt::format("{:.0f}", kSubPosBase - *pos); };
    auto bumpPos = [pos, setPos, posText](double delta) {
        return [pos, setPos, posText, delta]() {
            *pos = std::clamp(*pos - delta, 50.0, 120.0);  // the reference's -20..50 offset
            MPVCore::instance().setOption("sub-pos", fmt::format("{:.0f}", *pos));
            MPVCore::SUB_OFFSET = (int)std::lround(kSubPosBase - *pos);
            AppConfig::instance().setItem(AppConfig::SUB_OFFSET, MPVCore::SUB_OFFSET);
            (*setPos)(posText());
        };
    };
    *setPos = styleRail->addStepper(
        "main/player/panel/sub_offset"_i18n, posText(), bumpPos(-1), bumpPos(1));

    row->addView(styleRail);
    overlay->content()->addView(row);

    // The language cards' handlers need the rail they rebuild, which only
    // exists once every rail is built — so they are attached last.
    fillList(sid);
    for (size_t i = 0; i < langRail->cards().size() && i < langs.size(); i++) {
        std::string name = langs[i];
        PlayerCard* card = langRail->cards()[i];
        size_t which = i;
        card->registerClickAction([selectedLang, name, fillList, langRail, listRail, which](brls::View*) {
            *selectedLang = name;
            const auto& all = langRail->cards();
            for (size_t j = 0; j < all.size(); j++) all[j]->setSelected(j == which);
            fillList(MPVCore::instance().getInt("sid"));
            // Choosing the language is the first half of choosing a subtitle,
            // so focus carries straight on into the list it just built rather
            // than making the user step right into it.
            if (brls::View* target = listRail->focusTarget()) brls::Application::giveFocus(target);
            return true;
        });
        card->setSelected(name == *selectedLang);
        if (name == *selectedLang) langRail->setFocusTarget(card);
    }

    overlay->setFocusTarget(langRail->focusTarget());
    overlay->present();
}

void showAudio(const plex::Media* src) {
    auto& mpv = MPVCore::instance();

    auto* overlay = new PlayerOverlay(kAudPadX, kAudPadTop, kAudPadBottom);
    overlay->content()->addView(overlayTitle("main/player/audio"_i18n));

    auto* row = railRow();
    auto* tracks = new PlayerRail("main/player/audio"_i18n, kAudTrackWidth, 720, false);

    int64_t aidActive = mpv.getInt("aid");
    bool anyEmbedded = false;

    struct AudioTrack {
        int64_t id;
        std::string title, detail, lang;
    };
    std::vector<AudioTrack> embedded;
    int64_t count = mpv.getInt("track-list/count");
    for (int64_t n = 0; n < count; n++) {
        if (mpv.getString(fmt::format("track-list/{}/type", n)) != "audio") continue;
        AudioTrack t;
        t.id = mpv.getInt(fmt::format("track-list/{}/id", n));
        t.title = trackTitle(n, "audio", fmt::format("{} {}", "main/player/audio"_i18n, t.id));
        t.detail = trackDetail(n);
        t.lang = mpv.getString(fmt::format("track-list/{}/lang", n));
        embedded.push_back(std::move(t));
        anyEmbedded = true;
    }
    // English first, as in the subtitle rail — stable, so a file's own track
    // order survives within each language.
    std::stable_sort(embedded.begin(), embedded.end(), [](const AudioTrack& a, const AudioTrack& b) {
        return languageName(a.lang) == "English" && languageName(b.lang) != "English";
    });
    for (const AudioTrack& t : embedded) {
        int64_t id = t.id;
        tracks->addCard(t.title, t.detail, "", id == aidActive, [id]() {
            PlayerSetting::selectedAudio = id;
            MPVCore::instance().setInt("aid", id);
        });
    }

    if (!anyEmbedded && src != nullptr && !src->parts.empty()) {
        for (auto& s : src->parts.front().streams) {
            if (s.streamType != plex::streamTypeAudio) continue;
            int64_t id = s.id;
            tracks->addCard(s.displayTitle, "", "", id == PlayerSetting::selectedAudio, [id]() {
                PlayerSetting::selectedAudio = id;
                MPVCore::instance().getCustomEvent()->fire(QUALITY_CHANGE, nullptr);
            });
        }
    }
    row->addView(tracks);

    // The narrow column of controls beside the tracks. The reference's are
    // delay, amplification, centre mix and a persist toggle; mpv gives us the
    // first two honestly, so those are the two that are here.
    auto* controls = new PlayerRail("main/player/panel/audio_controls"_i18n, kAudControlWidth, 720, false);
    controls->setMarginLeft(24);  // spacing.md

    auto delayText = []() { return fmt::format("{:+.0f} ms", MPVCore::instance().getDouble("audio-delay") * 1000); };
    auto setDelay = std::make_shared<std::function<void(const std::string&)>>();
    auto bumpDelay = [setDelay, delayText](double deltaSec) {
        return [setDelay, delayText, deltaSec]() {
            auto& m = MPVCore::instance();
            m.setDouble("audio-delay", m.getDouble("audio-delay") + deltaSec);
            (*setDelay)(delayText());
        };
    };
    *setDelay = controls->addStepper(
        "main/player/panel/delay"_i18n, delayText(), bumpDelay(-0.05), bumpDelay(0.05));

    auto volText = []() { return fmt::format("{:.0f} %", MPVCore::instance().getDouble("volume")); };
    auto setVol = std::make_shared<std::function<void(const std::string&)>>();
    auto bumpVol = [setVol, volText](double delta) {
        return [setVol, volText, delta]() {
            auto& m = MPVCore::instance();
            m.setDouble("volume", std::clamp(m.getDouble("volume") + delta, 0.0, 200.0));
            (*setVol)(volText());
        };
    };
    *setVol = controls->addStepper("main/player/panel/boost"_i18n, volText(), bumpVol(-5), bumpVol(5));

    row->addView(controls);

    overlay->content()->addView(row);
    overlay->setFocusTarget(tracks->focusTarget());
    overlay->present();
}

namespace {

/// Builds (or rebuilds) a sources sheet's tab row and list. Separate from
/// showSources because an episode's sources arrive AFTER its sheet is on
/// screen, and the sheet then fills itself with exactly this.
void fillSourcesPanel(PlayerSidePanel* panel, const std::vector<plex::Media>& sources, int current,
    std::function<void(int)> onPick, std::function<void()> onReload) {
    panel->clearContents();

    if (sources.empty()) {
        auto* empty = new brls::Label();
        empty->setText("main/player/panel/sources_empty"_i18n);
        empty->setFontSize(32);
        empty->setTextColor(nvgRGBA(255, 255, 255, 179));
        panel->body()->addView(empty);
        return;
    }

    auto* rail = new PlayerRail("", kSheetInnerWidth, brls::Application::contentHeight - 420, false);

    std::vector<std::string> addons;
    for (const plex::Media& m : sources)
        if (!m.addonName.empty() && std::find(addons.begin(), addons.end(), m.addonName) == addons.end())
            addons.push_back(m.addonName);

    auto activeAddon = std::make_shared<std::string>();  // empty = All
    auto fill = [rail, sources, current, onPick, activeAddon, panel]() {
        rail->clear();
        for (size_t i = 0; i < sources.size(); i++) {
            const plex::Media& m = sources[i];
            if (!activeAddon->empty() && m.addonName != *activeAddon) continue;
            std::string name = m.label.empty() ? m.addonName : m.label;
            std::string meta = m.addonName;
            if (!m.videoResolution.empty()) meta += (meta.empty() ? "" : " · ") + m.videoResolution;
            int index = (int)i;
            rail->addCard(name, m.detail, meta, index == current, [onPick, index]() {
                if (onPick) onPick(index);
            });
        }
        // The rows are new after every tab change, so the routes are too.
        panel->linkTabs(rail->cards().empty() ? nullptr : (brls::View*)rail->cards().front());
    };

    auto* tabs = panel->tabs();
    tabs->setMarginBottom(24);
    if (onReload) {
        auto* refresh = PlayerPill::icon(kRefresh);
        refresh->setMarginRight(24);  // spacing.md
        refresh->registerClickAction([onReload](brls::View*) {
            brls::Application::popActivity(brls::TransitionAnimation::NONE, [onReload]() { onReload(); });
            return true;
        });
        tabs->addView(refresh);
    }

    std::vector<PlayerPill*> pills;
    auto select = std::make_shared<std::function<void(size_t)>>();
    std::vector<std::string> names;
    names.push_back("");  // All
    for (auto& a : addons) names.push_back(a);

    for (size_t i = 0; i < names.size(); i++) {
        auto* pill = new PlayerPill(names[i].empty() ? "main/player/panel/all"_i18n : names[i], i == 0);
        pill->setMarginRight(24);
        pills.push_back(pill);
        tabs->addView(pill);
    }
    *select = [pills, names, activeAddon, fill](size_t which) {
        *activeAddon = names[which];
        for (size_t j = 0; j < pills.size(); j++) pills[j]->setSelected(j == which);
        fill();
    };
    for (size_t i = 0; i < pills.size(); i++) {
        size_t which = i;
        pills[i]->registerClickAction([select, which](brls::View*) {
            (*select)(which);
            return true;
        });
    }

    fill();
    panel->body()->addView(rail);
    panel->setFocusTarget(rail->focusTarget());
    if (brls::View* t = rail->focusTarget()) brls::Application::giveFocus(t);
}

/// A single line in the body — "Loading…", or why nothing came back.
void sheetMessage(PlayerSidePanel* panel, const std::string& text) {
    panel->clearContents();
    auto* label = new brls::Label();
    label->setText(text);
    label->setFontSize(32);
    label->setTextColor(nvgRGBA(255, 255, 255, 179));
    panel->body()->addView(label);
}

}  // namespace

void showSources(const std::string& subtitle, const std::vector<plex::Media>& sources, int current,
    std::function<void(int)> onPick, std::function<void()> onReload) {
    auto* panel = new PlayerSidePanel("main/player/sources"_i18n, subtitle);
    fillSourcesPanel(panel, sources, current, std::move(onPick), std::move(onReload));
    panel->present();
}

void showSourcesFor(const plex::Item& item, const std::string& subtitle,
    std::function<void(plex::Item, std::vector<plex::Media>, int)> onPick) {
    auto* panel = new PlayerSidePanel("main/player/sources"_i18n, subtitle);
    // The sheet goes up at once and fills itself when the addons answer, so
    // picking an episode feels like the rest of the player rather than
    // handing the screen over to the full-screen picker.
    sheetMessage(panel, "main/stremio/source/loading"_i18n);
    panel->present();

    // Backing out while the fan-out is still running is entirely ordinary, and
    // the panel is gone by the time it lands.
    auto alive = std::make_shared<bool>(true);
    panel->setOnDestroy([alive]() { *alive = false; });

    std::string id = item.ratingKey;
    AppConfig::instance().backend().getItemDetail(
        id, true,
        [alive, panel, item, onPick](const media::Item& full) {
            if (!*alive) return;
            plex::Item chosen = full.title.empty() ? item : full;
            auto sources = full.media;
            fillSourcesPanel(
                panel, sources, -1,
                [chosen, sources, onPick](int picked) {
                    // NO pop here. PlayerRail::addCard already closed this
                    // sheet before calling us, and popping a second time took
                    // the PLAYER down with it — which is why picking a source
                    // for another episode looked like it just closed the one
                    // that was playing.
                    if (onPick) onPick(chosen, sources, picked);
                },
                nullptr);
        },
        [alive, panel](const std::string& ex) {
            brls::Logger::warning("player sources: {}", ex);
            if (*alive) sheetMessage(panel, "main/stremio/source/failed"_i18n);
        });
}

namespace {

/// The reference's episode row (EpisodesSidePanel.EpisodeItem), doubled: a
/// 260x180 still with the S/E code across its bottom-left and a state badge
/// top-right, then the title, the air date and two lines of synopsis.
class EpisodeRow : public brls::Box {
public:
    EpisodeRow(const plex::Item& ep, bool current) {
        auto theme = brls::Application::getTheme();
        this->setAxis(brls::Axis::ROW);
        this->setAlignItems(brls::AlignItems::FLEX_START);
        this->setPadding(20);         // 10dp
        this->setCornerRadius(32);    // radii.xl
        this->setBorderThickness(4);  // the focus ring, spacing.xxs
        this->setFocusable(true);
        this->setHideHighlightBackground(true);
        this->setHideHighlightBorder(true);

        auto* thumbBox = new brls::Box();
        thumbBox->setDimensions(260, 180);
        thumbBox->setShrink(0);
        thumbBox->setCornerRadius(24);  // radii.md
        thumbBox->setClipsToBounds(true);
        thumbBox->setBackgroundColor(theme.getColor("color/grey_2"));

        this->still = new brls::Image();
        this->still->setDimensions(260, 180);
        this->still->setScalingType(brls::ImageScalingType::FILL);
        thumbBox->addView(this->still);
        if (!ep.thumb.empty()) Image::load(this->still, ep.thumb, 260, 180);

        auto* code = new brls::Box();
        code->setPositionType(brls::PositionType::ABSOLUTE);
        code->setPositionLeft(16);
        code->setPositionBottom(16);
        code->setCornerRadius(12);
        code->setBackgroundColor(nvgRGBA(0, 0, 0, 191));
        code->setPadding(8, 16, 8, 16);
        auto* codeLabel = new brls::Label();
        codeLabel->setText(fmt::format("S{}E{}", ep.parentIndex, ep.index));
        codeLabel->setFontSize(24);  // labelMedium
        codeLabel->setTextColor(nvgRGB(255, 255, 255));
        code->addView(codeLabel);
        thumbBox->addView(code);

        // Playing now, or already watched — the reference marks both, in the
        // same disc, with an eye and a tick.
        if (current || ep.played()) {
            auto* badge = new brls::Box();
            badge->setPositionType(brls::PositionType::ABSOLUTE);
            badge->setPositionTop(12);
            badge->setPositionRight(12);
            badge->setDimensions(44, 44);
            badge->setCornerRadius(22);
            badge->setAlignItems(brls::AlignItems::CENTER);
            badge->setJustifyContent(brls::JustifyContent::CENTER);
            badge->setBackgroundColor(nvgRGBA(0, 0, 0, 179));
            auto* glyph = new SVGImage();
            glyph->setDimensions(28, 28);
            char svg[400];
            std::snprintf(svg, sizeof(svg),
                R"(<svg width="24" height="24" viewBox="0 0 24 24"><path d="%s" fill="%s"/></svg>)",
                current ? kEye : kCheck, hexColor(theme.getColor("color/app")).c_str());
            glyph->setImageFromSVGString(svg);
            badge->addView(glyph);
            thumbBox->addView(badge);
        }
        this->addView(thumbBox);

        auto* text = new brls::Box();
        text->setAxis(brls::Axis::COLUMN);
        text->setGrow(1);
        text->setMarginLeft(28);  // 14dp

        this->titleLabel = new brls::Label();
        this->titleLabel->setText(ep.title.empty() ? fmt::format("S{}E{}", ep.parentIndex, ep.index) : ep.title);
        this->titleLabel->setFontSize(32);  // titleMedium
        this->titleLabel->setSingleLine(true);
        text->addView(this->titleLabel);

        if (!ep.originallyAvailableAt.empty()) {
            auto* date = new brls::Label();
            date->setText(ep.originallyAvailableAt);
            date->setFontSize(24);  // bodySmall
            date->setMarginTop(8);
            date->setTextColor(theme.getColor("font/tertiary"));
            text->addView(date);
        }
        if (!ep.summary.empty()) {
            auto* over = new brls::Label();
            over->setText(ep.summary);
            over->setFontSize(24);
            over->setMarginTop(8);
            over->setTextColor(theme.getColor("font/grey"));
            text->addView(over);
        }
        this->addView(text);

        this->applyColors();
        this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
    }

    void onFocusGained() override {
        brls::Box::onFocusGained();
        this->applyColors();
    }
    void onFocusLost() override {
        brls::Box::onFocusLost();
        this->applyColors();
    }

private:
    void applyColors() {
        auto theme = brls::Application::getTheme();
        NVGcolor accent = theme.getColor("color/app");
        bool focused = this->isFocused();
        NVGcolor wash = accent;
        wash.a = 0.20f;
        this->setBackgroundColor(focused ? wash : theme.getColor("color/surface"));
        this->setBorderColor(focused ? accent : nvgRGBA(0, 0, 0, 0));
        this->titleLabel->setTextColor(nvgRGB(255, 255, 255));
    }

    brls::Image* still = nullptr;
    brls::Label* titleLabel = nullptr;
};

}  // namespace

void showEpisodes(const std::string& subtitle, const std::vector<plex::Item>& episodes, int current,
    std::function<void(int)> onPick) {
    auto* panel = new PlayerSidePanel("main/player/episode"_i18n, subtitle);

    // Seasons in the order the reference sorts them: the numbered ones
    // ascending, specials (season 0) last.
    std::vector<int64_t> seasons;
    for (const plex::Item& e : episodes)
        if (std::find(seasons.begin(), seasons.end(), e.parentIndex) == seasons.end()) seasons.push_back(e.parentIndex);
    std::sort(seasons.begin(), seasons.end(), [](int64_t a, int64_t b) {
        if ((a == 0) != (b == 0)) return b == 0;
        return a < b;
    });

    int64_t openSeason = seasons.empty() ? 0 : seasons.front();
    if (current >= 0 && current < (int)episodes.size()) openSeason = episodes[current].parentIndex;

    auto* scroll = new brls::ScrollingFrame();
    scroll->setGrow(1);
    scroll->setScrollingIndicatorVisible(false);
    auto* list = new brls::Box();
    list->setAxis(brls::Axis::COLUMN);
    scroll->setContentView(list);

    auto focusRow = std::make_shared<brls::View*>(nullptr);
    auto fill = [list, episodes, current, onPick, focusRow, panel](int64_t season) {
        list->clearViews();
        *focusRow = nullptr;
        brls::View* firstRow = nullptr;
        for (size_t i = 0; i < episodes.size(); i++) {
            if (episodes[i].parentIndex != season) continue;
            int index = (int)i;
            auto* row = new EpisodeRow(episodes[i], index == current);
            if (firstRow) row->setMarginTop(16);  // spacing.sm
            row->registerClickAction([onPick, index](brls::View*) {
                brls::Application::popActivity(brls::TransitionAnimation::NONE, [onPick, index]() {
                    if (onPick) onPick(index);
                });
                return true;
            });
            list->addView(row);
            if (!firstRow) firstRow = row;
            if (index == current || !*focusRow) *focusRow = row;
        }
        // The rows are new after every season change, so the routes are too.
        panel->linkTabs(firstRow);
    };

    auto* tabs = panel->tabs();
    tabs->setMarginBottom(24);
    std::vector<PlayerPill*> pills;
    for (size_t i = 0; i < seasons.size(); i++) {
        std::string label = seasons[i] == 0 ? "main/player/panel/specials"_i18n
                                            : fmt::format(fmt::runtime("main/player/panel/season"_i18n), seasons[i]);
        auto* pill = new PlayerPill(label, seasons[i] == openSeason);
        pill->setMarginRight(24);
        pills.push_back(pill);
        tabs->addView(pill);
    }
    for (size_t i = 0; i < pills.size(); i++) {
        int64_t season = seasons[i];
        size_t which = i;
        auto all = pills;
        pills[i]->registerClickAction([fill, season, all, which](brls::View*) {
            for (size_t j = 0; j < all.size(); j++) all[j]->setSelected(j == which);
            fill(season);
            return true;
        });
    }

    fill(openSeason);
    panel->body()->addView(scroll);
    panel->setFocusTarget(*focusRow);
    panel->present();
}

namespace {

/// One labelled field, in the reference's InfoItem shape: a tertiary caption
/// over a white value. Skipped entirely when there is no value, as it is there.
void addField(brls::Box* row, const std::string& label, const std::string& value) {
    if (value.empty()) return;
    auto* col = new brls::Box();
    col->setAxis(brls::Axis::COLUMN);
    col->setMarginRight(72);  // 36dp between fields

    auto* cap = new brls::Label();
    cap->setText(label);
    cap->setFontSize(24);  // bodySmall
    cap->setTextColor(brls::Application::getTheme().getColor("font/tertiary"));
    col->addView(cap);

    auto* val = new brls::Label();
    val->setText(value);
    val->setFontSize(32);  // titleMedium
    val->setSingleLine(true);
    val->setTextColor(nvgRGB(255, 255, 255));
    col->addView(val);

    row->addView(col);
}

brls::Box* section(brls::Box* parent, const std::string& title) {
    auto* cap = new brls::Label();
    cap->setText(title);
    cap->setFontSize(24);  // labelMedium
    cap->setFontWeight("semibold");
    cap->setTextColor(brls::Application::getTheme().getColor("font/tertiary"));
    cap->setMarginTop(32);  // spacing.lg between sections
    parent->addView(cap);

    auto* row = new brls::Box();
    row->setAxis(brls::Axis::ROW);
    row->setMarginTop(8);  // spacing.xs
    parent->addView(row);
    return row;
}

}  // namespace

void showStreamInfo(const plex::Media* src, const std::string& addonName, std::function<void()> onToggleStats) {
    auto& mpv = MPVCore::instance();

    // Top-anchored like the other two. It reads as a page of fields, and a
    // page that grows downward from its heading is the one people can scan.
    auto* overlay = new PlayerOverlay(kInfoPadX, kInfoPadTop, kInfoPadBottom);
    auto* col = overlay->content();
    col->addView(overlayTitle("main/player/info"_i18n));

    // SOURCE — who served this, and under what name.
    if (!addonName.empty() || (src && !src->label.empty())) {
        auto* row = section(col, "main/player/panel/sec_source"_i18n);
        addField(row, "main/player/panel/addon"_i18n, addonName);
        if (src) addField(row, "main/player/panel/stream"_i18n, src->label);
    }

    // FILE
    std::string filename = mpv.getString("filename");
    int64_t fileSize = mpv.getInt("file-size");
    if (!filename.empty() || fileSize > 0) {
        auto* row = section(col, "main/player/panel/sec_file"_i18n);
        addField(row, "main/player/panel/filename"_i18n, filename);
        addField(row, "main/player/panel/size"_i18n, fileSize > 0 ? formatSize(fileSize) : "");
        addField(row, "main/player/panel/container"_i18n, mpv.getString("file-format"));
    }

    // VIDEO
    int w = (int)mpv.getInt("width"), h = (int)mpv.getInt("height");
    std::string vcodec = mpv.getString("video-codec");
    double fps = mpv.getDouble("container-fps");
    int64_t vbitrate = mpv.getInt("video-bitrate");
    if (w > 0 || !vcodec.empty()) {
        auto* row = section(col, "main/player/panel/sec_video"_i18n);
        addField(row, "main/player/panel/codec"_i18n, vcodec);
        addField(row, "main/player/panel/resolution"_i18n, w > 0 && h > 0 ? formatResolution(w, h) : "");
        addField(row, "main/player/panel/framerate"_i18n, fps > 0 ? fmt::format("{:.3f} fps", fps) : "");
        addField(row, "main/player/panel/bitrate"_i18n, vbitrate > 0 ? formatBitrate(vbitrate) : "");
    }

    // AUDIO
    std::string acodec = mpv.getString("audio-codec-name");
    int64_t achannels = mpv.getInt("audio-params/channel-count");
    int64_t arate = mpv.getInt("audio-params/samplerate");
    if (!acodec.empty() || achannels > 0) {
        auto* row = section(col, "main/player/panel/sec_audio"_i18n);
        addField(row, "main/player/panel/codec"_i18n, acodec);
        addField(row, "main/player/panel/channels"_i18n, achannels > 0 ? fmt::format("{}", achannels) : "");
        addField(row, "main/player/panel/samplerate"_i18n, arate > 0 ? fmt::format("{} kHz", arate / 1000) : "");
        addField(row, "main/player/panel/bitrate"_i18n,
            mpv.getInt("audio-bitrate") > 0 ? formatBitrate(mpv.getInt("audio-bitrate")) : "");
    }

    // SUBTITLE
    int64_t sid = mpv.getInt("sid");
    if (sid > 0) {
        auto* row = section(col, "main/player/panel/sec_subtitle"_i18n);
        addField(row, "main/player/panel/name"_i18n, mpv.getString("current-tracks/sub/title"));
        addField(row, "main/player/panel/codec"_i18n, mpv.getString("current-tracks/sub/codec"));
        addField(row, "main/player/panel/language"_i18n, mpv.getString("current-tracks/sub/lang"));
    }

    // DIAGNOSTICS — the reference's playerStatsHudEnabled puts a button here
    // and nowhere else, so this row exists only while that setting is on. The
    // fields above are a snapshot; the overlay it opens is the live version of
    // the same numbers, and the panel gets out of the way so they can be read.
    if (MPVCore::PLAYER_STATS_HUD && onToggleStats) {
        auto* rail = new PlayerRail("", 640, 200, false);
        rail->setMarginTop(32);  // spacing.lg, as between the sections above
        rail->addSetting("main/player/panel/stats_hud"_i18n, "", [onToggleStats]() {
            brls::Application::popActivity(brls::TransitionAnimation::NONE, [onToggleStats]() { onToggleStats(); });
        });
        col->addView(rail);
        overlay->setFocusTarget(rail->focusTarget());
    }

    overlay->present();
}

}  // namespace player_panels
