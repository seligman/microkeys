#include "WinPlatform.h"
#include "WinText.h"
#include "../../core/Image.h"
#include <roapi.h>
#include <winstring.h>
#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>
#include <windows.foundation.h>
#include <windows.foundation.collections.h>
#include <windows.globalization.h>
#include <windows.graphics.imaging.h>
#include <windows.media.ocr.h>
#include <MemoryBuffer.h>
#include <math.h>
#include <algorithm>
#include <limits.h>

// Reading text off the screen with the OCR engine Windows 10 and later have
// built in (Windows.Media.Ocr), so there's no model to ship.  It reads the
// languages whose OCR component is installed, which includes the language
// Windows was set up in.

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HString;
using Microsoft::WRL::Wrappers::HStringReference;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Foundation::Collections;
using namespace ABI::Windows::Globalization;
using namespace ABI::Windows::Graphics::Imaging;
using namespace ABI::Windows::Media::Ocr;

#define RECOGNIZE_TIMEOUT_MS	30000
#define OCR_MARGIN				24

static string FromHString(HSTRING text) {
	UINT32 len = 0;
	const wchar_t* raw = WindowsGetStringRawBuffer(text, &len);
	return raw == NULL ? string() : Utf16ToUtf8(raw, len);
}

static vector<string> LanguageTags(IOcrEngineStatics* statics) {
	vector<string> ret;
	ComPtr<IVectorView<Language*>> languages;
	unsigned count = 0;
	if (FAILED(statics->get_AvailableRecognizerLanguages(&languages)) || FAILED(languages->get_Size(&count))) {
		return ret;
	}
	for (unsigned i = 0; i < count; i++) {
		ComPtr<ILanguage> language;
		HString tag;
		if (SUCCEEDED(languages->GetAt(i, &language)) && SUCCEEDED(language->get_LanguageTag(tag.GetAddressOf()))) {
			ret.push_back(FromHString(tag.Get()));
		}
	}
	return ret;
}

static string Joined(const vector<string>& items) {
	string ret;
	for (auto& cur : items) {
		ret += (ret.empty() ? "" : ", ") + cur;
	}
	return ret.empty() ? "none" : ret;
}

static bool CreateEngine(IOcrEngineStatics* statics, const string& tag, ComPtr<IOcrEngine>& engine, string& error) {
	const string addOne = "; add a language with OCR in Settings, Time & language, Language & region";
	if (tag.empty()) {
		if (FAILED(statics->TryCreateFromUserProfileLanguages(&engine)) || !engine) {
			error = "None of your languages can be read by OCR (installed: " + Joined(LanguageTags(statics)) + ")" + addOne;
			return false;
		}
		return true;
	}
	ComPtr<ILanguageFactory> factory;
	ComPtr<ILanguage> language;
	wstring wide = Utf8ToUtf16(tag);
	boolean supported = false;
	if (FAILED(RoGetActivationFactory(HStringReference(RuntimeClass_Windows_Globalization_Language).Get(), IID_PPV_ARGS(&factory))) ||
		FAILED(factory->CreateLanguage(HStringReference(wide.c_str(), (unsigned)wide.size()).Get(), &language))) {
		error = "\"" + tag + "\" isn't a language tag like \"en-US\"";
		return false;
	}
	if (FAILED(statics->IsLanguageSupported(language.Get(), &supported)) || !supported ||
		FAILED(statics->TryCreateFromLanguage(language.Get(), &engine)) || !engine) {
		error = "OCR for " + tag + " isn't installed (installed: " + Joined(LanguageTags(statics)) + ")" + addOne;
		return false;
	}
	return true;
}

static uint32_t BorderColor(const Bitmap& bitmap) {
	// The commonest colour around the edge: the background, mostly
	map<uint32_t, int> counts;
	const uint32_t* pixels = (const uint32_t*)bitmap.Pixels.data();
	for (int x = 0; x < bitmap.Width; x++) {
		counts[pixels[x]]++;
		counts[pixels[(size_t)(bitmap.Height - 1) * bitmap.Width + x]]++;
	}
	for (int y = 0; y < bitmap.Height; y++) {
		counts[pixels[(size_t)y * bitmap.Width]]++;
		counts[pixels[(size_t)y * bitmap.Width + bitmap.Width - 1]]++;
	}
	uint32_t ret = 0xFFFFFFFF;
	int most = 0;
	for (auto& cur : counts) {
		if (cur.second > most) {
			most = cur.second;
			ret = cur.first;
		}
	}
	return ret;
}

