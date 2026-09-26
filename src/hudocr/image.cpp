#include "internal.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace hud {
namespace detail {

Plane grayOf(const uint8_t *bgra, int w, int h, int stride)
{
	Plane g(w, h);
	for (int y = 0; y < h; y++) {
		const uint8_t *r = bgra + (size_t)y * stride;
		float *o = &g.p[(size_t)y * w];
		for (int x = 0; x < w; x++) {
			const uint8_t *px = r + x * 4;
			o[x] = 0.299f * px[2] + 0.587f * px[1] + 0.114f * px[0];
		}
	}
	return g;
}

Plane boxMean(const Plane &a, int r)
{
	const int w = a.w, h = a.h;
	std::vector<double> ii((size_t)(w + 1) * (h + 1), 0.0);
	for (int y = 0; y < h; y++) {
		double row = 0;
		for (int x = 0; x < w; x++) {
			row += a.at(x, y);
			ii[(size_t)(y + 1) * (w + 1) + x + 1] = ii[(size_t)y * (w + 1) + x + 1] + row;
		}
	}
	Plane o(w, h);
	for (int y = 0; y < h; y++) {
		int y0 = std::max(0, y - r), y1 = std::min(h, y + r + 1);
		for (int x = 0; x < w; x++) {
			int x0 = std::max(0, x - r), x1 = std::min(w, x + r + 1);
			double s = ii[(size_t)y1 * (w + 1) + x1] - ii[(size_t)y0 * (w + 1) + x1] -
				   ii[(size_t)y1 * (w + 1) + x0] + ii[(size_t)y0 * (w + 1) + x0];
			o.at(x, y) = (float)(s / ((double)(y1 - y0) * (x1 - x0)));
		}
	}
	return o;
}

template<bool Min> static Plane rankFilter(const Plane &a, int k)
{
	const int w = a.w, h = a.h, r = k / 2;
	Plane t(w, h), o(w, h);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			float v = a.at(x, y);
			for (int d = std::max(0, x - r); d <= std::min(w - 1, x + r); d++)
				v = Min ? std::min(v, a.at(d, y)) : std::max(v, a.at(d, y));
			t.at(x, y) = v;
		}
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			float v = t.at(x, y);
			for (int d = std::max(0, y - r); d <= std::min(h - 1, y + r); d++)
				v = Min ? std::min(v, t.at(x, d)) : std::max(v, t.at(x, d));
			o.at(x, y) = v;
		}
	return o;
}

Plane minFilter(const Plane &a, int k)
{
	return rankFilter<true>(a, k);
}

Plane maxFilter(const Plane &a, int k)
{
	return rankFilter<false>(a, k);
}

Plane topHat(const Plane &g, int k)
{
	Plane open = maxFilter(minFilter(g, k), k);
	Plane o(g.w, g.h);
	for (size_t i = 0; i < g.p.size(); i++)
		o.p[i] = g.p[i] - open.p[i];
	return o;
}

Plane textness(const Plane &th)
{
	Plane o(th.w, th.h);
	for (size_t i = 0; i < th.p.size(); i++)
		o.p[i] = std::min(1.f, std::max(0.f, (th.p[i] - 14.f) / 46.f));
	return o;
}

