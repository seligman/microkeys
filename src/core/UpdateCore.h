/******************************************************************************
*
*  UpdateCore.h - pure parts of the self-updater (Update.c, row 6.1): version
*  text, the restart command line, the start handshake's names and verdicts;
*  the feed: latest.json's reader and the host rules (row 8.30, shared with
*  the macOS app). No files, no processes, no network: the unit tests call
*  them.
*
*  Copied from Notepad 2 (src/UpdateCore.c), only the names changed.
*
*  Start handshake. After the swap (the new version is unpacked into its own
*  app-<version> folder) the old instance creates the manual-reset event
*  UpdateCore_EventName(<its pid>), hides its window and starts
*      "<exe>" /updated <its pid> <its version> ["<file>"]
*  The new instance sets that event once its main window is shown. The old
*  one waits up to UPDATECORE_START_TIMEOUT_MS: the event set means the new
*  version runs (the old instance closes; the new one deletes .old once the
*  old process has ended); the child ending first, or the time running out,
*  means it did not start (the old exe is put back and the version skipped).
*
*  Only a download that carries UPDATECORE_MARKER_W (as UTF-16LE bytes: every
*  build with this handshake holds it, as part of the event name) is started
*  so; an older build (a test feed's) is started the old way, with /n and no
*  wait, as before.
*
******************************************************************************/
#ifndef MICROKEYS_UPDATECORE_H
#define MICROKEYS_UPDATECORE_H

#include <stddef.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UPDATECORE_MARKER_W          L"MicroKeys-UpdateStarted-"
#define UPDATECORE_START_TIMEOUT_MS  20000
#define UPDATECORE_MAXVERSION        31

/* "a.b.c" against "x.y.z", numerically part by part (missing parts are 0): <0, 0, >0 */
int UpdateCore_CompareVersions(const char* a, const char* b);

/* 1 when psz is 1 to 3 decimal numbers (1 to 5 digits each) joined by single dots, at most
   UPDATECORE_MAXVERSION characters ("2.19.0", "9.9.9", "3"); 0 otherwise */
int UpdateCore_IsVersion(const char* psz);

/* the process id of /updated <pid>: 1 to 10 decimal digits, nonzero, within 32 bits. 0 otherwise. */
unsigned long UpdateCore_ParsePid(const wchar_t* psz);

/* L"Local\MicroKeys-UpdateStarted-<pid>" (pid 0: 0 is returned). The length, 0 when it does not fit. */
int UpdateCore_EventName(unsigned long dwPid, wchar_t* pszOut, int cchOut);

/* the download holds UPDATECORE_MARKER_W as UTF-16LE bytes (a build that answers the handshake) */
int UpdateCore_HasHandshakeMarker(const unsigned char* pData, size_t cb);

/* the new exe's command line (UTF-8): "<exe>" /updated <pid> <version> ["<file>"] when bHandshake,
   else "<exe>" /n ["<file>"] (the start of builds without the handshake). pszFile NULL or "": no file.
   The length, 0 when it does not fit or an argument is not usable (pid 0 or a bad version with
   bHandshake, a quote in a path). */
int UpdateCore_RestartCommand(const char* pszExe, int bHandshake, unsigned long dwPid, const char* pszVersion,
                              const char* pszFile, char* pszOut, int cchOut);

/* the old instance's wait, one look at a time: the new instance has set the event (it counts even when
   the process has ended since: its window was shown), the new process has ended without it, or the
   time is up without it */
#define UPDATECORE_WAIT_GOING   0   /* keep waiting */
#define UPDATECORE_WAIT_STARTED 1   /* the new version runs: close the old instance */
#define UPDATECORE_WAIT_FAILED  2   /* it did not start: put the old exe back */
int UpdateCore_WaitVerdict(int bEventSet, int bChildEnded, int bTimeUp);

/* ---------------------------------------------------------------------------
   The feed (row 8.30): latest.json and the hosts the updater talks to, as Update.c had them (the Windows app and the
   macOS one read the feed and judge its hosts alike). Plain C, UTF-8 strings throughout.
--------------------------------------------------------------------------- */

/* The minimal JSON reader: enough for the flat latest.json that deploy.py writes. A key is found as the text "<key>"
   (quotes included, so "url" is not "url_macos"; byte for byte, as StrStrA found it: "URL" is not "url") followed by
   optional blanks (space, tab, CR, LF) and ':'; an occurrence that is not followed by ':' is passed over. NULL json or
   key: not found. */
const char* UpdateCore_JsonFindValue(const char* json, const char* key);

/* The string value of key into out (at most cch - 1 bytes, the rest cut off): \n \r \t and \<c> (the character c) are
   unescaped, \uXXXX becomes '?' (a \u too near the end ends the text), the value ends at the first unescaped '"' or
   at the end of json. 1 when the value is a string ("" is one), else 0 with out "". cch < 1: 0, out untouched. */
int UpdateCore_JsonGetString(const char* json, const char* key, char* out, int cch);

/* The leading decimal digits of key's value, modulo 2^32 (0 when there are none or the key is missing). */
unsigned long UpdateCore_JsonGetUInt(const char* json, const char* key);

