#pragma once

#include <string>

enum class TextAlignment
{
    Left,
    Center,
    Right
};

struct TextStyle
{
    float size = 0.012f;

    float red = 1.00f;
    float green = 1.00f;
    float blue = 1.00f;
    float alpha = 1.00f;

    bool bold = false;
    bool shadow = true;
    // The font layer (see TextRenderer.cpp): -1 = decided by the old rule (bold text from DISPLAY_MIN_SIZE is display text); 0 = body (Chakra Petch Medium /
    // Bold, Noto Sans SC / M PLUS 1); 1 = display (Chakra Petch Bold, Smiley Sans / Dela Gothic One). The rows of every menu say 1 in both states: a row that
    // is selected must not change its typeface, only its colour.
    int layer = -1;

    TextAlignment alignment = TextAlignment::Left;

    float layoutWidth = 1.0f;
};

namespace TextRenderer
{
bool Initialize();

void Finalize();

void BeginFrame();

void QueueText(const std::string& text, float x, float y, const TextStyle& style);

void QueueText(const std::string& text, float x, float y, float size, float red, float green, float blue, float alpha);

void Render();

void ReleaseRenderTarget();

// Lets Loc switch to a CJK UI font; nullptr = the display font of the game.
void SetFontFamilyOverride(const wchar_t* family);
void SetGlobalAlpha(float alpha);

// The width of a line of text as it will be drawn (DirectWrite, the fonts and the CJK runs of the current language), in the units of
// the layout width (normalized: 2 = the whole width of the picture). `bold`: heavy text. Returns a negative value when the text system is
// not ready. Cached per text and size.
float MeasureWidth(const std::string& text, float size, bool bold, int layer = -1);
}



