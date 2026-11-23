// A short check of the parts of UpdateCore MicroKeys relies on.  The full
// corpus lives with the original in Notepad 2's tests.

#include "../../src/core/UpdateCore.h"
#include <stdio.h>
#include <string.h>

static int _failures = 0;
static int* _checks = NULL;

#define CHECK(cond) do { (*_checks)++; if (!(cond)) { _failures++; printf("FAILED: %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const char* _feed =
	"{\n"
	"  \"version\": \"1.2.0\",\n"
	"  \"url_x64\": \"https://example.com/apps/microkeys/MicroKeys-1.2.0-x64.zip\",\n"
	"  \"sha256_x64\": \"aa11\",\n"
	"  \"size_x64\": 15000000,\n"
	"  \"url_arm64\": \"https://example.com/apps/microkeys/MicroKeys-1.2.0-arm64.zip\",\n"
	"  \"sha256_arm64\": \"bb22\",\n"
	"  \"size_arm64\": 14000000,\n"
	"  \"notes\": \"Line one\\nLine two\"\n"
	"}\n";

int RunUpdateCoreTests(int& checks) {
	_checks = &checks;

	CHECK(UpdateCore_CompareVersions("1.0.0", "1.0.0") == 0);
	CHECK(UpdateCore_CompareVersions("1.10.0", "1.9.0") > 0);
	CHECK(UpdateCore_CompareVersions("1", "1.0.1") < 0);
	CHECK(UpdateCore_IsVersion("1.0.0") == 1);
	CHECK(UpdateCore_IsVersion("1.0.0.0") == 0);
	CHECK(UpdateCore_IsVersion("app-1.0.0") == 0);
	CHECK(UpdateCore_ParsePid(L"1234") == 1234);
	CHECK(UpdateCore_ParsePid(L"12x") == 0);

	wchar_t name[64];
	CHECK(UpdateCore_EventName(42, name, 64) > 0 && wcscmp(name, L"Local\\MicroKeys-UpdateStarted-42") == 0);
	CHECK(UpdateCore_WaitVerdict(1, 0, 0) == UPDATECORE_WAIT_STARTED);
	CHECK(UpdateCore_WaitVerdict(0, 1, 0) == UPDATECORE_WAIT_FAILED);
	CHECK(UpdateCore_WaitVerdict(0, 0, 0) == UPDATECORE_WAIT_GOING);

	UPDATECOREENTRY e;
	CHECK(UpdateCore_ReadEntry(_feed, "x64", &e) == UPDATECORE_ENTRY_OK);
	CHECK(strcmp(e.version, "1.2.0") == 0);
	CHECK(strcmp(e.sha256, "aa11") == 0 && e.size == 15000000);
	CHECK(strstr(e.url, "-x64.zip") != NULL);
	CHECK(strcmp(e.notes, "Line one\nLine two") == 0);
	CHECK(UpdateCore_ReadEntry(_feed, "arm64", &e) == UPDATECORE_ENTRY_OK && e.size == 14000000);
	CHECK(UpdateCore_ReadEntry(_feed, "macos", &e) == UPDATECORE_ENTRY_NOBUILD);
	CHECK(UpdateCore_ReadEntry("{}", "x64", &e) == UPDATECORE_ENTRY_BADFORMAT);

	char host[UPDATECORE_CBHOST];
	CHECK(UpdateCore_UrlHost("https://example.com/apps/", host, sizeof(host)) > 0 && strcmp(host, "example.com") == 0);
	CHECK(UpdateCore_UrlHost("http://example.com/apps/", host, sizeof(host)) == 0);
	CHECK(UpdateCore_UrlHost("https://good.example@evil.example/", host, sizeof(host)) == 0);

	UPDATECOREFEED feed;
	memset(&feed, 0, sizeof(feed));
	feed.pszDefaultUrl = "https://example.com/apps/microkeys/";
	CHECK(strcmp(UpdateCore_FeedUrl(&feed), "https://example.com/apps/microkeys/") == 0);
	CHECK(UpdateCore_FileHostAllowed(&feed, "https://example.com/apps/microkeys/x.zip") == 1);
	CHECK(UpdateCore_FileHostAllowed(&feed, "https://other.example/x.zip") == 0);
	feed.pszConfiguredUrl = "https://localhost/test/";
	CHECK(strcmp(UpdateCore_FeedUrl(&feed), "https://localhost/test/") == 0);
	feed.pszConfiguredUrl = "https://other.example/test/";
	CHECK(strcmp(UpdateCore_FeedUrl(&feed), "https://example.com/apps/microkeys/") == 0);

	return _failures;
}
