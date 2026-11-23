#include "WinText.h"

wstring Utf8ToUtf16(const string& text) {
	// Convert, replacing anything malformed with U+FFFD
	if (text.empty()) {
		return L"";
	}
	int len = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), NULL, 0);
	wstring ret(len, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), &ret[0], len);
	return ret;
}

string Utf16ToUtf8(const wchar_t* text, size_t len) {
	// Convert, replacing unpaired surrogates with U+FFFD
	if (len == 0) {
		return "";
	}
	int bytes = WideCharToMultiByte(CP_UTF8, 0, text, (int)len, NULL, 0, NULL, NULL);
	string ret(bytes, '\0');
	WideCharToMultiByte(CP_UTF8, 0, text, (int)len, &ret[0], bytes, NULL, NULL);
	return ret;
}

string Utf16ToUtf8(const wstring& text) {
	return Utf16ToUtf8(text.data(), text.size());
}

string LastErrorText(DWORD error) {
	// The system's message for an error code, without the trailing newline
	wchar_t* buffer = NULL;
	DWORD len = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, error, 0, (LPWSTR)&buffer, 0, NULL);
	string ret;
	if (len > 0 && buffer != NULL) {
		ret = Utf16ToUtf8(buffer, len);
		while (!ret.empty() && (ret.back() == '\n' || ret.back() == '\r' || ret.back() == ' ' || ret.back() == '.')) {
			ret.pop_back();
		}
	}
	if (buffer != NULL) {
		LocalFree(buffer);
	}
	if (ret.empty()) {
		ret = "error " + to_string(error);
	}
	return ret;
}
