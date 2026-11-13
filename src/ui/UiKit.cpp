#include "ui/UiKit.h"
#include "core/AssetManager.h"
#include "core/GameInput.h"
#include "core/Renderer2D.h"

#include <algorithm>
#include <cmath>

namespace
{
int GetAtlas()
{
    return AssetManager::Get(AssetManager::Tex::HudAtlas);
}

struct Uv
{
    float u0, v0, u1, v1;
};

Uv RectUv(const UiAtlas::Rect& r, float insetPx = 0.5f)
{
    return Uv{(r.x + insetPx) / UiAtlas::SIZE, (r.y + insetPx) / UiAtlas::SIZE, (r.x + r.w - insetPx) / UiAtlas::SIZE,
              (r.y + r.h - insetPx) / UiAtlas::SIZE};
}

const UiAtlas::Rect& RectOf(UiAtlas::Id id)
{
    return UiAtlas::RECTS[static_cast<int>(id)];
}

void Draw(int tex, float x, float y, float w, float h, const Uv& uv, UiKit::Color c)
{
    Renderer2D_DrawTexture(tex, UiKit::NX(x), UiKit::NY(y), UiKit::NX(x + w), UiKit::NY(y + h), c.r, c.g, c.b, c.a, uv.u0, uv.v0,
                           uv.u1, uv.v1);
}

struct BlendScope
{
    Renderer2DBlendMode previous;
    explicit BlendScope(bool additive) : previous(Renderer2D_GetBlendMode())
    {
        Renderer2D_SetBlendMode(additive ? Renderer2DBlendMode::Additive : Renderer2DBlendMode::Alpha);
    }
    ~BlendScope()
    {
        Renderer2D_SetBlendMode(previous);
    }
};

// Decode one UTF-8 code point, returns the number of bytes consumed.
int NextCodePoint(const std::string& s, size_t i, unsigned& cp)
{
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80)
    {
        cp = c;
        return 1;
    }
    if ((c >> 5) == 0x6 && i + 1 < s.size())
    {
        cp = ((c & 0x1F) << 6) | (s[i + 1] & 0x3F);
        return 2;
    }
    if ((c >> 4) == 0xE && i + 2 < s.size())
    {
        cp = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
        return 3;
    }
    if (i + 3 < s.size())
    {
        cp = ((c & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F);
        return 4;
    }
    cp = c;
    return 1;
}
}

