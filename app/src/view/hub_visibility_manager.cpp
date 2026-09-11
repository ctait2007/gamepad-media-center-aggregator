/*
    GMCA — hub visibility manager (see hub_visibility_manager.hpp).
*/

#include "view/hub_visibility_manager.hpp"
#include "api/backend.hpp"
#include "utils/config.hpp"
#include "view/loading_spinner.hpp"
#include <algorithm>

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

/// One row: title + a visible/hidden tag. Mirrors LibraryManager's
/// LibraryRow (grab-to-reorder), minus the icon column — but only a
/// non-hidden row can be grabbed: hidden rows always sort below every
/// non-hidden one (HubVisibilityManager::sortEntries), so there is nothing
/// meaningful to reorder them into.
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

        bool grabbed = mgr->isGrabbed(index);

        // A: grab / drop — hidden rows aren't reorderable, so they get no hint
        if (!hidden) {
            this->registerAction(
                grabbed ? "main/setting/libraries/drop"_i18n : "main/setting/libraries/grab"_i18n, brls::BUTTON_A,
                [mgr, index](brls::View*) {
                    mgr->toggleGrab(index);
                    return true;
                });
        }

        // Y: show / hide (ignored while a row is grabbed)
        this->registerAction("main/setting/libraries/toggle"_i18n, brls::BUTTON_Y, [mgr, index](brls::View*) {
            if (mgr->anyGrabbed()) return true;  // swallow: no visibility change mid-move
            mgr->toggleVisible(index);
            return true;
        });

        // D-pad up/down: move while grabbed (consumes navigation), otherwise
        // let the focus navigate normally (return false)
        this->registerAction(
            "", brls::BUTTON_NAV_UP,
            [mgr, index](brls::View*) {
                if (!mgr->isGrabbed(index)) return false;
                mgr->moveGrabbed(-1);
                return true;
            },
            true, true);
        this->registerAction(
            "", brls::BUTTON_NAV_DOWN,
            [mgr, index](brls::View*) {
                if (!mgr->isGrabbed(index)) return false;
                mgr->moveGrabbed(1);
                return true;
            },
            true, true);

        // mouse / touch: a tap toggles visibility (Y is gamepad-only, so this
        // is the show/hide affordance for pointer input). Ignored mid-grab.
        this->addGestureRecognizer(new brls::TapGestureRecognizer(this, [mgr, index]() {
            if (mgr->anyGrabbed()) return;
            mgr->toggleVisible(index);
        }));

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
        bool grabbed = this->mgr->isGrabbed(this->index);
        bool lift = focused || grabbed;

        float tint = grabbed ? 0.30f : (focused ? 0.18f : 0.f);
        this->setBackgroundColor(tint > 0 ? mix(this->surface, this->accent, tint) : nvgRGBA(0, 0, 0, 0));
        this->setBorderColor(this->accent);
        this->setBorderThickness(grabbed ? 3.f : (focused ? 2.f : 0.f));
        this->setShadowType(lift ? brls::ShadowType::GENERIC : brls::ShadowType::NONE);
        this->setShadowVisibility(lift);
        // dim hidden rows at rest so the state reads at a glance
        this->setAlpha((this->hiddenState && !lift) ? 0.45f : 1.f);
        this->label->setTextColor(lift ? this->accent : this->text);
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
            this->fetchSections();
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Logger::warning("HubVisibilityManager continueWatching: {}", ex);
            this->fetchSections();
        });
}

