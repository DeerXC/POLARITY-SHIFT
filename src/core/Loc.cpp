#include "core/Loc.h"
#include "core/DebugOptions.h"
#include "core/TextRenderer.h"

#include <fstream>
#include <map>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace
{
struct Language
{
    const char* code;
    const char* file;          // nullptr = the keys themselves (English)
    const wchar_t* fontFamily; // which CJK fonts to use (TextRenderer: Noto Sans SC / Smiley Sans for zh, M PLUS 1 / Dela Gothic One for ja; Latin is Chakra Petch)
};

const Language LANGUAGES[] = {
    {"en", "assets/lang/en.txt", nullptr}, {"ja", "assets/lang/ja.txt", L"PS CJK JP"}, {"zh", "assets/lang/zh.txt", L"PS CJK SC"}};
constexpr int LANGUAGE_COUNT = static_cast<int>(sizeof(LANGUAGES) / sizeof(LANGUAGES[0]));
constexpr const char* CONFIG_FILE = "config.ini";

int g_Language = 0;
bool g_TutorialDone = false;
std::map<std::string, std::string> g_Table;
std::string g_LanguageName = "ENGLISH";

void SaveConfig()
{
    // keep every other key of config.ini (debug switches) - DebugOptions normalises the file for us
    std::map<std::string, std::string> values = DebugOptions::ReadAll();
    values["language"] = LANGUAGES[g_Language].code;
    values["tutorial"] = g_TutorialDone ? "1" : "0";

    std::ofstream file(CONFIG_FILE, std::ios::trunc);
    if (!file) return;
    file << "[game]\n";
    for (const auto& kv : values)
        file << kv.first << "=" << kv.second << "\n";
}

int DetectSystemLanguage()
{
#ifdef _WIN32
    const LANGID id = GetUserDefaultUILanguage();
    switch (PRIMARYLANGID(id))
    {
    case LANG_JAPANESE:
        return 1;
    case LANG_CHINESE:
        return 2;
    default:
        break;
    }
#endif
    return 0;
}

void LoadTable(int index)
{
    g_Table.clear();
    g_LanguageName = "ENGLISH";
    const char* path = LANGUAGES[index].file;
    if (path == nullptr) return;

    std::ifstream file(path, std::ios::binary);
    if (!file) return;
    std::string line;
    bool first = true;
    while (std::getline(file, line))
    {
        if (first)
        {
            first = false;
            if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF && static_cast<unsigned char>(line[1]) == 0xBB
                && static_cast<unsigned char>(line[2]) == 0xBF)
            {
                line.erase(0, 3);
            }
        }
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        const std::string key = line.substr(0, tab);
        const std::string value = line.substr(tab + 1);
        if (key == "__name")
            g_LanguageName = value;
        else
            g_Table[key] = value;
    }
}

void ApplyFont()
{
    TextRenderer::SetFontFamilyOverride(LANGUAGES[g_Language].fontFamily);
}

std::string Replace(std::string text, const char* token, const std::string& value)
{
    const size_t pos = text.find(token);
    if (pos != std::string::npos) text.replace(pos, std::char_traits<char>::length(token), value);
    return text;
}
}

namespace Loc
{
void Initialize()
{
    DebugOptions::WriteReport(); // debug_report.txt: which config.ini was read

    const std::map<std::string, std::string> config = DebugOptions::ReadAll();
    g_Language = DetectSystemLanguage();
    const auto it = config.find("language");
    if (it != config.end())
    {
        for (int i = 0; i < LANGUAGE_COUNT; ++i)
        {
            if (it->second == LANGUAGES[i].code) g_Language = i;
        }
    }
    const auto tut = config.find("tutorial");
    g_TutorialDone = (tut != config.end() && tut->second == "1");
    LoadTable(g_Language);
    ApplyFont();
}

const std::string& T(const char* key)
{
    const auto it = g_Table.find(key);
    if (it != g_Table.end()) return it->second;

    // Not translated (or English): the key is the text. Remember it so that the returned reference stays valid.
    return g_Table.emplace(key, key).first->second;
}

std::string Fmt(const char* key, int a)
{
    return Replace(T(key), "{0}", std::to_string(a));
}

std::string Fmt(const char* key, int a, int b)
{
    return Replace(Replace(T(key), "{0}", std::to_string(a)), "{1}", std::to_string(b));
}

std::string FmtText(const char* key, const std::string& a)
{
    return Replace(T(key), "{0}", a);
}

std::string FmtText(const char* key, const std::string& a, const std::string& b)
{
    return Replace(Replace(T(key), "{0}", a), "{1}", b);
}

int GetLanguage()
{
    return g_Language;
}
void SetLanguage(int index)
{
    if (index < 0 || index >= LANGUAGE_COUNT) index = 0;
    g_Language = index;
    LoadTable(g_Language);
    ApplyFont();
    SaveConfig();
}

const std::string& GetLanguageName()
{
    return g_LanguageName;
}

bool IsTutorialDone()
{
    return g_TutorialDone;
}

void SetTutorialDone()
{
    if (g_TutorialDone) return;
    g_TutorialDone = true;
    SaveConfig();
}
}


