// layout_check: do the texts of the modules fit the pick cards and the hangar panel in every language? It uses the width model of the game (UiKit::TextWidth,
// UiKit::FitFontPx) and the wrapping of DrawEffectRowText (a long row breaks at the space nearest to its middle).
//   layout_check <project root>
#include "rogue/ModuleDefs.h"
#include "ui/UiKit.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>

static std::map<std::string, std::string> Load(const std::string& path)
{
    std::map<std::string, std::string> t;
    std::ifstream f(path, std::ios::binary);
    std::string line;
    while (std::getline(f, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() >= 3 && (unsigned char)line[0] == 0xEF) line.erase(0, 3);
        if (line.empty() || line[0] == '#') continue;
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        t[line.substr(0, tab)] = line.substr(tab + 1);
    }
    return t;
}

// a headline number as the card writes it (the same as FormatStat in ModulesDraw.cpp)
static std::string Format(Mod::StatFmt fmt, float value)
{
    char text[32];
    switch (fmt)
    {
    case Mod::StatFmt::PlusPercent: std::snprintf(text, sizeof text, "+%d%%", static_cast<int>(std::lround(value))); break;
    case Mod::StatFmt::Percent: std::snprintf(text, sizeof text, "%d%%", static_cast<int>(std::lround(value))); break;
    case Mod::StatFmt::Seconds: std::snprintf(text, sizeof text, "%.1f S", static_cast<double>(value)); break;
    case Mod::StatFmt::Count: std::snprintf(text, sizeof text, "%d", static_cast<int>(std::lround(value))); break;
    case Mod::StatFmt::Hp: std::snprintf(text, sizeof text, "%d HP", static_cast<int>(std::lround(value))); break;
    case Mod::StatFmt::Decimal1: std::snprintf(text, sizeof text, "%.1f", static_cast<double>(value)); break;
    default: std::snprintf(text, sizeof text, "%.2f", static_cast<double>(value)); break;
    }
    return text;
}

// returns the number of lines the row needs and the smallest font size that it has to use
static int Wrap(const std::string& text, float px, float textW, float minPx, float& usedPx)
{
    usedPx = px;
    if (UiKit::TextWidth(text, px) <= textW * 0.94f) return 1;
    size_t split = std::string::npos;
    float best = 1.0e9f;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] != ' ') continue;
        const float diff = std::abs(static_cast<float>(i) - static_cast<float>(text.size()) * 0.5f);
        if (diff < best) { best = diff; split = i; }
    }
    if (split == std::string::npos)
    {
        usedPx = UiKit::FitFontPx(text, px, textW, minPx);
        return 1;
    }
    const float a = UiKit::FitFontPx(text.substr(0, split), px, textW, minPx);
    const float b = UiKit::FitFontPx(text.substr(split + 1), px, textW, minPx);
    usedPx = a < b ? a : b;
    return 2;
}

int main(int argc, char** argv)
{
    const std::string root = argc > 1 ? argv[1] : ".";
    const char* langs[3] = {"en", "zh", "ja"};
    int problems = 0;
    for (const char* lang : langs)
    {
        auto en = Load(root + "/assets/lang/en.txt");
        auto tr = Load(root + "/assets/lang/" + lang + ".txt");
        auto T = [&](const char* key) -> std::string
        {
            if (std::string(lang) == "en") { auto it = en.find(key); return it != en.end() ? it->second : std::string(key); }
            auto it = tr.find(key);
            return it != tr.end() ? it->second : std::string(key);
        };
        std::printf("== %s\n", lang);
        for (int i = 0; i < Mod::COUNT; ++i)
        {
            const Mod::Info& info = Mod::INFO[i];
            const char* rows[3] = {info.gain, info.gain2, info.cost};
            float rowCentre = 286.0f; // the first row of a card (y + 286)
            float lastBottom = 0.0f;
            std::string report;
            bool bad = false;
            const std::string name = T(info.name);
            const float namePx = UiKit::FitFontPx(name, 26.0f, 296.0f - 32.0f, 13.0f);
            if (namePx < 20.0f) { report += "  NAME small (" + std::to_string((int)namePx) + "px)"; bad = true; }
            for (int r = 0; r < 3; ++r)
            {
                if (rows[r] == nullptr) continue;
                const std::string text = T(rows[r]);
                float cardPx, hangPx;
                const int cardLines = Wrap(text, 15.0f, 296.0f - 48.0f - 24.0f, 10.0f, cardPx);
                const int hangLines = Wrap(text, 14.0f, 380.0f - 24.0f, 10.0f, hangPx);
                lastBottom = rowCentre + (cardLines == 1 ? 9.0f : 15.0f * 1.22f * 0.5f + 9.0f);
                rowCentre += cardLines == 1 ? 28.0f : 15.0f * 1.22f + 26.0f;
                if (cardPx < 11.5f) { report += "  card row " + std::to_string(r) + " small (" + std::to_string((int)cardPx) + "px, " + std::to_string(cardLines) + " lines)"; bad = true; }
                if (hangPx < 11.5f) { report += "  hangar row " + std::to_string(r) + " small (" + std::to_string((int)hangPx) + "px)"; bad = true; }
                (void)hangLines;
            }
            if (lastBottom > 402.0f) { report += "  card rows reach " + std::to_string((int)lastBottom) + " of 410 px"; bad = true; }
            // the card of an UPGRADE of this module (ModulesDraw.cpp DrawCard): level line at 284, the label (13 px) at 322, "before > after" at 356 (17 px / 28 px), the cost line at 392
            {
                const std::string label = T(Mod::HEADLINE[i].label);
                const float labelPx = UiKit::FitFontPx(label, 13.0f, 296.0f - 48.0f, 9.0f);
                if (labelPx < 10.5f) { report += "  upgrade label small (" + std::to_string((int)labelPx) + "px)"; bad = true; }
                for (int level = 1; level <= 2; ++level) // level 1 > 2 and 2 > 3
                {
                    const std::string before = Format(Mod::HEADLINE[i].fmt, Mod::HeadlineValue(static_cast<Mod::Id>(i), level));
                    const std::string after = Format(Mod::HEADLINE[i].fmt, Mod::HeadlineValue(static_cast<Mod::Id>(i), level + 1));
                    const float width = UiKit::TextWidth(before, 17.0f) + 40.0f + UiKit::TextWidth(after, 28.0f);
                    if (width > 296.0f - 48.0f) { report += "  upgrade values too wide (" + before + " > " + after + ", " + std::to_string((int)width) + "px)"; bad = true; }
                }
            }
            if (bad) { std::printf("  %-12s%s\n", info.code, report.c_str()); ++problems; }
        }
    }
    std::printf("%d modules with a layout problem\n", problems);
    return 0;
}
