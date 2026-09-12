#include "view/home_hero.hpp"

#include "api/plex.hpp"
#include "tab/media_movie.hpp"
#include "tab/media_series.hpp"
#include "utils/config.hpp"
#include "utils/image.hpp"
#include "utils/misc.hpp"
#include "view/auto_tab_frame.hpp"

using namespace brls::literals;

HomeHero::HomeHero(const media::Item& item) : item(item) {
    this->inflateFromXMLRes("xml/view/home_hero.xml");
    brls::Logger::debug("View HomeHero: create");

    auto theme = brls::Application::getTheme();
    // The text sits on the artwork's dark fade, so it is light in BOTH themes
    // — the light theme's dark body colour would vanish against it. Same rule
    // as SourceList's scrim column.
    const NVGcolor onArt = nvgRGB(0xF5, 0xF5, 0xF5);
    const NVGcolor onArtDim = nvgRGB(0xB3, 0xB3, 0xB3);  // Nuvio textSecondary

    auto* title = dynamic_cast<brls::Label*>(this->getView("hero/title"));
    auto* meta = dynamic_cast<brls::Label*>(this->getView("hero/meta"));
    auto* overview = dynamic_cast<brls::Label*>(this->getView("hero/overview"));
    auto* button = this->getView("hero/button");
    auto* buttonLabel = dynamic_cast<brls::Label*>(this->getView("hero/button/label"));
    auto* backdrop = dynamic_cast<brls::Image*>(this->getView("hero/backdrop"));

    if (title) {
        title->setText(item.title);
        title->setTextColor(onArt);
    }

    // year · runtime · genres, skipping whatever the item does not carry
    if (meta) {
        std::vector<std::string> bits;
        if (item.year) bits.push_back(std::to_string(item.year));
        if (item.duration > 0) {
            // same shape as the movie page's meta line; sec2Time's HH:MM:SS is
            // for playback positions, not runtimes
            int min = int(item.duration / 60000);
            bits.push_back(min >= 60 ? fmt::format("{} h {:02d}", min / 60, min % 60) : fmt::format("{} min", min));
        }
        for (auto& g : item.genres) {
            if (bits.size() >= 5) break;
            bits.push_back(g);
        }
        std::string line;
        for (size_t i = 0; i < bits.size(); i++) line += (i ? "  ·  " : "") + bits[i];
        meta->setText(line);
        meta->setTextColor(onArtDim);
    }

    if (overview) {
        overview->setText(item.summary);
        overview->setTextColor(onArt);
    }

    if (buttonLabel) buttonLabel->setText("main/media/more_info"_i18n);

    // white pill with dark text: Nuvio's primary action, and deliberately NOT
    // the theme accent — on the Nuvio palette the accent IS near-white, and on
    // the others a coloured pill here fights the artwork behind it.
    if (button) {
        button->setBackgroundColor(nvgRGB(0xFF, 0xFF, 0xFF));
        if (buttonLabel) buttonLabel->setTextColor(nvgRGB(0x11, 0x11, 0x11));
        button->registerClickAction([this](brls::View* view) {
            if (this->item.type == plex::mediaTypeShow || this->item.type == plex::mediaTypeSeason)
                ui::presentDetail(view, new MediaSeries(this->item, false));
            else
                ui::presentDetail(view, new MediaMovie(this->item, false));
            return true;
        });
        button->addGestureRecognizer(new brls::TapGestureRecognizer(button));
    }

    std::string art = item.art.empty() ? item.thumb : item.art;
    if (backdrop && !art.empty()) Image::with(backdrop, art);
}

HomeHero::~HomeHero() { brls::Logger::debug("View HomeHero: delete"); }
