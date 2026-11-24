#include "Image.h"
#include <algorithm>
#include <string.h>

static bool Close(const uint8_t* a, const uint8_t* b, int tolerance) {
	// Blue, green, and red each within the tolerance
	return abs(a[0] - b[0]) <= tolerance && abs(a[1] - b[1]) <= tolerance && abs(a[2] - b[2]) <= tolerance;
}

bool FindImage(const Bitmap& haystack, const Bitmap& needle, int tolerance, size_t limit, vector<pair<int, int>>& found) {
	// Every place the needle appears in the haystack, top to bottom and left
	// to right, up to limit of them.  Needle pixels that are mostly
	// transparent match anything.  A match overlapping one already found is
	// skipped, so each copy of the needle is found once.
	found.clear();
	int width = haystack.Width;
	int height = haystack.Height;
	int nw = needle.Width;
	int nh = needle.Height;
	if (nw <= 0 || nh <= 0 || nw > width || nh > height || limit == 0 ||
		haystack.Pixels.size() < (size_t)width * height * 4 || needle.Pixels.size() < (size_t)nw * nh * 4) {
		return false;
	}

	const uint8_t* hay = (const uint8_t*)haystack.Pixels.data();
	const uint8_t* pins = (const uint8_t*)needle.Pixels.data();
	vector<pair<int, int>> points;
	for (int y = 0; y < nh; y++) {
		for (int x = 0; x < nw; x++) {
			if (pins[((size_t)y * nw + x) * 4 + 3] >= 128) {
				points.push_back(make_pair(x, y));
			}
		}
	}

	size_t stride = (size_t)width * 4;
	for (int y = 0; y <= height - nh; y++) {
		for (int x = 0; x <= width - nw; x++) {
			bool hit = true;
			for (auto& pt : points) {
				if (!Close(hay + (size_t)(y + pt.second) * stride + (size_t)(x + pt.first) * 4, pins + ((size_t)pt.second * nw + pt.first) * 4, tolerance)) {
					hit = false;
					break;
				}
			}
			if (!hit) {
				continue;
			}
			bool overlaps = false;
			for (auto& prev : found) {
				if (abs(prev.first - x) < nw && abs(prev.second - y) < nh) {
					overlaps = true;
					break;
				}
			}
			if (overlaps) {
				continue;
			}
			found.push_back(make_pair(x, y));
			if (found.size() >= limit) {
				return true;
			}
		}
	}
	return !found.empty();
}

// OCR reads ordinary lines of text well, but misses words in a box (buttons,
// text boxes), garbles underlined ones, and misreads small ones.  So thin
// straight lines are painted over with what's beside them, and the image is
// scaled up.  Letters' strokes are kept: a horizontal line has to be longer
// than any letter's, and a vertical one has to join horizontal lines at
// both ends, as the sides of a box do.

#define LINE_CONTRAST	24
#define LINE_THICKNESS	3
#define LINE_MIN_ACROSS	24
#define LINE_MIN_DOWN	6
#define LINE_JOIN		3

static int Luma(const uint8_t* pixel) {
	return (pixel[2] * 299 + pixel[1] * 587 + pixel[0] * 114) / 1000;
}

static vector<uint8_t> ThinLines(const vector<int>& luma, int width, int height, bool rows) {
	// Pixels on a line at most LINE_THICKNESS thick: across the rows for
	// horizontal lines, across the columns for vertical ones, it takes few
	// steps either way to reach a pixel that differs
	vector<uint8_t> ret((size_t)width * height, 0);
	int step = rows ? width : 1;
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int pos = rows ? y : x;
			int size = rows ? height : width;
			size_t i = (size_t)y * width + x;
			int c = luma[i];
			auto reach = [&](int dir) {
				for (int k = 1; k <= LINE_THICKNESS; k++) {
					int at = pos + k * dir;
					if (at < 0 || at >= size) {
						break;
					}
					if (abs(c - luma[i + (ptrdiff_t)k * dir * step]) > LINE_CONTRAST) {
						return k;
					}
				}
				return LINE_THICKNESS * 2;
			};
			ret[i] = reach(-1) + reach(1) - 1 <= LINE_THICKNESS;
		}
	}
	return ret;
}

static bool Near(const vector<uint8_t>& mask, int width, int height, int x, int y) {
	for (int yy = max(y - LINE_JOIN, 0); yy <= min(y + LINE_JOIN, height - 1); yy++) {
		for (int xx = max(x - LINE_JOIN, 0); xx <= min(x + LINE_JOIN, width - 1); xx++) {
			if (mask[(size_t)yy * width + xx]) {
				return true;
			}
		}
	}
	return false;
}

