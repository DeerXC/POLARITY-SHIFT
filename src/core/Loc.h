#pragma once

#include <string>

// Localisation. Keys are the English texts used in the code, so a missing translation (or the English
// language itself) simply falls back to the key.
//   assets/lang/ja.txt, assets/lang/zh.txt : "ENGLISH KEY<TAB>translation", '#' starts a comment. The single source of texts.
//   Placeholders: {0}, {1}
// Language and tutorial progress are stored in config.ini next to the exe.
namespace Loc
{
void Initialize();

const std::string& T(const char* key);
std::string Fmt(const char* key, int a);
std::string Fmt(const char* key, int a, int b);
std::string FmtText(const char* key, const std::string& a); // {0} = a text (a key name, a button name)
std::string FmtText(const char* key, const std::string& a, const std::string& b);

int GetLanguage();
void SetLanguage(int index);
const std::string& GetLanguageName(); // native name shown on the title screen

bool IsTutorialDone();
void SetTutorialDone();
}


