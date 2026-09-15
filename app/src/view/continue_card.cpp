#include "view/continue_card.hpp"
#include "utils/config.hpp"

#include "activity/player_view.hpp"
#include "api/plex.hpp"
#include "api/backend.hpp"
#include "tab/source_list.hpp"
#include "utils/image.hpp"
#include "view/action_sheet.hpp"
#include "view/auto_tab_frame.hpp"
#include "view/mpv_core.hpp"

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
        // use_episode_thumbnails_in_cw: the episode's own still by default,
        // the show's backdrop when the viewer would rather not be shown a
        // frame of something they have not watched yet.
        bool stills = AppConfig::instance().getItem(AppConfig::LAYOUT_CW_EPISODE_THUMBS, true);
        std::string backdrop = !item.grandparentArt.empty() ? item.grandparentArt : item.art;
        if (stills)
            art = !item.thumb.empty() ? item.thumb : backdrop;
        else
            art = !backdrop.empty() ? backdrop : item.thumb;
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

void ContinueDataSource::onItemSelected(brls::Box* recycler, size_t index) {
    // the trailing "+" card, and anything not a movie, keep the inherited
    // behaviour — episodes already resume there, and that path also knows
    // about downloaded local files.
    if (index >= this->list.size() || this->list.at(index).type != plex::mediaTypeMovie) {
        VideoDataSource::onItemSelected(recycler, index);
        return;
    }
    plex::Item item = this->list.at(index);
    // Resume where the row says, and let the backend pick the source: the tile
    // is the "carry on" control, not a way into the picker.
    PlayerView* view = new PlayerView(item, item.viewOffset);
    view->setTitie(item.year ? fmt::format("{} ({})", item.title, item.year) : item.title);
}

/// NuvioTV's ContinueWatchingOptionsDialog: Go to details, then Play manually,
/// Start from beginning (only when there IS something to start over) and
/// Remove. The wording and the order are the reference's.
void ContinueDataSource::onContextMenu(brls::Box* recycler, size_t index) {
    if (index >= this->list.size()) return;
    plex::Item item = this->list.at(index);  // by value: the sheet outlives the frame

    // The reference heads the sheet with the thing being resumed, which for an
    // episode is the show, not the episode name.
    std::string head = item.type == plex::mediaTypeEpisode && !item.grandparentTitle.empty()
                           ? item.grandparentTitle
                           : item.title;
    auto* sheet = new ActionSheet(head, "main/media/item_actions"_i18n);

    sheet->addAction("main/media/go_details"_i18n, [this, recycler, item]() { this->openDetail(recycler, item); });

    // "Play manually" = let me pick the source. Today EVERY play on an addon
    // backend goes through the picker, so it is the same journey as Play; the
    // entry exists because the reference has it, and because it is what will
    // stay honest once automatic source selection lands and Play stops asking.
    auto play = [recycler, item](int64_t resumeMs) {
        plex::Item it = item;
        it.viewOffset = resumeMs;
        std::string title = it.type == plex::mediaTypeEpisode
                                ? fmt::format("S{}E{} - {}", it.parentIndex, it.index, it.title)
                                : (it.year ? fmt::format("{} ({})", it.title, it.year) : it.title);
        auto bt = AppConfig::instance().backend().type();
        if (bt == media::BackendType::Stremio || bt == media::BackendType::Nuvio) {
            ui::presentDetail(recycler, new SourceList(it, title, resumeMs));
            return;
        }
        PlayerView* view = new PlayerView(it, resumeMs);
        view->setTitie(title);
        if (!it.grandparentRatingKey.empty()) view->setSeries(it.grandparentRatingKey);
    };

    sheet->addAction("main/media/play_manually"_i18n, [play, item]() { play(item.viewOffset); });

    if (item.viewOffset > 0) sheet->addAction("main/media/start_over"_i18n, [play]() { play(0); });

    std::string id = item.ratingKey;
    sheet->addAction("main/media/remove_resume"_i18n, [id]() {
        AppConfig::instance().backend().removeFromContinueWatching(id);
        // the row is rebuilt from the backend, which has just been told
        MPVCore::instance().getCustomEvent()->fire(VIDEO_CLOSE, nullptr);
    });

    sheet->present();
}
