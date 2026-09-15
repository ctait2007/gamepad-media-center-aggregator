#include "view/video_card.hpp"
#include "view/video_source.hpp"
#include "view/svg_image.hpp"
#include "utils/config.hpp"
#include "utils/keybind.hpp"
#include "utils/image.hpp"

using namespace brls::literals;

void BaseCardCell::applyPosterLabels() {
    if (AppConfig::instance().getItem(AppConfig::POSTER_LABELS, false)) return;
    if (auto* labels = this->getView("video/card/labels")) labels->setVisibility(brls::Visibility::GONE);
}

void BaseCardCell::applyCastStyle(float diameter) {
    this->setLabelsVisible(true);
    if (auto* pic = this->getView("video/card/pic_box")) {
        pic->setCornerRadius(diameter / 2);
        pic->setHighlightCornerRadius(diameter / 2);
    }
    this->picture->setCornerRadius(diameter / 2);
    // 20 above the name, a 12sp line for it, 8, then a 10sp line: the gaps
    // and sizes CastSection stacks under its portrait.
    if (auto* labels = dynamic_cast<brls::Box*>(this->getView("video/card/labels"))) {
        labels->setHeight(88);
        labels->setPadding(20, 0, 0, 0);
    }
    brls::Theme theme = brls::Application::getTheme();
    this->labelTitle->setFontSize(24);
    this->labelTitle->setHeight(32);
    this->labelTitle->setTextColor(theme["font/grey"]);
    this->labelExt->setFontSize(20);
    this->labelExt->setHeight(28);
    this->labelExt->setTextColor(theme["font/tertiary"]);
}

void BaseCardCell::setLabelsVisible(bool visible) {
    if (auto* labels = this->getView("video/card/labels"))
        labels->setVisibility(visible ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
}

void VideoCardCell::setWatched(bool played) {
    if (played) {
        this->badgeTopRight->setImageFromSVGRes("icon/ico-checkmark.svg");
        this->badgeTopRight->setVisibility(brls::Visibility::VISIBLE);
    } else {
        this->badgeTopRight->setVisibility(brls::Visibility::GONE);
    }
    // watched clears any resume bar; un-watched also hides it (offset reset)
    this->rectProgress->getParent()->setVisibility(brls::Visibility::GONE);
}

void BaseCardCell::registerContextMenu() {
    auto actionListener = [this](brls::View*) -> bool {
        // climbs the hierarchy up to the recycler (vertical RecyclingGrid
        // or the home rows' HRecyclerFrame) rather than freezing the depth
        // (getParent()->getParent()), fragile across containers
        brls::Box* view = this->getParent();
        RecyclingView* recycler = nullptr;
        while (view && !(recycler = dynamic_cast<RecyclingView*>(view))) view = view->getParent();
        if (!recycler) return false;
        VideoDataSource* dataSrc = dynamic_cast<VideoDataSource*>(recycler->getDataSource());
        if (!dataSrc) return false;
        // virtual: the Continue Watching row answers with its own list
        dataSrc->onContextMenu(view, this->getIndex());
        return true;
    };
    // visible hint ("X Options") in the bottom bar when the card is
    // focused — the action used to be hidden (hidden=true), undiscoverable
    // gamepad in hand on console
    this->registerAction("hints/option"_i18n, brls::BUTTON_X, actionListener);
    this->registerAction(KeyBind::getSetting(), actionListener);
}

VideoCardCell::VideoCardCell() {
    this->inflateFromXMLRes("xml/view/video_card.xml");
    this->applyPosterLabels();
    this->registerContextMenu();
}

/// ModernCarouselCard's landscape order: the backdrop, then the poster. An
/// episode's own `thumb` IS a 16:9 still, so it comes first for one.
const std::string& VideoCardCell::landscapeArt(const plex::Item& item) {
    static const std::string kEmpty;
    if (item.type == plex::mediaTypeEpisode) {
        if (!item.thumb.empty()) return item.thumb;
        if (!item.grandparentArt.empty()) return item.grandparentArt;
    }
    if (!item.art.empty()) return item.art;
    if (!item.thumb.empty()) return item.thumb;
    return kEmpty;
}

void VideoCardCell::applyLandscape(bool on, const plex::Item& item) {
    // Cells are recycled, so the overlay has to be taken back off as well as
    // put on — a portrait build never calls this with `on`, but one row can
    // still hand a cell an item with no logo after one that had one.
    Image::cancel(this->overlayLogo);
    this->overlayLogo->clear();
    this->overlayLogo->setVisibility(brls::Visibility::GONE);
    this->overlayTitle->setVisibility(brls::Visibility::GONE);
    if (!on) {
        this->overlayBox->setVisibility(brls::Visibility::GONE);
        return;
    }
    this->overlayBox->setVisibility(brls::Visibility::VISIBLE);

    // 34% of the card's height, as the reference sizes its logo box. The row
    // knows the height; the cell asks the style for the same number the row
    // was built from, less the label block.
    float cardHeight = brls::getStyle()["app/card/poster/row"] - brls::getStyle()["app/card/labels"];
    this->overlayLogo->setHeight(cardHeight * 0.34f);

    const std::string& title = item.grandparentTitle.empty() ? item.title : item.grandparentTitle;
    if (item.clearLogo.empty()) {
        this->overlayTitle->setText(title);
        this->overlayTitle->setVisibility(brls::Visibility::VISIBLE);
        return;
    }
    // The title stands in until the logo's pixels actually arrive, and stays
    // if they never do — an advertised url is not proof of an image.
    this->overlayTitle->setText(title);
    this->overlayTitle->setVisibility(brls::Visibility::VISIBLE);
    brls::Image* logo = this->overlayLogo;
    brls::Label* label = this->overlayTitle;
    Image::load(logo, item.clearLogo, (int)brls::getStyle()["app/card/poster/width"], 0,
        [logo, label](bool ok, bool) {
            if (!ok) return;
            logo->setVisibility(brls::Visibility::VISIBLE);
            label->setVisibility(brls::Visibility::GONE);
        });
}
