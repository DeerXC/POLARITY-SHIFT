#include "core/DebugOptions.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace
{
constexpr const char* CONFIG_FILE = "config.ini";

bool g_Loaded = false;
bool g_Found = false;
std::string g_Encoding = "none";
std::string g_RawText;
std::vector<std::string> g_Lines;
std::map<std::string, std::string> g_Values;
std::vector<std::string> g_Ignored;

const char* const KNOWN_KEYS[] = {"language", "tutorial", "start_at_boss", "boss_phase", "god_mode", "modules", "shards", "unlock_all", "no_talents", "flip_present"};

bool IsKnown(const std::string& key)
{
    for (const char* known : KNOWN_KEYS)
    {
        if (key == known)
        {
            return true;
        }
    }
    return false;
}

std::string Trim(const std::string& text)
{
    size_t begin = 0;
    size_t end = text.size();

    while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r' || text[begin] == '\n'))
    {
        ++begin;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' || text[end - 1] == '\n'))
    {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::string Lower(std::string text)
{
    for (char& c : text)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

// UTF-16 (BOM) -> narrow text; full-width forms are folded to ASCII.
std::string FromUtf16(const std::string& raw, bool littleEndian)
{
    std::string out;
    for (size_t i = 2; i + 1 < raw.size(); i += 2)
    {
        const unsigned lo = static_cast<unsigned char>(raw[i + (littleEndian ? 0 : 1)]);
        const unsigned hi = static_cast<unsigned char>(raw[i + (littleEndian ? 1 : 0)]);
        const unsigned ch = lo | (hi << 8);

        if (ch < 0x80)
            out += static_cast<char>(ch);
        else if (ch >= 0xFF01 && ch <= 0xFF5E)
            out += static_cast<char>(ch - 0xFEE0);
        else if (ch == 0x3000)
            out += ' ';
        else
            out += '?';
    }
    return out;
}

// UTF-8 / Shift-JIS: drop BOMs, fold full-width characters to ASCII.
std::string FoldFullWidth(const std::string& raw)
{
    std::string out;
    out.reserve(raw.size());

    for (size_t i = 0; i < raw.size(); ++i)
    {
        const unsigned char c = static_cast<unsigned char>(raw[i]);
        const unsigned char c1 = (i + 1 < raw.size()) ? static_cast<unsigned char>(raw[i + 1]) : 0;
        const unsigned char c2 = (i + 2 < raw.size()) ? static_cast<unsigned char>(raw[i + 2]) : 0;

        if (c == 0xEF && c1 == 0xBB && c2 == 0xBF) // BOM
        {
            i += 2;
        }
        else if (c == 0xEF && c1 == 0xBC && c2 >= 0x81 && c2 <= 0xBF) // U+FF01..FF3F
        {
            out += static_cast<char>(0x20 + (c2 - 0x80));
            i += 2;
        }
        else if (c == 0xEF && c1 == 0xBD && c2 >= 0x80 && c2 <= 0x9E) // U+FF40..FF5E
        {
            out += static_cast<char>(0x60 + (c2 - 0x80));
            i += 2;
        }
        else if (c == 0xE3 && c1 == 0x80 && c2 == 0x80) // ideographic space
        {
            out += ' ';
            i += 2;
        }
        else if (c == 0x81 && c1 != 0) // Shift-JIS symbols
        {
            char mapped = 0;
            if (c1 == 0x40)
                mapped = ' ';
            else if (c1 == 0x81)
                mapped = '=';
            else if (c1 == 0x51)
                mapped = '_';
            else if (c1 == 0x43)
                mapped = ',';
            else if (c1 == 0x44)
                mapped = '.';
            else if (c1 == 0x46)
                mapped = ':';
            else if (c1 == 0x47)
                mapped = ';';
            else if (c1 == 0x7C)
                mapped = '-';
            if (mapped != 0)
            {
                out += mapped;
                ++i;
            }
            else
            {
                out += static_cast<char>(c);
            }
        }
        else if (c == 0x82 && c1 != 0) // Shift-JIS digits / letters
        {
            char mapped = 0;
            if (c1 >= 0x4F && c1 <= 0x58)
                mapped = static_cast<char>('0' + (c1 - 0x4F));
            else if (c1 >= 0x60 && c1 <= 0x79)
                mapped = static_cast<char>('A' + (c1 - 0x60));
            else if (c1 >= 0x81 && c1 <= 0x9A)
                mapped = static_cast<char>('a' + (c1 - 0x81));
            if (mapped != 0)
            {
                out += mapped;
                ++i;
            }
            else
            {
                out += static_cast<char>(c);
            }
        }
        else
        {
            out += static_cast<char>(c);
        }
    }
    return out;
}

void Load()
{
    if (g_Loaded)
    {
        return;
    }

    g_Loaded = true;

    std::ifstream file(CONFIG_FILE, std::ios::binary);

    if (!file)
    {
        return;
    }

    g_Found = true;
    g_RawText.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());

    std::string text;
    const unsigned char b0 = g_RawText.size() > 0 ? static_cast<unsigned char>(g_RawText[0]) : 0;
    const unsigned char b1 = g_RawText.size() > 1 ? static_cast<unsigned char>(g_RawText[1]) : 0;

    if (b0 == 0xFF && b1 == 0xFE)
    {
        g_Encoding = "UTF-16 LE";
        text = FromUtf16(g_RawText, true);
    }
    else if (b0 == 0xFE && b1 == 0xFF)
    {
        g_Encoding = "UTF-16 BE";
        text = FromUtf16(g_RawText, false);
    }
    else
    {
        g_Encoding = "UTF-8 / ANSI";
        text = FoldFullWidth(g_RawText);
    }

    std::istringstream stream(text);
    std::string line;

    while (std::getline(stream, line))
    {
        g_Lines.push_back(line);

        const size_t comment = line.find_first_of(";#");

        if (comment != std::string::npos)
        {
            line.erase(comment);
        }

        line = Trim(line);

        if (line.empty() || line[0] == '[')
        {
            continue;
        }

        const size_t separator = line.find('=');

        if (separator == std::string::npos)
        {
            g_Ignored.push_back(line);
            continue;
        }

        const std::string key = Lower(Trim(line.substr(0, separator)));
        const std::string value = Trim(line.substr(separator + 1));

        if (key.empty())
        {
            continue;
        }

        if (!IsKnown(key))
        {
            g_Ignored.push_back(key + "=" + value);
        }

        g_Values[key] = value;
    }
}

int GetInt(const char* key)
{
    Load();

    const auto found = g_Values.find(key);

    if (found == g_Values.end())
    {
        return 0;
    }

    const std::string value = Lower(found->second);

    if (value == "true" || value == "on" || value == "yes")
    {
        return 1;
    }

    if (value == "false" || value == "off" || value == "no")
    {
        return 0;
    }

    return std::atoi(value.c_str());
}

std::string WorkingDirectory()
{
#ifdef _WIN32
    char buffer[MAX_PATH]{};
    const DWORD length = GetCurrentDirectoryA(MAX_PATH, buffer);
    return length > 0 && length < MAX_PATH ? std::string(buffer) : std::string("?");
#else
    char buffer[1024]{};
    return getcwd(buffer, sizeof buffer) ? std::string(buffer) : std::string("?");
#endif
}
}

bool DebugOptions::StartAtBoss()
{
    return GetInt("start_at_boss") != 0;
}
bool DebugOptions::PostFxOff()
{
    Load();
    const auto found = g_Values.find("post_fx");
    return found != g_Values.end() && GetInt("post_fx") == 0;
}

bool DebugOptions::FlipPresent()
{
    return GetInt("flip_present") != 0;
}

bool DebugOptions::GodMode()
{
    return GetInt("god_mode") != 0;
}
int DebugOptions::Shards()
{
    return GetInt("shards");
}
bool DebugOptions::UnlockAll()
{
    return GetInt("unlock_all") != 0;
}

bool DebugOptions::NoTalents()
{
    return GetInt("no_talents") != 0;
}
int DebugOptions::BossPhase()
{
    return GetInt("boss_phase");
}

std::string DebugOptions::ModuleList()
{
    Load();

    const auto found = g_Values.find("modules");

    return found == g_Values.end() ? std::string() : Lower(found->second);
}

std::map<std::string, std::string> DebugOptions::ReadAll()
{
    Load();
    return g_Values;
}

void DebugOptions::WriteReport()
{
    Load();

    std::ofstream report("debug_report.txt", std::ios::trunc);

    if (!report)
    {
        return;
    }

    report << "working directory : " << WorkingDirectory() << "\n";
    report << "config.ini        : " << (g_Found ? "found" : "NOT FOUND in the working directory") << "\n";

    if (g_Found)
    {
        report << "encoding          : " << g_Encoding << "\n";
        report << "start_at_boss     : " << GetInt("start_at_boss") << "\n";
        report << "boss_phase        : " << GetInt("boss_phase") << "\n";
        report << "god_mode          : " << GetInt("god_mode") << "\n";
        report << "modules           : " << (ModuleList().empty() ? "(none)" : ModuleList()) << "\n";
        report << "shards            : " << GetInt("shards") << "\n";
        report << "unlock_all        : " << GetInt("unlock_all") << "\n";
        report << "no_talents        : " << GetInt("no_talents") << "\n";

        for (const std::string& item : g_Ignored)
        {
            report << "IGNORED LINE      : " << item << "   <- not a known key, check the spelling\n";
        }

        report << "\n--- config.ini as the game read it ---\n";

        for (const std::string& line : g_Lines)
        {
            report << line << "\n";
        }
    }
    else
    {
        report << "\nPut a file named exactly config.ini (not config.ini.example, not config.ini.txt) in the folder above.\n";
    }
}
