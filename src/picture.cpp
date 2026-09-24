#include "picture.h"
#include <algorithm>

namespace Picture {

Look look(const uint8_t *bgra, int w, int h, int linesize)
{
	Look L;
	const int B = kBlock;
	const int cols = w / B, rows = h / B;
	if (cols < 4 || rows < 4) {
		L.empty = true;
		return L;
	}
	const int n = cols * rows;
	std::vector<uint8_t> pic((size_t)n, 0);
	int flat = 0;
	long drawn = 0;
	for (int by = 0; by < rows; by++)
		for (int bx = 0; bx < cols; bx++) {
			// An app paints a panel in one exact colour, text and all; a game never repeats one
			// exact shade across a block, not even in a smooth sky or a dark room (noise, grain,
			// the video codec). So: how much of the block is its single commonest shade.
			int hist[256] = {0};
			int alpha = 0;
			for (int y = by * B; y < by * B + B; y++) {
				const uint8_t *p = bgra + (size_t)y * linesize + (size_t)bx * B * 4;
				for (int x = 0; x < B; x++, p += 4) {
					int v = (p[0] * 29 + p[1] * 150 + p[2] * 77) >> 8;
					hist[v]++;
					alpha += p[3];
				}
			}
			int top = 0;
			for (int i = 0; i < 256; i++)
				top = std::max(top, hist[i]);
			const int k = by * cols + bx;
			drawn += alpha;
			// a transparent block (nothing drawn there) is never picture
			bool isPic = alpha > 0 && top < (B * B) * 6 / 10;
			pic[(size_t)k] = isPic;
			flat += !isPic;
		}
	L.blank = (double)flat / n;
	if (drawn == 0) {
		L.empty = true;
		return L;
	}

	// Join picture blocks that are at most one block apart (a smooth sky or a dark wall inside a
	// game frame must not split it in two), then take the patch whose box is biggest.
	std::vector<int> label((size_t)n, -1);
	std::vector<int> stack;
	double best = 0;
	for (int s = 0; s < n; s++) {
		if (!pic[(size_t)s] || label[(size_t)s] >= 0)
			continue;
		int x0 = s % cols, x1 = x0, y0 = s / cols, y1 = y0, count = 0;
		label[(size_t)s] = s;
		stack.assign(1, s);
		while (!stack.empty()) {
			int c = stack.back();
			stack.pop_back();
			int cx = c % cols, cy = c / cols;
			count++;
			x0 = std::min(x0, cx);
			x1 = std::max(x1, cx);
			y0 = std::min(y0, cy);
			y1 = std::max(y1, cy);
			for (int dy = -2; dy <= 2; dy++)
				for (int dx = -2; dx <= 2; dx++) {
					int nx = cx + dx, ny = cy + dy;
					if (nx < 0 || ny < 0 || nx >= cols || ny >= rows)
						continue;
					int m = ny * cols + nx;
					if (pic[(size_t)m] && label[(size_t)m] < 0) {
						label[(size_t)m] = s;
						stack.push_back(m);
					}
				}
		}
		int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
		double area = (double)(bw * bh) / n;
		double density = (double)count / (bw * bh);
		// the patch that best looks like a video: big, and mostly picture inside its box
		double score = area * std::min(1.0, density / kMinDensity);
		if (score > best) {
			best = score;
			L.area = area;
			L.density = density;
		}
	}
	L.picture = L.area >= kMinArea && L.density >= kMinDensity;
	return L;
}

} // namespace Picture
