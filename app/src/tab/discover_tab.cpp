/*
    GMCA — Discover (see discover_tab.hpp).
*/

#include "tab/discover_tab.hpp"

#include "api/backend.hpp"
#include "utils/config.hpp"
#include "view/discover_picker.hpp"
#include "view/recycling_grid.hpp"
#include "view/video_card.hpp"
#include "view/video_source.hpp"

using namespace brls::literals;

DiscoverTab::DiscoverTab() {
    brls::Logger::debug("View DiscoverTab: create");
    this->inflateFromXMLRes("xml/tabs/discover.xml");

    this->catalogs = AppConfig::instance().backend().discoverCatalogs();
    for (auto& c : this->catalogs) {
        if (std::find(this->types.begin(), this->types.end(), c.type) != this->types.end()) continue;
        this->types.push_back(c.type);
        this->typeLabels.push_back(c.typeLabel);
    }

    // The three pickers share the row equally, as the reference weights them.
    struct Spec {
        const char* caption;
        DiscoverPicker** slot;
    };
    const Spec specs[] = {
        {"main/discover/filter/type", &this->pickerType},
        {"main/discover/filter/catalog", &this->pickerCatalog},
        {"main/discover/filter/genre", &this->pickerGenre},
    };
    for (size_t i = 0; i < 3; i++) {
        auto* p = new DiscoverPicker(brls::getStr(specs[i].caption));
        p->setGrow(1);
        p->setWidth(brls::View::AUTO);
        if (i > 0) p->setMarginLeft(24);  // spacing.md
        this->boxFilters->addView(p);
        *specs[i].slot = p;
    }

    this->pickerType->onSelect([this](int picked) {
        if (picked < 0 || (size_t)picked >= this->types.size()) return;
        if ((size_t)picked == this->typeIndex) return;
        this->typeIndex = (size_t)picked;
        this->catalogIndex = 0;
        this->genreIndex = 0;
        this->refreshFilters();
        this->reload();
    });
    this->pickerCatalog->onSelect([this](int picked) {
        auto of = this->catalogsOfType();
        if (picked < 0 || (size_t)picked >= of.size()) return;
        if ((size_t)picked == this->catalogIndex) return;
        this->catalogIndex = (size_t)picked;
        this->genreIndex = 0;
        this->refreshFilters();
        this->reload();
    });
    this->pickerGenre->onSelect([this](int picked) {
        if (picked == this->genreIndex) return;
        this->genreIndex = picked;
        this->refreshFilters();
        this->reload();
    });

    // The grid is built here rather than in the XML so it can carry the same
    // poster geometry the catalog tabs used.
    this->grid = new RecyclingGrid();
    this->grid->setGrow(1.f);
    this->grid->registerCell("Cell", VideoCardCell::create);
    this->grid->spanCount = brls::getStyle().getMetric("app/grid/6");
    this->grid->itemImageRatio = 1.5f;
    this->grid->itemExtraHeight = AppConfig::instance().getItem(AppConfig::POSTER_LABELS, false) ? 55 : 0;
    this->grid->setPadding(0, 0, brls::getStyle()["main/content_padding_top_bottom"], 0);
    this->grid->onNextPage([this] { this->doRequest(); });
    this->boxGrid->addView(this->grid);

    this->restoreSelection();
    this->refreshFilters();
    this->reload();
}

void DiscoverTab::restoreSelection() {
    std::string saved = AppConfig::instance().getItem(AppConfig::DISCOVER_SELECTION, std::string());
    if (saved.empty()) return;
    auto t1 = saved.find('\t');
    if (t1 == std::string::npos) return;
    auto t2 = saved.find('\t', t1 + 1);
    if (t2 == std::string::npos) return;
    std::string type = saved.substr(0, t1);
    std::string key = saved.substr(t1 + 1, t2 - t1 - 1);
    std::string genre = saved.substr(t2 + 1);

    auto it = std::find(this->types.begin(), this->types.end(), type);
    if (it == this->types.end()) return;
    this->typeIndex = (size_t)(it - this->types.begin());

    auto of = this->catalogsOfType();
    for (size_t i = 0; i < of.size(); i++) {
        if (this->catalogs[of[i]].key != key) continue;
        this->catalogIndex = i;
        if (genre.empty()) return;
        const auto& gs = this->catalogs[of[i]].genres;
        auto g = std::find(gs.begin(), gs.end(), genre);
        // +1: index 0 of the picker is "Default"
        if (g != gs.end()) this->genreIndex = (int)(g - gs.begin()) + 1;
        return;
    }
}

