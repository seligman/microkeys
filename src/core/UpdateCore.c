/******************************************************************************
*
*  UpdateCore.c - pure parts of the self-updater (see UpdateCore.h)
*
******************************************************************************/
#include <stdio.h>
#include <string.h>
#include "UpdateCore.h"

/* up to three numbers "a.b.c" (missing ones 0; reading stops at the first character that does not
   continue the form, as sscanf("%d.%d.%d") read it before) */
static void ParseVersion(const char* s, long v[3])
{
	int i;
	v[0] = v[1] = v[2] = 0;
	if (!s)
		return;
	for (i = 0; i < 3; i++) {
		long n = 0;
		int nDigits = 0;
		while (*s >= '0' && *s <= '9') {
			if (n < 100000000L)
				n = n * 10 + (*s - '0');
			s++;
			nDigits++;
		}
		if (nDigits == 0)
			return;
		v[i] = n;
		if (*s != '.')
			return;
		s++;
	}
}

int UpdateCore_CompareVersions(const char* a, const char* b)
{
	long va[3], vb[3];
	int i;
	ParseVersion(a, va);
	ParseVersion(b, vb);
	for (i = 0; i < 3; i++)
		if (va[i] != vb[i])
			return va[i] < vb[i] ? -1 : 1;
	return 0;
}

int UpdateCore_IsVersion(const char* psz)
{
	int nParts = 0, nDigits = 0, n = 0;
	if (!psz || !*psz)
		return 0;
	for (; psz[n]; n++) {
		const char c = psz[n];
		if (n >= UPDATECORE_MAXVERSION)
			return 0;
		if (c >= '0' && c <= '9') {
			if (++nDigits > 5)
				return 0;
		}
		else if (c == '.') {
			if (nDigits == 0 || ++nParts >= 3)
				return 0;
			nDigits = 0;
		}
		else
			return 0;
	}
	return nDigits > 0;
}

unsigned long UpdateCore_ParsePid(const wchar_t* psz)
{
	unsigned long long v = 0;
	int n = 0;
	if (!psz)
		return 0;
	for (; psz[n]; n++) {
		if (psz[n] < L'0' || psz[n] > L'9' || n >= 10)
			return 0;
		v = v * 10 + (unsigned long long)(psz[n] - L'0');
	}
	if (n == 0 || v == 0 || v > 0xFFFFFFFFULL)
		return 0;
	return (unsigned long)v;
}

int UpdateCore_EventName(unsigned long dwPid, wchar_t* pszOut, int cchOut)
{
	wchar_t wsz[96];
	int n;
	if (!pszOut || cchOut <= 0)
		return 0;
	pszOut[0] = 0;
	if (dwPid == 0)
		return 0;
	n = swprintf(wsz, sizeof(wsz) / sizeof(wsz[0]), L"Local\\" UPDATECORE_MARKER_W L"%lu", dwPid);
	if (n <= 0 || n >= cchOut)
		return 0;
	memcpy(pszOut, wsz, ((size_t)n + 1) * sizeof(wchar_t));
	return n;
}

int UpdateCore_HasHandshakeMarker(const unsigned char* pData, size_t cb)
{
	static const wchar_t wszMarker[] = UPDATECORE_MARKER_W;
	unsigned char pat[2 * (sizeof(wszMarker) / sizeof(wszMarker[0]))];
	const size_t cchMarker = sizeof(wszMarker) / sizeof(wszMarker[0]) - 1;
	const size_t cbPat = cchMarker * 2;
	size_t i;
	for (i = 0; i < cchMarker; i++) {
		pat[2 * i] = (unsigned char)(wszMarker[i] & 0xFF);       /* UTF-16LE, whatever wchar_t's size */
		pat[2 * i + 1] = (unsigned char)((wszMarker[i] >> 8) & 0xFF);
	}
	if (!pData || cb < cbPat)
		return 0;
	for (i = 0; i + cbPat <= cb; i++) {
		if (pData[i] == pat[0] && memcmp(pData + i, pat, cbPat) == 0)
			return 1;
	}
	return 0;
}