std::vector<Comp> components(const std::vector<uint8_t> &mask, int w, int h)
{
	std::vector<int> lab((size_t)w * h, 0);
	std::vector<int> parent(1, 0);
	auto find = [&](int x) {
		while (parent[x] != x) {
			parent[x] = parent[parent[x]];
			x = parent[x];
		}
		return x;
	};
	auto unite = [&](int a, int b) {
		a = find(a);
		b = find(b);
		if (a != b) {
			if (a < b)
				parent[b] = a;
			else
				parent[a] = b;
		}
	};
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			if (!mask[(size_t)y * w + x])
				continue;
			int best = 0;
			const int nb[4][2] = {{-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
			int ns[4], nn = 0;
			for (auto &d : nb) {
				int xx = x + d[0], yy = y + d[1];
				if (xx < 0 || yy < 0 || xx >= w)
					continue;
				int l = lab[(size_t)yy * w + xx];
				if (l)
					ns[nn++] = l;
			}
			if (!nn) {
				best = (int)parent.size();
				parent.push_back(best);
			} else {
				best = ns[0];
				for (int i = 1; i < nn; i++)
					unite(best, ns[i]);
				best = find(best);
			}
			lab[(size_t)y * w + x] = best;
		}
	std::vector<int> slot(parent.size(), -1);
	std::vector<Comp> out;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			int l = lab[(size_t)y * w + x];
			if (!l)
				continue;
			int r = find(l);
			if (slot[r] < 0) {
				slot[r] = (int)out.size();
				Comp c;
				c.x0 = c.x1 = x;
				c.y0 = c.y1 = y;
				c.area = 0;
				out.push_back(c);
			}
			Comp &c = out[slot[r]];
			c.x0 = std::min(c.x0, x);
			c.y0 = std::min(c.y0, y);
			c.x1 = std::max(c.x1, x);
			c.y1 = std::max(c.y1, y);
			c.area++;
		}
	return out;
}

float sampleBilinear(const Plane &T, double x, double y)
{
	double fx = std::floor(x), fy = std::floor(y);
	int x0 = (int)fx, y0 = (int)fy;
	float ax = (float)(x - fx), ay = (float)(y - fy);
	auto px = [&](int xx, int yy) -> float {
		if (xx < 0 || yy < 0 || xx >= T.w || yy >= T.h)
			return 0.f;
		return T.at(xx, yy);
	};
	float a = px(x0, y0) * (1 - ax) + px(x0 + 1, y0) * ax;
	float b = px(x0, y0 + 1) * (1 - ax) + px(x0 + 1, y0 + 1) * ax;
	return a * (1 - ay) + b * ay;
}

void cellVec(const Plane &T, double cx, double top, double bot, double cap, float *out)
{
	// one advance (0.90 cap) wide, the cap band + 30 % above and below, onto CW x CH
	const double half = 0.90 * cap / 2.0;
	const double x0 = cx - half, x1 = cx + half;
	const double y0 = top - 0.30 * cap, y1 = bot + 0.30 * cap;
	const double sx = (x1 - x0) / CW, sy = (y1 - y0) / CH;
	for (int v = 0; v < CH; v++)
		for (int u = 0; u < CW; u++)
			out[v * CW + u] = sampleBilinear(T, x0 + sx * u, y0 + sy * v);
}

void glyphVec(const Plane &T, const Comp &g, double top, double bot, double cap, float *out)
{
	// the glyph's own width (+1 px each side), the line's cap band + 35 % margins, onto a GN-tall strip
	// centred in a GN x GN canvas: a comma stays small and low, a hyphen short and centred
	const double yt = std::round(top - 0.35 * cap), yb = std::round(bot + 0.35 * cap);
	const double xl = g.x0 - 1, xr = g.x1 + 1;
	const double ch = yb - yt + 1, cw = xr - xl + 1;
	const double scale = GN / ch;
	int nw = std::max(1, (int)std::lround(cw * scale));
	nw = std::min(nw, GN);
	const int ox = (GN - nw) / 2;
	std::fill(out, out + GLYPH, 0.f);
	const double fx = cw / nw, fy = ch / GN;
	for (int v = 0; v < GN; v++)
		for (int u = 0; u < nw; u++) {
			double sx = xl + (u + 0.5) * fx - 0.5, sy = yt + (v + 0.5) * fy - 0.5;
			// only the crop, not its neighbours, reaches the canvas
			if (sx < xl - 0.5 || sx > xr + 0.5)
				continue;
			out[v * GN + ox + u] = sampleBilinear(T, sx, sy);
		}
}

void unitize(float *v, int n)
{
	double m = 0;
	for (int i = 0; i < n; i++)
		m += v[i];
	m /= n;
	double s = 0;
	for (int i = 0; i < n; i++) {
		v[i] = (float)(v[i] - m);
		s += (double)v[i] * v[i];
	}
	s = std::sqrt(s);
	if (s > 1e-6)
		for (int i = 0; i < n; i++)
			v[i] = (float)(v[i] / s);
}

