#include "view/icon_button.hpp"
#include "view/svg_image.hpp"

const std::string iconButtonXML = R"xml(
    <brls:Box
        width="auto"
        height="44"
        axis="row"
        focusable="true"
        cornerRadius="22"
        highlightCornerRadius="22"
        alignItems="center"
        justifyContent="center"
        paddingLeft="22"
        paddingRight="24">

        <SVGImage
            id="icon_button/icon"
            width="18"
            height="18"
            marginRight="10" />

        <!-- singleLine: the button's async GONE->VISIBLE toggle runs a
             measure at a degenerate width, and the wrap branch of
             labelMeasureFunc cut "Lire" into "Lir / e"; in singleLine
             that branch is unreachable (label.cpp:150) -->
        <brls:Label
            id="icon_button/label"
            singleLine="true"
            fontSize="16"
            fontWeight="medium"
            horizontalAlign="center" />

    </brls:Box>
)xml";

IconButton::IconButton() {
    this->inflateFromXMLString(iconButtonXML);

    this->registerStringXMLAttribute("icon", [this](std::string value) { this->setIcon(value); });
    this->registerStringXMLAttribute("text", [this](std::string value) { this->setText(value); });
    this->registerStringXMLAttribute("buttonStyle", [this](std::string value) { this->setButtonStyle(value); });

    // The hero's Play button is far larger than the inline ones (NuvioTV sets
    // it 48dp tall with a 14sp label and an 18dp glyph, which at its 2.0
    // density is 96/28/36 here), so both are settable per instance.
    this->registerFloatXMLAttribute("fontSize", [this](float value) { this->label->setFontSize(value); });
    // Circular hero actions: the label stays gone even if a later state update
    // (download progress, say) tries to set text on it.
    this->registerBoolXMLAttribute("iconOnly", [this](bool value) {
        this->iconOnly = value;
        if (value) this->setText("");
    });
    this->registerFloatXMLAttribute("iconSize", [this](float value) {
        this->icon->setWidth(value);
        this->icon->setHeight(value);
    });

    // mouse/touch click: replays the A action registered by the caller
    this->addGestureRecognizer(new brls::TapGestureRecognizer(this));

    // the borealis highlight background would cover the primary style's
    // gold background on focus (unreadable dark text): animated border only
    this->setHideHighlightBackground(true);

    this->applyStyle();
}

void IconButton::setIcon(const std::string& res) {
    // XML attributes pass an @res path already resolved to a relative path
    std::string path = res;
    const std::string prefix = "@res/";
    if (path.rfind(prefix, 0) == 0) path = path.substr(prefix.size());
    this->icon->setImageFromSVGRes(path);
}

void IconButton::setText(const std::string& text) {
    this->label->setText(this->iconOnly ? "" : text);
    // Icon-only (the circular hero actions): drop the gap that would otherwise
    // push the glyph off-centre, and the label's own box.
    bool empty = this->iconOnly || text.empty();
    this->icon->setMarginRight(empty ? 0 : 10);
    this->label->setVisibility(empty ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
}

void IconButton::setButtonStyle(const std::string& style) {
    this->styleName = style;
    this->applyStyle();
}

void IconButton::setMuted(bool muted) {
    if (this->muted == muted) return;
    this->muted = muted;
    this->applyStyle();
}

void IconButton::applyStyle() {
    auto theme = brls::Application::getTheme();
    if (this->muted) {
        // disabled look (still focusable): no fill, dim outline, grey text
        this->setBackgroundColor(nvgRGBA(0, 0, 0, 0));
        this->setBorderColor(theme.getColor("color/grey_1"));
        this->setBorderThickness(2);
        this->label->setTextColor(theme.getColor("font/grey"));
    } else if (this->styleName == "icon") {
        // NuvioTV's round hero action: a filled card-coloured disc, no outline.
        this->setBackgroundColor(theme.getColor("color/surface"));
        this->setBorderThickness(0);
        this->label->setTextColor(theme.getColor("brls/text"));
    } else if (this->styleName == "primary") {
        this->setBackgroundColor(theme.getColor("color/app"));
        this->setBorderThickness(0);
        this->label->setTextColor(theme.getColor("brls/button/primary_enabled_text"));
    } else {
        this->setBackgroundColor(nvgRGBA(0, 0, 0, 0));
        this->setBorderColor(theme.getColor("color/grey_3"));
        this->setBorderThickness(2);
        this->label->setTextColor(theme.getColor("brls/text"));
    }
}

brls::View* IconButton::create() { return new IconButton(); }