static int Append(char* pszOut, int cchOut, int* pn, const char* psz)
{
	size_t len = strlen(psz);
	if ((size_t)*pn + len >= (size_t)cchOut)
		return 0;
	memcpy(pszOut + *pn, psz, len + 1);
	*pn += (int)len;
	return 1;
}

int UpdateCore_RestartCommand(const char* pszExe, int bHandshake, unsigned long dwPid, const char* pszVersion,
                              const char* pszFile, char* pszOut, int cchOut)
{
	char szNum[16];
	int n = 0;
	if (!pszOut || cchOut <= 0)
		return 0;
	pszOut[0] = 0;
	if (!pszExe || !*pszExe || strchr(pszExe, '"') || (pszFile && strchr(pszFile, '"')))
		return 0;
	if (bHandshake && (dwPid == 0 || !UpdateCore_IsVersion(pszVersion)))
		return 0;
	if (!Append(pszOut, cchOut, &n, "\"") || !Append(pszOut, cchOut, &n, pszExe) || !Append(pszOut, cchOut, &n, "\""))
		goto toolong;
	if (bHandshake) {
		snprintf(szNum, sizeof(szNum), "%lu", dwPid);
		if (!Append(pszOut, cchOut, &n, " /updated ") || !Append(pszOut, cchOut, &n, szNum) ||
		    !Append(pszOut, cchOut, &n, " ") || !Append(pszOut, cchOut, &n, pszVersion))
			goto toolong;
	}
	else if (!Append(pszOut, cchOut, &n, " /n"))
		goto toolong;
	if (pszFile && *pszFile &&
	    (!Append(pszOut, cchOut, &n, " \"") || !Append(pszOut, cchOut, &n, pszFile) || !Append(pszOut, cchOut, &n, "\"")))
		goto toolong;
	return n;
toolong:
	pszOut[0] = 0;
	return 0;
}

int UpdateCore_WaitVerdict(int bEventSet, int bChildEnded, int bTimeUp)
{
	if (bEventSet)
		return UPDATECORE_WAIT_STARTED;
	if (bChildEnded || bTimeUp)
		return UPDATECORE_WAIT_FAILED;
	return UPDATECORE_WAIT_GOING;
}

/* ---------------------------------------------------------------------------
   the feed (row 8.30): latest.json
--------------------------------------------------------------------------- */

