// preview_render: a small software renderer behind the Renderer2D / AssetManager interface, so that the REAL drawing code of the game (GameEnemy, EnemyArt, GameVfx, GameEffect) can
// be run on a machine without a window and its pictures looked at. It is not the game's renderer (no GPU, no shaders): the textures come from tex_<id>.rgba files that
// tools/make_preview_assets.py writes (the real sprites; the effect textures glow / ring / spark / flame / shard are approximations of the real ones).
//   Renderer2D_*   rasterise into a float RGB buffer with alpha or additive blending (the same blend equations as the game)
//   preview_begin / preview_end / preview_save    frame control, used by preview_enemies.cpp
#include "core/AssetManager.h"
#include "core/Renderer2D.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
struct Texture
{
    int w = 0, h = 0;
    std::vector<unsigned char> rgba;
};
std::vector<Texture> g_Textures;
Renderer2DBlendMode g_Blend = Renderer2DBlendMode::Alpha;
int g_W = 0, g_H = 0;
std::vector<float> g_Buffer; // RGB
bool g_Capture = false;
float g_ShiftX = 0.0f; // the picture is moved sideways so that an enemy at the edge of the screen can be looked at in the middle

void BlendPixel(int x, int y, float r, float g, float b, float a)
{
    if (x < 0 || y < 0 || x >= g_W || y >= g_H || a <= 0.0f) return;
    float* p = &g_Buffer[(static_cast<size_t>(y) * g_W + x) * 3];
    if (g_Blend == Renderer2DBlendMode::Additive)
    {
        p[0] += r * a;
        p[1] += g * a;
        p[2] += b * a;
    }
    else
    {
        p[0] = r * a + p[0] * (1.0f - a);
        p[1] = g * a + p[1] * (1.0f - a);
        p[2] = b * a + p[2] * (1.0f - a);
    }
}

struct V
{
    float x, y;         // pixels
    float r, g, b, a;   // colour
    float u, v;
};

void Sample(const Texture* t, float u, float v, float out[4])
{
    if (t == nullptr || t->w == 0)
    {
        out[0] = out[1] = out[2] = out[3] = 1.0f;
        return;
    }
    u = std::min(1.0f, std::max(0.0f, u));
    v = std::min(1.0f, std::max(0.0f, v));
    const float fx = u * (t->w - 1), fy = v * (t->h - 1);
    const int x0 = static_cast<int>(fx), y0 = static_cast<int>(fy);
    const int x1 = std::min(t->w - 1, x0 + 1), y1 = std::min(t->h - 1, y0 + 1);
    const float tx = fx - x0, ty = fy - y0;
    for (int c = 0; c < 4; ++c)
    {
        auto px = [&](int x, int y) { return t->rgba[(static_cast<size_t>(y) * t->w + x) * 4 + c] / 255.0f; };
        out[c] = (px(x0, y0) * (1 - tx) + px(x1, y0) * tx) * (1 - ty) + (px(x0, y1) * (1 - tx) + px(x1, y1) * tx) * ty;
    }
}

void Triangle(const V& a, const V& b, const V& c, const Texture* tex)
{
    const float minx = std::floor(std::min({a.x, b.x, c.x})), maxx = std::ceil(std::max({a.x, b.x, c.x}));
    const float miny = std::floor(std::min({a.y, b.y, c.y})), maxy = std::ceil(std::max({a.y, b.y, c.y}));
    const float den = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    if (std::fabs(den) < 1.0e-8f) return;
    for (int y = std::max(0, static_cast<int>(miny)); y <= std::min(g_H - 1, static_cast<int>(maxy)); ++y)
    {
        for (int x = std::max(0, static_cast<int>(minx)); x <= std::min(g_W - 1, static_cast<int>(maxx)); ++x)
        {
            const float px = x + 0.5f, py = y + 0.5f;
            const float w0 = ((b.y - c.y) * (px - c.x) + (c.x - b.x) * (py - c.y)) / den;
            const float w1 = ((c.y - a.y) * (px - c.x) + (a.x - c.x) * (py - c.y)) / den;
            const float w2 = 1.0f - w0 - w1;
            if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue;
            float s[4] = {1, 1, 1, 1};
            if (tex != nullptr) Sample(tex, w0 * a.u + w1 * b.u + w2 * c.u, w0 * a.v + w1 * b.v + w2 * c.v, s);
            const float r = (w0 * a.r + w1 * b.r + w2 * c.r) * s[0];
            const float g = (w0 * a.g + w1 * b.g + w2 * c.g) * s[1];
            const float bl = (w0 * a.b + w1 * b.b + w2 * c.b) * s[2];
            const float al = (w0 * a.a + w1 * b.a + w2 * c.a) * s[3];
            BlendPixel(x, y, r, g, bl, al);
        }
    }
}

