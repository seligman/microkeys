#include "PythonInclude.h"
#include "ScriptHost.h"
#include "Text.h"
#include <signal.h>
#include <chrono>

#define MAX_PENDING_JOBS	16

ScriptHost* _scriptHost = nullptr;
PyMODINIT_FUNC PyInit__microkeys();

ScriptHost::ScriptHost(IAppShell* shell, IPlatform* platform) {
	Shell = shell;
	Platform = platform;
	CancelFlag = false;
	_failures = 0;
	_inNative = 0;
	_scriptHost = this;
}

ScriptHost::~ScriptHost() {
	// If Stop() timed out the thread is stuck in a macro; let the process end it
	if (_thread.joinable()) {
		_thread.detach();
	}
}

void ScriptHost::Start(const HostStartup& startup) {
	// Spin up the script thread, which loads Python and then the scripts
	Startup = startup;
	_running = true;
	_busy = true;
	_thread = thread(&ScriptHost::ThreadMain, this);
}

void ScriptHost::Post(const ScriptJob& job) {
	// Queue a job for the script thread
	bool dropped = false;
	{
		lock_guard<mutex> guard(_lock);
		if (_finished) {
			return;
		}
		if (job.Type == ScriptJob::Reload) {
			for (auto& cur : _jobs) {
				if (cur.Type == ScriptJob::Reload) {
					return;
				}
			}
		}
		if (job.Type == ScriptJob::Fire && _jobs.size() >= MAX_PENDING_JOBS) {
			dropped = true;
		}
		else {
			_jobs.push_back(job);
		}
	}
	if (dropped) {
		Shell->Log(LOG_ERROR, "Too many macros are waiting to run, ignoring this one");
		return;
	}
	_jobReady.notify_one();
}

void ScriptHost::PostReload() {
	ScriptJob job;
	job.Type = ScriptJob::Reload;
	Post(job);
}

void ScriptHost::PostFire(int kind, int id, const string& extra) {
	ScriptJob job;
	job.Type = ScriptJob::Fire;
	job.FireKind = kind;
	job.ID = id;
	job.Text = extra;
	Post(job);
}

void ScriptHost::PostCommand(const string& name) {
	ScriptJob job;
	job.Type = ScriptJob::Command;
	job.Text = name;
	Post(job);
}

void ScriptHost::Cancel() {
	// Stop whatever is running, and anything waiting behind it.  The flag
	// stops our own long calls (typing, sleep), the interrupt stops Python
	// code, including time.sleep().
	{
		lock_guard<mutex> guard(_lock);
		for (auto it = _jobs.begin(); it != _jobs.end();) {
			if (it->Type == ScriptJob::Fire || it->Type == ScriptJob::Command) {
				it = _jobs.erase(it);
			}
			else {
				++it;
			}
		}
		if (!_busy) {
			return;
		}
	}
	{
		lock_guard<mutex> guard(_sleepLock);
		CancelFlag = true;
	}
	_cancelWake.notify_all();
	// Waits in our own code watch CancelFlag, and an interrupt on top of that
	// would land a second KeyboardInterrupt in the code handling the first
	if (Py_IsInitialized() && _inNative == 0) {
		PyErr_SetInterruptEx(SIGINT);
	}
}

bool ScriptHost::IsBusy() {
	lock_guard<mutex> guard(_lock);
	return _busy;
}

bool ScriptHost::WaitIdle(int timeoutMs) {
	// Wait until the queue is empty and nothing is running.  A negative
	// timeout waits for as long as it takes.
	unique_lock<mutex> guard(_lock);
	auto idle = [this] { return _finished || (!_busy && _jobs.empty()); };
	if (timeoutMs < 0) {
		_idle.wait(guard, idle);
		return true;
	}
	return _idle.wait_for(guard, chrono::milliseconds(timeoutMs), idle);
}

