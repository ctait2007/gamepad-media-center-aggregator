/*
    GMCA — hub visibility manager (see hub_visibility_manager.hpp).
*/

#include "view/hub_visibility_manager.hpp"
#include "api/backend.hpp"
#include "utils/config.hpp"
#include "view/loading_spinner.hpp"

using namespace brls::literals;  // for _i18n

namespace {

/// linear RGBA blend (t=0 -> a, t=1 -> b).
NVGcolor mix(NVGcolor a, NVGcolor b, float t) {
    NVGcolor c;
    for (int i = 0; i < 4; i++) c.rgba[i] = a.rgba[i] * (1.f - t) + b.rgba[i] * t;
    return c;
}

/// shell of the manager screen: title + description + a scrollable, centered
/// column of rows (populated programmatically) — mirrors library_manager.cpp.
const std::string managerXML = R"xml(
    <brls:Box
        axis="column"
        grow="1"
        justifyContent="flexStart"
        alignItems="center"
        paddingTop="40"
        paddingLeft="30"
        paddingRight="30"
        paddingBottom="20">

        <brls:Label
            fontSize="30"
            text="@i18n/main/setting/hidden_rows/header"
            marginBottom="6" />

        <brls:Label
            id="hub_manager/desc"
            fontSize="15"
            horizontalAlign="center"
            text="@i18n/main/setting/hidden_rows/description"
            marginBottom="24" />

        <brls:ScrollingFrame
            width="100%"
            grow="1">
            <brls:Box
                width="100%"
                axis="column"
                alignItems="center"
                paddingTop="4"
                paddingBottom="20">
                <brls:Box
                    id="hub_manager/rows"
                    width="680"
                    axis="column" />
            </brls:Box>
        </brls:ScrollingFrame>

    </brls:Box>
)xml";

/// One toggle row: title + a visible/hidden tag. No reorder (unlike
/// LibraryManager's LibraryRow, which this otherwise mirrors) — A/tap just
/// flips visibility directly.
class HubRow : public brls::Box {
public:
    HubRow(HubVisibilityManager* mgr, int index, const std::string& title, bool hidden)
        : mgr(mgr), index(index), hiddenState(hidden) {
        auto theme = brls::Application::getTheme();
        this->accent = theme.getColor("color/app");
        this->surface = theme.getColor("color/surface");
        this->text = theme.getColor("brls/text");
        this->grey = theme.getColor("font/grey");

        this->setAxis(brls::Axis::ROW);
        this->setWidthPercentage(100);
        this->setHeight(60);
        this->setCornerRadius(12);
        this->setMarginBottom(8);
        this->setPaddingLeft(18);
        this->setPaddingRight(18);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setFocusable(true);
        this->setHideHighlight(true);

        this->label = new brls::Label();
        this->label->setText(title);
        this->label->setFontSize(18);
        this->label->setGrow(1.0f);
        this->addView(this->label);

        this->tag = new brls::Label();
        this->tag->setText(
            this->hiddenState ? "main/setting/libraries/hidden"_i18n : "main/setting/libraries/visible"_i18n);
        this->tag->setFontSize(14);
        this->addView(this->tag);

        this->registerAction("main/setting/libraries/toggle"_i18n, brls::BUTTON_A, [mgr, index](brls::View*) {
            mgr->toggleVisible(index);
            return true;
        });
        this->addGestureRecognizer(
            new brls::TapGestureRecognizer(this, [mgr, index]() { mgr->toggleVisible(index); }));

        this->applyVisual(false);
    }

    void onFocusGained() override {
        brls::Box::onFocusGained();
        this->applyVisual(true);
    }
    void onFocusLost() override {
        brls::Box::onFocusLost();
        this->applyVisual(false);
    }

private:
    void applyVisual(bool focused) {
        this->setBackgroundColor(focused ? mix(this->surface, this->accent, 0.18f) : nvgRGBA(0, 0, 0, 0));
        this->setBorderColor(this->accent);
        this->setBorderThickness(focused ? 2.f : 0.f);
        this->setShadowType(focused ? brls::ShadowType::GENERIC : brls::ShadowType::NONE);
        this->setShadowVisibility(focused);
        // dim hidden rows at rest so the state reads at a glance
        this->setAlpha((this->hiddenState && !focused) ? 0.45f : 1.f);
        this->label->setTextColor(focused ? this->accent : this->text);
        this->tag->setTextColor(this->hiddenState ? this->grey : this->accent);
    }

