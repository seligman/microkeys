#include "WinApp.h"
#include "WinText.h"
#include "../../core/UpdateCore.h"

#define USAGE \
	L"Usage:\n" \
	L"  MicroKeys.exe                     Start MicroKeys\n" \
	L"  MicroKeys.exe run <command> ...   Run commands defined with @command\n" \
	L"\n" \
	L"For tests:\n" \
	L"  MicroKeysApp.exe --headless [--scripts <dir>] [--data <dir>] [--log <file>] [--run <a,b>]"

static bool ParseArgs(AppOptions& options) {
	// The command line, which the launcher passes on unchanged
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	bool ok = true;
	for (int i = 1; ok && i < argc; i++) {
		wstring arg = argv[i];
		bool hasValue = i + 1 < argc;
		if (arg == L"run") {
			ok = hasValue;
			while (i + 1 < argc) {
				options.Commands.push_back(Utf16ToUtf8(argv[++i]));
			}
		}
		else if (arg == L"--headless") {
			options.Headless = true;
		}
		else if (arg == L"--scripts" && hasValue) {
			options.ScriptDir = Utf16ToUtf8(argv[++i]);
		}
		else if (arg == L"--data" && hasValue) {
			options.DataDir = Utf16ToUtf8(argv[++i]);
		}
		else if (arg == L"--log" && hasValue) {
			options.LogFile = Utf16ToUtf8(argv[++i]);
		}
		else if (arg == L"--run" && hasValue) {
			string list = Utf16ToUtf8(argv[++i]);
			size_t start = 0;
			while (start <= list.size()) {
				size_t comma = list.find(',', start);
				string name = list.substr(start, comma == string::npos ? string::npos : comma - start);
				if (!name.empty()) {
					options.Commands.push_back(name);
				}
				if (comma == string::npos) {
					break;
				}
				start = comma + 1;
			}
		}
		else if (arg == L"/updated" && i + 2 < argc) {
			options.UpdatedFrom = UpdateCore_ParsePid(argv[++i]);
			options.UpdatedVersion = Utf16ToUtf8(argv[++i]);
		}
		else if (arg == L"/elevated" && hasValue) {
			options.ElevatedFrom = UpdateCore_ParsePid(argv[++i]);
		}
		else {
			ok = false;
		}
	}
	LocalFree(argv);
	return ok;
}

static void ForwardToRunningCopy(const AppOptions& options) {
	// Another copy is already running: hand it the commands, or bring up its log
	HWND other = FindWindowW(MICROKEYS_WINDOW_CLASS, NULL);
	if (other == NULL) {
		return;
	}
	DWORD pid = 0;
	GetWindowThreadProcessId(other, &pid);
	AllowSetForegroundWindow(pid);
	if (options.Commands.empty()) {
		PostMessageW(other, WM_APP_SHOW, 0, 0);
		return;
	}
	for (auto& cmd : options.Commands) {
		COPYDATASTRUCT cds = { 0 };
		cds.dwData = CDS_RUN_COMMAND;
		cds.cbData = (DWORD)cmd.size();
		cds.lpData = (void*)cmd.c_str();
		DWORD_PTR result;
		SendMessageTimeoutW(other, WM_COPYDATA, 0, (LPARAM)&cds, SMTO_ABORTIFHUNG, 5000, &result);
	}
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
	AppOptions options;
	if (!ParseArgs(options)) {
		MessageBoxW(NULL, USAGE, L"MicroKeys", MB_OK | MB_ICONINFORMATION);
		return 2;
	}

	// Headless runs are for tests, and may run next to a normal copy.  A copy
	// running as administrator holds a mutex an ordinary one can't open.
	if (!options.Headless) {
		_instanceMutex = CreateMutexW(NULL, TRUE, MICROKEYS_MUTEX);
		DWORD err = GetLastError();
		if (_instanceMutex == NULL && err == ERROR_ACCESS_DENIED) {
			ForwardToRunningCopy(options);
			return 0;
		}
		if (err == ERROR_ALREADY_EXISTS) {
			if (options.UpdatedFrom == 0 && options.ElevatedFrom == 0) {
				ForwardToRunningCopy(options);
				return 0;
			}
			// Started by an update, or to run as administrator: the old copy
			// lets go as it hands over
			DWORD wait = WaitForSingleObject(_instanceMutex, 15000);
			if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
				return 1;
			}
		}
	}

	INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
	InitCommonControlsEx(&icc);

	WinApp app;
	if (!app.Init(hInstance, options)) {
		return 1;
	}
	return app.Run();
}
