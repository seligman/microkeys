#include "framework.h"
#include "../../core/UpdateCore.h"
#include <shlwapi.h>
#include <string>

// MicroKeys.exe: the small launcher at the top of an install.  It starts
// app-<version>\MicroKeysApp.exe for the version named in current.txt (or the
// newest one there is), passing the command line along.  Updates unpack a new
// app-<version> folder next to the old one, so nothing in use is replaced.

static wstring ExeFolder() {
	wchar_t path[MAX_PATH * 2];
	DWORD len = GetModuleFileNameW(NULL, path, MAX_PATH * 2);
	wstring full(path, len);
	return full.substr(0, full.find_last_of(L'\\'));
}

static bool HasApp(const wstring& folder) {
	return GetFileAttributesW((folder + L"\\MicroKeysApp.exe").c_str()) != INVALID_FILE_ATTRIBUTES;
}

static string Narrow(const wstring& text) {
	// Folder names are app-<digits and dots>, so ASCII is enough here
	string ret;
	for (wchar_t c : text) {
		ret += c < 128 ? (char)c : '?';
	}
	return ret;
}

static wstring PickApp(const wstring& root) {
	// current.txt names the version to run; without it, the newest one wins
	HANDLE file = CreateFileW((root + L"\\current.txt").c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL, OPEN_EXISTING, 0, NULL);
	if (file != INVALID_HANDLE_VALUE) {
		char buffer[128] = { 0 };
		DWORD read = 0;
		ReadFile(file, buffer, sizeof(buffer) - 1, &read, NULL);
		CloseHandle(file);
		wstring name;
		for (DWORD i = 0; i < read && buffer[i] > ' '; i++) {
			name += (wchar_t)buffer[i];
		}
		if (name.rfind(L"app-", 0) == 0 && name.find_first_of(L"\\/") == wstring::npos && HasApp(root + L"\\" + name)) {
			return root + L"\\" + name;
		}
	}

	wstring best;
	string bestVersion;
	WIN32_FIND_DATAW fd;
	HANDLE find = FindFirstFileW((root + L"\\app-*").c_str(), &fd);
	if (find != INVALID_HANDLE_VALUE) {
		do {
			if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
				continue;
			}
			string version = Narrow(fd.cFileName + 4);
			if (UpdateCore_IsVersion(version.c_str()) && HasApp(root + L"\\" + fd.cFileName) &&
				(best.empty() || UpdateCore_CompareVersions(version.c_str(), bestVersion.c_str()) > 0)) {
				best = root + L"\\" + fd.cFileName;
				bestVersion = version;
			}
		} while (FindNextFileW(find, &fd));
		FindClose(find);
	}
	return best;
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
	wstring folder = PickApp(ExeFolder());
	if (folder.empty()) {
		MessageBoxW(NULL, L"MicroKeys couldn't find its app folder. Try unpacking the download again.", L"MicroKeys", MB_OK | MB_ICONERROR);
		return 1;
	}
	wstring exe = folder + L"\\MicroKeysApp.exe";
	wstring cmd = L"\"" + exe + L"\" " + PathGetArgsW(GetCommandLineW());
	STARTUPINFOW si = { sizeof(si) };
	PROCESS_INFORMATION pi = { 0 };
	if (!CreateProcessW(exe.c_str(), &cmd[0], NULL, NULL, FALSE, 0, NULL, folder.c_str(), &si, &pi)) {
		MessageBoxW(NULL, (L"MicroKeys couldn't start " + exe).c_str(), L"MicroKeys", MB_OK | MB_ICONERROR);
		return 1;
	}
	// Let the app take the foreground, since the user just started it
	AllowSetForegroundWindow(pi.dwProcessId);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return 0;
}
