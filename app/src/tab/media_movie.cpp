#include "activity/player_view.hpp"
#include "tab/media_movie.hpp"
#include "tab/source_list.hpp"
#include "view/h_recycling.hpp"
#include "view/icon_button.hpp"
#include "view/video_card.hpp"
#include "view/text_box.hpp"
#include "view/people_source.hpp"
#include "view/recyling_video.hpp"
#include "view/mpv_core.hpp"
#include "api/plex.hpp"
#include "api/plex/watchlist.hpp"
#include "api/backend.hpp"
#include "utils/misc.hpp"
#include "utils/dialog.hpp"
#include "utils/download.hpp"
#include "utils/rating.hpp"
#include "utils/media_source.hpp"
#include "utils/offline_library.hpp"
#include "utils/network_state.hpp"
#include "tab/remote_view.hpp"
#include <fmt/ranges.h>

using namespace brls::literals;  // for _i18n

namespace {

/// Single-line label with explicit size/color (used to compose source rows).
brls::Label* sourceLabel(const std::string& text, float size, NVGcolor color, bool grow = false) {
    auto* l = new brls::Label();
    l->setText(text);
    l->setFontSize(size);
    l->setTextColor(color);
    l->setSingleLine(true);
    if (grow) l->setGrow(1);
    return l;
}

/// Small rounded badge (quality / status / source-type chip).
brls::Box* sourcePill(const std::string& text, NVGcolor bg, NVGcolor fg) {
    auto* box = new brls::Box();
    box->setAxis(brls::Axis::ROW);
    box->setAlignItems(brls::AlignItems::CENTER);
    box->setHeight(22);
    box->setCornerRadius(11);
    box->setBackgroundColor(bg);
    box->setPaddingLeft(9);
    box->setPaddingRight(9);
    box->setMarginRight(8);
    box->addView(sourceLabel(text, 12, fg));
    return box;
}

/// A selectable release/source line. Paints a light-orange "selected" fill on
/// focus (like the active sidebar menu) instead of the default dark highlight
/// background — the rows ARE the primary interaction for Stremio, so the
/// selected state must read clearly on a 10-foot screen.
class SourceRow : public brls::Box {
public:
    SourceRow() {
        this->setAxis(brls::Axis::ROW);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setHeight(46);
        this->setCornerRadius(8);
        this->setHighlightCornerRadius(8);
        this->setFocusable(true);
        this->setPaddingLeft(12);
        this->setPaddingRight(12);
        this->setMarginBottom(6);
        // we paint our own orange fill; keep the gold focus border, drop the
        // default dark highlight background (wrong color for a "selected" row).
        this->setHideHighlightBackground(true);
    }
    void onFocusGained() override {
        brls::Box::onFocusGained();
        NVGcolor c = brls::Application::getTheme().getColor("color/app");
        c.a = 0.22f;  // light orange tint
        this->setBackgroundColor(c);
    }
    void onFocusLost() override {
        brls::Box::onFocusLost();
        this->setBackgroundColor(nvgRGBA(0, 0, 0, 0));
    }
};

}  // namespace

