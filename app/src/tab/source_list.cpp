#include "tab/source_list.hpp"

#include "activity/player_view.hpp"
#include "utils/config.hpp"
#include "utils/image.hpp"
#include "view/auto_tab_frame.hpp"
#include "view/button_close.hpp"
#include "view/svg_image.hpp"

#include <algorithm>
#include <set>

using namespace brls::literals;

namespace {

/// One selectable source. A column of labels rather than a single line: the
/// addon's own text is multi-line on purpose and we print it as sent.
class SourceCard : public brls::Box {
public:
    SourceCard(const media::Media& m, bool playable) {
        auto theme = brls::Application::getTheme();
        this->setAxis(brls::Axis::ROW);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setPadding(18, 22, 18, 22);
        this->setMarginBottom(12);
        this->setCornerRadius(16);  // Nuvio backdropCard / dialog radius
        this->setHighlightCornerRadius(20);
        this->setBackgroundColor(theme.getColor("color/surface"));
        this->setFocusable(playable);
        // A non-playable row (torrent / external link) stays visible so the
        // list explains itself, but must not look selectable.
        this->setAlpha(playable ? 1.f : 0.5f);

        auto* text = new brls::Box();
        text->setAxis(brls::Axis::COLUMN);
        text->setGrow(1);

        auto* head = new brls::Label();
        head->setText(m.labelRaw.empty() ? m.label : m.labelRaw);
        head->setFontSize(22);
        head->setTextColor(theme.getColor("brls/text"));
        text->addView(head);

        const std::string& body = m.detailRaw.empty() ? m.detail : m.detailRaw;
        if (!body.empty()) {
            auto* sub = new brls::Label();
            sub->setText(body);
            sub->setFontSize(17);
            sub->setTextColor(theme.getColor("font/grey"));
            sub->setMarginTop(6);
            text->addView(sub);
        }
        this->addView(text);

        // right rail: the addon that produced it, like Nuvio's source badge
        if (!m.addonName.empty()) {
            auto* addon = new brls::Label();
            addon->setText(m.addonName);
            addon->setFontSize(15);
            addon->setTextColor(theme.getColor("font/grey"));
            addon->setHorizontalAlign(brls::HorizontalAlign::RIGHT);
            addon->setMarginLeft(18);
            this->addView(addon);
        }
    }
};

/// Filter pill (refresh / All / one per addon).
class FilterPill : public brls::Box {
public:
    FilterPill(const std::string& text, bool active, std::function<void()> onPress) {
        auto theme = brls::Application::getTheme();
        this->setAxis(brls::Axis::ROW);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setJustifyContent(brls::JustifyContent::CENTER);
        this->setHeight(44);
        this->setPadding(0, 20, 0, 20);
        this->setMarginRight(10);
        this->setCornerRadius(22);        // Nuvio chip = fully rounded
        this->setHighlightCornerRadius(26);
        this->setBackgroundColor(active ? theme.getColor("brls/text") : theme.getColor("color/pill"));
        this->setFocusable(true);

        auto* label = new brls::Label();
        label->setText(text);
        label->setFontSize(18);
        label->setTextColor(active ? theme.getColor("brls/background") : theme.getColor("brls/text"));
        this->addView(label);

        this->registerClickAction([onPress](brls::View*) {
            onPress();
            return true;
        });
        this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
    }
};

}  // namespace

SourceList::SourceList(const media::Item& item, std::string title, int64_t resumeMs)
    : item(item), title(std::move(title)), resumeMs(resumeMs) {
    this->inflateFromXMLRes("xml/tabs/source_list.xml");
    brls::Logger::debug("View SourceList: create");

    this->labelTitle->setText(this->item.grandparentTitle.empty() ? this->item.title : this->item.grandparentTitle);

    // Episodes identify themselves as "S1 E2" + the episode name; a movie just
    // repeats nothing here and shows its year in the meta line instead.
    if (this->item.parentIndex > 0 || this->item.index > 0) {
        this->labelSubtitle->setText(fmt::format("S{} E{}", this->item.parentIndex, this->item.index));
        this->labelMeta->setText(this->item.title);
    } else {
        this->labelSubtitle->setText(this->item.year ? std::to_string(this->item.year) : "");
        this->labelMeta->setText("");
    }

    // The left column sits directly on the dark scrim over the artwork, so its
    // text is light in BOTH themes — theme text colours are dark in light mode
    // and would be unreadable there. The cards below carry their own themed
    // surface, so they follow the theme normally.
    const NVGcolor onScrim = nvgRGB(0xF5, 0xF5, 0xF5);
    const NVGcolor onScrimDim = nvgRGB(0xB3, 0xB3, 0xB3);  // Nuvio textSecondary
    this->labelTitle->setTextColor(onScrim);
    this->labelSubtitle->setTextColor(onScrim);
    this->labelMeta->setTextColor(onScrimDim);
    this->labelMessage->setTextColor(onScrimDim);

    std::string art = this->item.art.empty() ? this->item.thumb : this->item.art;
    if (!art.empty()) Image::with(this->imageBackdrop, art);

    auto* close = dynamic_cast<ButtonClose*>(this->getView("source/close"));
    if (close) close->registerClickAction([this](brls::View*) { return ui::popDetail(this); });

    this->registerAction(
        "hints/back"_i18n, brls::BUTTON_B, [this](brls::View*) { return ui::popDetail(this); }, true);
    // Triangle re-runs the fetch, matching Home's refresh binding.
    this->registerAction("hints/refresh"_i18n, brls::BUTTON_Y, [this](brls::View*) {
        this->fetchSources();
        return true;
    });

    this->fetchSources();
}

