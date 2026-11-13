// Cell layout of assets/textures/ui/hud_atlas.png (512 x 512 px). Keep this table in sync with the texture when the atlas changes.
#pragma once

namespace UiAtlas
{
constexpr float SIZE = 512.0f;
struct Rect
{
    float x, y, w, h, corner;
};
enum class Id
{
    PanelFill,
    PanelLine,
    BarBack,
    BarFrame,
    BarFill,
    CellOn,
    CellOff,
    PipOn,
    PipOff,
    KeyCap,
    KeyCapWide,
    Diamond,
    Chevron,
    Times,
    Banner,
    Glow,
    Stripe,
    Tick,
    Count
};

constexpr Rect RECTS[] = {
    {2.0f, 2.0f, 64.0f, 64.0f, 16.0f},   // PanelFill
    {70.0f, 2.0f, 64.0f, 64.0f, 16.0f},  // PanelLine
    {138.0f, 2.0f, 64.0f, 32.0f, 16.0f}, // BarBack
    {206.0f, 2.0f, 64.0f, 32.0f, 16.0f}, // BarFrame
    {274.0f, 2.0f, 32.0f, 16.0f, 0.0f},  // BarFill
    {310.0f, 2.0f, 40.0f, 20.0f, 0.0f},  // CellOn
    {354.0f, 2.0f, 40.0f, 20.0f, 0.0f},  // CellOff
    {398.0f, 2.0f, 28.0f, 28.0f, 0.0f},  // PipOn
    {430.0f, 2.0f, 28.0f, 28.0f, 0.0f},  // PipOff
    {462.0f, 2.0f, 48.0f, 48.0f, 0.0f},  // KeyCap
    {2.0f, 70.0f, 96.0f, 48.0f, 0.0f},   // KeyCapWide
    {102.0f, 70.0f, 24.0f, 24.0f, 0.0f}, // Diamond
    {130.0f, 70.0f, 24.0f, 24.0f, 0.0f}, // Chevron
    {158.0f, 70.0f, 24.0f, 24.0f, 0.0f}, // Times
    {186.0f, 70.0f, 256.0f, 4.0f, 0.0f}, // Banner
    {446.0f, 70.0f, 32.0f, 32.0f, 0.0f}, // Glow
    {482.0f, 70.0f, 8.0f, 8.0f, 0.0f},   // Stripe
    {494.0f, 70.0f, 6.0f, 20.0f, 0.0f},  // Tick
};
}

