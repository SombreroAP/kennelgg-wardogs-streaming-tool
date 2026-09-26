#include "internal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace hud {
using namespace detail;

namespace {

// ---- geometry of the money font, fitted on the 4K Reveal Trailer (rms 0.03 cap): proportional figures
double moneyAdv(char c)
{
	switch (c) {
	case '$':
		return 0.95;
	case '0':
		return 0.948;
	case '1':
		return 0.608;
	case '2':
		return 0.87;
	case '3':
		return 0.886;
	case '4':
		return 0.918;
	case '5':
		return 0.854;
	case '6':
		return 0.896;
	case '7':
		return 0.799;
	case '8':
		return 0.872;
	case '9':
		return 0.892;
	case '-':
		return 0.71;
	}
	return 0.9;
}
constexpr double kMoneyComma = 0.49;
constexpr double kRightMargin = 2.15; // balance's last digit ends this many caps from the right edge
constexpr double kDeltaGap = 3.15;    // the delta ends this many caps left of the balance's '$'

// ---- the feed amount font (SemiBold, narrower), in its own cap heights
double feedAdv(char c)
{
	switch (c) {
	case '1':
		return 0.50;
	case '7':
		return 0.80;
	case '$':
		return 0.86;
	case '+':
		return 0.75;
	}
	return 0.833;
}
constexpr double kFeedComma = 0.39;
constexpr double kAmountMargin = 2.56; // an amount's last digit ends this many MONEY caps from the right edge

constexpr float kZMin = 2.0f;

struct Hyp {
	float s;
	char c;
	double x;
};

struct Walker {
	const Plane &T;
	Band band;
	const Templates &tpl;
	const Pairs &pairs;
	float buf[CELL];

	// best score of one character's templates within +-search px of x
	std::pair<float, double> scoreAt(double x, char ch, int search)
	{
		std::pair<float, double> best{-2.f, x};
		auto it = tpl.byChar.find(ch);
		if (it == tpl.byChar.end())
			return best;
		for (int dx = -search; dx <= search; dx++) {
			cellVec(T, x + dx, band.top, band.bot, band.cap, buf);
			unitize(buf, CELL);
			float s = -2.f;
			for (const auto &t : it->second)
				s = std::max(s, dot(buf, t.data(), CELL));
			if (s > best.first)
				best = {s, x + dx};
		}
		return best;
	}

