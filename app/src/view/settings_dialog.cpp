#include "view/settings_dialog.hpp"

#include <cmath>

#include "view/svg_image.hpp"

#include <borealis.hpp>
#include <borealis/core/i18n.hpp>

using namespace brls::literals;

namespace settings_dialog {

namespace {

// The reference's dp, doubled. The panel is a fixed 420 wide and grows with
// its list up to 320 tall, at which point the list scrolls inside it.
constexpr float kPanelWidth = 840, kPanelMaxHeight = 640;
constexpr float kPanelRadius = 32;   // radii.xl
constexpr float kPanelPad = 48;      // spacing.xl
constexpr float kPanelGap = 32;      // spacing.lg
constexpr float kOptionRadius = 20;  // its own 10 dp
constexpr float kOptionPad = 32;     // spacing.lg
constexpr float kOptionGap = 16;     // spacing.sm
constexpr float kCheckSize = 40;     // its own 20 dp

// Drawn over a dimmed screen in BOTH themes, so these are the reference's own
// dark-surface values rather than theme tokens that invert in light mode —
// the same reasoning the source list uses for its cards.
const NVGcolor kElevated = nvgRGB(0x1A, 0x1A, 0x1A);  // BackgroundElevated
const NVGcolor kCard = nvgRGB(0x24, 0x24, 0x24);      // BackgroundCard
const NVGcolor kFocus = nvgRGB(0x33, 0x33, 0x33);     // FocusBackground
const NVGcolor kBorder = nvgRGB(0x33, 0x33, 0x33);
const NVGcolor kTextPrimary = nvgRGB(0xFF, 0xFF, 0xFF);  // TextPrimary is white
const NVGcolor kTextSecondary = nvgRGB(0xB3, 0xB3, 0xB3);

/// How wide a run of text is, in the face a Label would draw it in. Needed
/// because a Label sized "auto" inside a ROW whose other child grows is
/// measured against nothing left over and lays out at zero width, which is
/// how the option's trailing note came to be invisible.
float textWidth(const std::string& text, float size, float tracking) {
    NVGcontext* vg = brls::Application::getNVGContext();
    nvgFontSize(vg, size);
    nvgFontFaceId(vg, brls::Application::getDefaultFont());
    nvgTextLetterSpacing(vg, tracking);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    float bounds[4];
    nvgTextBounds(vg, 0, 0, text.c_str(), nullptr, bounds);
    nvgTextLetterSpacing(vg, 0);
    return std::ceil(bounds[2] - bounds[0]) + 2;
}

brls::Label* label(float size, NVGcolor color, float lineHeight = 0, float tracking = 0,
    const char* weight = nullptr) {
    auto* l = new brls::Label();
    l->setFontSize(size);
    l->setTextColor(color);
    if (lineHeight > 0) l->setLineHeight(lineHeight);
    l->setLetterSpacing(tracking);
    if (weight) l->setFontWeight(weight);
    // A borealis Label marquees whenever an ANCESTOR holds focus and the text
    // overruns; inside a focused card that slides the option's own name back
    // and forth under the reader.
    l->setAutoAnimate(false);
    return l;
}

/// One option. Its own view so the selected/focused fills can be kept apart:
/// the chosen one stays lifted while the focus moves over the others, which is
/// how the reference shows "this is the one in force" and "this is where you
/// are" at the same time.
class OptionCard : public brls::Box {
public:
    OptionCard(const Option& opt, bool selected, float width) : chosen(selected) {
        this->setAxis(brls::Axis::ROW);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setWidth(width);
        this->setPadding(kOptionPad, kOptionPad, kOptionPad, kOptionPad);
        this->setCornerRadius(kOptionRadius);
        this->setHighlightCornerRadius(kOptionRadius + 4);
        this->setFocusable(true);
        // Keep the card's own fill under the focus halo, otherwise borealis
        // paints its highlight background over it and the lift disappears.
        this->setHideHighlightBackground(true);

        // Yoga will not shrink the text column to make room for what follows
        // it -- a Label measured against no remaining space reports zero and
        // the note ends up over the card's padding -- so the column is given
        // exactly the room the trailing note and the check leave it.
        const float trailingWidth = opt.trailing.empty() ? 0 : textWidth(opt.trailing, 24, 0.8f) + kOptionGap * 1.5f;
        const float checkWidth = selected ? kCheckSize + kOptionGap * 1.5f : 0;

        auto* column = new brls::Box();
        column->setAxis(brls::Axis::COLUMN);
        column->setWidth(width - kOptionPad * 2 - trailingWidth - checkWidth);

        // bodyLarge: 16sp Regular, tracked 0.5sp.
        this->titleLabel = label(32, selected ? brls::Application::getTheme().getColor("color/app") : kTextPrimary,
            24.0f / 16.0f, 1.0f);
        this->titleLabel->setText(opt.title);
        this->titleLabel->setSingleLine(true);
        column->addView(this->titleLabel);

        if (!opt.description.empty()) {
            auto* desc = label(24, kTextSecondary, 16.0f / 12.0f, 0.8f);  // bodySmall
            desc->setText(opt.description);
            desc->setMarginTop(8);  // spacing.xs
            column->addView(desc);
        }
        this->addView(column);

        if (!opt.trailing.empty()) {
            auto* note = label(24, kTextSecondary, 16.0f / 12.0f, 0.8f);  // bodySmall
            note->setText(opt.trailing);
            note->setMarginLeft(kOptionGap * 1.5f);  // spacing.md
            note->setSingleLine(true);
            // The title column grows; without this yoga squeezes the note to
            // nothing rather than taking the space off the title.
            note->setShrink(0);
            note->setWidth(textWidth(opt.trailing, 24, 0.8f));
            note->setHorizontalAlign(brls::HorizontalAlign::RIGHT);
            this->addView(note);
        }

        if (selected) {
            auto* check = new SVGImage();
            check->setWidth(kCheckSize);
            check->setHeight(kCheckSize);
            check->setMarginLeft(kOptionGap * 1.5f);  // spacing.md
            check->setImageFromSVGRes("icon/ico-check-light.svg");
            check->setGlyphColor(brls::Application::getTheme().getColor("color/app"));
            this->addView(check);
        }

        this->restyle();
    }

