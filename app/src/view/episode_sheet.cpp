#include "view/episode_sheet.hpp"

#include "utils/image.hpp"
#include "view/action_sheet.hpp"
#include "view/text_box.hpp"

using namespace brls::literals;

namespace {

/// Measurements are the reference's at its 2.0 density: 64dp of side padding,
/// 48dp top/bottom, 72dp between the two columns, a 360dp action column, 12dp
/// between entries. The scrim is its horizontal gradient flattened to two
/// layers — the opaque-to-clear ramp it already ships, plus a thin wash that
/// holds the right-hand end at the ~0.30 the reference stops at.
const std::string episodeSheetXML = R"xml(
    <brls:Box
        width="auto"
        height="auto"
        backgroundColor="#050505">

        <brls:Image
            id="episode/sheet/backdrop"
            positionType="absolute"
            positionTop="0"
            positionLeft="0"
            width="100%"
            height="100%"
            scalingType="fill" />

        <brls:Rectangle
            positionType="absolute"
            positionTop="0"
            positionLeft="0"
            width="100%"
            height="100%"
            color="#0505054D" />

        <brls:Image
            positionType="absolute"
            positionTop="0"
            positionLeft="0"
            width="100%"
            height="100%"
            scalingType="stretch"
            image="@res/img/fade-left-dark.png" />

        <brls:Box
            axis="row"
            width="100%"
            height="100%"
            alignItems="center"
            paddingLeft="128"
            paddingRight="128"
            paddingTop="96"
            paddingBottom="96">

            <brls:Box
                axis="column"
                grow="1"
                marginRight="144"
                justifyContent="center">
                <!-- titleMedium SemiBold in the theme accent -->
                <brls:Label
                    id="episode/sheet/number"
                    fontSize="32"
                    fontWeight="semibold" />
                <TextBox
                    id="episode/sheet/title"
                    fontSize="64"
                    fontWeight="semibold"
                    marginTop="32"
                    maxRows="3" />
                <TextBox
                    id="episode/sheet/summary"
                    fontSize="32"
                    marginTop="32"
                    textColor="#FFFFFFB8"
                    maxRows="6" />
            </brls:Box>

            <brls:Box
                id="episode/sheet/actions"
                axis="column"
                width="720"
                justifyContent="center" />
        </brls:Box>
    </brls:Box>
)xml";

}  // namespace

EpisodeSheet::EpisodeSheet(const media::Item& episode) {
    this->inflateFromXMLString(episodeSheetXML);

    this->labelNumber->setTextColor(brls::Application::getTheme().getColor("color/app"));
    if (episode.parentIndex > 0 && episode.index > 0)
        this->labelNumber->setText(fmt::format("S{} E{}", episode.parentIndex, episode.index));
    else if (episode.index > 0)
        this->labelNumber->setText(fmt::format("main/media/episode_n"_i18n, episode.index));
    else
        this->labelNumber->setVisibility(brls::Visibility::GONE);

    this->labelTitle->setText(episode.title);
    if (episode.summary.empty())
        this->labelSummary->setVisibility(brls::Visibility::GONE);
    else
        this->labelSummary->setText(episode.summary);

    // the episode's own still, else the show's art — same fallback the cards use
    const std::string& art = !episode.thumb.empty() ? episode.thumb
                             : !episode.grandparentArt.empty() ? episode.grandparentArt
                                                               : episode.art;
    if (!art.empty()) Image::load(this->backdrop, art, 1280, 720);

    this->registerAction("hints/cancel"_i18n, brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    });
}

void EpisodeSheet::addAction(const std::string& text, std::function<void()> onClick) {
    auto* button = new SheetButton(text, std::move(onClick));
    if (this->firstAction) button->setMarginTop(24);
    this->boxActions->addView(button);
    if (!this->firstAction) this->firstAction = button;
}

void EpisodeSheet::present() { brls::Application::pushActivity(new brls::Activity(this)); }

brls::View* EpisodeSheet::getDefaultFocus() {
    return this->firstAction ? this->firstAction : brls::Box::getDefaultFocus();
}
