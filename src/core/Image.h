#pragma once

#include "Platform.h"

// Looking for one picture inside another: the part of finding an image on
// screen that doesn't need the OS.  And getting a screenshot ready for OCR.

bool FindImage(const Bitmap& haystack, const Bitmap& needle, int tolerance, size_t limit, vector<pair<int, int>>& found);
Bitmap PrepareForOcr(const Bitmap& image, int scale);
