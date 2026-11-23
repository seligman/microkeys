#pragma once

#include "Core.h"

// UTF-8 is used for every string that crosses the core, the Python layer,
// and the platform interface.  These helpers convert to and from UTF-32 for
// the code that needs to work a character at a time.

u32string Utf8ToU32(const string& text);
string U32ToUtf8(const u32string& text);
string U32ToUtf8(char32_t c);
char32_t FoldCase(char32_t c);
bool IsWordChar(char32_t c);
bool GlobMatch(const u32string& pattern, const u32string& text);
bool GlobMatch(const string& pattern, const string& text);
wstring Utf8ToWide(const string& text);
