#include "ui/TextTheme.h"

// Flat, quiet palette: white / grey-blue text, no drop shadows on UI text. Text on a selected row is dark (the row is a solid ivory plate).
// (Boss label, warnings and the logo keep their bright colours on purpose.)
namespace
{
TextStyle MakeStyle(float size, float red, float green, float blue, float alpha, bool bold, bool shadow, TextAlignment alignment,
                    float layoutWidth)
{
    TextStyle style{};

    style.size = size;
    style.red = red;
    style.green = green;
    style.blue = blue;
    style.alpha = alpha;
    style.bold = bold;
    style.layer = -1;
    style.shadow = shadow;
    style.alignment = alignment;
    style.layoutWidth = layoutWidth;

    return style;
}

// a menu row: bold display text (the layer is explicit, not decided by the size)
TextStyle MenuRowStyle(float size, float red, float green, float blue, float alpha, TextAlignment alignment, float layoutWidth)
{
    TextStyle style = MakeStyle(size, red, green, blue, alpha, true, false, alignment, layoutWidth);
    style.layer = 1;
    return style;
}
}

TextStyle TextTheme::GetStyle(TextRole role)
{
    switch (role)
    {
    case TextRole::BossLabel:
        return MakeStyle(0.0105f, 1.00f, 0.54f, 0.36f, 1.00f, false, true, TextAlignment::Center, 0.34f);

    case TextRole::BossPhase:
        return MakeStyle(0.0068f, 0.66f, 0.74f, 0.90f, 0.95f, false, true, TextAlignment::Left, 0.20f);

    case TextRole::BossHp:
        return MakeStyle(0.0068f, 0.92f, 0.96f, 1.00f, 0.95f, false, true, TextAlignment::Right, 0.20f);

    case TextRole::WaveTitle:
        return MakeStyle(0.0185f, 0.90f, 0.95f, 1.00f, 1.00f, false, false, TextAlignment::Center, 0.90f);

    case TextRole::TitleRed:
        return MakeStyle(0.031f, 1.00f, 0.20f, 0.17f, 1.00f, false, true, TextAlignment::Center, 0.82f);

    case TextRole::TitleBlue:
        return MakeStyle(0.025f, 0.10f, 0.68f, 1.00f, 1.00f, false, true, TextAlignment::Center, 0.72f);

    case TextRole::PageTitle:
        return MakeStyle(0.0175f, 0.97f, 0.98f, 1.00f, 1.00f, false, false, TextAlignment::Center, 1.00f);

    case TextRole::SectionTitle:
        return MakeStyle(0.0115f, 0.62f, 0.72f, 0.86f, 1.00f, false, false, TextAlignment::Center, 1.00f);

    // THE ROWS OF EVERY MENU (title, pause, settings, results): ONE typeface in both states - the display layer (Chakra Petch Bold / Smiley Sans / Dela Gothic One) -
    // and ONE size. A row that is selected changes its colour (ivory plate: ink letters; at rest: pale letters) and nothing else: the letters never change shape,
    // width or place. (They used to: the selected row was heavy display text and the others were body text, so the font jumped when the cursor arrived.)
    case TextRole::MenuLabel:
        return MenuRowStyle(0.0095f, 0.72f, 0.78f, 0.88f, 0.96f, TextAlignment::Left, 0.42f);

    case TextRole::MenuLabelSelected:
        return MenuRowStyle(0.0095f, 0.05f, 0.06f, 0.09f, 1.00f, TextAlignment::Left, 0.42f);

    case TextRole::MenuValue:
        return MenuRowStyle(0.0090f, 0.64f, 0.80f, 1.00f, 0.96f, TextAlignment::Center, 0.32f);

    case TextRole::MenuValueSelected:
        return MenuRowStyle(0.0090f, 0.05f, 0.06f, 0.09f, 1.00f, TextAlignment::Center, 0.32f);

    case TextRole::Button:
        return MenuRowStyle(0.0115f, 0.74f, 0.80f, 0.90f, 0.96f, TextAlignment::Center, 0.72f);

    case TextRole::ButtonSelected:
        return MenuRowStyle(0.0115f, 0.05f, 0.06f, 0.09f, 1.00f, TextAlignment::Center, 0.72f);

    case TextRole::HudLabel:
        return MakeStyle(0.008f, 0.62f, 0.70f, 0.82f, 1.00f, false, false, TextAlignment::Left, 0.50f);

    case TextRole::HudValue:
        return MakeStyle(0.011f, 0.96f, 0.98f, 1.00f, 1.00f, false, false, TextAlignment::Left, 0.50f);

    case TextRole::Help:
        return MakeStyle(0.009f, 0.54f, 0.62f, 0.76f, 0.92f, false, false, TextAlignment::Center, 1.20f);

    default:
        return TextStyle{};
    }
}




