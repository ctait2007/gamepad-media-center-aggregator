/*
    GMCA — on-disk cache for external subtitle files (see subtitle_cache.hpp).
*/

#include "utils/subtitle_cache.hpp"
#include "utils/config.hpp"
#include "utils/misc.hpp"
#include "api/http.hpp"

#include <borealis/core/logger.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <mutex>
#include <stdexcept>

namespace subtitle_cache {

namespace {

std::mutex mtx;

std::string cacheDir() { return AppConfig::instance().configDir() + "/subtitles"; }

/// The url's extension, lower-cased, when it is one we recognize. Most addon
/// links carry none at all (OpenSubtitles v3 serves ".../file/1952846466"),
/// which is half of why probing them over the network was so fragile.
std::string extFromUrl(const std::string& url) {
    std::string path = url.substr(0, url.find_first_of("?#"));
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= path.size()) return "";
    std::string ext = path.substr(dot + 1);
    if (ext.size() > 4) return "";
    for (char& c : ext) c = (char)std::tolower((unsigned char)c);
    if (ext == "srt" || ext == "vtt" || ext == "ass" || ext == "ssa" || ext == "sub") return ext;
    return "";
}

/// Same question asked of the bytes, for the (common) case where the url says
/// nothing. Anything unrecognized is called SRT, as the reference does.
std::string extFromBody(const std::string& body) {
    std::string head = body.substr(0, 64);
    if (head.compare(0, 6, "WEBVTT") == 0) return "vtt";
    if (head.find("[Script Info]") != std::string::npos) return "ass";
    return "srt";
}

/// Is `s` valid UTF-8? Continuation bytes and overlong/surrogate forms both
/// count as "no" — being strict here is the point, since the answer decides
/// whether we transcode.
bool isUtf8(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        int extra;
        unsigned int cp;
        if (c < 0x80) {
            i++;
            continue;
        } else if ((c & 0xE0) == 0xC0) {
            extra = 1;
            cp = c & 0x1F;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2;
            cp = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3;
            cp = c & 0x07;
        } else {
            return false;
        }
        if (i + extra >= s.size()) return false;
        for (int n = 1; n <= extra; n++) {
            unsigned char cc = (unsigned char)s[i + n];
            if ((cc & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (extra == 1 && cp < 0x80) return false;
        if (extra == 2 && cp < 0x800) return false;
        if (extra == 3 && cp < 0x10000) return false;
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        i += extra + 1;
    }
    return true;
}

/// The 0x80..0x9F block, which is where CP1252 and Latin-1 disagree. Curly
/// quotes and dashes live here and they are all over subtitle files, so
/// treating the byte as Latin-1 would render them as control characters.
const unsigned short kCp1252High[32] = {0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030,
    0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};

/// Byte-per-character CP1252 -> UTF-8. Not a charset detector: it is the
/// single-byte fallback for a file that is not UTF-8, which covers the western
/// European encodings addons actually serve. A file in some other multi-byte
/// encoding still comes out wrong — but it comes out READABLE-ish and, more to
/// the point, it loads, where before it did not.
std::string cp1252ToUtf8(const std::string& in) {
    std::string out;
    out.reserve(in.size() + in.size() / 4);
    for (unsigned char c : in) {
        unsigned int cp = c < 0x80 ? c : (c < 0xA0 ? kCp1252High[c - 0x80] : c);
        if (cp < 0x80) {
            out.push_back((char)cp);
        } else if (cp < 0x800) {
            out.push_back((char)(0xC0 | (cp >> 6)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else {
            out.push_back((char)(0xE0 | (cp >> 12)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

}  // namespace

std::string fetch(const std::string& url) {
    if (url.empty()) throw std::runtime_error("empty subtitle url");

    std::lock_guard<std::mutex> lock(mtx);
    std::string dir = cacheDir();
    // A stable name for a stable url: the second time this track is picked the
    // file is already here, and mpv's own "cached" sub-add flag then re-selects
    // the track it made the first time instead of loading it again.
    std::string stem = fmt::format("{:016x}", (uint64_t)std::hash<std::string>{}(url));
    std::string known = extFromUrl(url);

    if (!known.empty()) {
        std::string path = fmt::format("{}/{}.{}", dir, stem, known);
        if (fs::exists(path) && fs::file_size(path) > 0) return path;
    } else {
        for (const char* ext : {"srt", "vtt", "ass"}) {
            std::string path = fmt::format("{}/{}.{}", dir, stem, ext);
            if (fs::exists(path) && fs::file_size(path) > 0) return path;
        }
    }

    std::string body = HTTP::get(url, HTTP::Timeout{20000L});
    if (body.empty()) throw std::runtime_error("empty subtitle body");
    // A BOM is legal but mpv's SRT reader is happier without one, and it breaks
    // the "1" of the first cue index for some parsers.
    if (body.compare(0, 3, "\xEF\xBB\xBF") == 0) body.erase(0, 3);
    if (!isUtf8(body)) body = cp1252ToUtf8(body);

    std::string ext = known.empty() ? extFromBody(body) : known;
    std::string path = fmt::format("{}/{}.{}", dir, stem, ext);
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + path);
    out.write(body.data(), (std::streamsize)body.size());
    out.close();
    brls::Logger::info("subtitles: cached {} bytes -> {}", body.size(), path);
    return path;
}

void clear() {
    std::lock_guard<std::mutex> lock(mtx);
    std::error_code ec;
    fs::remove_all(cacheDir(), ec);
}

}  // namespace subtitle_cache