void HubVisibilityManager::fetchSections() {
    // Enumerate every catalog row via listSections + getSectionHubs (uncapped)
    // rather than getHomeHubs, which only returns non-hidden catalogs (Home
    // needs the full list here, including hidden ones, so they stay
    // manageable).
    ASYNC_RETAIN
    AppConfig::instance().backend().listSections(
        [ASYNC_TOKEN](const media::Container<media::Section>& r) {
            ASYNC_RELEASE
            if (r.Items.empty()) {
                this->loaded = true;
                this->spinner->setSpinning(false);
                this->sortEntries();
                this->rebuild();
                return;
            }
            this->pendingSections = (int)r.Items.size();
            for (auto& section : r.Items) {
                ASYNC_RETAIN
                AppConfig::instance().backend().getSectionHubs(
                    section.key, 50,
                    [ASYNC_TOKEN](const media::Container<media::Hub>& hr) {
                        ASYNC_RELEASE
                        for (auto& hub : hr.Items) {
                            if (hub.items.empty() || hub.hubIdentifier.empty()) continue;
                            if (hub.hubIdentifier == "home.continue" || hub.hubIdentifier == "home.ondeck") continue;
                            bool exists = false;
                            for (auto& e : this->entries)
                                if (e.identifier == hub.hubIdentifier) exists = true;
                            if (!exists)
                                this->entries.push_back(
                                    {hub.hubIdentifier, hub.title, AppConfig::instance().isHubHidden(hub.hubIdentifier)});
                        }
                        this->finishIfDone();
                    },
                    [ASYNC_TOKEN](const std::string& ex) {
                        ASYNC_RELEASE
                        brls::Logger::warning("HubVisibilityManager sectionHubs: {}", ex);
                        this->finishIfDone();
                    });
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Logger::warning("HubVisibilityManager listSections: {}", ex);
            this->loaded = true;
            this->spinner->setSpinning(false);
            this->sortEntries();
            this->rebuild();
        });
}

void HubVisibilityManager::finishIfDone() {
    this->pendingSections--;
    if (this->pendingSections > 0) return;
    this->loaded = true;
    this->spinner->setSpinning(false);
    this->sortEntries();
    this->rebuild();
}

void HubVisibilityManager::sortEntries() {
    // non-hidden rows first (in the saved order; an id absent from it, e.g. a
    // newly discovered catalog, keeps its current — fetch — relative order),
    // then every hidden row after
    std::vector<std::string> order = AppConfig::instance().getHubOrder();
    std::stable_sort(this->entries.begin(), this->entries.end(), [&order](const Entry& a, const Entry& b) {
        if (a.hidden != b.hidden) return !a.hidden;
        auto ra = std::find(order.begin(), order.end(), a.identifier);
        auto rb = std::find(order.begin(), order.end(), b.identifier);
        return ra < rb;
    });
}

void HubVisibilityManager::persistOrder() {
    std::vector<std::string> order;
    for (auto& e : this->entries)
        if (!e.hidden) order.push_back(e.identifier);
    AppConfig::instance().setHubOrder(order);
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

void HubVisibilityManager::toggleGrab(int index) {
    if (index < 0 || index >= (int)this->entries.size()) return;
    if (this->entries[index].hidden) return;  // not reorderable
    this->grabbedIndex = (this->grabbedIndex == index) ? -1 : index;
    this->pendingFocus = index;
    this->rebuild();
}

void HubVisibilityManager::moveGrabbed(int delta) {
    if (this->grabbedIndex < 0) return;
    // clamp to the non-hidden prefix: a grabbed (always non-hidden) row can
    // never cross into the hidden block that always sorts after it
    int shownCount = 0;
    for (auto& e : this->entries)
        if (!e.hidden) shownCount++;

    int j = this->grabbedIndex + delta;
    if (j < 0 || j >= shownCount) return;

    std::swap(this->entries[this->grabbedIndex], this->entries[j]);
    this->grabbedIndex = j;
    this->pendingFocus = j;

    this->persistOrder();
    this->rebuild();
}

void HubVisibilityManager::toggleVisible(int index) {
    if (index < 0 || index >= (int)this->entries.size()) return;
    std::string id = this->entries[index].identifier;
    bool hidden = !this->entries[index].hidden;
    this->entries[index].hidden = hidden;
    AppConfig::instance().setHubHidden(id, hidden);

    this->sortEntries();
    // refocus the same entry wherever it landed after the resort
    this->pendingFocus = 0;
    for (int i = 0; i < (int)this->entries.size(); i++)
        if (this->entries[i].identifier == id) {
            this->pendingFocus = i;
            break;
        }

    this->persistOrder();
    this->rebuild();
}

brls::View* HubVisibilityManager::getDefaultFocus() {
    return this->focusTarget ? this->focusTarget : brls::Box::getDefaultFocus();
}
