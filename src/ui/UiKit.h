#pragma once

#include "ui/UiAtlas.h"
#include "core/TextRenderer.h"

#include <string>

// Small helper layer for HUD / menu drawing.
// Everything is laid out in a virtual 1280x720 pixel space with the origin at the
// top-left, then converted to the game's normalized (-1..1) coordinates.
namespace UiKit
{
struct Color
{
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

constexpr float VIRTUAL_W = 1280.0f;
constexpr float VIRTUAL_H = 720.0f;

// Everything that is drawn through UiKit is moved by this offset (virtual pixels) while it is set: a group of HUD elements can slide in
// as a whole. Set it back to 0, 0 afterwards.
inline float g_OriginX = 0.0f;
inline float g_OriginY = 0.0f;
inline void SetOrigin(float x, float y)
{
    g_OriginX = x;
    g_OriginY = y;
}

inline float NX(float px)
{
    return (px + g_OriginX) / (VIRTUAL_W * 0.5f) - 1.0f;
}

inline float NY(float py)
{
    return 1.0f - (py + g_OriginY) / (VIRTUAL_H * 0.5f);
}

inline Color Rgba(float r, float g, float b, float a = 1.0f)
{
    return Color{r, g, b, a};
}

// Atlas sprites. (x, y) is the top-left corner in virtual pixels.
void Image(UiAtlas::Id id, float x, float y, float w, float h, Color c, bool additive = false);
void ImageRotated(UiAtlas::Id id, float cx, float cy, float w, float h, float rotation, Color c, bool additive = false);
void Panel9(UiAtlas::Id id, float x, float y, float w, float h, float cornerPx, Color c);
void Fill(float x, float y, float w, float h, Color c);

// Text. y is the vertical centre of the line, x depends on the alignment.
void Text(const std::string& text, float x, float y, float fontPx, Color c, TextAlignment align = TextAlignment::Left,
          float boxWidth = 0.0f, bool shadow = true);

// Width of a UTF-8 string in virtual pixels, as heavy text: measured by DirectWrite with the real fonts (TextRenderer::MeasureWidth);
// an estimate when the text system is not running (tools).
float TextWidth(const std::string& text, float fontPx);

// Largest font size <= desiredPx at which the text still fits into maxWidth (never below minPx).
float FitFontPx(const std::string& text, float desiredPx, float maxWidth, float minPx = 9.0f);

// Keycap with a label inside. Returns the width that was used.
float KeyCap(const std::string& label, float x, float y, float height, Color tint);
}