void DiscoverTab::saveSelection() const {
    if (this->typeIndex >= this->types.size()) return;
    auto of = this->catalogsOfType();
    if (this->catalogIndex >= of.size()) return;
    const media::DiscoverCatalog& cat = this->catalogs[of[this->catalogIndex]];
    std::string genre;
    if (this->genreIndex > 0 && (size_t)(this->genreIndex - 1) < cat.genres.size())
        genre = cat.genres[(size_t)(this->genreIndex - 1)];
    AppConfig::instance().setItem(
        AppConfig::DISCOVER_SELECTION, this->types[this->typeIndex] + "\t" + cat.key + "\t" + genre);
}

std::vector<size_t> DiscoverTab::catalogsOfType() const {
    std::vector<size_t> out;
    if (this->typeIndex >= this->types.size()) return out;
    const std::string& t = this->types[this->typeIndex];
    for (size_t i = 0; i < this->catalogs.size(); i++)
        if (this->catalogs[i].type == t) out.push_back(i);
    return out;
}

void DiscoverTab::refreshFilters() {
    this->pickerType->setOptions(this->typeLabels, (int)this->typeIndex);

    auto of = this->catalogsOfType();
    std::vector<std::string> catNames;
    catNames.reserve(of.size());
    for (size_t i : of) catNames.push_back(this->catalogs[i].catalogName);
    if (this->catalogIndex >= of.size()) this->catalogIndex = 0;
    this->pickerCatalog->setOptions(catNames, (int)this->catalogIndex, "main/discover/no_catalog"_i18n);

    std::vector<std::string> genreNames = {"main/discover/genre_default"_i18n};
    const media::DiscoverCatalog* cat = nullptr;
    if (this->catalogIndex < of.size()) {
        cat = &this->catalogs[of[this->catalogIndex]];
        for (auto& g : cat->genres) genreNames.push_back(g);
    }
    if (this->genreIndex >= (int)genreNames.size()) this->genreIndex = 0;
    this->pickerGenre->setOptions(genreNames, this->genreIndex);

    // "addon • type • genre", the line the reference prints under the pickers
    std::string meta;
    if (cat) {
        auto add = [&meta](const std::string& s) {
            if (s.empty()) return;
            if (!meta.empty()) meta += "  •  ";
            meta += s;
        };
        add(cat->addonName);
        add(cat->typeLabel);
        if (this->genreIndex > 0) add(genreNames[(size_t)this->genreIndex]);
    }
    this->labelMeta->setText(meta);
    this->labelMeta->setVisibility(meta.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
}

void DiscoverTab::reload() {
    this->saveSelection();
    this->start = 0;
    this->generation++;
    auto of = this->catalogsOfType();
    if (this->catalogIndex >= of.size()) {
        this->grid->setEmpty();
        return;
    }
    this->grid->showSkeleton();
    this->doRequest();
}

void DiscoverTab::doRequest() {
    auto of = this->catalogsOfType();
    if (this->catalogIndex >= of.size()) return;
    const media::DiscoverCatalog& cat = this->catalogs[of[this->catalogIndex]];

    media::GridQuery q;
    q.kind = cat.type == media::mediaTypeShow ? media::MediaKind::Show : media::MediaKind::Movie;
    if (this->genreIndex > 0 && (size_t)(this->genreIndex - 1) < cat.genres.size())
        q.genreId = cat.genres[(size_t)(this->genreIndex - 1)];

    size_t reqStart = this->start;
    uint64_t gen = this->generation;
    ASYNC_RETAIN
    AppConfig::instance().backend().getLibraryGrid(cat.key, q, this->start, this->pageSize,
        [ASYNC_TOKEN, reqStart, gen](const media::Container<media::Item>& r) {
            ASYNC_RELEASE
            // a page from a filter the user has already moved off
            if (gen != this->generation) return;
            this->start = reqStart + this->pageSize;
            if (r.TotalRecordCount == 0 && reqStart == 0) {
                this->grid->setEmpty();
            } else if (reqStart == 0) {
                this->grid->setDataSource(new VideoDataSource(r.Items));
            } else if (!r.Items.empty()) {
                if (auto* src = dynamic_cast<VideoDataSource*>(this->grid->getDataSource())) {
                    src->appendData(r.Items);
                    this->grid->notifyDataChanged();
                }
            }
        },
        [ASYNC_TOKEN, reqStart, gen](const std::string& ex) {
            ASYNC_RELEASE
            if (gen != this->generation) return;
            if (reqStart == 0) this->grid->setError(ex);
        });
}

brls::View* DiscoverTab::getDefaultFocus() {
    if (this->pickerType) return this->pickerType;
    return AttachedView::getDefaultFocus();
}

brls::View* DiscoverTab::create() { return new DiscoverTab(); }