float dot(const float *a, const float *b, int n)
{
	float s = 0.f;
	for (int i = 0; i < n; i++)
		s += a[i] * b[i];
	return s;
}

bool Pairs::decide(const float *u, int n, char a, char b, float *z) const
{
	bool flip = false;
	auto it = byPair.find({a, b});
	if (it == byPair.end()) {
		it = byPair.find({b, a});
		flip = true;
	}
	if (it == byPair.end() || (int)it->second.w.size() != n)
		return false;
	float v = dot(it->second.w.data(), u, n) - it->second.t;
	*z = flip ? -v : v;
	return true;
}

void prepare(Frame &f)
{
	Plane g = grayOf(f.bgra, f.w, f.h, f.stride);
	f.g = boxMean(g, 1);
	Plane th = topHat(f.g, 9);
	f.T = textness(th);
	f.mask.assign((size_t)f.w * f.h, 0);
	for (size_t i = 0; i < th.p.size(); i++)
		f.mask[i] = (th.p[i] > 34.f && f.g.p[i] > 95.f) ? 1 : 0;
}

void prepareFeed(Frame &f)
{
	if (f.feedReady)
		return;
	f.feedReady = true;
	f.Tinv = f.T;
	f.feedGlyphs.clear();
	// white highlight boxes ("+$1,000" drawn dark on white): big, well-filled bright blobs
	std::vector<uint8_t> bright((size_t)f.w * f.h);
	for (size_t i = 0; i < bright.size(); i++)
		bright[i] = f.g.p[i] > 185.f ? 1 : 0;
	std::vector<Comp> boxes;
	for (const Comp &c : components(bright, f.w, f.h))
		if (c.ht() >= 24 && c.ht() <= 60 && c.wd() >= 50 && c.wd() <= 260 && c.area > 0.55 * c.wd() * c.ht())
			boxes.push_back(c);
	auto inBox = [&](const Comp &c) {
		for (const Comp &b : boxes)
			if (c.x0 >= b.x0 - 2 && c.x1 <= b.x1 + 2 && c.y0 >= b.y0 - 2 && c.y1 <= b.y1 + 2)
				return true;
		return false;
	};
	for (Comp c : components(f.mask, f.w, f.h)) {
		if (inBox(c))
			continue;
		if (c.ht() >= 2 && c.ht() <= 48 && c.wd() <= 70 && c.area >= 6 && c.x0 > 0 && c.y0 > 0) {
			c.inv = false;
			f.feedGlyphs.push_back(c);
		}
	}
	for (const Comp &b : boxes) {
		Plane sub(b.wd(), b.ht());
		for (int y = 0; y < sub.h; y++)
			for (int x = 0; x < sub.w; x++)
				sub.at(x, y) = 255.f - f.g.at(b.x0 + x, b.y0 + y);
		Plane th = topHat(sub, 7);
		Plane ts = textness(th);
		std::vector<uint8_t> ms((size_t)sub.w * sub.h);
		for (int y = 0; y < sub.h; y++)
			for (int x = 0; x < sub.w; x++) {
				bool inner = x >= 2 && y >= 2 && x < sub.w - 2 && y < sub.h - 2;
				ms[(size_t)y * sub.w + x] = (inner && th.at(x, y) > 34.f && sub.at(x, y) > 60.f) ? 1
														 : 0;
				f.Tinv.at(b.x0 + x, b.y0 + y) = ts.at(x, y);
			}
		for (Comp c : components(ms, sub.w, sub.h)) {
			if (c.ht() >= 2 && c.ht() <= 48 && c.wd() <= 70 && c.area >= 6) {
				c.x0 += b.x0;
				c.x1 += b.x0;
				c.y0 += b.y0;
				c.y1 += b.y0;
				c.inv = true;
				f.feedGlyphs.push_back(c);
			}
		}
	}
}

} // namespace detail
} // namespace hud
