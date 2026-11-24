#include "WinPlatform.h"
#include "WinText.h"
#include <wincodec.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// The clipboard: text, HTML, RTF, file lists and images, plus saving and
// restoring everything on it.  Images go in and out as PNG, converted to and
// from the DIBs Windows programs expect with WIC.

#define DROPEFFECT_COPY_VALUE	1
#define MAX_IMAGE_SIDE			32768

static UINT Format(const wchar_t* name) {
	return RegisterClipboardFormatW(name);
}

static UINT HtmlFormat() {
	static UINT format = Format(L"HTML Format");
	return format;
}

static UINT RtfFormat() {
	static UINT format = Format(L"Rich Text Format");
	return format;
}

static UINT PngFormat() {
	static UINT format = Format(L"PNG");
	return format;
}

static bool OpenClipboardRetry(HWND owner) {
	// Another program may have the clipboard open for a moment
	for (int i = 0; i < 20; i++) {
		if (OpenClipboard(owner)) {
			return true;
		}
		Sleep(10);
	}
	return false;
}

class ComScope {
	// COM for WIC, on whichever thread asks
public:
	ComScope() {
		_hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	}
	~ComScope() {
		if (SUCCEEDED(_hr)) {
			CoUninitialize();
		}
	}

private:
	HRESULT _hr;
};

static HGLOBAL MakeGlobal(const void* data, size_t size) {
	HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, size > 0 ? size : 1);
	if (mem == NULL) {
		return NULL;
	}
	void* locked = GlobalLock(mem);
	if (locked == NULL) {
		GlobalFree(mem);
		return NULL;
	}
	if (size > 0) {
		memcpy(locked, data, size);
	}
	GlobalUnlock(mem);
	return mem;
}

static bool ReadGlobal(HANDLE handle, string& data) {
	// The bytes behind a clipboard memory handle
	if (handle == NULL) {
		return false;
	}
	const char* locked = (const char*)GlobalLock(handle);
	if (locked == NULL) {
		return false;
	}
	data.assign(locked, GlobalSize(handle));
	GlobalUnlock(handle);
	return true;
}

static bool ReadFormat(UINT format, string& data) {
	// With the clipboard open
	return IsClipboardFormatAvailable(format) && ReadGlobal(GetClipboardData(format), data);
}

static void TrimNul(string& data) {
	size_t end = data.find('\0');
	if (end != string::npos) {
		data.resize(end);
	}
}

bool WinPlatform::GetClipboardText(string& text) {
	// False when the clipboard has no text, or couldn't be opened
	if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboardRetry(ClipboardOwner)) {
		return false;
	}
	bool ret = false;
	HANDLE data = GetClipboardData(CF_UNICODETEXT);
	if (data != NULL) {
		const wchar_t* locked = (const wchar_t*)GlobalLock(data);
		if (locked != NULL) {
			size_t max = GlobalSize(data) / sizeof(wchar_t);
			size_t len = 0;
			while (len < max && locked[len] != 0) {
				len++;
			}
			text = Utf16ToUtf8(locked, len);
			ret = true;
			GlobalUnlock(data);
		}
	}
	CloseClipboard();
	return ret;
}

static HGLOBAL TextGlobal(const string& text) {
	wstring wide = Utf8ToUtf16(text);
	return MakeGlobal(wide.c_str(), (wide.size() + 1) * sizeof(wchar_t));
}

bool WinPlatform::SetClipboardText(const string& text) {
	HGLOBAL mem = TextGlobal(text);
	if (mem == NULL) {
		return false;
	}
	if (!OpenClipboardRetry(ClipboardOwner)) {
		GlobalFree(mem);
		return false;
	}
	EmptyClipboard();
	bool ok = SetClipboardData(CF_UNICODETEXT, mem) != NULL;
	CloseClipboard();
	if (!ok) {
		GlobalFree(mem);
	}
	return ok;
}

uint32_t WinPlatform::ClipboardSequence() {
	return GetClipboardSequenceNumber();
}

vector<string> WinPlatform::ClipboardKinds() {
	// Checking what's there doesn't need the clipboard open
	vector<string> ret;
	if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
		ret.push_back("text");
	}
	if (IsClipboardFormatAvailable(HtmlFormat())) {
		ret.push_back("html");
	}
	if (IsClipboardFormatAvailable(RtfFormat())) {
		ret.push_back("rtf");
	}
	if (IsClipboardFormatAvailable(CF_HDROP)) {
		ret.push_back("files");
	}
	if (IsClipboardFormatAvailable(PngFormat()) || IsClipboardFormatAvailable(CF_DIB) || IsClipboardFormatAvailable(CF_DIBV5)) {
		ret.push_back("image");
	}
	return ret;
}