MediaMovie::MediaMovie(const plex::Item& item, bool localContext)
    : itemId(item.ratingKey), localContext(localContext) {
    brls::Logger::debug("Tab MediaMovie: create");
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/movie.xml");

    // Backs playback before doMovie resolves; non-addon-protocol backends are
    // always playable (Stremio/Nuvio defer until their sources are resolved).
    this->movieItem = item;
    auto backendType = AppConfig::instance().backend().type();
    bool stremioBackend = backendType == media::BackendType::Stremio || backendType == media::BackendType::Nuvio;
    // Stremio/Nuvio no longer resolve sources to open this page, so whether a
    // playable one exists is not knowable here — Play always works and the
    // source list answers that question after the user asks it.
    this->hasPlayableSource = true;
    // Still no version selector on the addon backends: the source list is it.
    if (stremioBackend) this->btnSource->setVisibility(brls::Visibility::GONE);

    this->labelTitle->setText(item.title);
    Image::load(this->imagePoster, item.thumb, 325);
    if (brls::Application::getThemeVariant() == brls::ThemeVariant::LIGHT)
        this->imageFade->setImageFromRes("img/fade-bottom-light.png");
    this->people->registerCell("Cell", MediaCardCell::create);
    // the buttons and the cast row have no geometric overlap: D-pad nav
    // cannot find it. Explicit route — the row materializes its first cell
    // if needed (HRecyclerFrame::getDefaultFocus) and the "centered" scroll
    // follows the focus
    this->btnPlay->setCustomNavigationRoute(brls::FocusDirection::DOWN, "movie/people");
    this->btnDownload->setCustomNavigationRoute(brls::FocusDirection::DOWN, "movie/people");
    this->btnWatchlist->setCustomNavigationRoute(brls::FocusDirection::DOWN, "movie/people");

    this->btnPlay->registerClickAction([this](...) {
        // in the offline downloads area (or fully offline) a downloaded movie
        // plays from the local file; from the ONLINE library it keeps streaming
        // with the server resume position (SPEC — no online regression)
        auto& dm = DownloadManager::instance();
        std::string local = media::preferLocal(this->localContext) && dm.isDownloaded(this->itemId)
                                ? dm.getLocalPath(this->itemId)
                                : "";
        if (!local.empty()) {
            std::string title = this->movieItem.year
                                     ? fmt::format("{} ({})", this->movieItem.title, this->movieItem.year)
                                     : this->movieItem.title;
            RemoteView::play(local, title, "Local");
            return true;
        }
        auto bt = AppConfig::instance().backend().type();
        if (bt == media::BackendType::Stremio || bt == media::BackendType::Nuvio) {
            // Addon backends: pressing Play is what triggers the /stream
            // fan-out, inside the source list. Opening this page no longer
            // pays for it.
            std::string title = this->movieItem.year
                                     ? fmt::format("{} ({})", this->movieItem.title, this->movieItem.year)
                                     : this->movieItem.title;
            ui::presentDetail(this, new SourceList(this->movieItem, title, this->viewOffsetMs));
            return true;
        }
        if (this->hasPlayableSource) {
            this->playSource(-1);
        } else {
            Dialog::show("main/stremio/source/none"_i18n);
        }
        return true;
    });

    auto& dm = DownloadManager::instance();
    this->updateDownloadButton();
    // live progress on the button (events emitted on the UI thread)
    this->progressSub = dm.getProgressEvent()->subscribe(
        [this](const std::string& id, int64_t downloaded, int64_t total, double) {
            if (id != this->itemId || total <= 0) return;
            // "Downloading... (42%)" — a bare percentage does not say what
            // the button does; completion goes back through updateDownloadButton
            this->btnDownload->setText(fmt::format(
                "{} ({:.0f}%)", "main/download/downloading"_i18n, downloaded * 100.0 / total));
        });
    this->statusSub = dm.getStatusEvent()->subscribe([this](const std::string& id, DownloadStatus status) {
        if (id == this->itemId) this->updateDownloadButton();
    });
    this->btnDownload->registerClickAction([this](...) {
        auto& dm = DownloadManager::instance();
        if (dm.isDownloading(this->itemId)) {
            Dialog::cancelable("main/download/confirm_cancel"_i18n, [this]() {
                DownloadManager::instance().cancelDownload(this->itemId);
                this->updateDownloadButton();
            });
        } else if (dm.isDownloaded(this->itemId)) {
            // already downloaded: offer to remove it (clicking "Downloaded"
            // should do something useful, not just re-announce the state)
            Dialog::cancelable("main/download/confirm_remove"_i18n, [this]() {
                DownloadManager::instance().removeDownload(this->itemId);
                this->updateDownloadButton();
            });
        } else {
            dm.addDownload(this->itemId);
            this->updateDownloadButton();
        }
        return true;
    });

    this->doMovie();
    this->doRelated();
}