    HubVisibilityManager* mgr;
    int index;
    bool hiddenState;

    NVGcolor accent {}, surface {}, text {}, grey {};
    brls::Label* label = nullptr;
    brls::Label* tag = nullptr;
};

}  // namespace

HubVisibilityManager::HubVisibilityManager() {
    this->inflateFromXMLString(managerXML);
    this->rowsBox = dynamic_cast<brls::Box*>(this->getView("hub_manager/rows"));
    if (auto* desc = dynamic_cast<brls::Label*>(this->getView("hub_manager/desc")))
        desc->setTextColor(brls::Application::getTheme().getColor("font/grey"));

    this->spinner = new LoadingSpinner();
    this->addView(this->spinner);

    this->doRequest();
    brls::Logger::debug("HubVisibilityManager: create");
}

void HubVisibilityManager::doRequest() {
    this->spinner->setSpinning(true);
    ASYNC_RETAIN
    AppConfig::instance().backend().getContinueWatching(
        20,
        [ASYNC_TOKEN](const media::Container<media::Hub>& r) {
            ASYNC_RELEASE
            for (auto& hub : r.Items) {
                if (hub.items.empty() || hub.hubIdentifier.empty()) continue;
                std::string title = hub.title.empty() ? "main/home/resume"_i18n : hub.title;
                this->entries.push_back({hub.hubIdentifier, title, AppConfig::instance().isHubHidden(hub.hubIdentifier)});
            }
            this->fetchHomeHubs();
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Logger::warning("HubVisibilityManager continueWatching: {}", ex);
            this->fetchHomeHubs();
        });
}

void HubVisibilityManager::fetchHomeHubs() {
    ASYNC_RETAIN
    AppConfig::instance().backend().getHomeHubs(
        50, false,
        [ASYNC_TOKEN](const media::Container<media::Hub>& r) {
            ASYNC_RELEASE
            for (auto& hub : r.Items) {
                if (hub.items.empty() || hub.hubIdentifier.empty()) continue;
                if (hub.hubIdentifier == "home.continue" || hub.hubIdentifier == "home.ondeck") continue;
                bool exists = false;
                for (auto& e : this->entries)
                    if (e.identifier == hub.hubIdentifier) exists = true;
                if (!exists)
                    this->entries.push_back({hub.hubIdentifier, hub.title, AppConfig::instance().isHubHidden(hub.hubIdentifier)});
            }
            this->loaded = true;
            this->spinner->setSpinning(false);
            this->rebuild();
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Logger::warning("HubVisibilityManager homeHubs: {}", ex);
            this->loaded = true;
            this->spinner->setSpinning(false);
            this->rebuild();
        });
}

void HubVisibilityManager::rebuild() {
    if (!this->rowsBox) return;
    this->rowsBox->clearViews();
    this->focusTarget = nullptr;

    if (this->loaded && this->entries.empty()) {
        auto* empty = new brls::Label();
        empty->setText("main/setting/hidden_rows/empty"_i18n);
        empty->setFontSize(16);
        empty->setTextColor(brls::Application::getTheme().getColor("font/grey"));
        empty->setMarginTop(40);
        this->rowsBox->addView(empty);
        return;
    }

    for (int i = 0; i < (int)this->entries.size(); i++)
        this->rowsBox->addView(new HubRow(this, i, this->entries[i].title, this->entries[i].hidden));

    int idx = this->pendingFocus;
    if (idx < 0) idx = 0;
    if (idx >= (int)this->entries.size()) idx = (int)this->entries.size() - 1;
    if (idx >= 0) {
        this->focusTarget = this->rowsBox->getChildren()[idx];
        brls::Application::giveFocus(this->focusTarget);
    }
}

void HubVisibilityManager::toggleVisible(int index) {
    if (index < 0 || index >= (int)this->entries.size()) return;
    this->entries[index].hidden = !this->entries[index].hidden;
    this->pendingFocus = index;
    AppConfig::instance().setHubHidden(this->entries[index].identifier, this->entries[index].hidden);
    this->rebuild();
}

brls::View* HubVisibilityManager::getDefaultFocus() {
    return this->focusTarget ? this->focusTarget : brls::Box::getDefaultFocus();
}