bool ScriptHost::Stop(int timeoutMs) {
	// Ask the script thread to finalize Python and end.  Returns false if a
	// macro wouldn't stop in time.
	if (!_running) {
		return true;
	}
	Cancel();
	ScriptJob job;
	job.Type = ScriptJob::Quit;
	{
		lock_guard<mutex> guard(_lock);
		_jobs.clear();
		_jobs.push_back(job);
	}
	_jobReady.notify_one();
	bool done;
	{
		unique_lock<mutex> guard(_lock);
		done = _idle.wait_for(guard, chrono::milliseconds(timeoutMs), [this] { return _finished; });
	}
	if (done && _thread.joinable()) {
		_thread.join();
	}
	_running = !done;
	return done;
}

int ScriptHost::Failures() {
	return _failures;
}

bool ScriptHost::Sleep(double seconds) {
	// Sleep that wakes early when the macro is cancelled.  False if cancelled.
	unique_lock<mutex> guard(_sleepLock);
	auto until = chrono::steady_clock::now() + chrono::duration_cast<chrono::steady_clock::duration>(chrono::duration<double>(seconds));
	_cancelWake.wait_until(guard, until, [this] { return CancelFlag.load(); });
	return !CancelFlag;
}

void ScriptHost::EnterNative() {
	// The script thread is about to wait in our code, where CancelFlag is seen
	if (this_thread::get_id() == _scriptThread) {
		_inNative++;
	}
}

void ScriptHost::LeaveNative() {
	if (this_thread::get_id() == _scriptThread) {
		_inNative--;
	}
}

bool ScriptHost::IsCancelled() {
	return CancelFlag;
}

void ScriptHost::ClearCancel() {
	CancelFlag = false;
}

void ScriptHost::ThreadMain() {
	// The script thread: Python lives here from start to finish
	_scriptThread = this_thread::get_id();
	bool ready = InitPython();
	PyThreadState* save = ready ? PyEval_SaveThread() : nullptr;

	while (true) {
		ScriptJob job;
		{
			unique_lock<mutex> guard(_lock);
			_busy = false;
			_idle.notify_all();
			_jobReady.wait(guard, [this] { return !_jobs.empty(); });
			job = _jobs.front();
			_jobs.pop_front();
			_busy = job.Type != ScriptJob::Quit;
		}
		if (job.Type == ScriptJob::Quit) {
			break;
		}
		if (!ready) {
			_failures++;
			continue;
		}
		PyEval_RestoreThread(save);
		CancelFlag = false;
		// An interrupt aimed at a macro that already finished shouldn't hit this one
		if (PyErr_CheckSignals() != 0) {
			PyErr_Clear();
		}
		RunJob(job);
		if (PyErr_CheckSignals() != 0) {
			PyErr_Clear();
		}
		save = PyEval_SaveThread();
	}

	if (ready) {
		PyEval_RestoreThread(save);
		PyObject* ret = PyObject_CallMethod((PyObject*)_runtime, "shutdown", NULL);
		if (ret == NULL) {
			PyErr_Clear();
		}
		Py_XDECREF(ret);
		Py_XDECREF((PyObject*)_runtime);
		_runtime = nullptr;
		Py_FinalizeEx();
	}

	{
		lock_guard<mutex> guard(_lock);
		_finished = true;
		_busy = false;
	}
	_idle.notify_all();
}

static PyStatus AddPath(PyConfig* config, const string& path) {
	// Helper to add one folder or zip to sys.path
	return PyWideStringList_Append(&config->module_search_paths, Utf8ToWide(path).c_str());
}

