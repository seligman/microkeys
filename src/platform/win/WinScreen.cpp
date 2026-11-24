#include "WinPlatform.h"
#include "WinText.h"

// The screen: copying its pixels, and images to and from PNG for finding
// them on it.  MicroKeys is per-monitor DPI aware, so screen coordinates are
// real pixels on every monitor.

#define MAX_CAPTURE_PIXELS	(256 * 1024 * 1024 / 4)

bool WinPlatform::CaptureScreen(const ScreenRect& rect, Bitmap& bitmap, string& error) {
	if (rect.Width <= 0 || rect.Height <= 0 || (int64_t)rect.Width * rect.Height > MAX_CAPTURE_PIXELS) {
		error = "The area to capture is empty or too big";
		return false;
	}
	HDC screen = GetDC(NULL);
	if (screen == NULL) {
		error = "Unable to read the screen: " + LastErrorText();
		return false;
	}
	BITMAPINFO bmi = { 0 };
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = rect.Width;
	bmi.bmiHeader.biHeight = -rect.Height;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;
	void* bits = NULL;
	HDC mem = CreateCompatibleDC(screen);
	HBITMAP dib = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
	bool ok = mem != NULL && dib != NULL;
	if (ok) {
		HGDIOBJ old = SelectObject(mem, dib);
		// CAPTUREBLT takes in layered windows, such as menus and tooltips
		ok = BitBlt(mem, 0, 0, rect.Width, rect.Height, screen, rect.X, rect.Y, SRCCOPY | CAPTUREBLT) != FALSE;
		SelectObject(mem, old);
		if (!ok) {
			error = "Unable to copy the screen: " + LastErrorText();
		}
	}
	else {
		error = "Not enough memory to capture the screen";
	}
	if (ok) {
		GdiFlush();
		size_t size = (size_t)rect.Width * rect.Height * 4;
		bitmap.Width = rect.Width;
		bitmap.Height = rect.Height;
		bitmap.Pixels.assign((const char*)bits, size);
		// The screen has no alpha; BitBlt leaves it zero
		for (size_t i = 3; i < size; i += 4) {
			bitmap.Pixels[i] = (char)0xFF;
		}
	}
	if (dib != NULL) {
		DeleteObject(dib);
	}
	if (mem != NULL) {
		DeleteDC(mem);
	}
	ReleaseDC(NULL, screen);
	return ok;
}

bool WinPlatform::DecodeImage(const string& data, Bitmap& bitmap, string& error) {
	vector<BYTE> pixels;
	int width = 0, height = 0;
	if (!ImageToPixels(data, pixels, width, height)) {
		error = "That isn't an image Windows can read";
		return false;
	}
	bitmap.Width = width;
	bitmap.Height = height;
	bitmap.Pixels.assign((const char*)pixels.data(), pixels.size());
	return true;
}

bool WinPlatform::EncodePng(const Bitmap& bitmap, string& png, string& error) {
	vector<BYTE> pixels(bitmap.Pixels.begin(), bitmap.Pixels.end());
	if (!PixelsToPng(pixels, bitmap.Width, bitmap.Height, png)) {
		error = "Unable to make a PNG of the image";
		return false;
	}
	return true;
}
