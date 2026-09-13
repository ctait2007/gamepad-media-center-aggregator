#include "view/action_sheet.hpp"

using namespace brls::literals;

namespace {

/// 40dp tall, RoundedCornerShape(50%), 16dp of horizontal content padding,
/// labelLarge text — tv-material3's Button, doubled.
const std::string sheetButtonXML = R"xml(
    <brls:Box
        width="auto"
        height="80"
        axis="row"
        focusable="true"
        hideHighlight="true"
        cornerRadius="40"
        alignItems="center"
        justifyContent="center"
        paddingLeft="32"
        paddingRight="32">
        <brls:Label
            id="sheet/button/label"
            fontSize="28"
            fontWeight="medium"
            singleLine="true"
            horizontalAlign="center" />
    </brls:Box>
)xml";

const std::string actionSheetXML = R"xml(
    <brls:Box
        width="auto"
        height="auto"
        alignItems="center"
        justifyContent="center"
        backgroundColor="@theme/brls/backdrop">

        <!-- tapping outside closes, as the reference's Dialog does -->
        <brls:Box
            id="sheet/scrim"
            positionType="absolute"
            positionTop="0"
            positionLeft="0"
            width="100%"
            height="100%"
            hideClickAnimation="true" />

        <brls:Box
            id="sheet/panel"
            width="1040"
            height="auto"
            axis="column"
            cornerRadius="32"
            borderThickness="2"
            paddingTop="48"
            paddingBottom="48"
            paddingLeft="48"
            paddingRight="48" />
    </brls:Box>
)xml";

}  // namespace

SheetButton::SheetButton(const std::string& text, std::function<void()> handler)
    : onClick(std::make_shared<std::function<void()>>(std::move(handler))) {
    this->inflateFromXMLString(sheetButtonXML);
    this->label = dynamic_cast<brls::Label*>(this->getView("sheet/button/label"));
    this->label->setText(text);
    this->applyColors(false);

    auto held = this->onClick;
    this->registerClickAction([held](brls::View*) {
        // close first: an action that navigates would otherwise do it from
        // inside a view the pop is about to destroy. The handler is held by
        // shared_ptr so a later setOnClick is what actually runs.
        brls::Application::popActivity(brls::TransitionAnimation::NONE, [held]() {
            if (*held) (*held)();
        });
        return true;
    });
    this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
}

void SheetButton::setLabel(const std::string& text) { this->label->setText(text); }

void SheetButton::applyColors(bool focused) {
    auto theme = brls::Application::getTheme();
    this->setBackgroundColor(theme.getColor(focused ? "color/sheet/focus_bg" : "color/surface"));
    this->label->setTextColor(theme.getColor(focused ? "color/sheet/focus_fg" : "brls/text"));
}

void SheetButton::onFocusGained() {
    brls::Box::onFocusGained();
    this->applyColors(true);
}

void SheetButton::onFocusLost() {
    brls::Box::onFocusLost();
    this->applyColors(false);
}

ActionSheet::ActionSheet(const std::string& title, const std::string& subtitle) {
    this->inflateFromXMLString(actionSheetXML);

    auto theme = brls::Application::getTheme();
    this->panel->setBackgroundColor(theme.getColor("color/grey_1"));
    this->panel->setBorderColor(theme.getColor("color/grey_2"));

    // The panel's children are built here rather than in the XML because the
    // subtitle is optional and the actions are not known until the caller has
    // added them; spacing is carried by each child's own margin (16dp).
    auto* head = new brls::Label();
    head->setText(title);
    head->setFontSize(40);
    head->setFontWeight("medium");
    head->setSingleLine(true);
    this->panel->addView(head);

    if (!subtitle.empty()) {
        auto* sub = new brls::Label();
        sub->setText(subtitle);
        sub->setFontSize(28);
        sub->setTextColor(theme.getColor("font/grey"));
        sub->setMarginTop(32);
        this->panel->addView(sub);
    }

    this->boxActions = new brls::Box();
    this->boxActions->setAxis(brls::Axis::COLUMN);
    this->boxActions->setMarginTop(32);
    this->panel->addView(this->boxActions);

    this->registerAction("hints/cancel"_i18n, brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    });
    this->scrim->registerClickAction([](brls::View*) {
        brls::Application::popActivity();
        return true;
    });
    this->scrim->addGestureRecognizer(new brls::TapGestureRecognizer(this->scrim));
}

SheetButton* ActionSheet::addAction(const std::string& text, std::function<void()> onClick) {
    auto* button = new SheetButton(text, std::move(onClick));
    if (this->firstAction) button->setMarginTop(32);
    this->boxActions->addView(button);
    if (!this->firstAction) this->firstAction = button;
    return button;
}

void ActionSheet::present() { brls::Application::pushActivity(new brls::Activity(this)); }

brls::View* ActionSheet::getDefaultFocus() {
    return this->firstAction ? this->firstAction : brls::Box::getDefaultFocus();
}
