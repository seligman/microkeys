#pragma once

#include "framework.h"
#include "WinPlatform.h"
#include "../../core/Triggers.h"
#include "../../core/ScriptHost.h"

#define MICROKEYS_MUTEX			L"MicroKeys_nZ3SC4MxQsEfQlmmWhc2WaELCZrGdam1IGrOh7XL"
#define MICROKEYS_WINDOW_CLASS	L"MicroKeysMainWindow"
#define CDS_RUN_COMMAND			1001

#define WM_APP_LOG				(WM_APP + 1)
#define WM_APP_BINDINGS			(WM_APP + 2)
#define WM_APP_CONFIGURE		(WM_APP + 3)
#define WM_APP_LOADED			(WM_APP + 4)
#define WM_APP_TOAST			(WM_APP + 5)
#define WM_APP_TRAY				(WM_APP + 6)
#define WM_APP_SCRIPTS_CHANGED	(WM_APP + 7)
#define WM_APP_MACRO			(WM_APP + 8)
#define WM_APP_SHOW				(WM_APP + 9)
#define WM_APP_HEADLESS_DONE	(WM_APP + 10)
#define WM_APP_UPDATE			(WM_APP + 11)
#define WM_APP_CONSOLE			(WM_APP + 12)
#define WM_APP_OVERLAY			(WM_APP + 13)

#define TIMER_RELOAD			101
#define TIMER_BUSY_ICON			102
#define TIMER_UPDATE_CHECK		103
#define TIMER_FOREGROUND		104

#define ABORT_HOTKEY_ID			0xBFF0

class Updater;
class Overlay;

class AppOptions {
	// What the command line asked for
public:
	bool Headless = false;
	string ScriptDir;
	string DataDir;
	string LogFile;
	vector<string> Commands;
	DWORD UpdatedFrom = 0;
	string UpdatedVersion;
};

class WinApp : public IAppShell, public ITriggerSink {
	// The Windows app: tray icon, log window, hotkeys, hooks, and the script
	// host.  Lives on the UI thread; the IAppShell methods may be called from
	// the script thread and post their work here.
public:
	WinApp();
	~WinApp();
	bool Init(HINSTANCE hInstance, const AppOptions& options);
	int Run();

	// IAppShell
	void Log(int level, const string& text) override;
	void SetBindings(const Bindings& bindings) override;
	void Configure(const ShellConfig& config) override;
	void ScriptsLoaded(bool ok, const string& summary) override;
	void MacroState(const string& running) override;
	void ConsoleState(bool more) override;

	// ITriggerSink
	bool RegisterHotkey(int id, int mods, const string& key, string& error) override;
	void UnregisterHotkey(int id) override;
	void Fire(int kind, int id, const string& extra) override;
	void TriggerLog(int level, const string& text) override;

	// Called from the static window, hook, and thread procs
	LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	void OnKeyDown(const KBDLLHOOKSTRUCT* kb);
	void OnMouseButton();
	void OnWinEvent(DWORD event, HWND hwnd, LONG idObject, LONG idChild);
	bool OnConsoleKey(UINT vk);

	// For the updater
	void ShowToast(const string& title, const string& text);
	bool Suspend();
	void Resume();
	void ExitApp();
	HWND Window();
	string InstallDir();
	string AppDir();
	string DataDir();

private:
	void CreateMainWindow();
	void AddTrayIcon();
	void UpdateTray();
	void ShowTrayMenu(int x, int y);
	void ShowLog();
	void AppendLogText();
	void FormatLogText(LONG start, const wstring& text);
	void OpenLink(const wstring& text);
	void OpenAt(const string& file, int line);
	void ApplyFont();
	void Layout();
	void OnCommand(int id);
	void ApplyConfig(const ShellConfig& config);
	void UpdateHooks();
	void StartWatcher(const string& dir);
	void StopWatcher();
	void WatchThread(wstring dir, HANDLE stop);
	void RefreshForeground(bool newWindow);
	void OnWindowShown(HWND hwnd);
	void Abort();
	bool PassAltGr(UINT mods, UINT vk);
	bool RunAtLogin();
	void SetRunAtLogin(bool enable);
	void StartHost();
	void ShowOverlay(const string& text, int timeoutMs);
	void WriteLogFile(const string& text);
	HICON MakeBusyIcon();

	AppOptions _options;
	HINSTANCE _hInst = NULL;
	HWND _hWnd = NULL;
	HWND _hEdit = NULL;
	HWND _hPrompt = NULL;
	HWND _hInput = NULL;
	HFONT _font = NULL;
	HICON _icon = NULL;
	HICON _busyIcon = NULL;
	NOTIFYICONDATAW _nid = { 0 };
	bool _trayAdded = false;
	UINT _taskbarCreated = 0;
	bool _exiting = false;
	bool _suspended = false;

	WinPlatform _platform;
	unique_ptr<ScriptHost> _host;
	unique_ptr<Triggers> _triggers;
	unique_ptr<Updater> _updater;
	unique_ptr<Overlay> _overlay;
	WindowInfo _foreground;
	HKL _foregroundLayout = NULL;
	HHOOK _keyboardHook = NULL;
	HHOOK _mouseHook = NULL;
	HWINEVENTHOOK _foregroundHook = NULL;
	HWINEVENTHOOK _titleHook = NULL;
	HWINEVENTHOOK _showHook = NULL;
	HWINEVENTHOOK _uncloakHook = NULL;
	set<HWND> _knownWindows;
	bool _clipboardListener = false;

	mutex _logLock;
	deque<string> _logQueue;
	HANDLE _logFile = INVALID_HANDLE_VALUE;
	string _logPath;
	HANDLE _console = NULL;
	bool _logInError = false;
	vector<wstring> _consoleHistory;
	size_t _historyPos = 0;

	ShellConfig _config;
	bool _configured = false;
	string _running;
	bool _lastLoadOk = true;
	string _lastSummary;

	thread _watcher;
	HANDLE _watchStop = NULL;
	string _watchDir;

	string _appDir;
	string _installDir;
	string _dataDir;
	string _localDir;
	string _docsDir;
};

extern WinApp* _app;
extern HANDLE _instanceMutex;
string KnownFolder(REFKNOWNFOLDERID id);
string ExeDir();