static long HeaderValue(const string& header, const char* key) {
	// A number from CF_HTML's header, like "StartFragment:0000000105"
	size_t pos = header.find(key);
	if (pos == string::npos) {
		return -1;
	}
	return strtol(header.c_str() + pos + strlen(key), NULL, 10);
}

static string HtmlFragment(const string& data) {
	// CF_HTML is a header of byte offsets, then the document; the copied part
	// is marked as the fragment
	size_t headerEnd = data.find('<');
	string header = data.substr(0, headerEnd);
	long start = HeaderValue(header, "StartFragment:");
	long end = HeaderValue(header, "EndFragment:");
	if (start < 0 || end < start || (size_t)end > data.size()) {
		start = HeaderValue(header, "StartHTML:");
		end = HeaderValue(header, "EndHTML:");
	}
	if (start < 0 || end < start || (size_t)end > data.size()) {
		return headerEnd == string::npos ? "" : data.substr(headerEnd);
	}
	return data.substr(start, end - start);
}

static string MakeHtml(const string& fragment) {
	// CF_HTML around a fragment.  The offsets are fixed width, so the header's
	// length is known before they are.
	const char* layout = "Version:0.9\r\nStartHTML:%010d\r\nEndHTML:%010d\r\nStartFragment:%010d\r\nEndFragment:%010d\r\n";
	string before = "<html><body>\r\n<!--StartFragment-->";
	string after = "<!--EndFragment-->\r\n</body>\r\n</html>";
	char header[256];
	int headerLen = sprintf_s(header, layout, 0, 0, 0, 0);
	int startFragment = headerLen + (int)before.size();
	int endFragment = startFragment + (int)fragment.size();
	sprintf_s(header, layout, headerLen, endFragment + (int)after.size(), startFragment, endFragment);
	return string(header) + before + fragment + after;
}

static bool ReadFiles(string& data) {
	// CF_HDROP as full paths joined with NUL
	HDROP drop = (HDROP)GetClipboardData(CF_HDROP);
	if (drop == NULL) {
		return false;
	}
	UINT count = DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0);
	data.clear();
	for (UINT i = 0; i < count; i++) {
		UINT len = DragQueryFileW(drop, i, NULL, 0);
		vector<wchar_t> buffer((size_t)len + 1);
		DragQueryFileW(drop, i, buffer.data(), len + 1);
		if (i > 0) {
			data += '\0';
		}
		data += Utf16ToUtf8(buffer.data(), len);
	}
	return true;
}

static HGLOBAL FilesGlobal(const string& paths) {
	// A DROPFILES block: the header, then each path and a closing NUL
	wstring list;
	size_t start = 0;
	while (start <= paths.size()) {
		size_t end = paths.find('\0', start);
		string path = paths.substr(start, end == string::npos ? string::npos : end - start);
		if (!path.empty()) {
			list += Utf8ToUtf16(path) + L'\0';
		}
		if (end == string::npos) {
			break;
		}
		start = end + 1;
	}
	list += L'\0';
	DROPFILES header = { sizeof(DROPFILES), { 0, 0 }, FALSE, TRUE };
	string block((const char*)&header, sizeof(header));
	block.append((const char*)list.c_str(), list.size() * sizeof(wchar_t));
	return MakeGlobal(block.data(), block.size());
}

static size_t DibPixelOffset(const string& dib) {
	// Where a packed DIB's pixels start: after the header, any colour masks,
	// and the colour table.  0 if it doesn't look like a DIB.
	if (dib.size() < sizeof(BITMAPINFOHEADER)) {
		return 0;
	}
	const BITMAPINFOHEADER* bih = (const BITMAPINFOHEADER*)dib.data();
	size_t offset = bih->biSize;
	if (bih->biSize == sizeof(BITMAPINFOHEADER) && bih->biCompression == BI_BITFIELDS) {
		offset += 3 * sizeof(DWORD);
	}
	DWORD colors = bih->biClrUsed;
	if (colors == 0 && bih->biBitCount <= 8) {
		colors = 1u << bih->biBitCount;
	}
	offset += (size_t)colors * sizeof(RGBQUAD);
	return offset < dib.size() ? offset : 0;
}

