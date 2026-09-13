#include "tab/source_list.hpp"

#include "activity/player_view.hpp"
#include "utils/config.hpp"
#include "utils/image.hpp"
#include "utils/misc.hpp"
#include "view/auto_tab_frame.hpp"
#include "view/pill_button.hpp"
#include "view/svg_image.hpp"

#include <algorithm>
#include <set>

using namespace brls::literals;

namespace {

// The picker draws on a dark scrim over the artwork in BOTH themes, so it uses
// the reference's own dark-surface values rather than theme tokens that invert
// in light mode.
constexpr unsigned char kElevated[3] = { 0x1A, 0x1A, 0x1A };  // BackgroundElevated
constexpr unsigned char kBorder[3]   = { 0x33, 0x33, 0x33 };  // Border
const NVGcolor kTextPrimary   = nvgRGB(0xF5, 0xF5, 0xF5);
const NVGcolor kTextSecondary = nvgRGB(0xB3, 0xB3, 0xB3);  // neutral400
const NVGcolor kTextTertiary  = nvgRGB(0x80, 0x80, 0x80);  // neutral600

/// One selectable source, laid out like NuvioTV's StreamCard: a 12dp-radius
/// card on BackgroundElevated, padded 16dp, with the stream name (titleMedium)
/// over its description (bodySmall) on the left and the addon that produced it
/// (labelSmall) right-aligned on the other side. A column of labels rather
/// than a single line: the addon's own text is multi-line on purpose and we
/// print it as sent.
class SourceCard : public brls::Box {
public:
    SourceCard(const media::Media& m, bool playable) {
        this->setAxis(brls::Axis::ROW);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setPadding(32, 32, 32, 32);
        // spacing.md between cards plus the reference's spacing.xs of padding
        // on each item.
        this->setMarginBottom(40);
        this->setCornerRadius(24);  // radii.md
        this->setHighlightCornerRadius(28);
        this->setBackgroundColor(nvgRGB(kElevated[0], kElevated[1], kElevated[2]));
        this->setFocusable(playable);
        // A non-playable row (torrent / external link) stays visible so the
        // list explains itself, but must not look selectable.
        this->setAlpha(playable ? 1.f : 0.5f);

        auto* text = new brls::Box();
        text->setAxis(brls::Axis::COLUMN);
        text->setGrow(1);

        auto* head = new brls::Label();
        head->setText(m.labelRaw.empty() ? m.label : m.labelRaw);
        head->setFontSize(32);  // titleMedium
        head->setFontWeight("medium");
        head->setTextColor(kTextPrimary);
        text->addView(head);

        const std::string& body = m.detailRaw.empty() ? m.detail : m.detailRaw;
        if (!body.empty()) {
            auto* sub = new brls::Label();
            sub->setText(body);
            sub->setFontSize(24);  // bodySmall
            sub->setTextColor(kTextSecondary);
            sub->setMarginTop(8);
            text->addView(sub);
        }
        this->addView(text);

        // right rail: the addon that produced it, like Nuvio's source badge
        if (!m.addonName.empty()) {
            auto* addon = new brls::Label();
            addon->setText(m.addonName);
            addon->setFontSize(20);  // labelSmall
            addon->setFontWeight("medium");
            addon->setTextColor(kTextTertiary);
            addon->setHorizontalAlign(brls::HorizontalAlign::RIGHT);
            addon->setSingleLine(true);
            // the stream text takes the slack; the badge keeps its natural width
            addon->setShrink(0);
            addon->setMarginLeft(32);
            this->addView(addon);
        }
    }
};

/// NuvioTV's RefreshFilterChip: the first item of the filter row is not a
/// "Refresh" word but a round chip carrying only the refresh glyph, styled
/// exactly like the filter chips beside it (BackgroundCard + hairline border,
/// accent fill once focused).
class RefreshChip : public brls::Box {
public:
    explicit RefreshChip(std::function<void()> onPress) {
        this->setAxis(brls::Axis::ROW);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setJustifyContent(brls::JustifyContent::CENTER);
        this->setWidth(72);
        this->setHeight(72);
        this->setMarginRight(32);
        this->setCornerRadius(36);
        this->setHighlightCornerRadius(40);
        this->setBorderThickness(2);
        this->setFocusable(true);
        this->setHideHighlightBackground(true);

        this->icon = new SVGImage();
        this->icon->setWidth(40);   // 20dp
        this->icon->setHeight(40);
        this->addView(this->icon);
        this->icon->setImageFromSVGRes("icon/ico-refresh.svg");

        this->restyle();
        this->registerClickAction([onPress](brls::View*) {
            onPress();
            return true;
        });
        this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
    }

    void onFocusGained() override {
        brls::Box::onFocusGained();
        this->focused = true;
        this->restyle();
    }

