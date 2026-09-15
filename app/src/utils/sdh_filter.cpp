#include "utils/sdh_filter.hpp"

#include <regex>
#include <sstream>
#include <vector>

namespace sdh {

namespace {

/// The reference's four patterns, verbatim where ECMAScript allows it.
///
/// `speakerLabel` is the one with a capture: it keeps a leading "- " so that a
/// two-speaker cue still reads as two lines of dialogue once the names go.
///
/// `parentheses` looks ahead before it eats anything, which is what stops it
/// taking a real parenthetical out of the dialogue: the content has to be made
/// only of the plain characters a sound description uses, and must not be
/// nothing but digits (a year, a count).
const std::regex kSpeakerChevrons(R"([<>]{2,}[ \t]*)");
const std::regex kSpeakerLabel(R"(^([ \t]*-[ \t]*)?(?:[A-Za-z0-9 ()'#.,]+|\[[^\]\r\n]*\]):(?=\s|$)[ \t]*)",
    std::regex::ECMAScript | std::regex::multiline);
const std::regex kSquareBrackets(R"(\[[^\]]*\][ \t]*)");
const std::regex kParentheses(R"(\((?=[A-Za-z0-9 '#.,"\\\-\r\n]*\))(?![0-9]*\))[^)]*\)[ \t]*)");

/// True when a line still has something worth showing. The reference keeps a
/// line only if it holds a character that is neither whitespace nor a dash —
/// a bare "-" is what a stripped speaker label leaves behind.
bool worthKeeping(const std::string& line) {
    for (unsigned char c : line) {
        if (std::isspace(c)) continue;
        if (c == '-') continue;
        return true;
    }
    return false;
}

std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '\n') {
            if (!cur.empty() && cur.back() == '\r') cur.pop_back();
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

}  // namespace

std::string filterPlainText(const std::string& text) {
    // Chevrons first, so ">> NAME:" is a speaker label by the time the next
    // pattern looks at it.
    std::string filtered = std::regex_replace(text, kSpeakerChevrons, "");
    filtered = std::regex_replace(filtered, kSpeakerLabel, "$1");
    filtered = std::regex_replace(filtered, kSquareBrackets, "");
    filtered = std::regex_replace(filtered, kParentheses, "");

    std::string out;
    for (const std::string& line : splitLines(filtered)) {
        if (!worthKeeping(line)) continue;
        if (!out.empty()) out += '\n';
        out += line;
    }
    // Blank overall -> the cue goes.
    for (unsigned char c : out)
        if (!std::isspace(c)) return out;
    return "";
}

std::string filterDocument(const std::string& body) {
    std::vector<std::string> lines = splitLines(body);
    std::string out;
    std::vector<std::string> block;

    // A cue ends at a blank line. Everything from the timing line on is text;
    // an index or a WEBVTT header before it is passed through as-is.
    auto flush = [&out](std::vector<std::string>& b) {
        if (b.empty()) return;
        size_t timing = b.size();
        for (size_t i = 0; i < b.size(); i++) {
            if (b[i].find("-->") == std::string::npos) continue;
            timing = i;
            break;
        }
        if (timing == b.size()) {
            // No timing line: a header or stray text, kept untouched.
            for (const std::string& l : b) out += l + "\n";
            out += "\n";
            b.clear();
            return;
        }
        std::string text;
        for (size_t i = timing + 1; i < b.size(); i++) {
            if (!text.empty()) text += '\n';
            text += b[i];
        }
        std::string filtered = filterPlainText(text);
        if (filtered.empty()) {  // nothing left to show: the cue goes entirely
            b.clear();
            return;
        }
        for (size_t i = 0; i <= timing; i++) out += b[i] + "\n";
        out += filtered + "\n\n";
        b.clear();
    };

    for (const std::string& line : lines) {
        bool blank = true;
        for (unsigned char c : line)
            if (!std::isspace(c)) {
                blank = false;
                break;
            }
        if (blank) {
            flush(block);
            continue;
        }
        block.push_back(line);
    }
    flush(block);
    return out;
}

}  // namespace sdh