static bool DibToPixels(const string& dib, vector<BYTE>& bgra, int& width, int& height) {
	// Any packed DIB to 32-bit top-down pixels.  GDI draws it, which copes
	// with every layout, but drops alpha, so a 32-bit source's alpha is
	// copied over afterwards.
	size_t offset = DibPixelOffset(dib);
	if (offset == 0) {
		return false;
	}
	const BITMAPINFOHEADER* bih = (const BITMAPINFOHEADER*)dib.data();
	width = bih->biWidth;
	height = abs(bih->biHeight);
	if (width <= 0 || height <= 0 || width > MAX_IMAGE_SIDE || height > MAX_IMAGE_SIDE) {
		return false;
	}
	BITMAPINFO bmi = { 0 };
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = width;
	bmi.bmiHeader.biHeight = -height;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;
	void* bits = NULL;
	HDC dc = CreateCompatibleDC(NULL);
	HBITMAP bitmap = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
	if (bitmap == NULL) {
		DeleteDC(dc);
		return false;
	}
	HGDIOBJ old = SelectObject(dc, bitmap);
	bool ok = SetDIBitsToDevice(dc, 0, 0, width, height, 0, 0, 0, height, dib.data() + offset, (const BITMAPINFO*)dib.data(), DIB_RGB_COLORS) != 0;
	GdiFlush();
	if (ok) {
		bgra.assign((const BYTE*)bits, (const BYTE*)bits + (size_t)width * height * 4);
	}
	SelectObject(dc, old);
	DeleteObject(bitmap);
	DeleteDC(dc);
	if (!ok) {
		return false;
	}

	bool alpha = false;
	size_t stride = (size_t)width * 4;
	if (bih->biBitCount == 32 && (bih->biCompression == BI_RGB || bih->biCompression == BI_BITFIELDS) && dib.size() - offset >= stride * height) {
		const BYTE* src = (const BYTE*)dib.data() + offset;
		for (int y = 0; y < height; y++) {
			const BYTE* row = src + (bih->biHeight > 0 ? height - 1 - y : y) * stride;
			for (int x = 0; x < width; x++) {
				BYTE a = row[x * 4 + 3];
				bgra[y * stride + x * 4 + 3] = a;
				alpha = alpha || a != 0;
			}
		}
	}
	if (!alpha) {
		for (size_t i = 3; i < bgra.size(); i += 4) {
			bgra[i] = 0xFF;
		}
	}
	return true;
}

bool PixelsToPng(const vector<BYTE>& bgra, int width, int height, string& png) {
	ComScope com;
	ComPtr<IWICImagingFactory> factory;
	ComPtr<IStream> stream;
	ComPtr<IWICBitmapEncoder> encoder;
	ComPtr<IWICBitmapFrameEncode> frame;
	ComPtr<IPropertyBag2> props;
	WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
	if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
		FAILED(CreateStreamOnHGlobal(NULL, TRUE, &stream)) ||
		FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, NULL, &encoder)) ||
		FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
		FAILED(encoder->CreateNewFrame(&frame, &props)) ||
		FAILED(frame->Initialize(props.Get())) ||
		FAILED(frame->SetSize(width, height)) ||
		FAILED(frame->SetPixelFormat(&format)) || !IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA) ||
		FAILED(frame->WritePixels(height, width * 4, (UINT)bgra.size(), (BYTE*)bgra.data())) ||
		FAILED(frame->Commit()) || FAILED(encoder->Commit())) {
		return false;
	}
	HGLOBAL mem = NULL;
	STATSTG stat = { 0 };
	if (FAILED(GetHGlobalFromStream(stream.Get(), &mem)) || FAILED(stream->Stat(&stat, STATFLAG_NONAME))) {
		return false;
	}
	string all;
	if (!ReadGlobal(mem, all)) {
		return false;
	}
	png = all.substr(0, (size_t)stat.cbSize.QuadPart);
	return true;
}

bool ImageToPixels(const string& data, vector<BYTE>& bgra, int& width, int& height) {
	// PNG (or anything else WIC reads) to 32-bit top-down pixels
	ComScope com;
	ComPtr<IWICImagingFactory> factory;
	ComPtr<IWICStream> stream;
	ComPtr<IWICBitmapDecoder> decoder;
	ComPtr<IWICBitmapFrameDecode> frame;
	ComPtr<IWICFormatConverter> converter;
	UINT w = 0, h = 0;
	if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
		FAILED(factory->CreateStream(&stream)) ||
		FAILED(stream->InitializeFromMemory((BYTE*)data.data(), (DWORD)data.size())) ||
		FAILED(factory->CreateDecoderFromStream(stream.Get(), NULL, WICDecodeMetadataCacheOnDemand, &decoder)) ||
		FAILED(decoder->GetFrame(0, &frame)) ||
		FAILED(frame->GetSize(&w, &h)) || w == 0 || h == 0 || w > MAX_IMAGE_SIDE || h > MAX_IMAGE_SIDE ||
		FAILED(factory->CreateFormatConverter(&converter)) ||
		FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom))) {
		return false;
	}
	bgra.resize((size_t)w * h * 4);
	if (FAILED(converter->CopyPixels(NULL, w * 4, (UINT)bgra.size(), bgra.data()))) {
		return false;
	}
	width = (int)w;
	height = (int)h;
	return true;
}

