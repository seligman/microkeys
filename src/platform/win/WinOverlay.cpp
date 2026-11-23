#include "WinOverlay.h"
#include <shellscalingapi.h>

#define OVERLAY_ALPHA		240
#define OVERLAY_FADE_STEPS	10
#define TIMER_HIDE			1
#define TIMER_FADE			2

#define TEXT_FLAGS			(DT_CENTER | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX | DT_EXPANDTABS)

static LRESULT CALLBACK OverlayProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	if (message == WM_NCCREATE) {
		SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)lParam)->lpCreateParams);
	}
	Overlay* overlay = (Overlay*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
	if (overlay != nullptr) {
		return overlay->WndProc(hWnd, message, wParam, lParam);
	}
	return DefWindowProcW(hWnd, message, wParam, lParam);
}

static int Scale(int value, UINT dpi) {
	return MulDiv(value, dpi, 96);
}

Overlay::Overlay(HINSTANCE hInst) : _hInst(hInst) {
	_background = CreateSolidBrush(RGB(32, 32, 32));
	_border = CreateSolidBrush(RGB(80, 80, 80));
}

Overlay::~Overlay() {
	if (_hWnd != NULL) {
		DestroyWindow(_hWnd);
	}
	if (_font != NULL) {
		DeleteObject(_font);
	}
	DeleteObject(_background);
	DeleteObject(_border);
}

void Overlay::Create() {
	WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
	wcex.lpfnWndProc = OverlayProc;
	wcex.hInstance = _hInst;
	wcex.lpszClassName = OVERLAY_WINDOW_CLASS;
	RegisterClassExW(&wcex);

	// Layered and transparent so clicks go through it, and a tool window so
	// it stays out of the taskbar and Alt+Tab
	_hWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
		OVERLAY_WINDOW_CLASS, L"", WS_POPUP, 0, 0, 10, 10, NULL, NULL, _hInst, this);
}