/* One platform's download in latest.json: version, then url_<platform>, sha256_<platform>, size_<platform> (the flat
   url / sha256 / size for "x86" when it has no url_x86 or sha256_x86: the manifests from before the per-platform keys),
   then notes. The fields' sizes are Update.c's (a longer value is cut off). Platforms: "x86", "x64", "arm64" (Windows),
   "macos" (the Mac app). */
typedef struct UPDATECOREENTRY {
	char version[32];
	char url[512];
	char sha256[72];
	unsigned long size;   /* 0 when it is not given (or the url / sha256 are missing) */
	char notes[1024];
} UPDATECOREENTRY;
#define UPDATECORE_ENTRY_BADFORMAT 0   /* no "version" string: not the expected format (the other fields "", 0) */
#define UPDATECORE_ENTRY_NOBUILD   1   /* version (and notes) read, but no url and sha256 for this platform */
#define UPDATECORE_ENTRY_OK        2   /* version, url, sha256 (size may still be 0: the caller refuses that) */
int UpdateCore_ReadEntry(const char* json, const char* pszPlatform, UPDATECOREENTRY* pe);

/* The host of an https URL, as Windows' WinHttpCrackUrl gives it (Update.c's rule since row 6.1): "https" (in any case)
   then "://"; the authority ends at the first '/', '?' or '#'; a user name or password before its first '@' (split at
   the first ':') refuses the URL ("https://good.example@evil.example/" names evil.example), an empty one ("https://@h/")
   does not; the host is a bracketed "[...]" (to the first ']', which a ':' port, a '/' or the end must follow, with
   nothing past ASCII inside) or runs
   to the first ':', '/', '?' or '#'; a port is 1 to any number of decimal digits up to 65535 (leading zeros allowed)
   or empty; nothing is decoded (percent signs, backslashes, blanks, trailing dots stay in the host) and the case is
   kept. Ill-formed UTF-8 counts as U+FFFD (one per piece, the way MultiByteToWideChar reads it) and comes back so in
   host, which is always well-formed UTF-8. The URL may be at most UPDATECORE_MAXURL UTF-16 units long, the host at
   most UPDATECORE_MAXHOST (Update.c's buffers). The host's length in bytes; 0 with host "" when the URL is refused or
   the host does not fit in cbHost (UPDATECORE_CBHOST always fits). */
#define UPDATECORE_MAXURL  1023
#define UPDATECORE_MAXHOST 255
#define UPDATECORE_CBHOST  (3 * UPDATECORE_MAXHOST + 1)
int UpdateCore_UrlHost(const char* pszUrl, char* pszHost, int cbHost);

/* How two host names compare: 0 when they are the same, bIgnoreCase: in either case. NULL in UPDATECOREFEED: ASCII
   (A-Z as a-z with bIgnoreCase, byte for byte otherwise). Update.c passes lstrcmpiW / lstrcmpW (on the UTF-16 forms),
   as it had them: they also take a few hosts with ignorable or compatibility characters for an ASCII one
   ("local<U+00AD>host"), which the ASCII rule refuses. */
typedef int (*UPDATECORE_HOSTCMP)(const char* a, const char* b, int bIgnoreCase);

/* The updater's feed settings. The hosts it talks to: the one of pszDefaultUrl; pszConfiguredUrl (Settings\UpdateUrl)
   may point at another path there (the test feeds), but at another host only when that is a test server on this
   machine (https://localhost/... or https://127.0.0.1/... configured, and the host asked about loopback too);
   bAnyHttpsHost (Update.c: a _DEBUG build) takes any https host once pszConfiguredUrl has one. */
typedef struct UPDATECOREFEED {
	const char* pszDefaultUrl;      /* the built-in feed (Update.h's UPDATE_DEFAULT_URL) */
	const char* pszConfiguredUrl;   /* Settings\UpdateUrl ("" or NULL: none) */
	int bAnyHttpsHost;              /* a test build: any https host */
	UPDATECORE_HOSTCMP pfnCompare;  /* NULL: ASCII */
} UPDATECOREFEED;

/* "localhost" (in any case) or "127.0.0.1" (pfnCompare NULL: ASCII) */
int UpdateCore_IsLoopbackHost(const char* pszHost, UPDATECORE_HOSTCMP pfnCompare);

/* 1 when the updater may talk to pszHost (a host UpdateCore_UrlHost gave; "" never) */
int UpdateCore_HostAllowed(const UPDATECOREFEED* pf, const char* pszHost);

/* The feed's base URL (latest.json is under it): pszConfiguredUrl when it is set, https and on an allowed host, else
   pszDefaultUrl (one of the two pointers) */
const char* UpdateCore_FeedUrl(const UPDATECOREFEED* pf);

/* 1 when an update file at pszFileUrl may be downloaded: an https URL on the feed's own host
   (UpdateCore_FeedUrl's, compared in either case) that UpdateCore_HostAllowed takes */
int UpdateCore_FileHostAllowed(const UPDATECOREFEED* pf, const char* pszFileUrl);

#ifdef __cplusplus
}
#endif

#endif