bool ScriptHost::InitPython() {
	// Start an isolated interpreter that only sees the runtime we ship and
	// our own package, then hand control to microkeys._runtime
	PyPreConfig pre;
	PyPreConfig_InitIsolatedConfig(&pre);
	pre.utf8_mode = 1;
	PyStatus status = Py_PreInitialize(&pre);
	if (PyStatus_Exception(status)) {
		Shell->Log(LOG_ERROR, string("Unable to start Python: ") + (status.err_msg ? status.err_msg : "unknown error"));
		return false;
	}

	PyImport_AppendInittab("_microkeys", PyInit__microkeys);

	PyConfig config;
	PyConfig_InitIsolatedConfig(&config);
	config.install_signal_handlers = 1;
	config.write_bytecode = 0;
	config.module_search_paths_set = 1;
	status = PyConfig_SetString(&config, &config.home, Utf8ToWide(Startup.AppDir).c_str());
	if (!PyStatus_Exception(status)) {
		status = PyConfig_SetString(&config, &config.program_name, L"MicroKeys");
	}
	if (!PyStatus_Exception(status)) {
		status = AddPath(&config, Startup.PythonZip);
	}
	if (!PyStatus_Exception(status)) {
		status = AddPath(&config, Startup.AppDir);
	}
	if (!PyStatus_Exception(status)) {
		status = AddPath(&config, Startup.LibDir);
	}
	if (!PyStatus_Exception(status)) {
		status = Py_InitializeFromConfig(&config);
	}
	PyConfig_Clear(&config);
	if (PyStatus_Exception(status)) {
		Shell->Log(LOG_ERROR, string("Unable to start Python: ") + (status.err_msg ? status.err_msg : "unknown error"));
		return false;
	}

	PyObject* runtime = PyImport_ImportModule("microkeys._runtime");
	if (runtime == NULL) {
		LogPythonError("Unable to load the microkeys package");
		Py_FinalizeEx();
		return false;
	}
	_runtime = runtime;

	PyObject* config_dict = Py_BuildValue("{s:s,s:s,s:s,s:s,s:s,s:O}",
		"app_dir", Startup.AppDir.c_str(),
		"lib_dir", Startup.LibDir.c_str(),
		"data_dir", Startup.DataDir.c_str(),
		"documents_dir", Startup.DocumentsDir.c_str(),
		"script_dir", Startup.ScriptDir.c_str(),
		"headless", Startup.Headless ? Py_True : Py_False);
	PyObject* ret = config_dict ? PyObject_CallMethod(runtime, "startup", "O", config_dict) : NULL;
	Py_XDECREF(config_dict);
	if (ret == NULL) {
		LogPythonError("Unable to start the script runtime");
		_failures++;
	}
	Py_XDECREF(ret);
	return true;
}

void ScriptHost::RunJob(const ScriptJob& job) {
	// Hand one job to microkeys._runtime, which handles its own errors.  It
	// returns False when something the user should know about failed.
	PyObject* runtime = (PyObject*)_runtime;
	PyObject* ret = NULL;
	switch (job.Type) {
	case ScriptJob::Reload:
		ret = PyObject_CallMethod(runtime, "reload", NULL);
		break;
	case ScriptJob::Fire:
		ret = PyObject_CallMethod(runtime, "fire", "iis", job.FireKind, job.ID, job.Text.c_str());
		break;
	case ScriptJob::Command:
		ret = PyObject_CallMethod(runtime, "run_command", "s", job.Text.c_str());
		break;
	default:
		return;
	}
	if (ret == NULL) {
		LogPythonError("Internal error in the script runtime");
		_failures++;
	}
	else {
		if (ret == Py_False) {
			_failures++;
		}
		Py_DECREF(ret);
	}
}

void ScriptHost::LogPythonError(const string& context) {
	// Log the pending Python exception with its traceback
	PyObject* exc = PyErr_GetRaisedException();
	string text = context;
	if (exc != NULL) {
		PyObject* tb = PyImport_ImportModule("traceback");
		PyObject* lines = tb ? PyObject_CallMethod(tb, "format_exception", "O", exc) : NULL;
		if (lines != NULL) {
			PyObject* empty = PyUnicode_FromString("");
			PyObject* joined = empty ? PyUnicode_Join(empty, lines) : NULL;
			const char* utf8 = joined ? PyUnicode_AsUTF8(joined) : NULL;
			if (utf8 != NULL) {
				text += ":\n" + string(utf8);
			}
			Py_XDECREF(joined);
			Py_XDECREF(empty);
		}
		Py_XDECREF(lines);
		Py_XDECREF(tb);
		Py_DECREF(exc);
		PyErr_Clear();
	}
	Shell->Log(LOG_ERROR, text);
}