    void onFocusGained() override {
        brls::Box::onFocusGained();
        this->focused = true;
        this->restyle();
    }

    void onFocusLost() override {
        brls::Box::onFocusLost();
        this->focused = false;
        this->restyle();
    }

private:
    void restyle() { this->setBackgroundColor(this->focused || this->chosen ? kFocus : kCard); }

    brls::Label* titleLabel = nullptr;
    bool chosen = false;
    bool focused = false;
};

/// The panel and its scrim. Pushed as its own translucent activity so what is
/// behind it keeps drawing, as the reference's Dialog does.
class ChoiceDialog : public brls::Box {
public:
    ChoiceDialog(const std::string& title, const std::string& subtitle, const std::vector<Option>& options,
        int selected, std::function<void(int)> onPick, std::function<void()> onDismiss)
        : dismissCb(std::move(onDismiss)) {
        this->setWidth(brls::Application::contentWidth);
        this->setHeight(brls::Application::contentHeight);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setJustifyContent(brls::JustifyContent::CENTER);
        this->setBackgroundColor(nvgRGBA(0, 0, 0, 178));  // the dim behind a dialog

        auto* panel = new brls::Box();
        panel->setAxis(brls::Axis::COLUMN);
        panel->setWidth(kPanelWidth);
        panel->setCornerRadius(kPanelRadius);
        panel->setBackgroundColor(kElevated);
        panel->setBorderThickness(2);  // spacing.hairline
        panel->setBorderColor(kBorder);
        panel->setPadding(kPanelPad, kPanelPad, kPanelPad, kPanelPad);
        this->addView(panel);

        auto* heading = label(40, kTextPrimary, 28.0f / 20.0f, 0, "medium");  // titleLarge 20sp Medium
        heading->setText(title);
        heading->setSingleLine(true);
        panel->addView(heading);

        if (!subtitle.empty()) {
            auto* sub = label(28, kTextSecondary, 20.0f / 14.0f, 0.5f);  // bodyMedium 14sp, 0.25sp
            sub->setText(subtitle);
            sub->setMarginTop(kPanelGap);
            panel->addView(sub);
        }

        // The list, scrolling once it outgrows the panel's own height.
        auto* scroll = new brls::ScrollingFrame();
        scroll->setMarginTop(kPanelGap);
        scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
        auto* list = new brls::Box();
        list->setAxis(brls::Axis::COLUMN);
        scroll->setContentView(list);

        float inner = kPanelWidth - kPanelPad * 2;
        float content = 0;
        for (size_t i = 0; i < options.size(); i++) {
            auto* card = new OptionCard(options[i], (int)i == selected, inner);
            if (i > 0) card->setMarginTop(kOptionGap);
            int index = (int)i;
            card->registerClickAction([this, onPick, index](brls::View*) {
                // The answer is delivered AFTER the dialog is off screen: a
                // handler that rebuilds the page it was opened from must not
                // run while this activity is still on the stack.
                this->picked = true;
                brls::Application::popActivity(
                    brls::TransitionAnimation::FADE, [onPick, index]() { onPick(index); });
                return true;
            });
            card->addGestureRecognizer(new brls::TapGestureRecognizer(card));
            list->addView(card);
            if (i == (size_t)selected) this->focusTarget = card;
            if (!this->firstCard) this->firstCard = card;
            // A card is its two paddings plus a 32 px title and, when it has
            // one, an 8 px gap and a 24 px description.
            content += kOptionPad * 2 + 40 + (options[i].description.empty() ? 0 : 8 + 32);
            if (i > 0) content += kOptionGap;
        }
        scroll->setHeight(std::min(content, kPanelMaxHeight));
        panel->addView(scroll);

        this->registerAction(
            "hints/back"_i18n, brls::BUTTON_B,
            [](brls::View*) { return brls::Application::popActivity(brls::TransitionAnimation::FADE); }, false,
            false, brls::SOUND_BACK);
    }

    brls::View* getDefaultFocus() override {
        return this->focusTarget ? this->focusTarget : this->firstCard;
    }

    bool isTranslucent() override { return true; }

    ~ChoiceDialog() override {
        // Dismissed without choosing: the caller may have a restart prompt or
        // some other "you are done here" step hanging off that.
        if (!this->picked && this->dismissCb) this->dismissCb();
    }

private:
    brls::View* focusTarget = nullptr;
    brls::View* firstCard = nullptr;
    std::function<void()> dismissCb;
    bool picked = false;
};

}  // namespace

void choose(const std::string& title, const std::string& subtitle, const std::vector<Option>& options,
    int selected, std::function<void(int)> onPick, std::function<void()> onDismiss) {
    if (options.empty()) return;
    auto* dialog = new ChoiceDialog(title, subtitle, options, selected, std::move(onPick), std::move(onDismiss));
    brls::Application::pushActivity(new brls::Activity(dialog), brls::TransitionAnimation::FADE);
}

void choose(const std::string& title, const std::vector<std::string>& labels, int selected,
    std::function<void(int)> onPick, std::function<void()> onDismiss) {
    std::vector<Option> options;
    options.reserve(labels.size());
    for (const std::string& l : labels) options.push_back(Option{l, "", ""});
    choose(title, "", options, selected, std::move(onPick), std::move(onDismiss));
}

}  // namespace settings_dialog
