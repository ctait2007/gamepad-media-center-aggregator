#include "utils/stream_select.hpp"

#include <borealis.hpp>

#include <regex>

namespace stream_select {

namespace {

/// The reference's isPlayable, at what this app knows: a Media is playable iff
/// it carries a real url. Its debrid-cache states have no counterpart here —
/// `cached` is a display hint from the stream's own text, not a live check, so
/// it is not used to rule a source out.
bool playable(const media::Media& m) { return m.playable(); }

/// Every word inside the negative lookaheads of `pattern`, as the reference
/// pulls them out: \(\?![^)]*?\(([^)]+)\) , then split on "|".
std::vector<std::string> exclusionWords(const std::string& pattern) {
    std::vector<std::string> out;
    static const std::regex kLookahead(R"(\(\?![^)]*?\(([^)]+)\))");
    auto begin = std::sregex_iterator(pattern.begin(), pattern.end(), kLookahead);
    for (auto it = begin; it != std::sregex_iterator(); ++it) {
        std::string group = (*it)[1].str();
        size_t start = 0;
        while (start <= group.size()) {
            size_t bar = group.find('|', start);
            std::string word = group.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
            size_t a = word.find_first_not_of(" \t");
            size_t b = word.find_last_not_of(" \t");
            if (a != std::string::npos) out.push_back(word.substr(a, b - a + 1));
            if (bar == std::string::npos) break;
            start = bar + 1;
        }
    }
    return out;
}

}  // namespace

std::string searchableText(const media::Media& m) {
    std::string text = m.addonName + " " + m.labelRaw + " " + m.detailRaw;
    if (!m.parts.empty()) text += " " + m.parts.front().key;
    return text;
}

int pick(const std::vector<media::Media>& sources, Mode mode, const std::string& regex,
    const std::string& bingeGroup, bool preferBinge) {
    if (sources.empty()) return -1;

    // Binge group first, ahead of the mode — the reference's own comment says
    // it: "even in MANUAL mode, a persisted binge group should auto-play
    // without showing the picker".
    if (preferBinge && !bingeGroup.empty()) {
        for (size_t i = 0; i < sources.size(); i++)
            if (sources[i].bingeGroup == bingeGroup && playable(sources[i])) return (int)i;
        // bingeGroupOnly: with nothing else to go on, a miss means the list,
        // not a different release.
        if (mode == Mode::Manual) return -1;
    }

    if (mode == Mode::Manual) return -1;

    if (mode == Mode::FirstStream) {
        for (size_t i = 0; i < sources.size(); i++)
            if (playable(sources[i])) return (int)i;
        return -1;
    }

    // REGEX_MATCH. A pattern that does not compile selects nothing at all,
    // rather than falling back to the first stream — the reference returns null
    // there, and silently playing something the pattern rules out is worse than
    // showing the list.
    std::string pattern = regex;
    size_t a = pattern.find_first_not_of(" \t");
    size_t b = pattern.find_last_not_of(" \t");
    pattern = a == std::string::npos ? "" : pattern.substr(a, b - a + 1);
    if (pattern.empty()) return -1;

    std::regex include;
    try {
        include = std::regex(pattern, std::regex::ECMAScript | std::regex::icase);
    } catch (const std::exception& ex) {
        brls::Logger::warning("stream_select: '{}' is not a usable pattern ({})", pattern, ex.what());
        return -1;
    }

    std::regex exclude;
    bool hasExclude = false;
    std::vector<std::string> words = exclusionWords(pattern);
    if (!words.empty()) {
        std::string joined;
        for (const std::string& w : words) joined += (joined.empty() ? "" : "|") + w;
        try {
            exclude = std::regex("\\b(" + joined + ")\\b", std::regex::ECMAScript | std::regex::icase);
            hasExclude = true;
        } catch (const std::exception&) {
            hasExclude = false;  // a word with regex punctuation in it: no second pass
        }
    }

    for (size_t i = 0; i < sources.size(); i++) {
        if (!playable(sources[i])) continue;
        std::string text = searchableText(sources[i]);
        if (!std::regex_search(text, include)) continue;
        if (hasExclude && std::regex_search(text, exclude)) continue;
        return (int)i;
    }
    return -1;
}

}  // namespace stream_select