MediaMovie::~MediaMovie() {
    brls::Logger::debug("Tab MediaMovie: delete");
    auto& dm = DownloadManager::instance();
    dm.getProgressEvent()->unsubscribe(this->progressSub);
    dm.getStatusEvent()->unsubscribe(this->statusSub);
    Image::cancel(this->imageLogo);
    Image::cancel(this->imagePoster);
    Image::cancel(this->imageBackdrop);
}

/// Logo first, title as the fallback: an advertised logo url is not proof the
/// artwork exists, so nothing is revealed until the load reports back.
void MediaMovie::applyHeroLogo(const std::string& url) {
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
    Image::load(this->imageLogo, url, 690, 0, [ASYNC_TOKEN, showTitle](bool ok, bool retryable) {
        ASYNC_RELEASE
        (void)retryable;
        if (ok)
            this->imageLogo->setVisibility(brls::Visibility::VISIBLE);
        else
            showTitle();
    });
}

void MediaMovie::updateDownloadButton() {
    // Backends without original-quality download (Stremio: debrid links are
    // ephemeral) never show this button — avoids offering a download next to a
    // movie that has no playable source either.
    if (!AppConfig::instance().backend().caps().downloadOriginal) {
        this->btnDownload->setVisibility(brls::Visibility::GONE);
        return;
    }
    this->btnDownload->setVisibility(brls::Visibility::VISIBLE);
    auto& dm = DownloadManager::instance();
    if (dm.isDownloaded(this->itemId)) {
        this->btnDownload->setText("main/download/completed"_i18n);
    } else if (dm.isDownloading(this->itemId)) {
        this->btnDownload->setText("main/download/downloading"_i18n);
    } else {
        this->btnDownload->setText("main/download/start"_i18n);
    }
}

void MediaMovie::initWatchlist(const media::Item& item) {
    auto& be = AppConfig::instance().backend();
    // gated by the backend's personal-list capability + per-item applicability
    if (be.caps().listKind == media::ListKind::None || !be.canList(item)) return;
    this->listItem = item;
    // label matches the backend's personal list: Plex → Watchlist, Jellyfin/Emby → Favoris
    this->btnWatchlist->setText(be.caps().listKind == media::ListKind::Favorites
                                    ? "main/favorites/title"_i18n
                                    : "main/watchlist/title"_i18n);

    this->btnWatchlist->registerClickAction([this](...) {
        this->toggleWatchlist();
        return true;
    });

    ASYNC_RETAIN
    // the button stays hidden until the state (watchlisted / favorite) is known
    be.getWatchlistState(
        item,
        [ASYNC_TOKEN](bool state) {
            ASYNC_RELEASE
            this->watchlisted = state;
            this->updateWatchlistButton();
            this->btnWatchlist->setVisibility(brls::Visibility::VISIBLE);
            // Favoris is shown: make sure the buttons row (collapsed for Stremio
            // when it had no visible button) is visible again.
            if (this->btnWatchlist->getParent())
                this->btnWatchlist->getParent()->setVisibility(brls::Visibility::VISIBLE);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Logger::warning("MediaMovie list state: {}", ex);
        });
}

void MediaMovie::toggleWatchlist() {
    bool add = !this->watchlisted;
    auto& be = AppConfig::instance().backend();
    bool fav = be.caps().listKind == media::ListKind::Favorites;
    ASYNC_RETAIN
    be.setWatchlisted(
        this->listItem, add,
        [ASYNC_TOKEN, add, fav]() {
            ASYNC_RELEASE
            this->watchlisted = add;
            this->updateWatchlistButton();
            brls::Application::notify(add ? (fav ? "main/favorites/added"_i18n : "main/watchlist/added"_i18n)
                                          : (fav ? "main/favorites/removed"_i18n : "main/watchlist/removed"_i18n));
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::notify(ex);
        });
}

void MediaMovie::updateWatchlistButton() {
    // filled bookmark = already in the Watchlist (Plex convention)
    this->btnWatchlist->setIcon(
        this->watchlisted ? "@res/icon/ico-bookmark-fill-light.svg" : "@res/icon/ico-bookmark-light.svg");
}

