// Kennel.gg Tournament - WARDOGS cash HUD reader.
//
// Portable C++17, no dependencies: the OBS plugin and the Mac lab harness (tools/ocrlab) build the same
// code. Input is the top-right corner of the game frame rendered at the REFERENCE SCALE: the region
// [W - 0.42 H, W] x [0, 0.22 H] of an H-tall frame drawn into a 907 x 475 image (what a 4K frame looks
// like), so every resolution and aspect ratio reaches the reader at the same size. The plugin gets that for
// free by rendering the source region straight into a texture of that size.
//
// What is read:
//   - the balance box (right) and the match-change box left of it, digit cell by digit cell: the HUD font
//     has proportional figures with known advances, so once one digit is found every other character sits
//     at a predictable place and is classified on its own (tolerates lost commas, touching digits, specks);
//   - the feed lines under the balance ("CONTROL ZONE PRESENCE +$150", "KILL 250XP"): amounts the same way,
//     reasons letter by letter and snapped to the known reasons.
// Every number carries a confidence and a margin over the runner-up; callers should accept a number only
// when both clear the thresholds in Options and it repeats over consecutive frames.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hud {

constexpr int kRefW = 907;        // 0.42 * 2160
constexpr int kRefH = 475;        // 0.22 * 2160
constexpr double kRegionW = 0.42; // of the frame HEIGHT, measured from the right edge
constexpr double kRegionH = 0.22; // of the frame height, from the top (money line + ~8 feed lines)

struct Model;
/// Loads data/hud/model.bin. Returns null with a reason in *err.
std::shared_ptr<const Model> loadModel(const std::string &path, std::string *err);
std::shared_ptr<const Model> loadModelFromMemory(const uint8_t *data, size_t size, std::string *err);

struct Number {
	bool ok = false;
	int64_t value = 0;
	std::string text; // "$161,627", "-$3,009", "+$1,000"
	float conf = 0;   // lowest per-character template score
	float margin = 0; // lowest per-character lead over the runner-up (0 = a pair the reader could not settle)
	double xLeft = 0, xRight = 0;
};

struct FeedLine {
	std::string reason; // snapped to a known reason when close, else the raw letters
	std::string raw;    // the letters as read
	bool snapped = false;
	bool hasAmount = false;
	int64_t amount = 0;
	float amountConf = 0, amountMargin = 0;
	bool hasXp = false;
	int xp = 0;
	int x0 = 0, y0 = 0, x1 = 0, y1 = 0; // the line's box in the reference image
};

struct Reading {
	bool line = false; // a money line was found
	float cap = 0;     // its digit height at the reference scale (27 at the game's default HUD size)
	Number balance;    // ok only when found at the HUD's right margin
	Number delta;      // the match change, sign from the '-' or the arrow colour
	int arrow = 0;     // +1 green, -1 red, 0 unknown
	std::vector<FeedLine> feed;
	std::string why; // why nothing was read (for the dock's "Test reader")
};

struct Options {
	bool readFeed = true;
	float minConf = 0.85f;   // Number::ok needs conf >= this ...
	float minMargin = 0.03f; // ... and margin >= this
	float feedMinConf = 0.72f;
	bool marginAnchor = true;     // no digit at the balance's margin: walk from where it must end anyway
	bool lowContrastRetry = true; // no money line: look again with a lower stroke threshold
};

/// Reads one reference-scale image. `bgra` is 4 bytes per pixel (B, G, R, A), `stride` bytes per row.
Reading read(const Model &m, const uint8_t *bgra, int w, int h, int stride, const Options &opt = Options());

/// Known reasons (English). Used for snapping; the server keeps the authoritative mapping.
const std::vector<std::string> &knownReasons();
/// Every word a reason line may contain: a read word that does not snap to one of these is noise.
const std::vector<std::string> &knownWords();

} // namespace hud