SourceList::~SourceList() { brls::Logger::debug("View SourceList: delete"); }

brls::View* SourceList::create() { return new SourceList(media::Item{}, "", 0); }

void SourceList::showMessage(const std::string& text, bool spinner) {
    this->labelMessage->setText(text);
    this->boxMessage->setVisibility(brls::Visibility::VISIBLE);
    this->scroll->setVisibility(spinner ? brls::Visibility::GONE : brls::Visibility::GONE);
}

void SourceList::fetchSources() {
    if (this->loading) return;
    this->loading = true;
    this->sources.clear();
    this->boxList->clearViews();
    this->boxFilters->clearViews();
    this->showMessage("main/stremio/source/loading"_i18n, true);

    ASYNC_RETAIN
    // full=true: THIS is the call that fans out /stream across every addon.
    // Nothing else in the app asks for it any more — the detail pages fetch
    // metadata only — so it happens once, here, after the user has chosen.
    AppConfig::instance().backend().getItemDetail(
        this->item.ratingKey, true,
        [ASYNC_TOKEN](const media::Item& full) {
            ASYNC_RELEASE
            this->loading = false;
            this->sources = full.media;
            // keep the richer detail (runtime, art) the fetch came back with
            if (!full.title.empty()) this->item.media = full.media;
            this->buildFilters();
            this->renderSources();
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->loading = false;
            brls::Logger::warning("source list: {}", ex);
            this->showMessage("main/stremio/source/failed"_i18n, false);
        });
}

void SourceList::buildFilters() {
    this->boxFilters->clearViews();

    // one pill per addon that actually returned something, in first-seen order
    std::vector<std::string> addons;
    for (auto& m : this->sources) {
        if (m.addonName.empty()) continue;
        if (std::find(addons.begin(), addons.end(), m.addonName) == addons.end()) addons.push_back(m.addonName);
    }
    // a filter that no longer matches anything would hide every row
    if (!this->activeAddon.empty() &&
        std::find(addons.begin(), addons.end(), this->activeAddon) == addons.end())
        this->activeAddon.clear();

    this->boxFilters->addView(new FilterPill("main/stremio/source/refresh"_i18n, false, [this]() {
        this->fetchSources();
    }));
    this->boxFilters->addView(new FilterPill("main/stremio/source/all"_i18n, this->activeAddon.empty(), [this]() {
        this->applyFilter("");
    }));
    // Only worth pilling per-addon when more than one contributed.
    if (addons.size() > 1) {
        for (const auto& a : addons)
            this->boxFilters->addView(new FilterPill(a, this->activeAddon == a, [this, a]() { this->applyFilter(a); }));
    }
}

void SourceList::applyFilter(const std::string& addon) {
    this->activeAddon = addon;
    this->buildFilters();
    this->renderSources();
}

void SourceList::renderSources() {
    this->boxList->clearViews();

    std::vector<int> shown;  // index into this->sources, so play() stays correct
    for (size_t i = 0; i < this->sources.size(); i++) {
        if (!this->activeAddon.empty() && this->sources[i].addonName != this->activeAddon) continue;
        shown.push_back((int)i);
    }

    if (shown.empty()) {
        this->showMessage("main/stremio/source/none"_i18n, false);
        return;
    }

    this->boxMessage->setVisibility(brls::Visibility::GONE);
    this->scroll->setVisibility(brls::Visibility::VISIBLE);

    brls::View* firstFocusable = nullptr;
    for (int idx : shown) {
        const media::Media& m = this->sources[idx];
        bool playable = m.playable();
        auto* card = new SourceCard(m, playable);
        if (playable) {
            card->registerClickAction([this, idx](brls::View*) {
                this->play(idx);
                return true;
            });
            card->addGestureRecognizer(new brls::TapGestureRecognizer(card));
            if (!firstFocusable) firstFocusable = card;
        }
        this->boxList->addView(card);
    }

    if (firstFocusable) {
        brls::sync([firstFocusable]() { brls::Application::giveFocus(firstFocusable); });
    }
}

void SourceList::play(int mediaIndex) {
    if (mediaIndex < 0 || mediaIndex >= (int)this->sources.size()) return;
    media::Item toPlay = this->item;
    toPlay.media = this->sources;
    PlayerView* view = new PlayerView(toPlay, this->resumeMs, mediaIndex);
    view->setTitie(this->title);
    if (!this->item.grandparentRatingKey.empty()) view->setSeries(this->item.grandparentRatingKey);
    brls::sync([view]() { brls::Application::giveFocus(view); });
}
