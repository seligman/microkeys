#pragma once

#include "framework.h"

#define OVERLAY_WINDOW_CLASS	L"MicroKeysOverlay"

class Overlay {
	// The message ui.show() puts up: a dark box near the bottom of the screen,
	// above every other window, that never takes the focus or the mouse.
	// UI thread only.
public:
	Overlay(HINSTANCE hInst);
	~Overlay();
	void Show(const wstring& text, int timeoutMs);
	void Hide();
	LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
	void Create();
	void Place(bool newMonitor);
	void Paint(HDC hdc);

	HINSTANCE _hInst;
	HWND _hWnd = NULL;
	HFONT _font = NULL;
	UINT _dpi = 0;
	HMONITOR _monitor = NULL;
	HBRUSH _background = NULL;
	HBRUSH _border = NULL;
	wstring _text;
	int _alpha = 0;
};
