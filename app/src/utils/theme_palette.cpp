/*
    GMCA — per-backend theme palettes (see theme_palette.hpp).

    Values are { accent, accentGlowTop, onAccentText, listValue }, dark then
    light. Hex are the verified brand colors. The light variant flips the accent
    toward a darker shade so it stays legible on a light background; the dark
    variant is the primary "dark theater" experience.
*/

#include "utils/theme_palette.hpp"

namespace plenx {

// ---- DEFAULT (pleNx neutral) ------------------------------------------------
// Off-white accent #E8E8E8 reads as "white" on the near-black #0D0E11 chrome
// without the glare of pure #FFFFFF as a large fill. Light variant inverts to a
// dark-grey accent so it stays visible on a light background.
static const ThemeColors kDefault = {
    /* dark  */ {{0xE8, 0xE8, 0xE8}, {0xFF, 0xFF, 0xFF}, {0x10, 0x12, 0x16}, {0xC9, 0xC9, 0xC9}},
    /* light */ {{0x3A, 0x3A, 0x3A}, {0x5A, 0x5A, 0x5A}, {0xFF, 0xFF, 0xFF}, {0x77, 0x77, 0x77}},
};

// ---- PLEX -------------------------------------------------------------------
// Current brand gold "Corn" #EBAF00 (Aug-2024 logo redesign), #F7C600 as the
// brighter glow top-stop, verified Plex black #191919 on the gold.
static const ThemeColors kPlex = {
    /* dark  */ {{0xEB, 0xAF, 0x00}, {0xF7, 0xC6, 0x00}, {0x19, 0x19, 0x19}, {0xC9, 0xA8, 0x6A}},
    /* light */ {{0xCC, 0x7C, 0x19}, {0xEB, 0xAF, 0x00}, {0x19, 0x19, 0x19}, {0xA6, 0x75, 0x1D}},
};

// ---- JELLYFIN ---------------------------------------------------------------
// Official accent #00A4DC; the logo-gradient purple #AA5CC3 tops the glow as a
// nod to the brand's blue->purple gradient.
static const ThemeColors kJellyfin = {
    /* dark  */ {{0x00, 0xA4, 0xDC}, {0xAA, 0x5C, 0xC3}, {0x00, 0x10, 0x18}, {0x5F, 0xC3, 0xE6}},
    /* light */ {{0x00, 0x83, 0xB0}, {0x00, 0xA4, 0xDC}, {0x00, 0x10, 0x18}, {0x1B, 0x7E, 0x9E}},
};

// ---- EMBY -------------------------------------------------------------------
// Official Emby green #52B54B (verified in Emby's own dark skin); #6FCF68 is a
// lighter synthesized tint for the glow top-stop (Emby publishes no 2nd color).
static const ThemeColors kEmby = {
    /* dark  */ {{0x52, 0xB5, 0x4B}, {0x6F, 0xCF, 0x68}, {0x0A, 0x14, 0x09}, {0x8F, 0xD0, 0x89}},
    /* light */ {{0x3E, 0x84, 0x37}, {0x52, 0xB5, 0x4B}, {0x0A, 0x14, 0x09}, {0x35, 0x7A, 0x2E}},
};

// ---- STREMIO ----------------------------------------------------------------
// Vivid logo-gradient purple #7B5BF5 (the recognizable accent, not the muted
// system token #664181); #A970CD purple-pink tops the glow. White on-accent
// text — the only theme dark enough to need it instead of a dark label.
static const ThemeColors kStremio = {
    /* dark  */ {{0x7B, 0x5B, 0xF5}, {0xA9, 0x70, 0xCD}, {0xFF, 0xFF, 0xFF}, {0xA8, 0x8E, 0xF7}},
    /* light */ {{0x5E, 0x45, 0xC9}, {0x7B, 0x5B, 0xF5}, {0xFF, 0xFF, 0xFF}, {0x6B, 0x53, 0xC9}},
};

// ---- NUVIO ------------------------------------------------------------------
// Verified against NuvioMedia/NuvioTV's own theme tokens (Color.kt/
// PrimitiveTokens.kt/ThemeColors.kt): its default "White" theme has no
// colored accent at all — secondary/focusRing are near-white/white, onSecondary
// a near-black. Light variant follows every other theme's own pattern
// (inverted toward a darker neutral for legibility on a light background),
// since Nuvio itself has no light-mode equivalent to check against.
static const ThemeColors kNuvio = {
    /* dark  */ {{0xF5, 0xF5, 0xF5}, {0xFF, 0xFF, 0xFF}, {0x11, 0x11, 0x11}, {0xE0, 0xE0, 0xE0}},
    /* light */ {{0x4D, 0x4D, 0x4D}, {0x9E, 0x9E, 0x9E}, {0xFF, 0xFF, 0xFF}, {0x80, 0x80, 0x80}},
};

const ThemeColors& defaultPalette() { return kDefault; }

const ThemeColors& backendPalette(media::BackendType type) {
    switch (type) {
        case media::BackendType::Plex: return kPlex;
        case media::BackendType::Jellyfin: return kJellyfin;
        case media::BackendType::Emby: return kEmby;
        case media::BackendType::Stremio: return kStremio;
        case media::BackendType::Nuvio: return kNuvio;
    }
    return kPlex;
}

// ---- NuvioTV's colour themes ------------------------------------------------
//
// Values transcribed from NuvioTV's own tokens, not eyeballed: AppTheme.kt for
// the list and its order, ThemeColors.kt + SupporterThemeColors.kt for the
// palettes, PrimitiveTokens.kt for the primitives they reference.
//
//   accent        = palette.secondary
//   accentGlowTop = palette.focusRing
//   onAccentText  = palette.onSecondary (white unless the accent is light)
//   listValue     = palette.secondaryVariant
//
// NuvioTV has no light mode, so each light variant follows the convention the
// backend palettes in this file already use: the darker `secondaryVariant` cut
// as the accent (legible on a light ground) with white text on it.
namespace {

constexpr ThemeColors named(AccentRGB secondary, AccentRGB focusRing, AccentRGB onSecondary,
    AccentRGB secondaryVariant) {
    return ThemeColors{
        /* dark  */ {secondary, focusRing, onSecondary, secondaryVariant},
        /* light */ {secondaryVariant, secondary, {0xFF, 0xFF, 0xFF}, secondary},
    };
}

const AccentRGB kNear = {0x11, 0x11, 0x11};   // neutral925, text on a light accent
const AccentRGB kWhite = {0xFF, 0xFF, 0xFF};

const std::vector<NamedTheme> kNamed = {
    {"gold", "Gold", named({0xE8, 0xA9, 0x1C}, {0xFF, 0xD4, 0x5C}, kNear, {0x9A, 0x62, 0x00})},
    {"jade", "Jade", named({0x22, 0xD3, 0x7C}, {0x7B, 0xF0, 0x8D}, kNear, {0x0B, 0xBF, 0x9A})},
    {"rose_gold", "Rose Gold", named({0xEC, 0x70, 0xA9}, {0xFF, 0xB3, 0x7A}, kNear, {0xB7, 0x5A, 0xFF})},
    {"arctic_blue", "Arctic Blue", named({0x31, 0x85, 0xF5}, {0x4D, 0xE3, 0xFF}, kWhite, {0x4D, 0x55, 0xE8})},
    {"graphite", "Graphite", named({0xAA, 0xB2, 0xBE}, {0xF3, 0xF5, 0xF7}, kNear, {0x68, 0x73, 0x81})},
    {"crimson", "Crimson", named({0xE5, 0x39, 0x35}, {0xFF, 0x52, 0x52}, kWhite, {0xC6, 0x28, 0x28})},
    {"ocean", "Ocean", named({0x1E, 0x88, 0xE5}, {0x42, 0xA5, 0xF5}, kWhite, {0x15, 0x65, 0xC0})},
    {"violet", "Violet", named({0x8E, 0x24, 0xAA}, {0xAB, 0x47, 0xBC}, kWhite, {0x6A, 0x1B, 0x9A})},
    {"emerald", "Emerald", named({0x43, 0xA0, 0x47}, {0x66, 0xBB, 0x6A}, kWhite, {0x2E, 0x7D, 0x32})},
    {"amber", "Amber", named({0xFB, 0x8C, 0x00}, {0xFF, 0xA7, 0x26}, kWhite, {0xEF, 0x6C, 0x00})},
    {"rose", "Rose", named({0xD8, 0x1B, 0x60}, {0xEC, 0x40, 0x7A}, kWhite, {0xC2, 0x18, 0x5B})},
    {"white", "White", named({0xF5, 0xF5, 0xF5}, {0xFF, 0xFF, 0xFF}, kNear, {0xE0, 0xE0, 0xE0})},
};

}  // namespace

const std::vector<NamedTheme>& namedThemes() { return kNamed; }

const ThemeColors* namedPalette(const std::string& id) {
    if (id.empty() || id == "auto") return nullptr;
    for (auto& t : kNamed)
        if (id == t.id) return &t.colors;
    return nullptr;
}

}  // namespace plenx