class Sample {
	// Where one new pixel's middle falls among the old ones: the two it's
	// between, and how far toward the second, out of 256
public:
	int First = 0;
	int Second = 0;
	int Weight = 0;
};

static vector<Sample> Samples(int size, int scale) {
	vector<Sample> ret(size * scale);
	for (int i = 0; i < size * scale; i++) {
		int pos = max(((i * 2 + 1) * 256) / (scale * 2) - 128, 0);
		ret[i].First = min(pos >> 8, size - 1);
		ret[i].Second = min(ret[i].First + 1, size - 1);
		ret[i].Weight = pos & 255;
	}
	return ret;
}

static Bitmap ScaleUp(const Bitmap& image, int scale) {
	// Bilinear, in fixed point
	Bitmap ret;
	ret.Width = image.Width * scale;
	ret.Height = image.Height * scale;
	ret.Pixels.resize((size_t)ret.Width * ret.Height * 4);
	const uint8_t* src = (const uint8_t*)image.Pixels.data();
	uint8_t* dst = (uint8_t*)&ret.Pixels[0];
	vector<Sample> across = Samples(image.Width, scale), down = Samples(image.Height, scale);
	for (int y = 0; y < ret.Height; y++) {
		const Sample& sy = down[y];
		const uint8_t* row0 = src + (size_t)sy.First * image.Width * 4;
		const uint8_t* row1 = src + (size_t)sy.Second * image.Width * 4;
		uint8_t* out = dst + (size_t)y * ret.Width * 4;
		for (int x = 0; x < ret.Width; x++, out += 4) {
			const Sample& sx = across[x];
			const uint8_t *a = row0 + sx.First * 4, *b = row0 + sx.Second * 4, *c = row1 + sx.First * 4, *d = row1 + sx.Second * 4;
			for (int ch = 0; ch < 4; ch++) {
				int top = a[ch] * 256 + (b[ch] - a[ch]) * sx.Weight;
				int bottom = c[ch] * 256 + (d[ch] - c[ch]) * sx.Weight;
				out[ch] = (uint8_t)((top * 256 + (bottom - top) * sy.Weight + 32768) >> 16);
			}
		}
	}
	return ret;
}

Bitmap PrepareForOcr(const Bitmap& image, int scale) {
	int width = image.Width, height = image.Height;
	if (width <= 0 || height <= 0 || image.Pixels.size() < (size_t)width * height * 4) {
		return image;
	}
	const uint8_t* src = (const uint8_t*)image.Pixels.data();
	vector<int> luma((size_t)width * height);
	for (size_t i = 0; i < luma.size(); i++) {
		luma[i] = Luma(src + i * 4);
	}

	vector<uint8_t> thin = ThinLines(luma, width, height, true);
	vector<uint8_t> across((size_t)width * height, 0);
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width;) {
			int end = x;
			while (end < width && thin[(size_t)y * width + end]) {
				end++;
			}
			if (end - x >= LINE_MIN_ACROSS) {
				fill(across.begin() + (size_t)y * width + x, across.begin() + (size_t)y * width + end, 1);
			}
			x = max(end, x + 1);
		}
	}
	thin = ThinLines(luma, width, height, false);
	vector<uint8_t> down((size_t)width * height, 0);
	for (int x = 0; x < width; x++) {
		for (int y = 0; y < height;) {
			int end = y;
			while (end < height && thin[(size_t)end * width + x]) {
				end++;
			}
			if (end - y >= LINE_MIN_DOWN && Near(across, width, height, x, y) && Near(across, width, height, x, end - 1)) {
				for (int yy = y; yy < end; yy++) {
					down[(size_t)yy * width + x] = 1;
				}
			}
			y = max(end, y + 1);
		}
	}

	// Each pixel on a line takes the colour of the nearest one beside it
	// that isn't
	Bitmap clean = image;
	uint8_t* dst = (uint8_t*)&clean.Pixels[0];
	static const int offsets[] = { -1, 1, -2, 2, -3, 3 };
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			size_t i = (size_t)y * width + x;
			if (!across[i] && !down[i]) {
				continue;
			}
			for (int k : offsets) {
				int xx = across[i] ? x : x + k, yy = across[i] ? y + k : y;
				size_t j = (size_t)yy * width + xx;
				if (xx >= 0 && xx < width && yy >= 0 && yy < height && !across[j] && !down[j]) {
					memcpy(dst + i * 4, src + j * 4, 4);
					break;
				}
			}
		}
	}
	return scale > 1 ? ScaleUp(clean, scale) : clean;
}
