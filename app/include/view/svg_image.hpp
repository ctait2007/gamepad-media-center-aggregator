//
// Created by fang on 2022/6/5.
//

#pragma once

#include <borealis.hpp>
#include <lunasvg.h>

class SVGImage : public brls::Image {
public:
    SVGImage();

    ~SVGImage() override;

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override;

    void setImageFromSVGRes(const std::string& value);

    void setImageFromSVGFile(const std::string& value);

    void setImageFromSVGString(const std::string& value);

    void rotate(float value);

    /// Override the colour the idle glyph grey (#61666D, the colour baked into
    /// this icon set) is recoloured to, instead of the theme's color/icon_idle.
    /// Lets a view repaint one glyph for a state the theme has no token for --
    /// NuvioTV's filter chips, for one, draw their icon in OnSecondary once the
    /// chip is focused and its fill turns accent. Reloads the SVG.
    void setGlyphColor(NVGcolor color);
    /// Back to the theme token.
    void clearGlyphColor();

    void updateBitmap();

    static View* create();

private:
    std::unique_ptr<lunasvg::Document> document = nullptr;
    brls::VoidEvent::Subscription subscription;
    std::string filePath;
    /// "#RRGGBB" when set, else empty (= follow color/icon_idle).
    std::string glyphHex;
    float angle = 0;
};