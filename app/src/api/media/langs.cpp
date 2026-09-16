/*
    GMCA — subtitle language helpers (see api/media/langs.hpp).

    The catalog is NuvioTV's own (AVAILABLE_SUBTITLE_LANGUAGES): its 78
    languages, its codes, its English display names and its alphabetical order,
    so the picker reads exactly as the reference's does. It used to be a
    shorter list of endonyms, which showed a viewer fewer languages than the
    app could actually match.

    Each row also carries the aliases we want to RECOGNISE — 639-2/B and /T,
    common OpenSubtitles variants and the English name — because an addon or a
    muxed track names its language in whichever of those it feels like. An
    alias -> index map is built once on first use for O(1) lookups.
*/

#include "api/media/langs.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace media {

namespace {

/// One catalog row: canonical code, English name, and the aliases that map onto it.
struct LangRow {
    const char* code;
    const char* display;
    std::vector<const char*> aliases;
};

// The reference's own order: alphabetical by English name, with its regional
// variants sitting beside their parent (Chinese, Portuguese, Spanish).
// Aliases are lowercase; the `code` itself is matched implicitly too.
const std::vector<LangRow>& rows() {
    static const std::vector<LangRow> table = {
        {"af", "Afrikaans", {"afr", "afrikaans"}},
        {"sq", "Albanian", {"alb", "sqi", "albanian"}},
        {"am", "Amharic", {"amh", "amharic"}},
        {"ar", "Arabic", {"ara", "arabic"}},
        {"hy", "Armenian", {"arm", "hye", "armenian"}},
        {"az", "Azerbaijani", {"aze", "azerbaijani"}},
        {"eu", "Basque", {"baq", "eus", "basque"}},
        {"be", "Belarusian", {"bel", "belarusian"}},
        {"bn", "Bengali", {"ben", "bengali"}},
        {"bs", "Bosnian", {"bos", "bosnian"}},
        {"bg", "Bulgarian", {"bul", "bulgarian"}},
        {"my", "Burmese", {"bur", "mya", "burmese"}},
        {"ca", "Catalan", {"cat", "catalan"}},
        {"zh", "Chinese", {"chi", "zho", "zhe", "cn", "chinese"}},
        {"zh-CN", "Chinese (Simplified)", {"zh-hans", "zh_cn", "zhs", "chs", "simplified chinese"}},
        {"zh-TW", "Chinese (Traditional)", {"zh-hant", "zh_tw", "zht", "cht", "traditional chinese"}},
        {"hr", "Croatian", {"hrv", "scr", "croatian"}},
        {"cs", "Czech", {"cze", "ces", "czech"}},
        {"da", "Danish", {"dan", "danish"}},
        {"nl", "Dutch", {"dut", "nld", "dutch"}},
        {"en", "English", {"eng", "english"}},
        {"et", "Estonian", {"est", "estonian"}},
        {"tl", "Filipino", {"tgl", "fil", "filipino", "tagalog"}},
        {"fi", "Finnish", {"fin", "finnish"}},
        {"fr", "French", {"fre", "fra", "fr-ca", "french"}},
        {"gl", "Galician", {"glg", "galician"}},
        {"ka", "Georgian", {"geo", "kat", "georgian"}},
        {"de", "German", {"ger", "deu", "german"}},
        {"el", "Greek", {"gre", "ell", "greek"}},
        {"gu", "Gujarati", {"guj", "gujarati"}},
        {"he", "Hebrew", {"heb", "iw", "hebrew"}},
        {"hi", "Hindi", {"hin", "hindi"}},
        {"hu", "Hungarian", {"hun", "hungarian"}},
        {"is", "Icelandic", {"ice", "isl", "icelandic"}},
        {"id", "Indonesian", {"ind", "in", "indonesian"}},
        {"ga", "Irish", {"gle", "irish"}},
        {"it", "Italian", {"ita", "italian"}},
        {"ja", "Japanese", {"jpn", "japanese"}},
        {"kn", "Kannada", {"kan", "kannada"}},
        {"kk", "Kazakh", {"kaz", "kazakh"}},
        {"km", "Khmer", {"khm", "khmer", "cambodian"}},
        {"ko", "Korean", {"kor", "korean"}},
        {"lo", "Lao", {"lao", "laotian"}},
        {"lv", "Latvian", {"lav", "latvian"}},
        {"lt", "Lithuanian", {"lit", "lithuanian"}},
        {"mk", "Macedonian", {"mac", "mkd", "macedonian"}},
        {"ms", "Malay", {"may", "msa", "malay"}},
        {"ml", "Malayalam", {"mal", "malayalam"}},
        {"mt", "Maltese", {"mlt", "maltese"}},
        {"mr", "Marathi", {"mar", "marathi"}},
        {"mn", "Mongolian", {"mon", "mongolian"}},
        {"ne", "Nepali", {"nep", "nepali"}},
        {"no", "Norwegian", {"nor", "nob", "nno", "norwegian"}},
        {"pa", "Punjabi", {"pan", "punjabi", "panjabi"}},
        {"fa", "Persian", {"per", "fas", "persian", "farsi"}},
        {"pl", "Polish", {"pol", "polish"}},
        {"pt", "Portuguese (Portugal)", {"por", "pt-pt", "portuguese"}},
        {"pt-br", "Portuguese (Brazil)", {"pob", "pt_br", "brazilian portuguese"}},
        {"ro", "Romanian", {"rum", "ron", "romanian", "moldavian"}},
        {"ru", "Russian", {"rus", "russian"}},
        {"sr", "Serbian", {"srp", "scc", "serbian"}},
        {"si", "Sinhala", {"sin", "sinhala", "sinhalese"}},
        {"sk", "Slovak", {"slo", "slk", "slovak"}},
        {"sl", "Slovenian", {"slv", "slovenian", "slovene"}},
        {"es", "Spanish", {"spa", "spl", "spn", "spanish"}},
        {"es-419", "Spanish (Latin America)", {"es_419", "spanish (latin america)", "latin american spanish"}},
        {"sw", "Swahili", {"swa", "swahili"}},
        {"sv", "Swedish", {"swe", "swedish"}},
        {"ta", "Tamil", {"tam", "tamil"}},
        {"te", "Telugu", {"tel", "telugu"}},
        {"th", "Thai", {"tha", "thai"}},
        {"tr", "Turkish", {"tur", "turkish"}},
        {"uk", "Ukrainian", {"ukr", "ukrainian"}},
        {"ur", "Urdu", {"urd", "urdu"}},
        {"uz", "Uzbek", {"uzb", "uzbek"}},
        {"vi", "Vietnamese", {"vie", "vietnamese"}},
        {"cy", "Welsh", {"wel", "cym", "welsh"}},
        {"zu", "Zulu", {"zul", "zulu"}},
    };
    return table;
}

/// Trim + lowercase.
std::string normalize(const std::string& raw);

/// alias/code -> row index, built once. The CODE is normalised on the way in
/// as well as the aliases: the catalog spells its regional variants the way the
/// reference does ("zh-CN"), lookups arrive lowercased, and an unnormalised key
/// would miss — sending "zh-cn" down the prefix fallback to plain Chinese and
/// losing the Simplified/Traditional distinction entirely.
const std::unordered_map<std::string, size_t>& aliasIndex() {
    static const std::unordered_map<std::string, size_t> map = [] {
        std::unordered_map<std::string, size_t> m;
        const auto& t = rows();
        for (size_t i = 0; i < t.size(); ++i) {
            m[normalize(t[i].code)] = i;
            for (const char* a : t[i].aliases) m[normalize(a)] = i;
        }
        return m;
    }();
    return map;
}

std::string normalize(const std::string& raw) {
    size_t b = 0, e = raw.size();
    while (b < e && std::isspace((unsigned char)raw[b])) ++b;
    while (e > b && std::isspace((unsigned char)raw[e - 1])) --e;
    std::string s = raw.substr(b, e - b);
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

/// Resolve a raw lang to a catalog index, or npos. Tries the full normalized
/// string, then (for region-tagged codes like "pt-br") the part before '-'.
size_t lookup(const std::string& raw) {
    const auto& idx = aliasIndex();
    std::string s = normalize(raw);
    if (s.empty()) return std::string::npos;
    auto it = idx.find(s);
    if (it != idx.end()) return it->second;
    auto dash = s.find('-');
    if (dash != std::string::npos) {
        it = idx.find(s.substr(0, dash));
        if (it != idx.end()) return it->second;
    }
    return std::string::npos;
}

}  // namespace

std::string subtitleLangCode(const std::string& raw) {
    size_t i = lookup(raw);
    return i == std::string::npos ? std::string() : rows()[i].code;
}

std::string subtitleLangDisplay(const std::string& raw) {
    size_t i = lookup(raw);
    if (i != std::string::npos) return rows()[i].display;
    // Unrecognized: show the raw value (the SDK allows lang to be free text).
    std::string s = raw;
    size_t b = 0, e = s.size();
    while (b < e && std::isspace((unsigned char)s[b])) ++b;
    while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
    return s.substr(b, e - b);
}

std::string langMatchList(const std::string& raw) {
    size_t i = lookup(raw);
    if (i == std::string::npos) return {};
    const LangRow& r = rows()[i];
    std::string out = normalize(r.code);
    for (const char* a : r.aliases) {
        std::string n = normalize(a);
        // A display name with a space is no use to mpv, and a region-tagged
        // alias would only ever match what the code already matches.
        if (n.empty() || n.find(' ') != std::string::npos) continue;
        out += "," + n;
    }
    return out;
}

const std::vector<LangOption>& subtitleLangCatalog() {
    static const std::vector<LangOption> cat = [] {
        std::vector<LangOption> c;
        for (const auto& r : rows()) c.push_back({r.code, r.display});
        return c;
    }();
    return cat;
}

}  // namespace media
