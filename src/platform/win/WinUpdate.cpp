#include "WinUpdate.h"
#include "WinApp.h"
#include "WinText.h"
#include "../../core/UpdateCore.h"
#include <winhttp.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>

#define UPDATE_AVAILABLE	1
#define UPDATE_NONE			2
#define UPDATE_ERROR		3
#define UPDATE_DOWNLOADED	4

#define MAX_FEED_BYTES		(64 * 1024)
#define MAX_ZIP_BYTES		(200 * 1024 * 1024)
#define CHECK_EVERY_SECONDS	(24 * 60 * 60)

namespace fs = std::filesystem;

#if defined(_M_ARM64)
#define MK_ARCH_KEY "arm64"
#define MK_ARCH_MACHINE IMAGE_FILE_MACHINE_ARM64
#else
#define MK_ARCH_KEY "x64"
#define MK_ARCH_MACHINE IMAGE_FILE_MACHINE_AMD64
#endif

static const char* PreferredArch(const string& json) {
	// An x64 copy running on an ARM64 PC moves to the native build once the
	// feed has one
	USHORT process = 0, native = 0;
	if (IsWow64Process2(GetCurrentProcess(), &process, &native) && native == IMAGE_FILE_MACHINE_ARM64 &&
		json.find("\"url_arm64\"") != string::npos) {
		return "arm64";
	}
	return MK_ARCH_KEY;
}

static bool HttpGet(const string& url, size_t maxBytes, string& body, string& error) {
	// A plain HTTPS GET.  Redirects aren't followed, since the feed's host
	// rules only cover the URLs it names.
	wstring wurl = Utf8ToUtf16(url);
	URL_COMPONENTS uc = { sizeof(uc) };
	wchar_t host[256] = { 0 };
	wchar_t path[2048] = { 0 };
	wchar_t extra[1024] = { 0 };
	uc.lpszHostName = host;
	uc.dwHostNameLength = 256;
	uc.lpszUrlPath = path;
	uc.dwUrlPathLength = 2048;
	uc.lpszExtraInfo = extra;
	uc.dwExtraInfoLength = 1024;
	if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) {
		error = "not an https URL: " + url;
		return false;
	}

	wstring agent = L"MicroKeys/" _T(VERSION_TEXT);
	HINTERNET session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (session == NULL) {
		session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	}
	HINTERNET connect = session ? WinHttpConnect(session, host, uc.nPort, 0) : NULL;
	wstring object = wstring(path) + extra;
	HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", object.c_str(), NULL, WINHTTP_NO_REFERER,
		WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : NULL;
	bool ok = false;
	if (request != NULL) {
		WinHttpSetTimeouts(session, 10000, 10000, 15000, 30000);
		DWORD feature = WINHTTP_DISABLE_REDIRECTS;
		WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &feature, sizeof(feature));
		DWORD status = 0, size = sizeof(status);
		if (WinHttpSendRequest(request, L"Cache-Control: no-cache\r\n", (DWORD)-1, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
			WinHttpReceiveResponse(request, NULL) &&
			WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) {
			if (status != 200) {
				error = "the server answered " + to_string(status);
			}
			else {
				ok = true;
				DWORD avail = 0;
				while (ok && WinHttpQueryDataAvailable(request, &avail) && avail > 0) {
					size_t at = body.size();
					if (at + avail > maxBytes) {
						error = "the download is larger than expected";
						ok = false;
						break;
					}
					body.resize(at + avail);
					DWORD got = 0;
					if (!WinHttpReadData(request, &body[at], avail, &got)) {
						ok = false;
					}
					body.resize(at + got);
				}
			}
		}
	}
	if (!ok && error.empty()) {
		error = LastErrorText();
	}
	if (request) {
		WinHttpCloseHandle(request);
	}
	if (connect) {
		WinHttpCloseHandle(connect);
	}
	if (session) {
		WinHttpCloseHandle(session);
	}
	return ok;
}