	// the top hypothesis and its margin; a pairwise discriminant settles close calls, or margin 0
	Hyp resolve(std::vector<Hyp> &hy, float *margin)
	{
		std::sort(hy.begin(), hy.end(), [](const Hyp &a, const Hyp &b) { return a.s > b.s; });
		if (hy.size() == 1) {
			*margin = 1.f;
			return hy[0];
		}
		const Hyp &a = hy[0], &b = hy[1];
		*margin = a.s - b.s;
		if (*margin < 0.08f && !pairs.byPair.empty()) {
			float z;
			cellVec(T, a.x, band.top, band.bot, band.cap, buf);
			unitize(buf, CELL);
			if (pairs.decide(buf, CELL, a.c, b.c, &z)) {
				if (z >= kZMin) {
					*margin = 0.1f;
					return a;
				}
				if (z <= -kZMin) {
					float z2;
					cellVec(T, b.x, band.top, band.bot, band.cap, buf);
					unitize(buf, CELL);
					if (pairs.decide(buf, CELL, a.c, b.c, &z2) && z2 <= -kZMin) {
						*margin = 0.1f;
						return b;
					}
				}
				*margin = 0.f;
				return a;
			}
		}
		return a;
	}
};

struct Decoded {
	bool ok = false;
	int64_t value = 0;
	std::string digits;
	float conf = 0, margin = 0;
	double xDollar = 0, xRight = 0;
	float minus = -2.f;
};

// Walks left from the digit centred at x0: each candidate for the next character is scored where THAT
// character would sit (its advance, plus a comma after every third digit); stops at the '$'.
Decoded walk(Walker &w, double x0, double (*adv)(char), double comma, int maxDigits, int search0)
{
	Decoded d;
	std::vector<Hyp> hy;
	for (char c = '0'; c <= '9'; c++) {
		auto s = w.scoreAt(x0, c, search0);
		hy.push_back({s.first, c, s.second});
	}
	float mg;
	Hyp h = w.resolve(hy, &mg);
	d.digits = std::string(1, h.c);
	d.conf = h.s;
	d.margin = mg;
	d.xRight = h.x;
	char ch = h.c;
	double xc = h.x;
	int n = 1;
	for (;;) {
		hy.clear();
		const std::string allowed = n >= maxDigits ? "$" : "0123456789$";
		for (char c : allowed) {
			double cm = (c != '$' && n % 3 == 0) ? comma * w.band.cap : 0.0;
			double x = xc - (adv(ch) + adv(c)) / 2.0 * w.band.cap - cm;
			auto s = w.scoreAt(x, c, 3);
			hy.push_back({s.first, c, s.second});
		}
		h = w.resolve(hy, &mg);
		if (h.c == '$') {
			d.xDollar = h.x;
			d.conf = std::min(d.conf, h.s);
			d.margin = std::min(d.margin, mg);
			d.ok = true;
			break;
		}
		if (h.s < 0.45f)
			return d; // no '$': not a number we can trust
		d.digits.insert(d.digits.begin(), h.c);
		d.conf = std::min(d.conf, h.s);
		d.margin = std::min(d.margin, mg);
		ch = h.c;
		xc = h.x;
		n++;
	}
	d.value = std::stoll(d.digits);
	return d;
}

std::string withCommas(int64_t v)
{
	std::string s = std::to_string(v < 0 ? -v : v), o;
	int n = 0;
	for (int i = (int)s.size() - 1; i >= 0; i--) {
		o.insert(o.begin(), s[i]);
		if (++n % 3 == 0 && i > 0)
			o.insert(o.begin(), ',');
	}
	return o;
}

int arrowSign(const Frame &f, const Band &b, double x0d, double x1d)
{
	int x0 = (int)std::max(0.0, x0d + 2), x1 = (int)std::min((double)f.w, x1d);
	if (x1 - x0 < 3)
		return 0;
	int y0 = (int)std::max(0.0, b.top - 0.2 * b.cap), y1 = (int)std::min((double)f.h, b.bot + 0.2 * b.cap);
	int red = 0, grn = 0;
	for (int y = y0; y < y1; y++)
		for (int x = x0; x < x1; x++) {
			const uint8_t *p = f.bgra + (size_t)y * f.stride + x * 4;
			float r = p[2], g = p[1], bl = p[0];
			if (r > 140 && r > 1.6f * g && r > 1.6f * bl)
				red++;
			if (g > 110 && g > 1.3f * r && g > 1.15f * bl)
				grn++;
		}
	if (red >= 8 && red > 3 * grn)
		return -1;
	if (grn >= 8 && grn > 3 * red)
		return 1;
	return 0;
}

// where a walk starts on a number's last glyph: its centre, or for a blob of merged digits the centre of
// the blob's last digit (a digit's ink ends ~0.43 cap right of its cell centre), searched a little wider
double digitStart(const Comp &k, double cap)
{
	return k.wd() <= 0.9 * k.ht() ? k.cx() : k.x1 - 0.43 * cap;
}
int digitSearch(const Comp &k)
{
	return k.wd() <= 0.9 * k.ht() ? 2 : 4;
}

double medianOf(std::vector<int> v)
{
	std::sort(v.begin(), v.end());
	size_t n = v.size();
	return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

} // namespace

namespace detail {

bool findMoneyLine(const Frame &f, MoneyLine *out)
{
	// tall digit-like glyphs in the top band, grouped into the row with the most of them
	const int yLo = 20, yHi = 115, capLo = 18, capHi = 38;
	std::vector<Comp> cand;
	for (const Comp &c : components(f.mask, f.w, f.h))
		// up to 4 digits wide: a soft or small frame merges touching digits into one blob
		if (c.ht() >= capLo && c.ht() <= capHi * 1.3 && c.wd() >= 3 && c.wd() <= 4.0 * c.ht() && c.y0 >= yLo &&
		    c.y0 <= yHi)
			cand.push_back(c);
	if (cand.size() < 2)
		return false;
	std::sort(cand.begin(), cand.end(),
		  [](const Comp &a, const Comp &b) { return a.cy() != b.cy() ? a.cy() < b.cy() : a.x0 < b.x0; });
	struct Row {
		double cy;
		std::vector<Comp> c;
	};
	std::vector<Row> rows;
	for (const Comp &c : cand) {
		bool placed = false;
		for (Row &r : rows)
			if (std::abs(r.cy - c.cy()) <= 0.35 * c.ht()) {
				r.c.push_back(c);
				double s = 0;
				for (const Comp &k : r.c)
					s += k.cy();
				r.cy = s / r.c.size();
				placed = true;
				break;
			}
		if (!placed)
			rows.push_back({c.cy(), {c}});
	}
	const Row *best = nullptr;
	for (const Row &r : rows) {
		if (r.c.size() < 2)
			continue;
		auto key = [](const Row &q) {
			int mx = 0;
			for (const Comp &k : q.c)
				mx = std::max(mx, k.x1);
			return std::make_pair((int)q.c.size(), mx);
		};
		if (!best || key(r) > key(*best))
			best = &r;
	}
	if (!best)
		return false;
	std::vector<int> hs;
	for (const Comp &k : best->c)
		hs.push_back(k.ht());
	std::sort(hs.begin(), hs.end());
	int cap = hs.size() >= 3 ? hs[hs.size() / 3] : hs[0];
	std::vector<Comp> digits;
	for (const Comp &k : best->c)
		if (std::abs(k.ht() - cap) <= std::max(2.0, 0.12 * cap))
			digits.push_back(k);
	if (digits.empty())
		return false;
	std::vector<int> tops, bots;
	for (const Comp &k : digits) {
		tops.push_back(k.y0);
		bots.push_back(k.y1);
	}
	std::sort(tops.begin(), tops.end());
	std::sort(bots.begin(), bots.end());
	out->band.top = tops[tops.size() / 2];
	out->band.bot = bots[bots.size() / 2];
	out->band.cap = out->band.bot - out->band.top + 1;
	out->comps = best->c;
	std::sort(out->comps.begin(), out->comps.end(), [](const Comp &a, const Comp &b) { return a.x0 < b.x0; });
	out->digits = digits;
	return true;
}

std::vector<std::vector<Comp>> groupLines(std::vector<Comp> glyphs, std::vector<Band> *bands)
{
	std::vector<Comp> tall, small;
	for (const Comp &g : glyphs)
		(g.ht() >= 10 ? tall : small).push_back(g);
	std::sort(tall.begin(), tall.end(),
		  [](const Comp &a, const Comp &b) { return a.cy() != b.cy() ? a.cy() < b.cy() : a.x0 < b.x0; });
	struct L {
		int y0, y1;
		std::vector<Comp> g;
		Band b;
	};
	std::vector<L> lines;
	for (const Comp &g : tall) {
		L *hit = nullptr;
		for (L &l : lines) {
			int ov = std::min(l.y1, g.y1) - std::max(l.y0, g.y0);
			if (ov >= 0.5 * std::min(l.y1 - l.y0, g.y1 - g.y0)) {
				hit = &l;
				break;
			}
		}
		if (hit)
			hit->g.push_back(g);
		else
			lines.push_back({g.y0, g.y1, {g}, {}});
	}
	auto bandOf = [](L &l) {
		std::vector<int> t, b;
		for (const Comp &g : l.g) {
			t.push_back(g.y0);
			b.push_back(g.y1);
		}
		std::sort(t.begin(), t.end());
		std::sort(b.begin(), b.end());
		l.b.top = t[t.size() / 2];
		l.b.bot = b[b.size() / 2];
		l.b.cap = l.b.bot - l.b.top + 1;
	};
	for (L &l : lines)
		bandOf(l);
	// one row can start as two lines when its first glyph was a tall '$' or a clipped letter: merge
	// lines whose bands (nearly) coincide, or the row's amount ends up in a fragment ("KILL +$2")
	for (bool again = true; again;) {
		again = false;
		for (size_t i = 0; i < lines.size() && !again; i++)
			for (size_t j = i + 1; j < lines.size() && !again; j++) {
				const Band &a = lines[i].b, &b = lines[j].b;
				double tol = 0.3 * std::min(a.cap, b.cap);
				if (std::abs(a.top - b.top) <= tol && std::abs(a.bot - b.bot) <= tol) {
					lines[i].g.insert(lines[i].g.end(), lines[j].g.begin(), lines[j].g.end());
					lines[i].y0 = std::min(lines[i].y0, lines[j].y0);
					lines[i].y1 = std::max(lines[i].y1, lines[j].y1);
					lines.erase(lines.begin() + j);
					bandOf(lines[i]);
					again = true;
				}
			}
	}
	for (const Comp &g : small)
		for (L &l : lines)
			if (g.cy() >= l.b.top - 0.2 * l.b.cap && g.cy() <= l.b.bot + 0.45 * l.b.cap) {
				l.g.push_back(g);
				break;
			}
	std::vector<std::vector<Comp>> out;
	std::sort(lines.begin(), lines.end(), [](const L &a, const L &b) { return a.b.top < b.b.top; });
	for (L &l : lines) {
		std::sort(l.g.begin(), l.g.end(), [](const Comp &a, const Comp &b) { return a.x0 < b.x0; });
		std::vector<Comp> merged;
		for (const Comp &g : l.g) {
			if (!merged.empty()) {
				Comp &p = merged.back();
				int ov = std::min(p.x1, g.x1) - std::max(p.x0, g.x0);
				if (ov > 0.5 * std::min(p.wd(), g.wd())) {
					p.x0 = std::min(p.x0, g.x0);
					p.y0 = std::min(p.y0, g.y0);
					p.x1 = std::max(p.x1, g.x1);
					p.y1 = std::max(p.y1, g.y1);
					p.area += g.area;
					continue;
				}
			}
			merged.push_back(g);
		}
		out.push_back(merged);
		if (bands)
			bands->push_back(l.b);
	}
	return out;
}

} // namespace detail

namespace {

// ---- feed letters

struct Cls {
	char c = '?';
	float s = -2.f, s2 = -2.f;
};

Cls classify(const Templates &t, const float *u, const char *alphabet)
{
	Cls r;
	for (const char *a = alphabet; *a; a++) {
		auto it = t.byChar.find(*a);
		if (it == t.byChar.end())
			continue;
		float s = -2.f;
		for (const auto &v : it->second)
			s = std::max(s, dot(u, v.data(), GLYPH));
		if (s > r.s) {
			r.s2 = r.s;
			r.s = s;
			r.c = *a;
		} else if (s > r.s2)
			r.s2 = s;
	}
	return r;
}

const char *kLetters = "ABCDEFGHIJKLMNOPRSTUVWXYZ"; // no Q: never in a reason seen so far, and it steals O
const char *kDigits = "0123456789";

std::vector<Comp> splitWide(const Frame &f, const std::vector<Comp> &gl, const Band &b, const Templates &t,
			    const char *alpha)
{
	std::vector<Comp> out;
	float v[GLYPH];
	for (const Comp &k : gl) {
		if (k.wd() <= 0.95 * b.cap) {
			out.push_back(k);
			continue;
		}
		glyphVec(f.Tinv, k, b.top, b.bot, b.cap, v);
		unitize(v, GLYPH);
		float whole = classify(t, v, alpha).s;
		int w = k.wd(), lo = (int)(0.25 * w), hi = (int)(0.75 * w);
		if (hi <= lo) {
			out.push_back(k);
			continue;
		}
		int bestc = lo;
		float bestv = 1e30f;
		for (int c = lo; c < hi; c++) {
			float s = 0;
			for (int y = (int)b.top; y <= (int)b.bot; y++)
				if (y >= 0 && y < f.h)
					s += f.Tinv.at(k.x0 + c, y);
			if (s < bestv) {
				bestv = s;
				bestc = c;
			}
		}
		Comp a = k, bb = k;
		a.x1 = k.x0 + bestc - 1;
		bb.x0 = k.x0 + bestc + 1;
		if (a.x1 - a.x0 < 2 || bb.x1 - bb.x0 < 2) {
			out.push_back(k);
			continue;
		}
		glyphVec(f.Tinv, a, b.top, b.bot, b.cap, v);
		unitize(v, GLYPH);
		float sa = classify(t, v, alpha).s;
		glyphVec(f.Tinv, bb, b.top, b.bot, b.cap, v);
		unitize(v, GLYPH);
		float sb = classify(t, v, alpha).s;
		if (std::min(sa, sb) > whole + 0.05f) {
			out.push_back(a);
			out.push_back(bb);
		} else
			out.push_back(k);
	}
	return out;
}

struct G {
	Comp k;
	Cls L, N;
};
using Tok = std::vector<G>;

// the classified glyphs of a line, split into tokens at word gaps
std::vector<Tok> tokensOf(const Frame &f, const std::vector<Comp> &glyphs, const Band &b, const Templates &t)
{
	std::string both = std::string(kLetters) + kDigits;
	std::vector<Comp> gl = splitWide(f, glyphs, b, t, both.c_str());
	std::vector<G> keep;
	float v[GLYPH];
	for (const Comp &k : gl) {
		if (k.ht() < 0.7 * b.cap || k.ht() > 1.35 * b.cap)
			continue;
		glyphVec(f.Tinv, k, b.top, b.bot, b.cap, v);
		unitize(v, GLYPH);
		G g{k, classify(t, v, kLetters), classify(t, v, kDigits)};
		if (std::max(g.L.s, g.N.s) < 0.55f)
			continue;
		keep.push_back(g);
	}
	std::vector<Tok> toks;
	for (size_t i = 0; i < keep.size(); i++) {
		if (i == 0 || keep[i].k.x0 - keep[i - 1].k.x1 - 1 > 0.28 * b.cap)
			toks.emplace_back();
		toks.back().push_back(keep[i]);
	}
	return toks;
}

// "S250", "SZBO", "OOO": a money amount read as letters (the '$' as S, digits as their look-alikes)
bool amountLike(const Tok &tk)
{
	if (tk.empty())
		return false;
	int digitish = 0;
	for (const G &g : tk)
		if (g.N.s >= g.L.s - 0.04f || std::strchr("OSBZIGDQ", g.L.c))
			digitish++;
	return digitish * 4 >= (int)tk.size() * 3;
}

} // namespace

Reading read(const Model &m, const uint8_t *bgra, int w, int h, int stride, const Options &opt)
{
	Reading out;
	Frame f;
	f.w = w;
	f.h = h;
	f.bgra = bgra;
	f.stride = stride;
	prepare(f);
	MoneyLine ml;
	if (!findMoneyLine(f, &ml)) {
		// low contrast (the grey HUD box over a bright sky, a soft or small frame): look again with a
		// lower stroke threshold. Only for finding the line; the feed keeps the normal mask, and every
		// digit still has to clear the same template gates.
		bool found = false;
		if (opt.lowContrastRetry) {
			std::vector<uint8_t> keep = f.mask;
			const float tLo = (24.f - 14.f) / 46.f; // top-hat 24 instead of 34, in textness units
			for (size_t i = 0; i < f.mask.size(); i++)
				f.mask[i] = (f.T.p[i] > tLo && f.g.p[i] > 95.f) ? 1 : 0;
			found = findMoneyLine(f, &ml);
			f.mask.swap(keep);
		}
		if (!found) {
			out.why = "no money line";
			return out;
		}
	}
	out.line = true;
	out.cap = (float)ml.band.cap;
	const double cap = ml.band.cap;

	// ---- balance: the number whose last digit ends at the HUD's right margin
	std::vector<Comp> ds = ml.digits;
	std::sort(ds.begin(), ds.end(), [](const Comp &a, const Comp &b) { return a.x1 < b.x1; });
	const Comp &last = ds.back();
	Walker mw{f.T, ml.band, m.moneyCells, m.moneyPairs, {}};
	Decoded b;
	if (std::abs((w - 1 - last.x1) - kRightMargin * cap) <= 0.6 * cap)
		b = walk(mw, digitStart(last, cap), moneyAdv, kMoneyComma, 9, digitSearch(last));
	else if (opt.marginAnchor) {
		// the last digit was lost (touching the box edge, merged, too faint for the mask): the balance
		// still ends at the margin, so start where its last digit's centre must be
		b = walk(mw, w - 1 - kRightMargin * cap - 0.43 * cap, moneyAdv, kMoneyComma, 9, 4);
		if (!b.ok) {
			out.why = "balance not at the HUD's right margin";
			return out;
		}
	} else {
		out.why = "balance not at the HUD's right margin";
		return out;
	}
	if (!b.ok) {
		out.why = "balance unreadable";
		return out;
	}
	out.balance.value = b.value;
	out.balance.text = "$" + withCommas(b.value);
	out.balance.conf = b.conf;
	out.balance.margin = b.margin;
	out.balance.xLeft = b.xDollar;
	out.balance.xRight = b.xRight;
	out.balance.ok = b.conf >= opt.minConf && b.margin >= opt.minMargin;
	if (!out.balance.ok)
		out.why = "balance uncertain";

	// ---- delta: ends kDeltaGap caps left of the balance's '$' (the arrow box between them)
	const double want = b.xDollar - kDeltaGap * cap;
	const Comp *k0 = nullptr;
	for (const Comp &k : ml.digits)
		if (std::abs(k.x1 - want) <= 0.45 * cap && (!k0 || k.x1 > k0->x1))
			k0 = &k;
	if (k0) {
		Decoded d = walk(mw, digitStart(*k0, cap), moneyAdv, kMoneyComma, 9, digitSearch(*k0));
		if (d.ok) {
			auto ms = mw.scoreAt(d.xDollar - (moneyAdv('$') + moneyAdv('-')) / 2.0 * cap, '-', 2);
			bool neg = ms.first > 0.6f;
			out.arrow = arrowSign(f, ml.band, k0->x1, b.xDollar - 0.5 * cap);
			float conf = d.conf;
			if (out.arrow < 0)
				neg = true;
			else if (out.arrow > 0 && neg)
				conf = std::min(conf, 0.5f); // a green arrow and a minus: something is off
			out.delta.value = neg ? -d.value : d.value;
			out.delta.text = std::string(neg ? "-" : "") + "$" + withCommas(d.value);
			out.delta.conf = conf;
			out.delta.margin = d.margin;
			out.delta.xLeft = d.xDollar;
			out.delta.xRight = d.xRight;
			out.delta.ok = conf >= opt.minConf && d.margin >= opt.minMargin;
		}
	}

	// ---- feed lines under the money line
	if (!opt.readFeed)
		return out;
	prepareFeed(f);
	const double capf = 0.667 * cap;
	const double yLo = ml.band.bot + 0.9 * cap, yHi = ml.band.bot + 14.0 * cap; // every line on screen
	const double xr = w - 1 - kAmountMargin * cap;
	std::vector<Comp> gl;
	for (const Comp &k : f.feedGlyphs)
		if (k.y0 >= yLo && k.y0 <= yHi && k.x0 >= w - 32 * capf)
			gl.push_back(k);
	std::vector<Band> bands;
	auto lines = groupLines(gl, &bands);
	for (auto &L : lines) {
		std::vector<int> tops, bots;
		for (const Comp &k : L)
			if (k.ht() >= 0.8 * capf && k.ht() <= 1.15 * capf) {
				tops.push_back(k.y0);
				bots.push_back(k.y1);
			}
		if (tops.size() < 2)
			continue;
		Band band;
		band.top = (int)medianOf(tops);
		band.bot = (int)medianOf(bots);
		band.cap = band.bot - band.top + 1;
		FeedLine fl;
		double reasonEnd = 1e9;
		const Comp *tail = nullptr;
		for (const Comp &k : L)
			if (k.ht() >= 0.8 * capf && k.ht() <= 1.15 * capf && std::abs(k.x1 - xr) <= 0.5 * capf &&
			    (!tail || k.x1 > tail->x1))
				tail = &k;
		if (tail) {
			Walker fw{f.Tinv, band, m.feedCells, m.feedPairs, {}};
			Decoded d = walk(fw, tail->cx(), feedAdv, kFeedComma, 7, 3);
			if (d.ok && d.conf >= opt.feedMinConf && d.margin >= opt.minMargin) {
				fl.hasAmount = true;
				fl.amount = d.value;
				fl.amountConf = d.conf;
				fl.amountMargin = d.margin;
				reasonEnd = d.xDollar - 0.6 * band.cap - 0.3 * capf;
			}
		}
		std::vector<Comp> rg;
		for (const Comp &k : L)
			if (k.x1 < reasonEnd)
				rg.push_back(k);
		std::vector<Tok> toks = tokensOf(f, rg, band, m.glyphs);
		// a trailing amount the walker missed (another right margin, a highlight box over it) reads as
		// letter tokens ("SZBO"): take them off the reason and decode them as money
		size_t firstAmt = toks.size();
		while (firstAmt > 1 && amountLike(toks[firstAmt - 1]))
			firstAmt--;
		if (firstAmt < toks.size() && !fl.hasAmount) {
			Walker fw{f.Tinv, band, m.feedCells, m.feedPairs, {}};
			Decoded d = walk(fw, toks.back().back().k.cx(), feedAdv, kFeedComma, 7, 3);
			if (d.ok && d.conf >= opt.feedMinConf && d.margin >= opt.minMargin) {
				fl.hasAmount = true;
				fl.amount = d.value;
				fl.amountConf = d.conf;
				fl.amountMargin = d.margin;
			} else {
				// the glyph classifier's digit reading: "$" then digits, every one of them clear
				std::string num;
				float c = 1.f;
				bool ok = toks[firstAmt][0].L.c == 'S';
				for (size_t ti = firstAmt; ok && ti < toks.size(); ti++)
					for (size_t gi = ti == firstAmt ? 1 : 0; gi < toks[ti].size(); gi++) {
						const G &g = toks[ti][gi];
						if (g.N.s < 0.7f || g.N.s - g.N.s2 < 0.03f)
							ok = false;
						num += g.N.c;
						c = std::min(c, g.N.s);
					}
				// payouts are round ($150, $250, $1,000): a lone digit or an odd tail is a broken read
				if (ok && num.size() >= 2 && num.size() <= 6 && num[0] != '0' &&
				    (num.back() == '0' || num.back() == '5')) {
					fl.hasAmount = true;
					fl.amount = std::stoll(num);
					fl.amountConf = std::min(c, 0.7f); // weaker than a walked amount
					fl.amountMargin = 0.f;
				}
			}
		}
		std::vector<std::string> raw, snapped;
		int known = 0, knownLetters = 0, letters = 0;
		for (size_t ti = 0; ti < firstAmt; ti++) {
			const Tok &tk = toks[ti];
			if (tk.size() >= 3 && tk[tk.size() - 2].L.c == 'X' && tk.back().L.c == 'P') {
				std::string num;
				for (size_t i = 0; i + 2 < tk.size(); i++)
					num += tk[i].N.c;
				if (!num.empty() && num.size() <= 5) {
					fl.hasXp = true;
					fl.xp = std::stoi(num);
					continue;
				}
			}
			std::string word;
			for (const G &g : tk)
				word += g.L.c;
			if (word.size() < 2)
				continue;
			bool ok = false;
			std::string sw = snapWord(word, &ok);
			raw.push_back(word);
			snapped.push_back(ok ? sw : std::string());
			letters += (int)word.size();
			if (ok) {
				known++;
				knownLetters += (int)word.size();
			}
		}
		for (size_t i = 0; i < raw.size(); i++)
			fl.raw += (i ? " " : "") + raw[i];
		// the reason: a known reason when the (word-snapped) letters are close to one, else the dictionary
		// words when they are most of the line, else nothing (noise: background texture, a highlight box)
		std::string clean, mixed, mixedNs;
		for (size_t i = 0; i < raw.size(); i++) {
			const std::string &s = snapped[i].empty() ? raw[i] : snapped[i];
			mixed += (i ? " " : "") + s;
			mixedNs += s;
			if (!snapped[i].empty())
				clean += (clean.empty() ? "" : " ") + snapped[i];
		}
		bool phrase = false;
		std::string r = mixed.empty() ? std::string() : snapReason(mixed, &phrase);
		if (phrase) {
			std::string rNs;
			for (char c : r)
				if (c != ' ')
					rNs += c;
			// most of the letters are known words, or the whole line is one edit from the reason ("ILL")
			phrase = knownLetters * 2 >= letters || (letters >= 3 && weightedDistance(mixedNs, rNs) <= 2);
		}
		bool cleanExact = false;
		if (!phrase && !clean.empty()) {
			// "ROTORS DESTROYED" + an XP token read as letters: the known words alone are a known reason
			std::string rc = snapReason(clean, &cleanExact);
			cleanExact = cleanExact && rc == clean;
		}
		if (phrase) {
			fl.reason = r;
			fl.snapped = true;
		} else if (cleanExact) {
			fl.reason = clean;
			fl.snapped = true;
		} else if (known >= 1 && knownLetters * 3 >= letters * 2 && knownLetters >= 4) {
			fl.reason = clean;
			fl.snapped = known == (int)raw.size();
		}
		if (fl.reason.empty() && !fl.hasAmount && !fl.hasXp)
			continue;
		fl.x0 = w;
		fl.x1 = 0;
		for (const Comp &k : L) {
			fl.x0 = std::min(fl.x0, k.x0);
			fl.x1 = std::max(fl.x1, k.x1);
		}
		fl.y0 = (int)band.top;
		fl.y1 = (int)band.bot;
		out.feed.push_back(fl);
	}
	std::sort(out.feed.begin(), out.feed.end(), [](const FeedLine &a, const FeedLine &b) { return a.y0 < b.y0; });
	return out;
}

} // namespace hud