V Make(float nx, float ny, float r, float g, float b, float a, float u, float v)
{
    return V{(nx + g_ShiftX + 1.0f) * 0.5f * g_W, (1.0f - ny) * 0.5f * g_H, r, g, b, a, u, v};
}

const Texture* TexById(int id)
{
    if (id < 0 || id >= static_cast<int>(g_Textures.size()) || g_Textures[static_cast<size_t>(id)].w == 0) return nullptr;
    return &g_Textures[static_cast<size_t>(id)];
}
}

// ---- the preview interface ------------------------------------------------------------------------------------------------------------------------------
bool preview_load(const char* directory)
{
    const std::string manifest = std::string(directory) + "/manifest.txt";
    FILE* f = std::fopen(manifest.c_str(), "r");
    if (!f) return false;
    int id, w, h;
    char name[256];
    while (std::fscanf(f, "%d %d %d %255s", &id, &w, &h, name) == 4)
    {
        if (id >= static_cast<int>(g_Textures.size())) g_Textures.resize(static_cast<size_t>(id) + 1);
        Texture& t = g_Textures[static_cast<size_t>(id)];
        t.w = w;
        t.h = h;
        t.rgba.resize(static_cast<size_t>(w) * h * 4);
        FILE* d = std::fopen((std::string(directory) + "/" + name).c_str(), "rb");
        if (!d || std::fread(t.rgba.data(), 1, t.rgba.size(), d) != t.rgba.size()) { std::fprintf(stderr, "cannot read %s\n", name); return false; }
        std::fclose(d);
    }
    std::fclose(f);
    return true;
}

void preview_begin(int width, int height, float r, float g, float b)
{
    g_W = width;
    g_H = height;
    g_Buffer.assign(static_cast<size_t>(width) * height * 3, 0.0f);
    for (int y = 0; y < height; ++y)
    {
        const float k = 0.7f + 0.3f * static_cast<float>(y) / static_cast<float>(height);
        for (int x = 0; x < width; ++x)
        {
            float* p = &g_Buffer[(static_cast<size_t>(y) * width + x) * 3];
            p[0] = r * k;
            p[1] = g * k;
            p[2] = b * k;
        }
    }
    g_Capture = true;
}

void preview_shift(float dx) { g_ShiftX = dx; }
void preview_skip() { g_Capture = false; } // the next draw calls are dropped (frames that only advance the animation)

// writes a crop of the buffer (centre in normalized coordinates, half size in pixels) as a binary PPM
bool preview_save(const char* path, float cx, float cy, int halfW, int halfH)
{
    const int px = static_cast<int>((cx + 1.0f) * 0.5f * g_W), py = static_cast<int>((1.0f - cy) * 0.5f * g_H);
    const int x0 = std::max(0, px - halfW), x1 = std::min(g_W, px + halfW), y0 = std::max(0, py - halfH), y1 = std::min(g_H, py + halfH);
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", x1 - x0, y1 - y0);
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
        {
            const float* p = &g_Buffer[(static_cast<size_t>(y) * g_W + x) * 3];
            for (int c = 0; c < 3; ++c)
            {
                const float v = std::min(1.0f, std::max(0.0f, p[c]));
                const unsigned char byte = static_cast<unsigned char>(std::pow(v, 1.0f / 1.1f) * 255.0f + 0.5f);
                std::fputc(byte, f);
            }
        }
    std::fclose(f);
    return true;
}

// ---- Renderer2D ------------------------------------------------------------------------------------------------------------------------------------------
void Renderer2D_SetBlendMode(Renderer2DBlendMode m) { g_Blend = m; }
Renderer2DBlendMode Renderer2D_GetBlendMode() { return g_Blend; }
void Renderer2D_SetCameraOffset(float, float) {}
void Renderer2D_ClearCameraOffset() {}
void Renderer2D_BindExternalTexture(ID3D11ShaderResourceView*) {}
void Renderer2D_Flush() {}
void Renderer2D_DrawPolygonTextured(int, const Renderer2DVertex*, int) {}