static string Sha256Hex(const string& data) {
	// SHA-256 through CNG, as lowercase hex
	BCRYPT_ALG_HANDLE alg = NULL;
	BCRYPT_HASH_HANDLE hash = NULL;
	unsigned char digest[32];
	string ret;
	if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) != 0) {
		return ret;
	}
	if (BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0) == 0) {
		if (BCryptHashData(hash, (PUCHAR)data.data(), (ULONG)data.size(), 0) == 0 &&
			BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0) {
			static const char* hex = "0123456789abcdef";
			for (unsigned char b : digest) {
				ret += hex[b >> 4];
				ret += hex[b & 15];
			}
		}
		BCryptDestroyHash(hash);
	}
	BCryptCloseAlgorithmProvider(alg, 0);
	return ret;
}

static string ReadText(const fs::path& fn) {
	ifstream f(fn, ios::binary);
	string ret((istreambuf_iterator<char>(f)), istreambuf_iterator<char>());
	while (!ret.empty() && (ret.back() == '\n' || ret.back() == '\r' || ret.back() == ' ')) {
		ret.pop_back();
	}
	return ret;
}

static void WriteText(const fs::path& fn, const string& text) {
	ofstream f(fn, ios::binary | ios::trunc);
	f << text << "\n";
}

static string EnvVar(const wchar_t* name) {
	wchar_t buffer[1024];
	DWORD len = GetEnvironmentVariableW(name, buffer, 1024);
	return len > 0 && len < 1024 ? Utf16ToUtf8(buffer) : "";
}

Updater::Updater(WinApp* app) {
	_app = app;
}

Updater::~Updater() {
	if (_thread.joinable()) {
		_thread.detach();
	}
}

void Updater::Configure(const string& feed, bool checkDaily) {
	// The feed comes from the release's defaults.json (or a settings
	// override), never from the source, so builds without one don't update
	_feed = feed;
	if (!_feed.empty() && _feed.back() != '/') {
		_feed += "/";
	}
	_daily = checkDaily;
	if (HasFeed() && _daily) {
		SetTimer(_app->Window(), TIMER_UPDATE_CHECK, 30 * 1000, NULL);
	}
}

bool Updater::HasFeed() {
	return !_feed.empty() || !EnvVar(L"MICROKEYS_TEST_UPDATE_FILE").empty();
}

bool Updater::DailyCheckDue() {
	fs::path fn = fs::u8path(_app->DataDir()) / "last_update_check.txt";
	string last = ReadText(fn);
	time_t now = time(NULL);
	return last.empty() || now - (time_t)_atoi64(last.c_str()) >= CHECK_EVERY_SECONDS;
}

void Updater::RecordCheck() {
	WriteText(fs::u8path(_app->DataDir()) / "last_update_check.txt", to_string((long long)time(NULL)));
}

void Updater::Check(bool manual) {
	if (_busy || !HasFeed() || (!manual && !DailyCheckDue())) {
		return;
	}
	_busy = true;
	if (_thread.joinable()) {
		_thread.join();
	}
	_thread = thread(&Updater::CheckThread, this, manual);
}

void Updater::Post(int what, const string& text) {
	auto payload = new string(text);
	if (!PostMessageW(_app->Window(), WM_APP_UPDATE, what, (LPARAM)payload)) {
		delete payload;
	}
}

