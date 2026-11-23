#pragma once

#include "framework.h"

class WinApp;

#ifdef MK_NO_UPDATES

class Updater {
	// A build without the updater: it never has a feed, so nothing about
	// updates shows
public:
	Updater(WinApp* app) {}
	void Configure(const string& feed, bool checkDaily) {}
	bool HasFeed() { return false; }
	void Check(bool manual) {}
	void OnMessage(WPARAM wParam, LPARAM lParam) {}
	void Startup(DWORD updatedFrom, const string& updatedVersion) {}
};

#else

class Updater {
	// Self-update: read latest.json from the feed, download this
	// architecture's zip, check its size and SHA-256, unpack it into its own
	// app-<version> folder next to this one, and hand over to it.  The feed
	// rules and the start handshake are UpdateCore's, shared with Notepad 2.
public:
	Updater(WinApp* app);
	~Updater();
	void Configure(const string& feed, bool checkDaily);
	bool HasFeed();
	void Check(bool manual);
	void OnMessage(WPARAM wParam, LPARAM lParam);
	void Startup(DWORD updatedFrom, const string& updatedVersion);

private:
	void CheckThread(bool manual);
	void DownloadThread();
	void Install();
	bool DailyCheckDue();
	void RecordCheck();
	void Post(int what, const string& text);

	WinApp* _app;
	string _feed;
	bool _daily = true;
	bool _busy = false;
	string _version;
	string _url;
	string _sha256;
	unsigned long _size = 0;
	string _notes;
	string _zipPath;
	thread _thread;
};

#endif