void Renderer2D_DrawQuad(float left, float top, float right, float bottom, float r, float g, float b, float a)
{
    if (!g_Capture) return;
    const V tl = Make(left, top, r, g, b, a, 0, 0), tr = Make(right, top, r, g, b, a, 1, 0), bl = Make(left, bottom, r, g, b, a, 0, 1), br = Make(right, bottom, r, g, b, a, 1, 1);
    Triangle(tl, tr, bl, nullptr);
    Triangle(tr, br, bl, nullptr);
}

void Renderer2D_DrawTexture(int id, float left, float top, float right, float bottom, float r, float g, float b, float a, float u0, float v0, float u1, float v1)
{
    if (!g_Capture) return;
    const Texture* t = TexById(id);
    if (!t) return;
    const V tl = Make(left, top, r, g, b, a, u0, v0), tr = Make(right, top, r, g, b, a, u1, v0), bl = Make(left, bottom, r, g, b, a, u0, v1), br = Make(right, bottom, r, g, b, a, u1, v1);
    Triangle(tl, tr, bl, t);
    Triangle(tr, br, bl, t);
}

void Renderer2D_DrawTextureEx(int id, float cx, float cy, float w, float h, float rot, float r, float g, float b, float a, float u0, float v0, float u1, float v1)
{
    if (!g_Capture || w <= 0 || h <= 0) return;
    const Texture* t = TexById(id);
    if (!t) return;
    const float hw = w * 0.5f, hh = h * 0.5f, c = std::cos(rot), s = std::sin(rot);
    constexpr float ASPECT = 720.0f / 1280.0f; // the same rotation as Renderer2D.cpp
    auto rp = [&](float lx, float ly, float& ox, float& oy)
    {
        ox = cx + lx * c - ly * ASPECT * s;
        oy = cy + lx / ASPECT * s + ly * c;
    };
    float x[4], y[4];
    rp(-hw, hh, x[0], y[0]);
    rp(hw, hh, x[1], y[1]);
    rp(-hw, -hh, x[2], y[2]);
    rp(hw, -hh, x[3], y[3]);
    const V tl = Make(x[0], y[0], r, g, b, a, u0, v0), tr = Make(x[1], y[1], r, g, b, a, u1, v0), bl = Make(x[2], y[2], r, g, b, a, u0, v1), br = Make(x[3], y[3], r, g, b, a, u1, v1);
    Triangle(tl, tr, bl, t);
    Triangle(tr, br, bl, t);
}

void Renderer2D_DrawPolygon(const Renderer2DVertex* v, int n)
{
    if (!g_Capture || n < 3) return;
    for (int k = 1; k + 1 < n; ++k)
    {
        const V a = Make(v[0].x, v[0].y, v[0].r, v[0].g, v[0].b, v[0].a, 0, 0), b = Make(v[k].x, v[k].y, v[k].r, v[k].g, v[k].b, v[k].a, 0, 0),
                c = Make(v[k + 1].x, v[k + 1].y, v[k + 1].r, v[k + 1].g, v[k + 1].b, v[k + 1].a, 0, 0);
        Triangle(a, b, c, nullptr);
    }
}

