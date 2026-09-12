#include "view/continue_card.hpp"

#include "api/plex.hpp"
#include "utils/image.hpp"

using namespace brls::literals;

std::string humanDuration(int64_t ms) {
    int64_t minutes = ms / 60000;
    if (minutes < 1) minutes = 1;  // "0m left" reads as finished; it is not
    if (minutes < 60) return fmt::format("{}m", minutes);
    return fmt::format("{}h {}m", minutes / 60, minutes % 60);
}

RecyclingGridItem* ContinueDataSource::cellForRow(RecyclingView* recycler, size_t index) {
    auto* cell = dynamic_cast<ContinueCardCell*>(recycler->dequeueReusableCell("Continue"));
    auto& item = this->list.at(index);
    cell->setId(item.ratingKey);
    // recycled cell: purge the previous still, otherwise an item without art
    // inherits another's texture
    cell->picture->clear();

    if (item.type == plex::mediaTypeEpisode) {
        cell->labelEpisode->setText(fmt::format("S{} E{}", item.parentIndex, item.index));
        cell->labelEpisode->setVisibility(brls::Visibility::VISIBLE);
        // the show is the headline; the episode name goes underneath, as in
        // the reference. A stray episode with no show name keeps its own.
        if (item.grandparentTitle.empty()) {
            cell->labelName->setText(item.title);
            cell->labelSubtitle->setVisibility(brls::Visibility::GONE);
        } else {
            cell->labelName->setText(item.grandparentTitle);
            cell->labelSubtitle->setText(item.title);
            cell->labelSubtitle->setVisibility(item.title.empty() ? brls::Visibility::GONE
                                                                 : brls::Visibility::VISIBLE);
        }
    } else {
        cell->labelEpisode->setVisibility(brls::Visibility::GONE);
        cell->labelSubtitle->setVisibility(brls::Visibility::GONE);
        cell->labelName->setText(item.title);
    }

    // 16:9 art for a 16:9 tile: an episode's own still, else the show's
    // backdrop. A movie's `thumb` is its 2:3 poster, which would be cropped to
    // a strip here, so the backdrop wins for those too.
    std::string art;
    if (item.type == plex::mediaTypeEpisode) {
        art = !item.thumb.empty() ? item.thumb : (!item.grandparentArt.empty() ? item.grandparentArt : item.art);
    } else {
        art = !item.art.empty() ? item.art : item.thumb;
    }
    if (!art.empty()) Image::load(cell->picture, art, 500, 281);

    int64_t left = item.duration - item.viewOffset;
    if (item.duration > 0 && item.viewOffset > 0 && left > 0) {
        // same words as the download ETA ("{} left"), already translated
        cell->labelLeft->setText(brls::getStr("main/download/eta", humanDuration(left)));
        cell->boxLeft->setVisibility(brls::Visibility::VISIBLE);
        cell->rectProgress->setWidthPercentage(float(item.viewOffset) / float(item.duration) * 100.f);
        cell->rectProgress->getParent()->setVisibility(brls::Visibility::VISIBLE);
    } else {
        cell->boxLeft->setVisibility(brls::Visibility::GONE);
        cell->rectProgress->getParent()->setVisibility(brls::Visibility::GONE);
    }

    // every card in this row resumes playback on select
    cell->setPlayOverlay(true);
    cell->updateActionHint(brls::BUTTON_A, "main/media/play"_i18n);
    return cell;
}