void MediaMovie::doRequest() {
    int64_t seconds = MPVCore::instance().playback_time;
    this->viewOffsetMs = seconds * 1000;
    this->btnPlay->setText(seconds > 0 ? misc::sec2Time(seconds) : "main/media/play"_i18n);
}

void MediaMovie::doMovie() {
    // downloaded item, or fully offline: render from the local catalog and skip
    // the server round-trip entirely (SPEC AC5/AC6).
    if (media::preferLocal(this->localContext)) {
        media::Item it;
        if (OfflineLibrary::instance().getItem(this->itemId, it)) {
            this->applyMovie(it);
            return;
        }
        if (NetworkState::isOffline()) {
            this->people->setVisibility(brls::Visibility::GONE);
            return;
        }
    }

    ASYNC_RETAIN
    // full=false on purpose: `full` fans /stream out to every addon, which is
    // by far the slowest call in the app and pointless while merely browsing.
    // SourceList performs it when Play is pressed. Plex/Jellyfin ignore the
    // flag for streams (they carry media[] in the meta response anyway).
    AppConfig::instance().backend().getItemDetail(
        this->itemId, false,
        [ASYNC_TOKEN](const media::Item& item) {
            ASYNC_RELEASE
            this->applyMovie(item);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->peopleHeader->setVisibility(brls::Visibility::GONE);
            this->people->setVisibility(brls::Visibility::GONE);
        });
}

void MediaMovie::playSource(int mediaIndex) {
    // mediaIndex -1 = best (first accessible); otherwise the chosen source row.
    PlayerView* view = new PlayerView(this->movieItem, this->viewOffsetMs, mediaIndex);
    view->setTitie(this->movieItem.year ? fmt::format("{} ({})", this->movieItem.title, this->movieItem.year)
                                        : this->movieItem.title);
}

void MediaMovie::downloadSource(int mediaIndex) {
    if (mediaIndex < 0 || mediaIndex >= (int)this->movieItem.media.size()) return;
    const media::Media& m = this->movieItem.media[mediaIndex];
    if (!m.playable()) return;  // only playable sources carry a downloadable URL
    // addDownload dedups by ratingKey (one download per movie); reflect the real
    // outcome instead of always claiming "queued".
    auto& dm = DownloadManager::instance();
    if (dm.isDownloaded(this->itemId)) {
        brls::Application::notify("main/download/completed"_i18n);
        return;
    }
    if (dm.isDownloading(this->itemId)) {
        brls::Application::notify("main/download/downloading"_i18n);
        return;
    }
    dm.addDownload(this->movieItem, m.parts.front().key);
    brls::Application::notify("main/download/queued"_i18n);
}