static int AsciiLower(int c)
{
	return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static int IsJsonBlank(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* "<key>" at p, byte for byte: the length matched, 0 when it does not match */
static size_t MatchQuotedKey(const char* p, const char* key)
{
	size_t n = 0;
	if (p[0] != '"')
		return 0;
	for (n = 1; *key; key++, n++) {
		if (p[n] != *key)
			return 0;
	}
	return p[n] == '"' ? n + 1 : 0;
}

const char* UpdateCore_JsonFindValue(const char* json, const char* key)
{
	const char* p = json;
	if (!json || !key)
		return NULL;
	for (; *p; p++) {
		size_t n = MatchQuotedKey(p, key);
		const char* q;
		if (!n)
			continue;
		q = p + n;
		while (IsJsonBlank(*q))
			q++;
		if (*q == ':') {
			q++;
			while (IsJsonBlank(*q))
				q++;
			return q;
		}
		p += n - 1;   /* the search goes on after this occurrence (the loop's p++ makes it p + n) */
	}
	return NULL;
}

int UpdateCore_JsonGetString(const char* json, const char* key, char* out, int cch)
{
	const char* p;
	int n = 0;
	if (!out || cch < 1)
		return 0;
	out[0] = '\0';
	p = UpdateCore_JsonFindValue(json, key);
	if (!p || *p != '"')
		return 0;
	p++;
	while (*p && *p != '"' && n < cch - 1) {
		if (*p == '\\' && p[1]) {
			p++;
			switch (*p) {
			case 'n': out[n++] = '\n'; break;
			case 'r': out[n++] = '\r'; break;
			case 't': out[n++] = '\t'; break;
			case 'u':   /* no unicode escapes in the manifests; never step past the end of the text */
				if (!(p[1] && p[2] && p[3] && p[4])) {
					out[n] = '\0';
					return 1;
				}
				p += 4;
				out[n++] = '?';
				break;
			default: out[n++] = *p; break;
			}
		}
		else
			out[n++] = *p;
		p++;
	}
	out[n] = '\0';
	return 1;
}

unsigned long UpdateCore_JsonGetUInt(const char* json, const char* key)
{
	const char* p = UpdateCore_JsonFindValue(json, key);
	unsigned long v = 0;
	if (!p)
		return 0;
	while (*p >= '0' && *p <= '9')
		v = (v * 10 + (unsigned long)(*p++ - '0')) & 0xFFFFFFFFUL;   /* a 32-bit DWORD, as Update.c read it */
	return v;
}

int UpdateCore_ReadEntry(const char* json, const char* pszPlatform, UPDATECOREENTRY* pe)
{
	char keyUrl[48], keySha[48], keySize[48];
	int bHave = 0;
	if (!pe)
		return UPDATECORE_ENTRY_BADFORMAT;
	memset(pe, 0, sizeof(*pe));
	if (!UpdateCore_JsonGetString(json, "version", pe->version, (int)sizeof(pe->version)))
		return UPDATECORE_ENTRY_BADFORMAT;
	if (pszPlatform && strlen(pszPlatform) < 32) {
		snprintf(keyUrl, sizeof(keyUrl), "url_%s", pszPlatform);
		snprintf(keySha, sizeof(keySha), "sha256_%s", pszPlatform);
		snprintf(keySize, sizeof(keySize), "size_%s", pszPlatform);
		bHave = UpdateCore_JsonGetString(json, keyUrl, pe->url, (int)sizeof(pe->url)) &&
			UpdateCore_JsonGetString(json, keySha, pe->sha256, (int)sizeof(pe->sha256));
		pe->size = bHave ? UpdateCore_JsonGetUInt(json, keySize) : 0;
		if (!bHave && strcmp(pszPlatform, "x86") == 0) {
			/* a manifest from before the per-architecture keys: the flat keys are the x86 build */
			bHave = UpdateCore_JsonGetString(json, "url", pe->url, (int)sizeof(pe->url)) &&
				UpdateCore_JsonGetString(json, "sha256", pe->sha256, (int)sizeof(pe->sha256));
			pe->size = bHave ? UpdateCore_JsonGetUInt(json, "size") : 0;
		}
	}
	UpdateCore_JsonGetString(json, "notes", pe->notes, (int)sizeof(pe->notes));
	return bHave ? UPDATECORE_ENTRY_OK : UPDATECORE_ENTRY_NOBUILD;
}

/* ---------------------------------------------------------------------------
   the feed (row 8.30): URLs and hosts
--------------------------------------------------------------------------- */

/* One code point of NUL-terminated UTF-8 as MultiByteToWideChar(CP_UTF8, 0) reads it: the bytes it takes (0 at the
   end); an ill-formed piece is U+FFFD: a byte that cannot start a sequence; a lead byte cut short by a byte that does
   not continue it (that byte is not taken); a lead byte whose second byte continues sequences but not this one (E0 80,
   ED A0, F0 80, F4 90: an overlong form, a surrogate, past U+10FFFF), which takes the two. */
static int Utf8Next(const unsigned char* p, unsigned long* pcp)
{
	unsigned long c = p[0], cp;
	unsigned lo = 0x80, hi = 0xBF;
	int len, i;
	if (c == 0)
		return 0;
	if (c < 0x80) {
		*pcp = c;
		return 1;
	}
	if (c >= 0xC2 && c <= 0xDF) {
		len = 2;
		cp = c & 0x1F;
	}
	else if (c >= 0xE0 && c <= 0xEF) {
		len = 3;
		cp = c & 0x0F;
		if (c == 0xE0)
			lo = 0xA0;
		else if (c == 0xED)
			hi = 0x9F;
	}
	else if (c >= 0xF0 && c <= 0xF4) {
		len = 4;
		cp = c & 0x07;
		if (c == 0xF0)
			lo = 0x90;
		else if (c == 0xF4)
			hi = 0x8F;
	}
	else {
		*pcp = 0xFFFD;
		return 1;
	}
	for (i = 1; i < len; i++) {
		unsigned t = p[i];
		if (t < lo || t > hi) {
			*pcp = 0xFFFD;
			return (i == 1 && t >= 0x80 && t <= 0xBF) ? 2 : i;
		}
		lo = 0x80;
		hi = 0xBF;
		cp = (cp << 6) | (t & 0x3F);
	}
	*pcp = cp;
	return len;
}

/* the UTF-16 units of the code points in [p, end) (end NULL: to the NUL) */
static size_t Utf16Units(const char* p, const char* end)
{
	size_t n = 0;
	const unsigned char* q = (const unsigned char*)p;
	while (!end || (const char*)q < end) {
		unsigned long cp;
		int k = Utf8Next(q, &cp);
		if (k == 0)
			break;
		q += k;
		n += cp >= 0x10000 ? 2 : 1;
	}
	return n;
}

/* [p, end) as well-formed UTF-8 (each ill-formed piece as U+FFFD) into out: the length, -1 when it does not fit */
static int CopyWellFormed(const char* p, const char* end, char* out, int cbOut)
{
	int n = 0;
	const unsigned char* q = (const unsigned char*)p;
	while ((const char*)q < end) {
		unsigned long cp = 0;
		unsigned char b[4];
		int k = Utf8Next(q, &cp), nb, i;
		if (k == 0)
			break;
		q += k;
		if (cp < 0x80) {
			b[0] = (unsigned char)cp;
			nb = 1;
		}
		else if (cp < 0x800) {
			b[0] = (unsigned char)(0xC0 | (cp >> 6));
			b[1] = (unsigned char)(0x80 | (cp & 0x3F));
			nb = 2;
		}
		else if (cp < 0x10000) {
			b[0] = (unsigned char)(0xE0 | (cp >> 12));
			b[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
			b[2] = (unsigned char)(0x80 | (cp & 0x3F));
			nb = 3;
		}
		else {
			b[0] = (unsigned char)(0xF0 | (cp >> 18));
			b[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
			b[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
			b[3] = (unsigned char)(0x80 | (cp & 0x3F));
			nb = 4;
		}
		if (n + nb >= cbOut)
			return -1;
		for (i = 0; i < nb; i++)
			out[n++] = (char)b[i];
	}
	out[n] = '\0';
	return n;
}

static int IsAuthorityEnd(char c)
{
	return c == '\0' || c == '/' || c == '?' || c == '#';
}

int UpdateCore_UrlHost(const char* pszUrl, char* pszHost, int cbHost)
{
	static const char s_scheme[] = "https://";
	const char *p, *pAuthEnd, *pAt, *pHost, *pHostEnd;
	unsigned long port = 0;
	int i, n;
	if (!pszHost || cbHost < 1)
		return 0;
	pszHost[0] = '\0';
	if (!pszUrl || Utf16Units(pszUrl, NULL) > UPDATECORE_MAXURL)
		return 0;
	for (i = 0; s_scheme[i]; i++) {
		if (AsciiLower((unsigned char)pszUrl[i]) != s_scheme[i])
			return 0;
	}
	p = pszUrl + i;
	for (pAuthEnd = p; !IsAuthorityEnd(*pAuthEnd); pAuthEnd++)
		;
	pAt = (const char*)memchr(p, '@', (size_t)(pAuthEnd - p));
	if (pAt) {
		/* a user name or a password, however short, refuses the URL; "https://@host/" and "https://:@host/" do not */
		if (pAt > p && !(pAt == p + 1 && *p == ':'))
			return 0;
		pHost = pAt + 1;
	}
	else
		pHost = p;
	if (*pHost == '[') {
		const char* pClose = strchr(pHost, ']');
		const char* q;
		if (!pClose)
			return 0;
		for (q = pHost; q < pClose; q++) {
			if ((unsigned char)*q >= 0x80)
				return 0;   /* nothing past ASCII between the brackets */
		}
		pHostEnd = pClose + 1;
		if (*pHostEnd != ':' && *pHostEnd != '/' && *pHostEnd != '\0')
			return 0;
	}
	else {
		for (pHostEnd = pHost; *pHostEnd != ':' && !IsAuthorityEnd(*pHostEnd); pHostEnd++)
			;
	}
	if (pHostEnd == pHost)
		return 0;
	if (*pHostEnd == ':') {
		const char* q = pHostEnd + 1;
		for (; !IsAuthorityEnd(*q); q++) {
			if (*q < '0' || *q > '9')
				return 0;
			port = port * 10 + (unsigned long)(*q - '0');
			if (port > 65535)
				return 0;
		}
	}
	if (Utf16Units(pHost, pHostEnd) > UPDATECORE_MAXHOST)
		return 0;
	n = CopyWellFormed(pHost, pHostEnd, pszHost, cbHost);
	if (n <= 0) {
		pszHost[0] = '\0';
		return 0;
	}
	return n;
}

static int HostCompare(UPDATECORE_HOSTCMP pfn, const char* a, const char* b, int bIgnoreCase)
{
	if (pfn)
		return pfn(a, b, bIgnoreCase);
	for (;; a++, b++) {
		int ca = (unsigned char)*a, cb = (unsigned char)*b;
		if (bIgnoreCase) {
			ca = AsciiLower(ca);
			cb = AsciiLower(cb);
		}
		if (ca != cb)
			return ca < cb ? -1 : 1;
		if (!ca)
			return 0;
	}
}

int UpdateCore_IsLoopbackHost(const char* pszHost, UPDATECORE_HOSTCMP pfnCompare)
{
	if (!pszHost)
		return 0;
	return HostCompare(pfnCompare, pszHost, "localhost", 1) == 0 || HostCompare(pfnCompare, pszHost, "127.0.0.1", 0) == 0;
}

int UpdateCore_HostAllowed(const UPDATECOREFEED* pf, const char* pszHost)
{
	char szDefault[UPDATECORE_CBHOST], szConfigured[UPDATECORE_CBHOST];
	if (!pf || !pszHost || !pszHost[0])
		return 0;
	UpdateCore_UrlHost(pf->pszDefaultUrl, szDefault, (int)sizeof(szDefault));
	if (szDefault[0] && HostCompare(pf->pfnCompare, pszHost, szDefault, 1) == 0)
		return 1;
	UpdateCore_UrlHost(pf->pszConfiguredUrl, szConfigured, (int)sizeof(szConfigured));
	if (pf->bAnyHttpsHost)
		return UpdateCore_IsLoopbackHost(pszHost, pf->pfnCompare) || szConfigured[0] != '\0';
	return UpdateCore_IsLoopbackHost(pszHost, pf->pfnCompare) && UpdateCore_IsLoopbackHost(szConfigured, pf->pfnCompare);
}

const char* UpdateCore_FeedUrl(const UPDATECOREFEED* pf)
{
	char szHost[UPDATECORE_CBHOST];
	if (!pf)
		return NULL;
	if (pf->pszConfiguredUrl && pf->pszConfiguredUrl[0]) {
		UpdateCore_UrlHost(pf->pszConfiguredUrl, szHost, (int)sizeof(szHost));
		if (UpdateCore_HostAllowed(pf, szHost))
			return pf->pszConfiguredUrl;
	}
	return pf->pszDefaultUrl;
}

int UpdateCore_FileHostAllowed(const UPDATECOREFEED* pf, const char* pszFileUrl)
{
	char szFeed[UPDATECORE_CBHOST], szFile[UPDATECORE_CBHOST];
	if (!pf)
		return 0;
	UpdateCore_UrlHost(UpdateCore_FeedUrl(pf), szFeed, (int)sizeof(szFeed));
	UpdateCore_UrlHost(pszFileUrl, szFile, (int)sizeof(szFile));
	return szFile[0] && HostCompare(pf->pfnCompare, szFeed, szFile, 1) == 0 && UpdateCore_HostAllowed(pf, szFile);
}