static bool ToSoftwareBitmap(const Bitmap& bitmap, ComPtr<ISoftwareBitmap>& result, string& error) {
	// The image goes in the middle of a margin of its background, since OCR
	// reads nothing that's right up against the edge, such as a single
	// button's text
	ComPtr<ISoftwareBitmapFactory> factory;
	ComPtr<ISoftwareBitmap> soft;
	int width = bitmap.Width + OCR_MARGIN * 2, height = bitmap.Height + OCR_MARGIN * 2;
	if (FAILED(RoGetActivationFactory(HStringReference(RuntimeClass_Windows_Graphics_Imaging_SoftwareBitmap).Get(), IID_PPV_ARGS(&factory))) ||
		FAILED(factory->Create(BitmapPixelFormat_Bgra8, width, height, &soft))) {
		error = "Not enough memory for the image";
		return false;
	}
	ComPtr<IBitmapBuffer> buffer;
	ComPtr<IMemoryBuffer> memory;
	ComPtr<IMemoryBufferReference> reference;
	ComPtr<::Windows::Foundation::IMemoryBufferByteAccess> access;
	BitmapPlaneDescription plane = { 0 };
	BYTE* data = NULL;
	UINT32 capacity = 0;
	bool ok = SUCCEEDED(soft->LockBuffer(BitmapBufferAccessMode_Write, &buffer)) &&
		SUCCEEDED(buffer->GetPlaneDescription(0, &plane)) &&
		SUCCEEDED(buffer.As(&memory)) && SUCCEEDED(memory->CreateReference(&reference)) &&
		SUCCEEDED(reference.As(&access)) && SUCCEEDED(access->GetBuffer(&data, &capacity));
	size_t row = (size_t)bitmap.Width * 4;
	if (ok && (plane.Stride < width * 4 || (size_t)plane.StartIndex + (size_t)plane.Stride * (height - 1) + (size_t)width * 4 > capacity)) {
		ok = false;
	}
	if (ok) {
		uint32_t background = BorderColor(bitmap);
		for (int y = 0; y < height; y++) {
			uint32_t* line = (uint32_t*)(data + plane.StartIndex + (size_t)plane.Stride * y);
			fill(line, line + width, background);
		}
		for (int y = 0; y < bitmap.Height; y++) {
			memcpy(data + plane.StartIndex + (size_t)plane.Stride * (y + OCR_MARGIN) + OCR_MARGIN * 4, bitmap.Pixels.data() + row * y, row);
		}
	}
	// The bitmap stays locked until both are closed
	ComPtr<IClosable> closer;
	if (reference && SUCCEEDED(reference.As(&closer))) {
		closer->Close();
	}
	if (buffer && SUCCEEDED(buffer.As(&closer))) {
		closer->Close();
	}
	if (!ok) {
		error = "Unable to hand the image to OCR";
		return false;
	}
	result = soft;
	return true;
}

static bool Recognize(IOcrEngine* engine, ISoftwareBitmap* soft, ComPtr<IOcrResult>& result, string& error) {
	ComPtr<IAsyncOperation<OcrResult*>> op;
	ComPtr<IAsyncInfo> info;
	HRESULT hr = engine->RecognizeAsync(soft, &op);
	if (FAILED(hr) || FAILED(op.As(&info))) {
		error = "OCR didn't start: " + LastErrorText(hr);
		return false;
	}
	// Recognizing takes a few hundred milliseconds, so checking now and then
	// costs nothing next to it
	AsyncStatus status = AsyncStatus::Started;
	ULONGLONG start = GetTickCount64();
	while (SUCCEEDED(info->get_Status(&status)) && status == AsyncStatus::Started) {
		if (GetTickCount64() - start > RECOGNIZE_TIMEOUT_MS) {
			info->Cancel();
			error = "OCR took too long";
			return false;
		}
		Sleep(5);
	}
	if (status != AsyncStatus::Completed) {
		info->get_ErrorCode(&hr);
		error = "OCR failed: " + LastErrorText(hr);
		return false;
	}
	if (FAILED(op->GetResults(&result)) || !result) {
		error = "OCR gave no result";
		return false;
	}
	return true;
}

