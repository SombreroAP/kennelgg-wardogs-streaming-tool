// Turns per-frame HUD readings into the events the backend scores (SPEC.md 4.2). No OBS, no Qt: the lab
// harness unit-tests it on the Mac.
//
//   bal  - the balance, only once the same value was read on 2+ frames at least 300 ms apart (the counter
//          rolls up after a payout, and a single frame can be a killcam or a misread); on change, plus a
//          heartbeat every 10 s while it stays visible
//   dlt  - the match-change box, same rule, on change
//   feed - each line under the balance, exactly once per appearance, with a line id (lid) so two identical
//          "KILL +$1,000" lines a second apart stay two payouts
#pragma once
#include "hudocr.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace tourney {

struct Event {
	enum Kind { Bal, Dlt, Feed, Snap, Status };
	Kind kind = Bal;
	int64_t t = 0; // local clock, ms since epoch
	int64_t v = 0;
	float c = 0;
	bool hb = false;
	// feed
	std::string txt;
	bool hasAmt = false;
	int64_t amt = 0;
	bool hasXp = false;
	int xp = 0;
	int64_t lid = 0;
	// snap
	std::string tag, evidence;
	// status (filled by the engine)
	bool hud = false, streaming = false, recording = false;
	std::string src, err;
	std::string steamId, steamName;
	std::string srcUuid, srcType, srcExe;
	bool onStream = false, locked = false, changed = false;
	int resW = 0, resH = 0;
	double scale = 0, fps = 0, okRate = 0, cap = 0;
};

class Stabilizer {
public:
	void push(int64_t t, const hud::Reading &r, std::vector<Event> &out);

	bool hudVisible(int64_t now) const { return lastBalOk_ > 0 && now - lastBalOk_ <= 2500; }
	// the locally confirmed balance and when it was last seen on screen
	bool balance(int64_t *v, int64_t *seenAt) const
	{
		if (!hasStable_)
			return false;
		*v = stable_;
		*seenAt = stableSeen_;
		return true;
	}
	bool delta(int64_t *v) const
	{
		if (!hasDelta_)
			return false;
		*v = dStable_;
		return true;
	}
	double okRate() const { return okRate_; }
	float cap() const { return cap_; }
	int64_t nextLid() const { return nextLid_; }
	void setNextLid(int64_t v) { nextLid_ = v; }

private:
	struct Cand {
		bool set = false;
		int64_t v = 0, first = 0, last = 0;
		int n = 0;
		float c = 0;
	};
	static bool settle(Cand &cand, int64_t t, int64_t v, float c);

	Cand bal_, dlt_;
	bool hasStable_ = false, hasDelta_ = false;
	int64_t stable_ = 0, stableSeen_ = 0, emitted_ = -1, lastHb_ = 0, lastBalOk_ = 0;
	int64_t dStable_ = 0, dEmitted_ = INT64_MIN;
	bool dEmittedSet_ = false;
	double okRate_ = 0;
	float cap_ = 0;

	struct Track {
		std::map<std::string, int> reasons; // votes
		bool hasAmt = false;
		int64_t amt = 0;
		int amtSeen = 0;
		bool hasXp = false;
		int xp = 0;
		int y = 0;
		int64_t first = 0, last = 0;
		int seen = 0;
		bool emitted = false;
		int64_t lid = 0;
		std::string reason() const;
	};
	std::vector<Track> tracks_;
	int64_t nextLid_ = 1;
};

} // namespace tourney