// Renders the fiche from an Item — shared by the server and local-catalog
// (offline / downloaded) paths.
void MediaMovie::applyMovie(const media::Item& item) {
    this->labelTitle->setText(item.title);
    Image::load(this->imagePoster, item.thumb, 325);
    // banner: backdrop (art) + centered cut-out logo; the poster
    // block overlaps the banner (XML margins) to keep the buttons
    // visible. The banner stays shown while loading (dark
    // placeholder): no layout jump when the image arrives — and
    // the gone->visible transition triggered a first-render bug
    // (gradient + image fill).
    // cut-out logo nested at the bottom of the banner fade; the
    // text title is ALWAYS shown above the pills
    // The hero is a whole screenful of backdrop, as in the reference; the rest
    // of the page scrolls up from underneath it.
    this->bannerBox->setHeight((float)brls::Application::ORIGINAL_WINDOW_HEIGHT);
    if (!item.art.empty()) {
        Image::load(this->imageBackdrop, item.art, 1280, 720);
        // Logo OR title, never both.
        this->applyHeroLogo(item.clearLogo);
    } else {
        this->applyHeroLogo("");
        // no backdrop (e.g. an un-scanned Jellyfin/Emby item, or a poster-only
        // offline snapshot): drop the banner AND the overlap margins that assumed
        // it. The poster (marginTop 64 in XML, to rise into the banner) and the
        // info column (marginTop 184, to clear it) must reset too, otherwise the
        // title jams against the very top, misaligned with the poster.
        this->bannerBox->setVisibility(brls::Visibility::GONE);
        float topPad = brls::getStyle()["main/content_padding_top_bottom"];
        this->contentRow->setMarginTop(topPad);
        this->contentInfo->setMarginTop(0);
        this->imagePoster->getParent()->setMarginTop(0);
        // no banner: vertically center the info column (title, pills,
        // buttons, synopsis) against the poster instead of top-aligning
        this->contentRow->setAlignItems(brls::AlignItems::CENTER);
        this->invalidate();
    }
    // Primary line carries the year; the runtime moved to the secondary line
    // beside the age rating, as the reference lays them out.
    bool haveYear = item.year > 0;
    if (haveYear) this->labelYear->setText(std::to_string(item.year));
    this->labelYear->setVisibility(haveYear ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    if (item.duration > 0) {
        int min = int(item.duration / 60000);
        this->labelRuntime->setText(min >= 60 ? fmt::format("{}h {}m", min / 60, min % 60)
                                              : fmt::format("{}m", min));
        this->labelRuntime->setVisibility(brls::Visibility::VISIBLE);
    } else {
        this->labelRuntime->setVisibility(brls::Visibility::GONE);
    }
    if (item.contentRating.empty()) {
        this->parentalRating->getParent()->setVisibility(brls::Visibility::GONE);
    } else {
        this->parentalRating->setText(item.contentRating);
        this->parentalRating->getParent()->setVisibility(brls::Visibility::VISIBLE);
    }
    // critic (ratingImage: RT tomato / IMDb / TMDb) + audience
    // (audienceRatingImage: RT popcorn), official icons with a
    // generic-star fallback; each pill hides itself when absent
    rating::applyPill(this->iconRating, this->labelRating, item.ratingImage, item.rating);
    rating::applyPill(this->iconAudience, this->labelAudience, item.audienceRatingImage, item.audienceRating);
    this->labelOverview->setText(item.summary);

    bool haveGenres = !item.genres.empty();
    if (haveGenres) this->labelGenres->setText(fmt::format("{}", fmt::join(item.genres, "  •  ")));
    this->labelGenres->setVisibility(haveGenres ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    bool haveRating = item.rating > 0;
    this->sep1->setVisibility(haveGenres && (haveYear || haveRating) ? brls::Visibility::VISIBLE
                                                                    : brls::Visibility::GONE);
    this->sep2->setVisibility(haveYear && haveRating ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    // the director opens the row, subtitled "Director", clickable
    // to their person page like the actors
    std::vector<media::Role> credits = item.directors;
    for (auto& d : credits) d.role = "main/media/director"_i18n;
    credits.insert(credits.end(), item.roles.begin(), item.roles.end());
    if (credits.size() > 0) {
        this->people->setDataSource(new PeopleDataSource(credits));
    } else {
        // no cast/crew: hide the section header too, not just the row,
        // otherwise a lone "People" title sits over an empty space
        this->peopleHeader->setVisibility(brls::Visibility::GONE);
        this->people->setVisibility(brls::Visibility::GONE);
    }

    this->movieItem = item;  // resolved detail backs per-source playback
    this->viewOffsetMs = item.viewOffset;

    auto backendType = AppConfig::instance().backend().type();
    if (backendType == media::BackendType::Stremio || backendType == media::BackendType::Nuvio) {
        // Stremio/Nuvio: no Lire/version buttons — the inline source list is the
        // play/download UI (built here; sets hasPlayableSource). Collapse
        // the now-empty buttons row so it leaves no gap between the genres
        // and the synopsis; initWatchlist re-shows it if Favoris appears.
        // The inline source list is gone: sources are not fetched to render
        // this page any more, so there is nothing to list here. Play is the
        // affordance, and it opens SourceList.
        this->btnSource->setVisibility(brls::Visibility::GONE);
        this->sourcesBox->setVisibility(brls::Visibility::GONE);
        this->noticeBox->setVisibility(brls::Visibility::GONE);
        this->btnPlay->setMuted(false);
        this->btnPlay->setVisibility(brls::Visibility::VISIBLE);
        if (this->btnPlay->getParent()) this->btnPlay->getParent()->setVisibility(brls::Visibility::VISIBLE);
        this->btnPlay->setText(
            item.viewOffset > 0 ? misc::sec2Time(item.viewOffset / 1000) : "main/media/play"_i18n);
    } else {
        this->sourcesBox->setVisibility(brls::Visibility::GONE);
        this->hasPlayableSource = true;
        // multiple versions (item.media[]): the selector remembers the choice
        // but v1 playback always uses the first accessible version.
        if (item.media.size() > 1) {
            std::vector<std::string> names;
            for (auto& m : item.media)
                names.push_back(fmt::format("{} {} ({} kbps)", m.videoResolution, m.videoCodec, m.bitrate));
            this->btnSource->init(
                "main/setting/version"_i18n, names, 0, [this](int index) { this->selectedVersion = index; });
            this->btnSource->setVisibility(brls::Visibility::VISIBLE);
        } else {
            this->btnSource->setVisibility(brls::Visibility::GONE);
        }
        this->btnPlay->setMuted(false);
        this->btnPlay->setText(
            this->viewOffsetMs > 0 ? misc::sec2Time(this->viewOffsetMs / 1000) : "main/media/play"_i18n);
    }

    // the personal list (watchlist/favorite) needs the account — online only
    if (!NetworkState::isOffline()) this->initWatchlist(item);

    // Open the detail at the TOP. "centered" auto-centers the focused Play button
    // on first appear, which scrolls the page down for no reason — hiding the
    // title (backdrop-less items) or the top of the banner. We snap back to the
    // top once loaded; the centered follow-focus behavior still applies as soon
    // as the user navigates down (buttons → cast → related). Deferred a frame so
    // the layout (collapsed banner/cast) is settled before resetting.
    ASYNC_RETAIN
    brls::sync([ASYNC_TOKEN]() {
        ASYNC_RELEASE
        // Focus a VISIBLE target: Play on every backend now that the inline
        // release list is gone (it was the Stremio focus target). Fall back to
        // Favoris, else let borealis pick (cast/related).
        brls::View* target = nullptr;
        if (this->btnPlay->getVisibility() == brls::Visibility::VISIBLE) target = this->btnPlay;
        if (!target && this->btnWatchlist->getVisibility() == brls::Visibility::VISIBLE) target = this->btnWatchlist;
        if (target) brls::Application::giveFocus(target);
    });
}

void MediaMovie::doRelated() {
    // no "related" rows offline — that content is not downloaded (SPEC AC7)
    if (NetworkState::isOffline()) {
        this->boxRelated->clearViews();
        return;
    }

    ASYNC_RETAIN
    // all the server's "related" rows, localized titles
    AppConfig::instance().backend().getRelated(this->itemId, 12,
        [ASYNC_TOKEN](const media::Container<media::Hub>& r) {
            ASYNC_RELEASE
            this->boxRelated->clearViews();
            for (auto& hub : r.Items) {
                if (hub.items.empty()) continue;
                RecylingVideo* row = new RecylingVideo();
                row->setTitle(hub.title);
                row->setFrameHeight(brls::getStyle()["app/card/poster/row"]);
                row->setItemWidth(brls::getStyle()["app/card/poster/width"]);
                row->setSidePadding(brls::getStyle()["main/content_padding_sides"]);
                // truncated hub (more=1): "+" card to the full page
                if (hub.more && !hub.key.empty()) {
                    row->setItems(hub.items, hub.title, hub.key);
                } else {
                    row->setItems(hub.items);
                }
                this->boxRelated->addView(row);
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::notify(ex);
        });
}