static string MakeDib(const vector<BYTE>& bgra, int width, int height) {
	// A bottom-up 32-bit DIB, the layout programs reading CF_DIB expect
	BITMAPINFOHEADER bih = { sizeof(BITMAPINFOHEADER) };
	bih.biWidth = width;
	bih.biHeight = height;
	bih.biPlanes = 1;
	bih.biBitCount = 32;
	bih.biCompression = BI_RGB;
	size_t stride = (size_t)width * 4;
	bih.biSizeImage = (DWORD)(stride * height);
	string ret((const char*)&bih, sizeof(bih));
	for (int y = height - 1; y >= 0; y--) {
		ret.append((const char*)bgra.data() + y * stride, stride);
	}
	return ret;
}

static size_t PngLength(const string& data) {
	// Clipboard memory can be longer than what was put there; a PNG ends
	// after its IEND chunk's CRC
	size_t pos = data.rfind("IEND");
	return pos == string::npos || pos + 8 > data.size() ? data.size() : pos + 8;
}

static bool ReadImage(string& png) {
	// With the clipboard open: PNG if a program put it there, else the DIB
	// encoded as PNG
	string data;
	if (ReadFormat(PngFormat(), data) && data.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0) {
		png = data.substr(0, PngLength(data));
		return true;
	}
	if (!ReadFormat(CF_DIB, data) && !ReadFormat(CF_DIBV5, data)) {
		return false;
	}
	vector<BYTE> pixels;
	int width, height;
	return DibToPixels(data, pixels, width, height) && PixelsToPng(pixels, width, height, png);
}

bool WinPlatform::ReadClipboard(const string& kind, string& data) {
	// False when there's none of that kind, or the clipboard was busy
	if (kind == "text") {
		return GetClipboardText(data);
	}
	if (!OpenClipboardRetry(ClipboardOwner)) {
		return false;
	}
	bool ok = false;
	if (kind == "html") {
		ok = ReadFormat(HtmlFormat(), data);
		if (ok) {
			TrimNul(data);
			data = HtmlFragment(data);
		}
	}
	else if (kind == "rtf") {
		ok = ReadFormat(RtfFormat(), data);
		if (ok) {
			TrimNul(data);
		}
	}
	else if (kind == "files") {
		ok = IsClipboardFormatAvailable(CF_HDROP) && ReadFiles(data);
	}
	else if (kind == "image") {
		ok = ReadImage(data);
	}
	CloseClipboard();
	return ok;
}

bool WinPlatform::WriteClipboard(const vector<ClipItem>& items, string& error) {
	// Everything goes on together, replacing what was there
	vector<pair<UINT, HGLOBAL>> formats;
	auto add = [&formats](UINT format, HGLOBAL mem) {
		if (mem != NULL) {
			formats.push_back(make_pair(format, mem));
		}
		return mem != NULL;
	};
	bool ok = true;
	for (auto& item : items) {
		if (item.Kind == "text") {
			ok = add(CF_UNICODETEXT, TextGlobal(item.Data));
		}
		else if (item.Kind == "html") {
			string html = MakeHtml(item.Data);
			ok = add(HtmlFormat(), MakeGlobal(html.c_str(), html.size() + 1));
		}
		else if (item.Kind == "rtf") {
			ok = add(RtfFormat(), MakeGlobal(item.Data.c_str(), item.Data.size() + 1));
		}
		else if (item.Kind == "files") {
			DWORD effect = DROPEFFECT_COPY_VALUE;
			ok = add(CF_HDROP, FilesGlobal(item.Data)) && add(Format(CFSTR_PREFERREDDROPEFFECT), MakeGlobal(&effect, sizeof(effect)));
		}
		else if (item.Kind == "image") {
			// PNG for programs that take it, and a DIB for the rest
			vector<BYTE> pixels;
			int width, height;
			if (!ImageToPixels(item.Data, pixels, width, height)) {
				error = "That image couldn't be read";
				ok = false;
			}
			else {
				string dib = MakeDib(pixels, width, height);
				ok = add(PngFormat(), MakeGlobal(item.Data.data(), item.Data.size())) && add(CF_DIB, MakeGlobal(dib.data(), dib.size()));
			}
		}
		else {
			error = "Unknown kind of clipboard data: " + item.Kind;
			ok = false;
		}
		if (!ok) {
			break;
		}
	}
	if (ok && !OpenClipboardRetry(ClipboardOwner)) {
		ok = false;
	}
	if (!ok) {
		for (auto& cur : formats) {
			GlobalFree(cur.second);
		}
		return false;
	}
	EmptyClipboard();
	for (auto& cur : formats) {
		if (SetClipboardData(cur.first, cur.second) == NULL) {
			GlobalFree(cur.second);
			ok = false;
		}
	}
	CloseClipboard();
	return ok;
}