namespace UiKit
{
void Image(UiAtlas::Id id, float x, float y, float w, float h, Color c, bool additive)
{
    const int tex = GetAtlas();
    if (tex < 0 || c.a <= 0.0f) return;
    BlendScope scope(additive);
    Draw(tex, x, y, w, h, RectUv(RectOf(id)), c);
}

void ImageRotated(UiAtlas::Id id, float cx, float cy, float w, float h, float rotation, Color c, bool additive)
{
    const int tex = GetAtlas();
    if (tex < 0 || c.a <= 0.0f) return;
    BlendScope scope(additive);
    const Uv uv = RectUv(RectOf(id));
    // DrawTextureEx works in normalized units and rotates in pixel space.
    Renderer2D_DrawTextureEx(tex, NX(cx), NY(cy), w / (VIRTUAL_W * 0.5f), h / (VIRTUAL_H * 0.5f), rotation, c.r, c.g, c.b, c.a,
                             uv.u0, uv.v0, uv.u1, uv.v1);
}

void Panel9(UiAtlas::Id id, float x, float y, float w, float h, float cornerPx, Color c)
{
    const int tex = GetAtlas();
    if (tex < 0 || c.a <= 0.0f) return;
    const UiAtlas::Rect& r = RectOf(id);
    const float ca = r.corner; // corner size inside the atlas
    float cs = cornerPx;       // corner size on screen
    cs = std::min(cs, std::min(w, h) * 0.5f);
    const float xs[4] = {x, x + cs, x + w - cs, x + w};
    const float ys[4] = {y, y + cs, y + h - cs, y + h};
    const float inset = 0.5f;
    const float us[4] = {r.x + inset, r.x + ca, r.x + r.w - ca, r.x + r.w - inset};
    const float vs[4] = {r.y + inset, r.y + ca, r.y + r.h - ca, r.y + r.h - inset};
    BlendScope scope(false);
    for (int j = 0; j < 3; ++j)
    {
        for (int i = 0; i < 3; ++i)
        {
            const Uv uv{us[i] / UiAtlas::SIZE, vs[j] / UiAtlas::SIZE, us[i + 1] / UiAtlas::SIZE, vs[j + 1] / UiAtlas::SIZE};
            Draw(tex, xs[i], ys[j], xs[i + 1] - xs[i], ys[j + 1] - ys[j], uv, c);
        }
    }
}

void Fill(float x, float y, float w, float h, Color c)
{
    if (c.a <= 0.0f || w <= 0.0f || h <= 0.0f) return;
    Renderer2D_DrawQuad(NX(x), NY(y), NX(x + w), NY(y + h), c.r, c.g, c.b, c.a);
}

void Text(const std::string& text, float x, float y, float fontPx, Color c, TextAlignment align, float boxWidth, bool shadow)
{
    if (text.empty() || c.a <= 0.0f) return;
    if (boxWidth <= 0.0f) boxWidth = 640.0f;
    TextStyle style{};
    style.size = fontPx / 2160.0f; // TextRenderer: font px = size * 3 * backbuffer height
    style.red = c.r;
    style.green = c.g;
    style.blue = c.b;
    style.alpha = c.a;
    style.shadow = shadow;
    style.alignment = align;
    style.layoutWidth = boxWidth / (VIRTUAL_W * 0.5f);

    float left = x;
    if (align == TextAlignment::Center)
        left = x - boxWidth * 0.5f;
    else if (align == TextAlignment::Right)
        left = x - boxWidth;

    // TextRenderer places the layout box 0.18 em above the given y.
    const float top = y - fontPx * 0.62f;
    const float ny = 1.0f - 2.0f * (top + 0.18f * fontPx + g_OriginY) / VIRTUAL_H;
    TextRenderer::QueueText(text, NX(left), ny, style);
}

float TextWidth(const std::string& text, float fontPx)
{
    if (text.empty() || fontPx <= 0.0f) return 0.0f;
    const float measured = TextRenderer::MeasureWidth(text, fontPx / 2160.0f, true);
    if (measured >= 0.0f) return measured * VIRTUAL_W * 0.5f;
    // the estimate (Chakra Petch Bold: about 0.62 em a capital, 0.56 a digit, 0.26 a space; CJK about 0.94 em)
    float width = 0.0f;
    for (size_t i = 0; i < text.size();)
    {
        unsigned cp = 0;
        i += static_cast<size_t>(NextCodePoint(text, i, cp));
        if (cp >= 0x2E80) width += 0.96f;
        else if (cp == ' ') width += 0.26f;
        else if (cp >= '0' && cp <= '9') width += 0.58f;
        else if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')) width += 0.64f;
        else width += 0.40f;
    }
    return width * fontPx;
}

float FitFontPx(const std::string& text, float desiredPx, float maxWidth, float minPx)
{
    const float target = maxWidth * 0.94f; // 6 % safety margin: DirectWrite wraps a line that is even slightly too wide
    const float width = TextWidth(text, desiredPx);
    if (width <= target || width <= 0.0f) return desiredPx;
    return std::max(minPx, desiredPx * target / width);
}

float KeyCap(const std::string& keyLabel, float x, float y, float height, Color tint)
{
    const std::string label = GameInput::PromptLabel(keyLabel); // the gamepad button when the gamepad is in use
    const float font = height * 0.46f;
    const bool wide = label.size() > 1;
    const float width = wide ? std::max(height * 1.6f, TextWidth(label, font) + height * 0.7f) : height;
    const UiAtlas::Id id = wide ? UiAtlas::Id::KeyCapWide : UiAtlas::Id::KeyCap;
    Image(id, x, y, width, height, tint);
    Text(label, x + width * 0.5f, y + height * 0.5f, font, Color{1.0f, 1.0f, 1.0f, tint.a}, TextAlignment::Center, width + 20.0f,
         false);
    return width;
}
}

