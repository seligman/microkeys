#pragma once

#include "framework.h"

// UTF-8 is what the core speaks, UTF-16 is what Windows speaks

wstring Utf8ToUtf16(const string& text);
string Utf16ToUtf8(const wstring& text);
string Utf16ToUtf8(const wchar_t* text, size_t len);
string LastErrorText(DWORD error = GetLastError());