static bool IsHandleFormat(UINT format) {
	// Formats whose data is a GDI or private handle rather than memory, so it
	// can't be copied as bytes.  CF_BITMAP is made from CF_DIB anyway.
	switch (format) {
	case CF_BITMAP: case CF_METAFILEPICT: case CF_PALETTE: case CF_OWNERDISPLAY:
	case CF_DSPBITMAP: case CF_DSPMETAFILEPICT: case CF_DSPENHMETAFILE:
		return true;
	}
	return (format >= CF_PRIVATEFIRST && format <= CF_PRIVATELAST) || (format >= CF_GDIOBJFIRST && format <= CF_GDIOBJLAST);
}

static string FormatName(UINT format) {
	// Registered formats are saved by name, since their numbers differ
	// between sessions; the standard ones by number
	if (format >= 0xC000) {
		wchar_t name[256];
		int len = GetClipboardFormatNameW(format, name, 256);
		if (len > 0) {
			return Utf16ToUtf8(name, len);
		}
	}
	return "#" + to_string(format);
}

bool WinPlatform::SaveClipboard(vector<ClipItem>& items) {
	if (!OpenClipboardRetry(ClipboardOwner)) {
		return false;
	}
	items.clear();
	for (UINT format = EnumClipboardFormats(0); format != 0; format = EnumClipboardFormats(format)) {
		ClipItem item;
		item.Kind = FormatName(format);
		if (format == CF_ENHMETAFILE) {
			// Office copies drawings as metafiles, which have their own handle type
			HENHMETAFILE emf = (HENHMETAFILE)GetClipboardData(format);
			UINT size = emf != NULL ? GetEnhMetaFileBits(emf, 0, NULL) : 0;
			if (size > 0) {
				item.Data.resize(size);
				GetEnhMetaFileBits(emf, size, (BYTE*)&item.Data[0]);
				items.push_back(item);
			}
		}
		else if (!IsHandleFormat(format) && ReadGlobal(GetClipboardData(format), item.Data)) {
			items.push_back(item);
		}
	}
	CloseClipboard();
	return true;
}

bool WinPlatform::RestoreClipboard(const vector<ClipItem>& items) {
	if (!OpenClipboardRetry(ClipboardOwner)) {
		return false;
	}
	EmptyClipboard();
	bool ok = true;
	for (auto& item : items) {
		UINT format = item.Kind.size() > 1 && item.Kind[0] == '#' ? (UINT)strtoul(item.Kind.c_str() + 1, NULL, 10) : Format(Utf8ToUtf16(item.Kind).c_str());
		HANDLE data;
		if (format == CF_ENHMETAFILE) {
			data = SetEnhMetaFileBits((UINT)item.Data.size(), (const BYTE*)item.Data.data());
		}
		else {
			data = MakeGlobal(item.Data.data(), item.Data.size());
		}
		if (format == 0 || data == NULL || SetClipboardData(format, data) == NULL) {
			if (data != NULL) {
				if (format == CF_ENHMETAFILE) {
					DeleteEnhMetaFile((HENHMETAFILE)data);
				}
				else {
					GlobalFree(data);
				}
			}
			ok = false;
		}
	}
	CloseClipboard();
	return ok;
}

bool WinPlatform::ClipboardConcealed() {
	// Password managers mark what they copy so clipboard tools leave it out
	if (IsClipboardFormatAvailable(Format(L"ExcludeClipboardContentFromMonitorProcessing")) ||
		IsClipboardFormatAvailable(Format(L"Clipboard Viewer Ignore"))) {
		return true;
	}
	UINT history = Format(L"CanIncludeInClipboardHistory");
	if (!IsClipboardFormatAvailable(history) || !OpenClipboardRetry(ClipboardOwner)) {
		return false;
	}
	string data;
	bool hidden = ReadGlobal(GetClipboardData(history), data) && data.size() >= sizeof(DWORD) && *(const DWORD*)data.data() == 0;
	CloseClipboard();
	return hidden;
}
