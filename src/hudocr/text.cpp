#include "internal.h"
#include <algorithm>
#include <cstdlib>

namespace hud {

const std::vector<std::string> &knownReasons()
{
	// Seen in the Reveal Trailer HUD (24 Sep 2026) plus the obvious neighbours. The server holds the
	// authoritative reason -> category map; this list only straightens out misread letters.
	static const std::vector<std::string> r = {
		"KILL", "REVENGE KILL", "KILL CONFIRMED", "VEHICLE DESTROYED", "ROTORS DESTROYED",
		"CONTROL ZONE PRESENCE", "HOT ZONE PRESENCE", "REVIVED TEAMMATE", "HEADSHOT", "ASSIST",
		"PURCHASE REFUNDED",
		// reported from a real 1440p match (read as "CONTROL ZONE ENTEREO", ...)
		"CONTROL ZONE ENTERED", "HOT ZONE ENTERED", "SUPPLIED PLAYER ASSIST"};
	return r;
}

const std::vector<std::string> &knownWords()
{
	// Every word of the known reasons plus the HUD words of the server's English check (reasons.py
	// _HUD_WORDS and the category stems): a read word is only kept when it snaps to one of these.
	static const std::vector<std::string> w = [] {
		std::vector<std::string> v = {
			"KILL",          "CONFIRMED", "REVENGE",   "CONTROL",    "ZONE",        "PRESENCE",
			"HOT",           "ROTORS",    "DESTROYED", "VEHICLE",    "REVIVE",      "REVIVED",
			"HEADSHOT",      "ASSIST",    "SUPPLY",    "SUPPLIES",   "SUPPLIED",    "DELIVERED",
			"PASSENGER",     "TRANSPORT", "SPAWN",     "FOB",        "BUILT",       "REPAIR",
			"REPAIRED",      "REFUNDED",  "PURCHASE",  "BRIBE",      "CAPTURE",     "CAPTURED",
			"OBJECTIVE",     "ENEMY",     "DOWNED",    "EXECUTION",  "LONG",        "RANGE",
			"SHOT",          "MULTI",     "DOUBLE",    "TRIPLE",     "BONUS",       "TEAMMATE",
			"SQUAD",         "FRIENDLY",  "HEAL",      "HEALED",     "AMMO",        "RESUPPLY",
			"DEFIBRILLATOR", "ENTERED",   "ENTERING",  "HELD",       "PLAYER",      "KILLED",
			"ASSISTED",      "SECURED",   "DESTROY",   "ELIMINATED", "STREAK",      "EXTRACTED",
			"SPOTTED",       "DAMAGED",   "DISABLED",  "BUILD",      "CRATE",       "PALLET",
			"SECTOR",        "HOLD",      "REFUND",    "ZONES",      "TRANSPORTED", "RESUSCITATED"};
		for (const std::string &r : knownReasons()) {
			size_t a = 0;
			while (a < r.size()) {
				size_t b = r.find(' ', a);
				if (b == std::string::npos)
					b = r.size();
				std::string word = r.substr(a, b - a);
				if (std::find(v.begin(), v.end(), word) == v.end())
					v.push_back(word);
				a = b + 1;
			}
		}
		return v;
	}();
	return w;
}

namespace detail {

// letters this font's classifier mixes up (both ways): a swap between them costs half an edit
static bool confusable(char a, char b)
{
	static const char *pairs[] = {"DO", "OG", "OQ", "CG", "UL", "IL", "IJ", "HB", "BE", "PF",
				      "VY", "MN", "HN", "EF", "RB", "KX", "TI", "ZS", "CO", "UV"};
	for (const char *p : pairs)
		if ((a == p[0] && b == p[1]) || (a == p[1] && b == p[0]))
			return true;
	return false;
}

int weightedDistance(const std::string &a, const std::string &b)
{
	std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
	for (size_t j = 0; j <= b.size(); j++)
		prev[j] = 2 * (int)j;
	for (size_t i = 1; i <= a.size(); i++) {
		cur[0] = 2 * (int)i;
		for (size_t j = 1; j <= b.size(); j++) {
			int sub = a[i - 1] == b[j - 1] ? 0 : confusable(a[i - 1], b[j - 1]) ? 1 : 2;
			cur[j] = std::min({prev[j] + 2, cur[j - 1] + 2, prev[j - 1] + sub});
		}
		std::swap(prev, cur);
	}
	return prev[b.size()];
}

// half edits a word of n letters may be off by and still snap: short words must be (nearly) exact
static int wordBudget(size_t n)
{
	if (n <= 3)
		return 0;
	if (n <= 4)
		return 1;
	if (n <= 6)
		return 2;
	return 4;
}

std::string snapWord(const std::string &w, bool *ok)
{
	*ok = false;
	const std::string *best = nullptr;
	int bd = 1 << 30, second = 1 << 30;
	for (const std::string &k : knownWords()) {
		if (std::abs((int)k.size() - (int)w.size()) > 2)
			continue;
		int d = weightedDistance(w, k);
		if (d < bd) {
			second = bd;
			bd = d;
			best = &k;
		} else if (d < second)
			second = d;
	}
	// a tie between two different words is no snap ("REVIVE"/"REVIVED" never tie: one is exact)
	if (!best || bd > wordBudget(std::max(w.size(), best->size())) || (bd > 0 && second == bd))
		return w;
	*ok = true;
	return *best;
}

int levenshtein(const std::string &a, const std::string &b)
{
	std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
	for (size_t j = 0; j <= b.size(); j++)
		prev[j] = (int)j;
	for (size_t i = 1; i <= a.size(); i++) {
		cur[0] = (int)i;
		for (size_t j = 1; j <= b.size(); j++)
			cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] != b[j - 1] ? 1 : 0)});
		std::swap(prev, cur);
	}
	return prev[b.size()];
}

static std::string noSpaces(const std::string &s)
{
	std::string o;
	for (char c : s)
		if (c != ' ')
			o += c;
	return o;
}

std::string snapReason(const std::string &raw, bool *snapped)
{
	*snapped = false;
	const std::string r = noSpaces(raw);
	const std::string *best = nullptr;
	int bd = 1 << 30;
	for (const std::string &k : knownReasons()) {
		int d = weightedDistance(r, noSpaces(k));
		if (d < bd) {
			bd = d;
			best = &k;
		}
	}
	if (best) {
		int len = (int)noSpaces(*best).size();
		if (bd <= std::max(2, (int)(0.5 * len))) { // in half edits: a quarter of the letters
			*snapped = true;
			return *best;
		}
	}
	return raw;
}

} // namespace detail
} // namespace hud