void Updater::CheckThread(bool manual) {
	// Read latest.json and decide if there's something newer for us
	string test = EnvVar(L"MICROKEYS_TEST_UPDATE_FILE");
	if (!test.empty()) {
		// A local zip stands in for the download, no network involved
		ifstream f(fs::u8path(test), ios::binary);
		string bytes((istreambuf_iterator<char>(f)), istreambuf_iterator<char>());
		string ver = EnvVar(L"MICROKEYS_TEST_UPDATE_VERSION");
		_version = UpdateCore_IsVersion(ver.c_str()) ? ver : "9.9.9";
		if (UpdateCore_CompareVersions(_version.c_str(), VERSION_TEXT) <= 0) {
			Post(UPDATE_NONE, manual ? "You have the latest version of MicroKeys (" VERSION_TEXT ")." : "");
			return;
		}
		_url = test;
		_size = (unsigned long)bytes.size();
		_sha256 = Sha256Hex(bytes);
		_notes = "A test update from " + test;
		Post(UPDATE_AVAILABLE, (manual ? "1" : "0"));
		return;
	}

	UPDATECOREFEED feed = { 0 };
	feed.pszDefaultUrl = _feed.c_str();
	string url = string(UpdateCore_FeedUrl(&feed)) + "latest.json";
	string json, error;
	if (!HttpGet(url, MAX_FEED_BYTES, json, error)) {
		Post(manual ? UPDATE_ERROR : UPDATE_NONE, "Unable to check for updates: " + error);
		return;
	}
	RecordCheck();
	UPDATECOREENTRY entry;
	int got = UpdateCore_ReadEntry(json.c_str(), PreferredArch(json), &entry);
	if (got == UPDATECORE_ENTRY_BADFORMAT) {
		Post(manual ? UPDATE_ERROR : UPDATE_NONE, "The update feed isn't in the expected format");
		return;
	}
	string skipped = ReadText(fs::u8path(_app->DataDir()) / "skipped_version.txt");
	if (got != UPDATECORE_ENTRY_OK || entry.size == 0 || UpdateCore_CompareVersions(entry.version, VERSION_TEXT) <= 0 ||
		(!manual && skipped == entry.version)) {
		Post(UPDATE_NONE, manual ? "You have the latest version of MicroKeys (" VERSION_TEXT ")." : "");
		return;
	}
	_version = entry.version;
	_url = entry.url;
	_sha256 = entry.sha256;
	_size = entry.size;
	_notes = entry.notes;
	Post(UPDATE_AVAILABLE, manual ? "1" : "0");
}