static bool ReadWords(const Bitmap& original, const string& language, int scale, vector<TextWord>& words, string& error) {
	ComPtr<IOcrEngineStatics> statics;
	if (FAILED(RoGetActivationFactory(HStringReference(RuntimeClass_Windows_Media_Ocr_OcrEngine).Get(), IID_PPV_ARGS(&statics)))) {
		error = "Windows OCR isn't available";
		return false;
	}
	UINT32 most = 0;
	int room = SUCCEEDED(statics->get_MaxImageDimension(&most)) && most > OCR_MARGIN * 2 ? (int)most - OCR_MARGIN * 2 : INT_MAX;
	if (original.Width > room || original.Height > room) {
		error = "The image is too big to read at once (" + to_string(original.Width) + "x" + to_string(original.Height) +
			"); the most OCR takes is " + to_string(room) + " pixels across";
		return false;
	}
	scale = max(scale, 1);
	while (scale > 1 && ((int64_t)original.Width * scale > room || (int64_t)original.Height * scale > room)) {
		scale--;
	}
	Bitmap bitmap = PrepareForOcr(original, scale);
	ComPtr<IOcrEngine> engine;
	ComPtr<ISoftwareBitmap> soft;
	ComPtr<IOcrResult> result;
	if (!CreateEngine(statics.Get(), language, engine, error) || !ToSoftwareBitmap(bitmap, soft, error) ||
		!Recognize(engine.Get(), soft.Get(), result, error)) {
		return false;
	}
	ComPtr<IVectorView<OcrLine*>> lines;
	unsigned lineCount = 0;
	if (FAILED(result->get_Lines(&lines)) || !lines || FAILED(lines->get_Size(&lineCount))) {
		return true;
	}
	for (unsigned i = 0; i < lineCount; i++) {
		ComPtr<IOcrLine> line;
		ComPtr<IVectorView<OcrWord*>> lineWords;
		unsigned wordCount = 0;
		if (FAILED(lines->GetAt(i, &line)) || FAILED(line->get_Words(&lineWords)) || !lineWords || FAILED(lineWords->get_Size(&wordCount))) {
			continue;
		}
		for (unsigned j = 0; j < wordCount; j++) {
			ComPtr<IOcrWord> word;
			HString text;
			Rect rect = { 0 };
			if (FAILED(lineWords->GetAt(j, &word)) || FAILED(word->get_Text(text.GetAddressOf())) || FAILED(word->get_BoundingRect(&rect))) {
				continue;
			}
			TextWord cur;
			cur.Text = FromHString(text.Get());
			// Back to the original image's pixels
			int left = max((int)floor((rect.X - OCR_MARGIN) / scale), 0);
			int top = max((int)floor((rect.Y - OCR_MARGIN) / scale), 0);
			int right = min((int)ceil((rect.X + rect.Width - OCR_MARGIN) / scale), original.Width);
			int bottom = min((int)ceil((rect.Y + rect.Height - OCR_MARGIN) / scale), original.Height);
			if (right <= left || bottom <= top) {
				continue;
			}
			cur.Bounds.X = left;
			cur.Bounds.Y = top;
			cur.Bounds.Width = right - left;
			cur.Bounds.Height = bottom - top;
			cur.Line = (int)i;
			words.push_back(cur);
		}
	}
	return true;
}

template <class T>
static T OnWinRtThread(function<T()> work) {
	// WinRT wants a thread in the multithreaded apartment, and the script
	// thread may already be single-threaded for UI Automation, so the work
	// runs on a thread of its own
	T ret;
	thread worker([&] {
		HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
		ret = work();
		if (SUCCEEDED(hr)) {
			RoUninitialize();
		}
	});
	worker.join();
	return ret;
}

bool WinPlatform::ReadText(const Bitmap& bitmap, const string& language, int scale, vector<TextWord>& words, string& error) {
	if (bitmap.Width <= 0 || bitmap.Height <= 0 || bitmap.Pixels.size() != (size_t)bitmap.Width * bitmap.Height * 4) {
		error = "The image is empty";
		return false;
	}
	return OnWinRtThread<bool>([&] { return ReadWords(bitmap, language, scale, words, error); });
}

vector<string> WinPlatform::TextLanguages() {
	return OnWinRtThread<vector<string>>([] {
		ComPtr<IOcrEngineStatics> statics;
		if (FAILED(RoGetActivationFactory(HStringReference(RuntimeClass_Windows_Media_Ocr_OcrEngine).Get(), IID_PPV_ARGS(&statics)))) {
			return vector<string>();
		}
		return LanguageTags(statics.Get());
	});
}
