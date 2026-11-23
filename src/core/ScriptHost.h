#pragma once

#include "Platform.h"

class HostStartup {
	// What the app tells the script host when it starts
public:
	string AppDir;
	string LibDir;
	string DataDir;
	string DocumentsDir;
	string ScriptDir;
	string PythonZip;
	bool Headless = false;
};

class ScriptJob {
public:
	enum Kind { Reload, Fire, Command, Quit };
	Kind Type = Reload;
	int FireKind = 0;
	int ID = 0;
	string Text;
};

class ScriptHost {
	// Runs CPython on a thread of its own.  The UI thread hands it jobs
	// (reload the scripts, run a hotkey) and never waits on it, apart from
	// shutdown.  Python is initialized, used, and finalized on this thread.
public:
	ScriptHost(IAppShell* shell, IPlatform* platform);
	~ScriptHost();
	void Start(const HostStartup& startup);
	void PostReload();
	void PostFire(int kind, int id, const string& extra);
	void PostCommand(const string& name);
	void Cancel();
	bool IsBusy();
	bool Stop(int timeoutMs);
	bool WaitIdle(int timeoutMs);
	int Failures();

	// Used by the _microkeys module, on the script thread
	bool Sleep(double seconds);
	bool IsCancelled();
	void ClearCancel();
	void EnterNative();
	void LeaveNative();
	IAppShell* Shell;
	IPlatform* Platform;
	HostStartup Startup;
	atomic<bool> CancelFlag;

private:
	void ThreadMain();
	bool InitPython();
	void RunJob(const ScriptJob& job);
	void Post(const ScriptJob& job);
	void LogPythonError(const string& context);

	thread _thread;
	mutex _lock;
	mutex _sleepLock;
	condition_variable _jobReady;
	condition_variable _idle;
	condition_variable _cancelWake;
	deque<ScriptJob> _jobs;
	bool _busy = false;
	bool _running = false;
	bool _finished = false;
	atomic<int> _failures;
	atomic<int> _inNative;
	thread::id _scriptThread;
	void* _runtime = nullptr;
};

extern ScriptHost* _scriptHost;