void Updater::DownloadThread() {
	// Download, check, and unpack into <install>\app-<version>
	string zip, error;
	string test = EnvVar(L"MICROKEYS_TEST_UPDATE_FILE");
	if (!test.empty()) {
		ifstream f(fs::u8path(test), ios::binary);
		zip.assign((istreambuf_iterator<char>(f)), istreambuf_iterator<char>());
	}
	else {
		UPDATECOREFEED feed = { 0 };
		feed.pszDefaultUrl = _feed.c_str();
		if (!UpdateCore_FileHostAllowed(&feed, _url.c_str())) {
			Post(UPDATE_ERROR, "The update is on a host the feed doesn't allow: " + _url);
			return;
		}
		if (!HttpGet(_url, MAX_ZIP_BYTES, zip, error)) {
			Post(UPDATE_ERROR, "Unable to download the update: " + error);
			return;
		}
	}
	if (zip.size() != _size || Sha256Hex(zip) != _sha256) {
		Post(UPDATE_ERROR, "The download doesn't match the size and SHA-256 the feed gives, so it wasn't used.");
		return;
	}

	fs::path install = fs::u8path(_app->InstallDir());
	fs::path zipPath = install / ("update-" + _version + ".zip");
	fs::path tmp = install / ("update-" + _version + ".tmp");
	fs::path target = install / ("app-" + _version);
	error_code ec;
	fs::remove_all(tmp, ec);
	fs::create_directories(tmp, ec);
	{
		ofstream f(zipPath, ios::binary | ios::trunc);
		f.write(zip.data(), zip.size());
		if (!f) {
			Post(UPDATE_ERROR, "Unable to save the update to " + zipPath.u8string());
			return;
		}
	}

	// tar.exe ships with Windows 10 1803 and later, and reads zip files
	wchar_t system[MAX_PATH];
	GetSystemDirectoryW(system, MAX_PATH);
	wstring cmd = L"\"" + wstring(system) + L"\\tar.exe\" -xf \"" + zipPath.wstring() + L"\" -C \"" + tmp.wstring() + L"\"";
	STARTUPINFOW si = { sizeof(si) };
	PROCESS_INFORMATION pi = { 0 };
	DWORD exitCode = 1;
	if (CreateProcessW(NULL, &cmd[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
		WaitForSingleObject(pi.hProcess, 120000);
		GetExitCodeProcess(pi.hProcess, &exitCode);
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
	}
	fs::path unpacked = tmp / "MicroKeys" / ("app-" + _version);
	if (exitCode != 0 || !fs::exists(unpacked / "MicroKeysApp.exe")) {
		fs::remove_all(tmp, ec);
		fs::remove(zipPath, ec);
		Post(UPDATE_ERROR, "The update couldn't be unpacked");
		return;
	}

	fs::remove_all(target, ec);
	fs::rename(unpacked, target, ec);
	if (ec) {
		Post(UPDATE_ERROR, "Unable to put the update in place: " + ec.message());
		return;
	}

	// The launcher rarely changes, but when it does it's swapped the way
	// Notepad 2 swaps its exe: the running one is renamed out of the way
	fs::path newLauncher = tmp / "MicroKeys" / "MicroKeys.exe";
	fs::path launcher = install / "MicroKeys.exe";
	if (fs::exists(newLauncher) && (!fs::exists(launcher) || ReadText(newLauncher) != ReadText(launcher))) {
		fs::path old = install / "MicroKeys.exe.old";
		fs::remove(old, ec);
		if (fs::exists(launcher)) {
			fs::rename(launcher, old, ec);
		}
		fs::rename(newLauncher, launcher, ec);
	}
	fs::remove_all(tmp, ec);
	fs::remove(zipPath, ec);
	Post(UPDATE_DOWNLOADED, "");
}

void Updater::OnMessage(WPARAM wParam, LPARAM lParam) {
	// Results from the check and download threads, on the UI thread
	unique_ptr<string> text((string*)lParam);
	HWND hwnd = _app->Window();
	switch (wParam) {
	case UPDATE_AVAILABLE: {
		if (*text != "1") {
			// A check nobody asked for shouldn't pop up a dialog in the middle of
			// someone's work, so just say so; the tray menu does the rest
			_busy = false;
			_app->Log(LOG_INFO, "MicroKeys " + _version + " is available");
			_app->ShowToast("MicroKeys " + _version + " is available", "Choose Check for updates in the tray menu to install it.");
			return;
		}
		wstring msg = Utf8ToUtf16("MicroKeys " + _version + " is available (you have " VERSION_TEXT ").\n\n" +
			(_notes.empty() ? "" : _notes + "\n\n") +
			"Yes: update now\nNo: skip this version\nCancel: ask again later");
		int answer = MessageBoxW(hwnd, msg.c_str(), L"MicroKeys update", MB_YESNOCANCEL | MB_ICONINFORMATION | MB_SETFOREGROUND);
		if (answer == IDYES) {
			_app->Log(LOG_INFO, "Downloading MicroKeys " + _version);
			if (_thread.joinable()) {
				_thread.join();
			}
			_thread = thread(&Updater::DownloadThread, this);
			return;
		}
		if (answer == IDNO) {
			WriteText(fs::u8path(_app->DataDir()) / "skipped_version.txt", _version);
		}
		_busy = false;
		break;
	}
	case UPDATE_NONE:
		_busy = false;
		if (!text->empty()) {
			_app->Log(LOG_INFO, *text);
			if (text->rfind("Unable", 0) != 0) {
				MessageBoxW(hwnd, Utf8ToUtf16(*text).c_str(), L"MicroKeys update", MB_OK | MB_ICONINFORMATION);
			}
		}
		break;
	case UPDATE_ERROR:
		_busy = false;
		_app->Log(LOG_ERROR, *text);
		MessageBoxW(hwnd, Utf8ToUtf16(*text).c_str(), L"MicroKeys update", MB_OK | MB_ICONWARNING);
		break;
	case UPDATE_DOWNLOADED:
		Install();
		break;
	}
}

void Updater::Install() {
	// Hand over to the new version with UpdateCore's start handshake: let go
	// of everything, start it, and wait for it to say it's up
	fs::path install = fs::u8path(_app->InstallDir());
	fs::path exe = install / ("app-" + _version) / "MicroKeysApp.exe";
	wchar_t eventName[64];
	UpdateCore_EventName(GetCurrentProcessId(), eventName, 64);
	HANDLE started = CreateEventW(NULL, TRUE, FALSE, eventName);

	_app->Log(LOG_INFO, "Starting MicroKeys " + _version);
	_app->Suspend();

	wstring cmd = L"\"" + exe.wstring() + L"\" /updated " + to_wstring(GetCurrentProcessId()) + L" " _T(VERSION_TEXT);
	STARTUPINFOW si = { sizeof(si) };
	PROCESS_INFORMATION pi = { 0 };
	int verdict = UPDATECORE_WAIT_FAILED;
	// In its own folder, so nothing keeps the old version's folder in use
	wstring folder = exe.parent_path().wstring();
	if (started != NULL && CreateProcessW(exe.c_str(), &cmd[0], NULL, NULL, FALSE, 0, NULL, folder.c_str(), &si, &pi)) {
		ULONGLONG until = GetTickCount64() + UPDATECORE_START_TIMEOUT_MS;
		HANDLE handles[2] = { started, pi.hProcess };
		do {
			DWORD wait = MsgWaitForMultipleObjects(2, handles, FALSE, 100, QS_ALLINPUT);
			MSG msg;
			while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
				if (msg.message != WM_QUIT) {
					TranslateMessage(&msg);
					DispatchMessageW(&msg);
				}
			}
			verdict = UpdateCore_WaitVerdict(WaitForSingleObject(started, 0) == WAIT_OBJECT_0,
				WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0, GetTickCount64() >= until);
		} while (verdict == UPDATECORE_WAIT_GOING);
		if (verdict != UPDATECORE_WAIT_STARTED) {
			TerminateProcess(pi.hProcess, 1);
		}
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
	}

	if (verdict == UPDATECORE_WAIT_STARTED) {
		WriteText(install / "current.txt", "app-" + _version);
		ExitProcess(0);
	}

	// It didn't start: stay on this version, and don't offer that one again
	string self = fs::u8path(_app->AppDir()).filename().u8string();
	WriteText(install / "current.txt", self);
	WriteText(fs::u8path(_app->DataDir()) / "skipped_version.txt", _version);
	error_code ec;
	fs::remove_all(install / ("app-" + _version), ec);
	MessageBoxW(NULL, Utf8ToUtf16("MicroKeys " + _version + " didn't start, so MicroKeys " VERSION_TEXT " will keep running.").c_str(),
		L"MicroKeys update", MB_OK | MB_ICONWARNING);
	_app->Resume();
}

void Updater::Startup(DWORD updatedFrom, const string& updatedVersion) {
	// In a new version started by an update: tell the old one we're up, then
	// tidy away what the old versions left behind
	if (updatedFrom != 0) {
		wchar_t eventName[64];
		if (UpdateCore_EventName(updatedFrom, eventName, 64) > 0) {
			HANDLE started = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName);
			if (started != NULL) {
				SetEvent(started);
				CloseHandle(started);
			}
		}
		_app->Log(LOG_INFO, "Updated from MicroKeys " + updatedVersion + " to " VERSION_TEXT);
		_app->ShowToast("MicroKeys updated", "Now running version " VERSION_TEXT);
	}
	DWORD waitFor = updatedFrom;
	string install = _app->InstallDir();
	string self = fs::u8path(_app->AppDir()).filename().u8string();
	thread([waitFor, install, self] {
		if (waitFor != 0) {
			HANDLE old = OpenProcess(SYNCHRONIZE, FALSE, waitFor);
			if (old != NULL) {
				WaitForSingleObject(old, 30000);
				CloseHandle(old);
			}
		}
		// Only in a real install, where this exe sits in an app-<version> folder
		if (self.rfind("app-", 0) != 0) {
			return;
		}
		error_code ec;
		fs::path root = fs::u8path(install);
		for (auto& entry : fs::directory_iterator(root, ec)) {
			string name = entry.path().filename().u8string();
			bool oldApp = name.rfind("app-", 0) == 0 && entry.is_directory() &&
				UpdateCore_CompareVersions(name.c_str() + 4, self.c_str() + 4) < 0;
			bool leftover = name.rfind("update-", 0) == 0 || name == "MicroKeys.exe.old";
			if (oldApp || leftover) {
				fs::remove_all(entry.path(), ec);
			}
		}
	}).detach();
}
