#include "Text.h"
#include <cwctype>

u32string Utf8ToU32(const string& text) {
	// Decode UTF-8, turning any malformed sequence into U+FFFD
	u32string ret;
	size_t i = 0;
	while (i < text.size()) {
		unsigned char c = (unsigned char)text[i];
		char32_t cp = 0;
		int extra = 0;
		if (c < 0x80) {
			cp = c;
		}
		else if ((c & 0xE0) == 0xC0) {
			cp = c & 0x1F;
			extra = 1;
		}
		else if ((c & 0xF0) == 0xE0) {
			cp = c & 0x0F;
			extra = 2;
		}
		else if ((c & 0xF8) == 0xF0) {
			cp = c & 0x07;
			extra = 3;
		}
		else {
			ret += (char32_t)0xFFFD;
			i++;
			continue;
		}
		bool good = i + extra < text.size();
		for (int j = 1; good && j <= extra; j++) {
			if (((unsigned char)text[i + j] & 0xC0) != 0x80) {
				good = false;
			}
			else {
				cp = (cp << 6) | ((unsigned char)text[i + j] & 0x3F);
			}
		}
		if (!good) {
			ret += (char32_t)0xFFFD;
			i++;
			continue;
		}
		ret += cp;
		i += extra + 1;
	}
	return ret;
}

string U32ToUtf8(char32_t c) {
	// Encode a single code point as UTF-8
	string ret;
	if (c < 0x80) {
		ret += (char)c;
	}
	else if (c < 0x800) {
		ret += (char)(0xC0 | (c >> 6));
		ret += (char)(0x80 | (c & 0x3F));
	}
	else if (c < 0x10000) {
		ret += (char)(0xE0 | (c >> 12));
		ret += (char)(0x80 | ((c >> 6) & 0x3F));
		ret += (char)(0x80 | (c & 0x3F));
	}
	else {
		ret += (char)(0xF0 | (c >> 18));
		ret += (char)(0x80 | ((c >> 12) & 0x3F));
		ret += (char)(0x80 | ((c >> 6) & 0x3F));
		ret += (char)(0x80 | (c & 0x3F));
	}
	return ret;
}

string U32ToUtf8(const u32string& text) {
	// Encode a whole string as UTF-8
	string ret;
	for (auto c : text) {
		ret += U32ToUtf8(c);
	}
	return ret;
}

char32_t FoldCase(char32_t c) {
	// Simple case folding, good enough for hotstrings and window matching.
	// Characters outside the BMP are left alone since wchar_t may be 16 bits.
	if (c < 0x80) {
		return (c >= 'A' && c <= 'Z') ? c + 32 : c;
	}
	if (c <= 0xFFFF) {
		return (char32_t)towlower((wint_t)c);
	}
	return c;
}

bool IsWordChar(char32_t c) {
	// True for letters and digits, used for the hotstring word boundary check
	if (c < 0x80) {
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
	}
	if (c <= 0xFFFF) {
		return iswalnum((wint_t)c) != 0;
	}
	return false;
}

bool GlobMatch(const u32string& pattern, const u32string& text) {
	// Case insensitive match with '*' for any run of characters and '?' for
	// exactly one.  Iterative, with a single backtrack point for the last '*'.
	size_t p = 0, t = 0;
	size_t star = u32string::npos, mark = 0;
	while (t < text.size()) {
		if (p < pattern.size() && (pattern[p] == '?' || FoldCase(pattern[p]) == FoldCase(text[t]))) {
			p++;
			t++;
		}
		else if (p < pattern.size() && pattern[p] == '*') {
			star = p++;
			mark = t;
		}
		else if (star != u32string::npos) {
			p = star + 1;
			t = ++mark;
		}
		else {
			return false;
		}
	}
	while (p < pattern.size() && pattern[p] == '*') {
		p++;
	}
	return p == pattern.size();
}

bool GlobMatch(const string& pattern, const string& text) {
	// UTF-8 convenience wrapper
	return GlobMatch(Utf8ToU32(pattern), Utf8ToU32(text));
}

wstring Utf8ToWide(const string& text) {
	// UTF-16 where wchar_t is 16 bits (Windows), UTF-32 elsewhere
	wstring ret;
	for (char32_t c : Utf8ToU32(text)) {
		if (sizeof(wchar_t) == 2 && c >= 0x10000) {
			c -= 0x10000;
			ret += (wchar_t)(0xD800 + (c >> 10));
			ret += (wchar_t)(0xDC00 + (c & 0x3FF));
		}
		else {
			ret += (wchar_t)c;
		}
	}
	return ret;
}
