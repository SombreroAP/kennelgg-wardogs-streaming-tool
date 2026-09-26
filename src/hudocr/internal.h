// Internals shared by the reader and the lab harness. Not part of the plugin's interface.
#pragma once
#include "hudocr.h"
#include <map>
#include <utility>

namespace hud {
namespace detail {

struct Plane {
	int w = 0, h = 0;
	std::vector<float> p;
	Plane() = default;
	Plane(int w_, int h_, float v = 0.f) : w(w_), h(h_), p((size_t)w_ * h_, v) {}
	float at(int x, int y) const { return p[(size_t)y * w + x]; }
	float &at(int x, int y) { return p[(size_t)y * w + x]; }
};

struct Comp {
	int x0 = 0, y0 = 0, x1 = 0, y1 = 0, area = 0;
	bool inv = false;
	int wd() const { return x1 - x0 + 1; }
	int ht() const { return y1 - y0 + 1; }
	double cx() const { return (x0 + x1) / 2.0; }
	double cy() const { return (y0 + y1) / 2.0; }
};

// ---- image operations (every one mirrors the lab prototype, tools/ocrlab/proto.py)
Plane grayOf(const uint8_t *bgra, int w, int h, int stride);
Plane boxMean(const Plane &a, int r);   // mean over (2r+1)^2, in-bounds pixels only
Plane minFilter(const Plane &a, int k); // k x k, in-bounds only (cv2.erode)
Plane maxFilter(const Plane &a, int k); // (cv2.dilate)
Plane topHat(const Plane &g, int k);    // g - dilate(erode(g))
Plane textness(const Plane &th);        // clip((th - 14) / 46, 0, 1)
std::vector<Comp> components(const std::vector<uint8_t> &mask, int w, int h); // 8-connected
float sampleBilinear(const Plane &T, double x, double y);                     // zero outside

constexpr int CW = 20, CH = 36, CELL = CW * CH; // digit cell canvas
constexpr int GN = 32, GLYPH = GN * GN;         // letter glyph canvas

void cellVec(const Plane &T, double cx, double top, double bot, double cap, float *out);
void glyphVec(const Plane &T, const Comp &g, double top, double bot, double cap, float *out);
void unitize(float *v, int n);
float dot(const float *a, const float *b, int n);

// ---- model
struct Templates {
	int dim = 0;
	std::map<char, std::vector<std::vector<float>>> byChar;
	bool has(char c) const { return byChar.count(c) > 0; }
};
struct Pair {
	std::vector<float> w;
	float t = 0;
};
struct Pairs {
	std::map<std::pair<char, char>, Pair> byPair; // key (a, b) with a < b
	// + means a, - means b, in within-class standard deviations; false when the pair is unknown
	bool decide(const float *u, int n, char a, char b, float *z) const;
};

// ---- the image a reading works on
struct Frame {
	int w = 0, h = 0;
	const uint8_t *bgra = nullptr;
	int stride = 0;
	Plane g;                   // blurred gray
	Plane T;                   // textness (plain)
	std::vector<uint8_t> mask; // text mask
	Plane Tinv;                // textness with white highlight boxes inverted (feed)
	std::vector<Comp> feedGlyphs;
	bool feedReady = false;
};
void prepare(Frame &f);     // g, T, mask
void prepareFeed(Frame &f); // Tinv, feedGlyphs

// ---- line model shared by the money and amount walkers
struct Band {
	double top = 0, bot = 0, cap = 0;
};

} // namespace detail

struct Model {
	detail::Templates moneyCells, feedCells, glyphs;
	detail::Pairs moneyPairs, feedPairs;
};

namespace detail {
// exposed for the lab harness
struct MoneyLine {
	Band band;
	std::vector<Comp> comps;  // tall glyphs of the row, by x0
	std::vector<Comp> digits; // the digit-height ones
};
bool findMoneyLine(const Frame &f, MoneyLine *out);
std::vector<std::vector<Comp>> groupLines(std::vector<Comp> glyphs, std::vector<Band> *bands);
int levenshtein(const std::string &a, const std::string &b);
std::string snapReason(const std::string &raw, bool *snapped);
int weightedDistance(const std::string &a, const std::string &b); // half edits, confusable letters at 1
std::string snapWord(const std::string &w, bool *ok);             // to knownWords(), or w unchanged
} // namespace detail

} // namespace hud