void Overlay::Show(const wstring& text, int timeoutMs) {
	// A negative timeout leaves it up until the next Show or Hide
	if (text.empty()) {
		Hide();
		return;
	}
	if (_hWnd == NULL) {
		Create();
		if (_hWnd == NULL) {
			return;
		}
	}
	KillTimer(_hWnd, TIMER_FADE);
	if (!IsWindowVisible(_hWnd)) {
		// Painted before it can be seen, so it never shows stale pixels
		_text = text;
		SetLayeredWindowAttributes(_hWnd, 0, 0, LWA_ALPHA);
		Place(true);
		UpdateWindow(_hWnd);
	}
	else if (text != _text) {
		_text = text;
		Place(false);
		InvalidateRect(_hWnd, NULL, FALSE);
		UpdateWindow(_hWnd);
	}
	else {
		// The same text again: just stay in front
		SetWindowPos(_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
	}
	_alpha = OVERLAY_ALPHA;
	SetLayeredWindowAttributes(_hWnd, 0, (BYTE)_alpha, LWA_ALPHA);
	if (timeoutMs >= 0) {
		SetTimer(_hWnd, TIMER_HIDE, (UINT)max(timeoutMs, USER_TIMER_MINIMUM), NULL);
	}
	else {
		KillTimer(_hWnd, TIMER_HIDE);
	}
}

void Overlay::Hide() {
	if (_hWnd == NULL) {
		return;
	}
	KillTimer(_hWnd, TIMER_HIDE);
	KillTimer(_hWnd, TIMER_FADE);
	ShowWindow(_hWnd, SW_HIDE);
	_text.clear();
}

void Overlay::Place(bool newMonitor) {
	// Centred near the bottom of the screen the user is working on.  Once
	// up, it stays on that screen while its text changes.
	if (newMonitor || _monitor == NULL) {
		HWND front = GetForegroundWindow();
		POINT pt = { 0, 0 };
		GetCursorPos(&pt);
		_monitor = front != NULL ? MonitorFromWindow(front, MONITOR_DEFAULTTONEAREST) : MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
	}
	MONITORINFO info = { sizeof(info) };
	if (!GetMonitorInfoW(_monitor, &info)) {
		_monitor = MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
		GetMonitorInfoW(_monitor, &info);
	}
	UINT dpiX = 96, dpiY = 96;
	if (FAILED(GetDpiForMonitor(_monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) {
		dpiX = 96;
	}
	if (_font == NULL || dpiX != _dpi) {
		NONCLIENTMETRICSW metrics = { sizeof(metrics) };
		SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpiX);
		LOGFONTW font = metrics.lfMessageFont;
		font.lfHeight = -MulDiv(14, dpiX, 72);
		font.lfQuality = CLEARTYPE_QUALITY;
		if (_font != NULL) {
			DeleteObject(_font);
		}
		_font = CreateFontIndirectW(&font);
		_dpi = dpiX;
	}

	RECT work = info.rcWork;
	int workWidth = work.right - work.left;
	int workHeight = work.bottom - work.top;
	int padX = Scale(20, _dpi);
	int padY = Scale(12, _dpi);
	RECT text = { 0, 0, max(workWidth * 6 / 10 - 2 * padX, 1), 0 };
	HDC hdc = GetDC(_hWnd);
	HGDIOBJ old = SelectObject(hdc, _font);
	DrawTextW(hdc, _text.c_str(), (int)_text.size(), &text, TEXT_FLAGS | DT_CALCRECT);
	SelectObject(hdc, old);
	ReleaseDC(_hWnd, hdc);

	int width = min((int)text.right + 2 * padX, workWidth);
	int height = min((int)text.bottom + 2 * padY, workHeight / 2);
	int x = work.left + (workWidth - width) / 2;
	int y = work.bottom - workHeight / 10 - height;
	int round = Scale(16, _dpi);
	SetWindowRgn(_hWnd, CreateRoundRectRgn(0, 0, width + 1, height + 1, round, round), FALSE);
	SetWindowPos(_hWnd, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void Overlay::Paint(HDC hdc) {
	// Drawn off screen first, so quick updates don't flicker
	RECT rt;
	GetClientRect(_hWnd, &rt);
	HDC mem = CreateCompatibleDC(hdc);
	HBITMAP bitmap = CreateCompatibleBitmap(hdc, rt.right, rt.bottom);
	HGDIOBJ oldBitmap = SelectObject(mem, bitmap);
	FillRect(mem, &rt, _background);
	int round = Scale(16, _dpi);
	HRGN edge = CreateRoundRectRgn(0, 0, rt.right + 1, rt.bottom + 1, round, round);
	FrameRgn(mem, edge, _border, 1, 1);
	DeleteObject(edge);

	RECT text = rt;
	InflateRect(&text, -Scale(20, _dpi), -Scale(12, _dpi));
	SetBkMode(mem, TRANSPARENT);
	SetTextColor(mem, RGB(255, 255, 255));
	HGDIOBJ oldFont = SelectObject(mem, _font);
	DrawTextW(mem, _text.c_str(), (int)_text.size(), &text, TEXT_FLAGS);
	SelectObject(mem, oldFont);

	BitBlt(hdc, 0, 0, rt.right, rt.bottom, mem, 0, 0, SRCCOPY);
	SelectObject(mem, oldBitmap);
	DeleteObject(bitmap);
	DeleteDC(mem);
}

LRESULT Overlay::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hWnd, &ps);
		Paint(hdc);
		EndPaint(hWnd, &ps);
		return 0;
	}

	case WM_ERASEBKGND:
		return 1;

	case WM_NCHITTEST:
		return HTTRANSPARENT;

	case WM_MOUSEACTIVATE:
		return MA_NOACTIVATE;

	case WM_DPICHANGED:
		// Place() sizes it for the monitor it's on
		return 0;

	case WM_TIMER:
		if (wParam == TIMER_HIDE) {
			KillTimer(hWnd, TIMER_HIDE);
			SetTimer(hWnd, TIMER_FADE, 15, NULL);
		}
		else if (wParam == TIMER_FADE) {
			_alpha -= OVERLAY_ALPHA / OVERLAY_FADE_STEPS;
			if (_alpha <= 0) {
				Hide();
			}
			else {
				SetLayeredWindowAttributes(hWnd, 0, (BYTE)_alpha, LWA_ALPHA);
			}
		}
		return 0;
	}
	return DefWindowProcW(hWnd, message, wParam, lParam);
}