    void onFocusLost() override {
        brls::Box::onFocusLost();
        this->focused = false;
        this->restyle();
    }

private:
    void restyle() {
        auto theme = brls::Application::getTheme();
        if (this->focused) {
            this->setBackgroundColor(theme.getColor("color/app"));
            this->setBorderColor(theme.getColor("color/app"));
            this->icon->setGlyphColor(theme.getColor("brls/button/primary_enabled_text"));
        } else {
            this->setBackgroundColor(nvgRGB(0x24, 0x24, 0x24));
            this->setBorderColor(nvgRGB(kBorder[0], kBorder[1], kBorder[2]));
            this->icon->setGlyphColor(kTextSecondary);
        }
    }

    SVGImage* icon = nullptr;
    bool focused = false;
};

}  // namespace

SourceList::SourceList(const media::Item& item, std::string title, int64_t resumeMs)
    : item(item), title(std::move(title)), resumeMs(resumeMs) {
    this->inflateFromXMLRes("xml/tabs/source_list.xml");
    brls::Logger::debug("View SourceList: create");

    const std::string& name =
        this->item.grandparentTitle.empty() ? this->item.title : this->item.grandparentTitle;
    this->labelTitle->setText(name);

    // The left column sits directly on the dark scrim over the artwork, so its
    // text is light in BOTH themes — theme text colours are dark in light mode
    // and would be unreadable there. The cards below carry their own dark
    // surface for the same reason.
    this->labelTitle->setTextColor(kTextPrimary);
    this->labelSubtitle->setTextColor(kTextSecondary);
    this->labelEpisode->setTextColor(kTextPrimary);
    this->labelMeta->setTextColor(kTextSecondary);
    this->labelMessage->setTextColor(kTextSecondary);

    auto show = [](brls::Label* l, const std::string& text) {
        l->setText(text);
        l->setVisibility(text.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    };

    // The reference prints "S1 E2" / the episode name / its runtime for an
    // episode, and a single "genres • year" line for a movie (bodyLarge there,
    // one step up from the episode runtime's bodyMedium).
    if (this->item.parentIndex > 0 || this->item.index > 0) {
        show(this->labelSubtitle, fmt::format("S{} E{}", this->item.parentIndex, this->item.index));
        show(this->labelEpisode, this->item.title);
        show(this->labelMeta, misc::formatRuntime(this->item.duration));
    } else {
        std::string genres;
        for (const auto& g : this->item.genres) {
            if (!genres.empty()) genres += ", ";
            genres += g;
        }
        std::vector<std::string> bits;
        if (!genres.empty()) bits.push_back(genres);
        if (this->item.year) bits.push_back(std::to_string(this->item.year));
        std::string info;
        for (const auto& b : bits) {
            if (!info.empty()) info += "  •  ";
            info += b;
        }
        this->labelMeta->setFontSize(32);  // bodyLarge
        show(this->labelMeta, info);
    }

    std::string art = this->item.art.empty() ? this->item.thumb : this->item.art;
    if (!art.empty()) Image::load(this->imageBackdrop, art, 1280, 720);
    // Same cut-out logo the detail page showed, so the picker is visibly for
    // the thing you just pressed Play on. Episodes inherit the show's.
    this->applyLogo(this->item.clearLogo);

    this->registerAction(
        "hints/back"_i18n, brls::BUTTON_B, [this](brls::View*) { return ui::popDetail(this); }, true);
    // Triangle re-runs the fetch, matching Home's refresh binding.
    this->registerAction("hints/refresh"_i18n, brls::BUTTON_Y, [this](brls::View*) {
        this->fetchSources();
        return true;
    });

    this->fetchSources();
}

void SourceList::applyLogo(const std::string& url) {
    auto showTitle = [this]() {
        this->imageLogo->setVisibility(brls::Visibility::GONE);
        this->labelTitle->setVisibility(brls::Visibility::VISIBLE);
    };
    if (url.empty()) {
        showTitle();
        return;
    }
    Image::cancel(this->imageLogo);
    this->imageLogo->setVisibility(brls::Visibility::GONE);
    this->labelTitle->setVisibility(brls::Visibility::GONE);
    ASYNC_RETAIN
    Image::load(this->imageLogo, url, 500, 0, [ASYNC_TOKEN, showTitle](bool ok, bool retryable) {
        ASYNC_RELEASE
        (void)retryable;
        if (ok)
            this->imageLogo->setVisibility(brls::Visibility::VISIBLE);
        else
            showTitle();
    });
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

    this->boxFilters->addView(new RefreshChip([this]() { this->fetchSources(); }));
    this->boxFilters->addView(new PillButton("main/stremio/source/all"_i18n, this->activeAddon.empty(),
        [this]() { this->applyFilter(""); }, PillButton::Style::Filter));
    // Only worth chipping per-addon when more than one contributed.
    if (addons.size() > 1) {
        for (const auto& a : addons)
            this->boxFilters->addView(new PillButton(
                a, this->activeAddon == a, [this, a]() { this->applyFilter(a); }, PillButton::Style::Filter));
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
