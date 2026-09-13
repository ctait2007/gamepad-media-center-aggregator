#include "utils/local_library.hpp"

#include "utils/config.hpp"

#include <algorithm>
#include <fstream>

LocalLibrary::LocalLibrary() {
    this->path = AppConfig::instance().configDir() + "/library.json";
    std::ifstream f(this->path);
    if (!f.is_open()) return;
    try {
        this->list = nlohmann::json::parse(f).get<std::vector<media::Item>>();
    } catch (const std::exception& e) {
        brls::Logger::error("LocalLibrary: cannot read {}: {}", this->path, e.what());
    }
}

void LocalLibrary::save() const {
    try {
        nlohmann::json j = this->list;
        std::ofstream f(this->path);
        f << j.dump(2);
    } catch (const std::exception& e) {
        brls::Logger::error("LocalLibrary: cannot write {}: {}", this->path, e.what());
    }
}

bool LocalLibrary::contains(const std::string& ratingKey) const {
    std::lock_guard<std::mutex> lock(this->mutex);
    return std::any_of(this->list.begin(), this->list.end(),
        [&ratingKey](const media::Item& i) { return i.ratingKey == ratingKey; });
}

void LocalLibrary::add(const media::Item& item) {
    if (item.ratingKey.empty()) return;
    std::lock_guard<std::mutex> lock(this->mutex);
    for (const media::Item& i : this->list)
        if (i.ratingKey == item.ratingKey) return;
    this->list.insert(this->list.begin(), item);
    this->save();
}

void LocalLibrary::remove(const std::string& ratingKey) {
    std::lock_guard<std::mutex> lock(this->mutex);
    auto it = std::remove_if(this->list.begin(), this->list.end(),
        [&ratingKey](const media::Item& i) { return i.ratingKey == ratingKey; });
    if (it == this->list.end()) return;
    this->list.erase(it, this->list.end());
    this->save();
}

std::vector<media::Item> LocalLibrary::items(media::MediaKind kind) const {
    std::lock_guard<std::mutex> lock(this->mutex);
    if (kind == media::MediaKind::Any) return this->list;
    const std::string& want = kind == media::MediaKind::Movie ? media::mediaTypeMovie : media::mediaTypeShow;
    std::vector<media::Item> out;
    for (const media::Item& i : this->list)
        if (i.type == want) out.push_back(i);
    return out;
}

namespace personal {

media::ListKind kind() {
    media::ListKind k = AppConfig::instance().backend().caps().listKind;
    return k == media::ListKind::None ? media::ListKind::Library : k;
}

bool isLocal() { return AppConfig::instance().backend().caps().listKind == media::ListKind::None; }

bool canList(const media::Item& item) {
    // Whole titles only, either way: a season or an episode is not a thing you
    // put in a library, and no backend accepts one.
    if (isLocal()) return item.type == media::mediaTypeMovie || item.type == media::mediaTypeShow;
    return AppConfig::instance().backend().canList(item);
}

void state(const media::Item& item, media::Then<bool> then, media::OnError error) {
    if (!then) return;
    if (isLocal()) {
        then(LocalLibrary::instance().contains(item.ratingKey));
        return;
    }
    AppConfig::instance().backend().getWatchlistState(item, std::move(then), std::move(error));
}

void setListed(const media::Item& item, bool add, std::function<void()> then, media::OnError error) {
    if (isLocal()) {
        if (add)
            LocalLibrary::instance().add(item);
        else
            LocalLibrary::instance().remove(item.ratingKey);
        if (then) then();
        return;
    }
    AppConfig::instance().backend().setWatchlisted(item, add, std::move(then), std::move(error));
}

void list(const std::string& sortField, media::MediaKind kind, size_t start, size_t size,
    media::Then<media::Container<media::Item>> then, media::OnError error) {
    if (!isLocal()) {
        AppConfig::instance().backend().listWatchlist(sortField, kind, start, size, std::move(then), std::move(error));
        return;
    }
    // Already in memory and already newest-first, so there is nothing to sort
    // and nothing to wait for; it is still paged, because the grid asks for
    // pages and would otherwise re-append the whole list on every scroll.
    std::vector<media::Item> all = LocalLibrary::instance().items(kind);
    media::Container<media::Item> c;
    for (size_t i = start; i < all.size() && i < start + size; i++) c.Items.push_back(std::move(all[i]));
    c.StartIndex = (long)start;
    c.TotalRecordCount = (long)all.size();
    if (then) then(c);
}

}  // namespace personal
