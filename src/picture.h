#pragma once
#include <cstdint>
#include <vector>

/// Is there a game picture in this frame, or only an app's own screen? Discord's call grid, a
/// text channel or a "stream ended" card is flat colour with text and small avatars on it; a
/// game is textured almost everywhere. The frame is cut into blocks, a block is "picture" when
/// no one shade covers most of it, and the frame has a picture when one joined-up patch of such
/// blocks is big and dense enough to be a video. No OBS here, so it can be tested on its own.
namespace Picture {

struct Look {
	double area = 0;    // the biggest picture patch's box, as a share of the frame
	double density = 0; // the share of that box that is picture blocks
	double blank = 0;   // the share of the whole frame that is flat
	bool picture = false;
	bool empty = false; // nothing drawn at all (a window that is not there): no answer either way
};

constexpr int kWidth = 800;       // the capture is looked at this wide
constexpr int kBlock = 10;        // pixels on that capture
constexpr double kMinArea = 0.25; // a squad mate's stream fills at least a quarter of what we see
constexpr double kMinDensity = 0.5;

Look look(const uint8_t *bgra, int w, int h, int linesize);

} // namespace Picture