// The pattern shader (shaders/shader_pattern_2d.hlsl) on the CPU, line for line: the previews show what the GPU draws.
namespace
{
float Sat(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
float LineCoverage(float dist, float width, float px)
{
    const float lo = std::max(dist - px * 0.5f, -width * 0.5f);
    const float hi = std::min(dist + px * 0.5f, width * 0.5f);
    return Sat((hi - lo) / px);
}
void Over(float c[4], float r, float g, float b, float a)
{
    c[0] = r * a + c[0] * (1.0f - a);
    c[1] = g * a + c[1] * (1.0f - a);
    c[2] = b * a + c[2] * (1.0f - a);
    c[3] = a + c[3] * (1.0f - a);
}
void EvalPattern(const Renderer2DPattern& q, float px_, float py_, float px, float c[4])
{
    c[0] = c[1] = c[2] = c[3] = 0.0f;
    const float dx = px_ - q.originX, dy = py_ - q.originY;
    const float along = dx * q.alongX + dy * q.alongY;
    const float out = dx * q.normalX + dy * q.normalY;
    if (q.kind == Renderer2DPatternKind::Halftone)
    {
        const float row = std::max(0.0f, std::round((out - 10.0f) / q.step));
        const float shift = std::fmod(row, 2.0f) * q.step * 0.5f;
        const float col = std::round((along - shift) / q.step);
        const float rowOut = 10.0f + row * q.step;
        const float cx = q.originX + q.alongX * (col * q.step + shift) + q.normalX * rowOut;
        const float cy = q.originY + q.alongY * (col * q.step + shift) + q.normalY * rowOut;
        const float k = Sat(1.0f - rowOut / q.reach);
        const float radius = q.radius * k * k;
        if (radius < 0.7f) return;
        const float l1 = std::fabs(px_ - cx) + std::fabs(py_ - cy);
        const float a = q.a * Sat((radius - l1) / px + 0.5f);
        c[0] = q.r * a, c[1] = q.g * a, c[2] = q.b * a, c[3] = a;
        return;
    }
    const float i = std::round(out / 26.0f);
    if (i >= 1.0f && i <= 26.0f) Over(c, q.r, q.g, q.b, (std::fmod(i, 4.0f) == 0.0f ? 0.10f : 0.05f) * LineCoverage(out - i * 26.0f, 1.0f, px));
    const float j = std::round(along / 52.0f);
    Over(c, q.r, q.g, q.b, 0.035f * LineCoverage(along - j * 52.0f, 1.0f, px));
    Over(c, 0.60f, 0.85f, 1.0f, 0.045f * LineCoverage(out - 158.0f, 76.0f, px));
    Over(c, q.r2, q.g2, q.b2, 0.22f * LineCoverage(out - 120.0f, 1.0f, px));
    Over(c, q.r, q.g, q.b, 0.30f * LineCoverage(out - 196.0f, 1.0f, px));
}
}

bool Renderer2D_DrawPolygonPattern(const Renderer2DVertex* v, int n, const Renderer2DPattern& pattern)
{
#ifdef PREVIEW_NO_PATTERN
    return false; // (compare with the polygon fallback of the callers)
#endif
    if (!g_Capture || n < 3) return true;
    // the polygon in pixels, then every pixel inside it (convex: inside all edges)
    float xs[32], ys[32];
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    n = std::min(n, 32);
    for (int k = 0; k < n; ++k)
    {
        const V p = Make(v[k].x, v[k].y, 1, 1, 1, 1, 0, 0);
        xs[k] = p.x, ys[k] = p.y;
        minX = std::min(minX, p.x), maxX = std::max(maxX, p.x), minY = std::min(minY, p.y), maxY = std::max(maxY, p.y);
    }
    float area = 0.0f;
    for (int k = 0; k < n; ++k) area += xs[k] * ys[(k + 1) % n] - xs[(k + 1) % n] * ys[k];
    const float scale = static_cast<float>(g_W) / 1280.0f; // (pixels per virtual px)
    const float strength = v[0].a;
    for (int y = std::max(0, static_cast<int>(minY)); y <= std::min(g_H - 1, static_cast<int>(maxY) + 1); ++y)
        for (int x = std::max(0, static_cast<int>(minX)); x <= std::min(g_W - 1, static_cast<int>(maxX) + 1); ++x)
        {
            const float fx = x + 0.5f, fy = y + 0.5f;
            bool inside = true;
            for (int k = 0; k < n && inside; ++k)
            {
                const float e = (xs[(k + 1) % n] - xs[k]) * (fy - ys[k]) - (ys[(k + 1) % n] - ys[k]) * (fx - xs[k]);
                inside = area > 0.0f ? e >= 0.0f : e <= 0.0f;
            }
            if (!inside) continue;
            float c[4];
            EvalPattern(pattern, fx / scale, fy / scale, 1.0f / scale, c);
            if (c[3] <= 0.0f) continue;
            const float a = c[3] * strength;
            BlendPixel(x, y, c[0] / c[3], c[1] / c[3], c[2] / c[3], a);
        }
    return true;
}

// ---- AssetManager: ids are the values of the enum (tools/make_preview_assets.py writes the files in that order) ------------------------------------------
namespace AssetManager
{
int Get(Tex tex)
{
    const int id = static_cast<int>(tex);
    return TexById(id) != nullptr ? id : -1;
}
int EnemyGlow(int type)
{
    return type < 0 || type > 7 ? -1 : Get(static_cast<Tex>(static_cast<int>(Tex::EnemyScoutGlow) + type));
}
void BulletUv(int, float& u0, float& v0, float& u1, float& v1) { u0 = v0 = 0.0f; u1 = v1 = 1.0f; }
}

