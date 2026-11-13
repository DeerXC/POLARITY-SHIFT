#pragma once

#include "core/TextRenderer.h"

enum class TextRole
{
    TitleRed,
    TitleBlue,

    PageTitle,
    SectionTitle,

    MenuLabel,
    MenuLabelSelected,

    MenuValue,
    MenuValueSelected,

    Button,
    ButtonSelected,

    HudLabel,
    HudValue,

    BossLabel,
    BossPhase,
    BossHp,
    WaveTitle,

    Help
};

namespace TextTheme
{
TextStyle GetStyle(TextRole role);
}


