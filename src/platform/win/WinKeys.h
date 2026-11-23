#pragma once

#include "framework.h"

class KeyCode {
	// A key as Windows sees it, plus any modifiers the active keyboard layout
	// needs to produce a character (Shift for "+" on a US layout)
public:
	UINT VK = 0;
	bool Extended = false;
	int ImpliedMods = 0;
};

bool LookupKey(const string& name, HKL layout, KeyCode& code);
bool IsKnownKeyName(const string& name);
HKL ForegroundLayout();
UINT ModsToHotkeyFlags(int mods);
