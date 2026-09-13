/*
    GMCA — Nuvio account library (see nuvio/library.hpp).
*/

#include "api/nuvio/library.hpp"

#include <algorithm>
#include <borealis/core/logger.hpp>
#include <chrono>

#include "api/nuvio/sync.hpp"
#include "api/stremio/types.hpp"
#include "utils/config.hpp"

namespace nuvio {

using media::jint;
using media::jstr;

namespace {

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

nlohmann::json toMutation(const LibraryRow& r) {
    // The field names are sync_push_library_items' own (LibraryMutationDto in
    // NuvioTV's SupabaseLibrarySyncRemoteDataSource) — every one is required,
    // so an absent value goes as null rather than being left out.
    nlohmann::json j = {
        {"content_id", r.contentId},
        {"content_type", r.contentType},
        {"name", r.name},
        {"poster_shape", "POSTER"},
        {"genres", r.genres},
        {"added_at", r.addedAt > 0 ? r.addedAt : nowMs()},
    };
    j["poster"]        = r.poster.empty() ? nlohmann::json(nullptr) : nlohmann::json(r.poster);
    j["background"]    = r.background.empty() ? nlohmann::json(nullptr) : nlohmann::json(r.background);
    j["description"]   = r.description.empty() ? nlohmann::json(nullptr) : nlohmann::json(r.description);
    j["release_info"]  = r.releaseInfo.empty() ? nlohmann::json(nullptr) : nlohmann::json(r.releaseInfo);
    j["imdb_rating"]   = r.imdbRating > 0 ? nlohmann::json(r.imdbRating) : nlohmann::json(nullptr);
    j["addon_base_url"] = r.addonBaseUrl.empty() ? nlohmann::json(nullptr) : nlohmann::json(r.addonBaseUrl);
    return j;
}

}  // namespace

media::Item itemFromLibraryRow(const LibraryRow& row) {
    media::Item it;
    // A library row carries its own metadata, so this needs no addon lookup —
    // the ratingKey is the same codec the rest of the app parses.
    it.ratingKey = row.contentType + ":" + row.contentId;
    it.guid = row.contentId;
    it.type = row.contentType == "series" ? media::mediaTypeShow : media::mediaTypeMovie;
    it.title = row.name;
    it.summary = row.description;
    it.thumb = row.poster;
    it.art = row.background;
    it.genres = row.genres;
    it.rating = row.imdbRating;
    if (row.imdbRating > 0) it.ratingImage = "imdb://image.rating";
    // "2019–" / "2019" — the year is the leading four digits either way.
    if (row.releaseInfo.size() >= 4) {
        try {
            it.year = std::stoll(row.releaseInfo.substr(0, 4));
        } catch (const std::exception&) {
        }
    }
    it.addedAt = row.addedAt / 1000;
    return it;
}

LibraryRow libraryRowFromItem(const media::Item& item) {
    LibraryRow r;
    // The library is keyed on the BARE content id, not on our prefixed
    // ratingKey: "movie:tt123" saved from GMCA has to be the same row Nuvio
    // wrote as "tt123".
    stremio::ParsedId pid = stremio::parseId(item.ratingKey);
    r.contentId = pid.baseId.empty() ? item.ratingKey : pid.baseId;
    r.contentType = item.type == media::mediaTypeShow ? "series" : "movie";
    r.name = item.title;
    r.poster = item.thumb;
    r.background = item.art;
    r.description = item.summary;
    r.releaseInfo = item.year > 0 ? std::to_string(item.year) : "";
    r.imdbRating = item.rating;
    r.genres = item.genres;
    r.addedAt = nowMs();
    return r;
}

void LibraryStore::ensureLoaded() {
    std::lock_guard<std::mutex> lock(mtx);
    if (loaded) return;

    int profileId = AppConfig::instance().getNuvioProfileIndex();
    try {
        nlohmann::json rows =
            nuvio::rpc("sync_pull_library", {{"p_profile_id", profileId}, {"p_limit", 500}, {"p_offset", 0}});
        list.clear();
        if (rows.is_array()) {
            for (auto& r : rows) {
                LibraryRow row;
                row.contentId = jstr(r, "content_id");
                if (row.contentId.empty()) continue;
                row.contentType = jstr(r, "content_type");
                row.name = jstr(r, "name");
                row.poster = jstr(r, "poster");
                row.background = jstr(r, "background");
                row.description = jstr(r, "description");
                row.releaseInfo = jstr(r, "release_info");
                row.addonBaseUrl = jstr(r, "addon_base_url");
                if (r.contains("imdb_rating") && r["imdb_rating"].is_number())
                    row.imdbRating = r["imdb_rating"].get<double>();
                if (r.contains("genres") && r["genres"].is_array())
                    for (auto& g : r["genres"])
                        if (g.is_string()) row.genres.push_back(g.get<std::string>());
                row.addedAt = jint(r, "added_at");
                list.push_back(std::move(row));
            }
        }
        loaded = true;
    } catch (const std::exception& ex) {
        // Not marked loaded: a transient failure must not leave the library
        // looking permanently empty for the rest of the session.
        brls::Logger::warning("nuvio: library pull failed: {}", ex.what());
    }
}

void LibraryStore::invalidate() {
    std::lock_guard<std::mutex> lock(mtx);
    loaded = false;
}

std::vector<LibraryRow> LibraryStore::rows() {
    std::lock_guard<std::mutex> lock(mtx);
    std::vector<LibraryRow> out = list;
    std::sort(out.begin(), out.end(), [](const LibraryRow& a, const LibraryRow& b) { return a.addedAt > b.addedAt; });
    return out;
}

bool LibraryStore::contains(const std::string& contentId, const std::string& contentType) {
    std::lock_guard<std::mutex> lock(mtx);
    for (auto& r : list)
        if (r.contentId == contentId && r.contentType == contentType) return true;
    return false;
}

void LibraryStore::add(const LibraryRow& row) {
    int profileId = AppConfig::instance().getNuvioProfileIndex();
    nuvio::rpc("sync_push_library_items",
        {{"p_profile_id", profileId}, {"p_items", nlohmann::json::array({toMutation(row)})},
            {"p_origin_client_id", nuvio::syncClientId()}});

    std::lock_guard<std::mutex> lock(mtx);
    for (auto& r : list) {
        if (r.contentId == row.contentId && r.contentType == row.contentType) {
            r = row;
            return;
        }
    }
    list.push_back(row);
}

void LibraryStore::remove(const std::string& contentId, const std::string& contentType) {
    int profileId = AppConfig::instance().getNuvioProfileIndex();
    nuvio::rpc("sync_delete_library_items",
        {{"p_profile_id", profileId},
            {"p_keys", nlohmann::json::array({{{"content_id", contentId}, {"content_type", contentType}}})},
            {"p_origin_client_id", nuvio::syncClientId()}});

    std::lock_guard<std::mutex> lock(mtx);
    list.erase(std::remove_if(list.begin(), list.end(),
                   [&](const LibraryRow& r) { return r.contentId == contentId && r.contentType == contentType; }),
        list.end());
}

}  // namespace nuvio
