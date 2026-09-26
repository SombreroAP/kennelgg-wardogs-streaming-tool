#include "stabilizer.h"
#include "internal.h"
#include <algorithm>
#include <cstdlib>

namespace tourney {

namespace {
constexpr int64_t kSettleMs = 300; // same value on 2+ frames spanning at least this
constexpr int64_t kHeartbeatMs = 10000;
constexpr int64_t kTrackGoneMs = 3000; // a feed line not seen for this long has left the screen
constexpr int64_t kNoAmountMs = 2500;  // a line whose amount never reads is sent as a reason only
constexpr int kLineMatchPx = 22;       // same line if within this of its last y (lines slide as new ones arrive)
} // namespace

bool Stabilizer::settle(Cand &cand, int64_t t, int64_t v, float c)
{
	if (cand.set && cand.v == v) {
		cand.n++;
		cand.last = t;
		cand.c = std::min(cand.c, c);
	} else {
		cand = Cand{true, v, t, t, 1, c};
	}
	return cand.n >= 2 && cand.last - cand.first >= kSettleMs;
}

std::string Stabilizer::Track::reason() const
{
	std::string best;
	int n = 0;
	for (auto &kv : reasons)
		if (kv.second > n || (kv.second == n && kv.first.size() > best.size())) {
			best = kv.first;
			n = kv.second;
		}
	return best;
}

void Stabilizer::push(int64_t t, const hud::Reading &r, std::vector<Event> &out)
{
	okRate_ = okRate_ * 0.95 + (r.balance.ok ? 0.05 : 0.0);
	if (r.line)
		cap_ = r.cap;

	// ---- balance
	if (r.balance.ok) {
		lastBalOk_ = t;
		if (settle(bal_, t, r.balance.value, r.balance.conf)) {
			stable_ = bal_.v;
			stableSeen_ = t;
			hasStable_ = true;
			if (stable_ != emitted_) {
				Event e;
				e.kind = Event::Bal;
				e.t = bal_.first; // when this value first showed
				e.v = stable_;
				e.c = bal_.c;
				out.push_back(e);
				emitted_ = stable_;
				lastHb_ = t;
			}
		}
	}
	if (hasStable_ && t - stableSeen_ <= 3000 && t - lastHb_ >= kHeartbeatMs) {
		Event e;
		e.kind = Event::Bal;
		e.t = t;
		e.v = stable_;
		e.c = bal_.c;
		e.hb = true;
		out.push_back(e);
		lastHb_ = t;
	}

	// ---- match change
	if (r.delta.ok && settle(dlt_, t, r.delta.value, r.delta.conf)) {
		dStable_ = dlt_.v;
		hasDelta_ = true;
		if (!dEmittedSet_ || dStable_ != dEmitted_) {
			Event e;
			e.kind = Event::Dlt;
			e.t = dlt_.first;
			e.v = dStable_;
			e.c = dlt_.c;
			out.push_back(e);
			dEmitted_ = dStable_;
			dEmittedSet_ = true;
		}
	}

	// ---- feed lines
	std::vector<bool> used(tracks_.size(), false);
	for (const hud::FeedLine &fl : r.feed) {
		int best = -1, bestDy = 1 << 30;
		for (size_t i = 0; i < tracks_.size(); i++) {
			if (used[i])
				continue;
			Track &tr = tracks_[i];
			int dy = std::abs(tr.y - fl.y0);
			if (dy > kLineMatchPx)
				continue;
			if (tr.hasAmt && fl.hasAmount && tr.amt != fl.amount && tr.amtSeen >= 2)
				continue;
			std::string rs = tr.reason();
			if (!rs.empty() && !fl.reason.empty() && rs != fl.reason &&
			    hud::detail::levenshtein(rs, fl.reason) > 2)
				continue;
			if (dy < bestDy) {
				best = (int)i;
				bestDy = dy;
			}
		}
		if (best < 0) {
			Track tr;
			tr.first = t;
			tr.lid = nextLid_++;
			tracks_.push_back(tr);
			used.push_back(false);
			best = (int)tracks_.size() - 1;
		}
		used[best] = true;
		Track &tr = tracks_[best];
		tr.y = fl.y0;
		tr.last = t;
		tr.seen++;
		if (!fl.reason.empty())
			tr.reasons[fl.reason]++;
		if (fl.hasAmount) {
			if (tr.hasAmt && tr.amt == fl.amount)
				tr.amtSeen++;
			else {
				tr.hasAmt = true;
				tr.amt = fl.amount;
				tr.amtSeen = 1;
			}
		}
		if (fl.hasXp) {
			tr.hasXp = true;
			tr.xp = fl.xp;
		}
	}
	for (Track &tr : tracks_) {
		if (tr.emitted)
			continue;
		bool money = tr.hasAmt && tr.amtSeen >= 2;
		bool xp = !tr.hasAmt && tr.hasXp && tr.seen >= 2;
		bool bare = !tr.hasAmt && !tr.hasXp && tr.seen >= 2 && t - tr.first >= kNoAmountMs &&
			    !tr.reason().empty();
		if (!money && !xp && !bare)
			continue;
		Event e;
		e.kind = Event::Feed;
		e.t = tr.first;
		e.txt = tr.reason();
		e.hasAmt = money;
		e.amt = tr.amt;
		e.hasXp = xp;
		e.xp = tr.xp;
		e.lid = tr.lid;
		out.push_back(e);
		tr.emitted = true;
	}
	// a line leaving the screen before it settled still counts if it was seen twice: send what was read
	for (Track &tr : tracks_) {
		if (tr.emitted || t - tr.last <= kTrackGoneMs || tr.seen < 2)
			continue;
		if (!tr.hasAmt && !tr.hasXp && tr.reason().empty())
			continue;
		Event e;
		e.kind = Event::Feed;
		e.t = tr.first;
		e.txt = tr.reason();
		e.hasAmt = tr.hasAmt;
		e.amt = tr.amt;
		e.hasXp = !tr.hasAmt && tr.hasXp;
		e.xp = tr.xp;
		e.lid = tr.lid;
		e.c = tr.hasAmt && tr.amtSeen < 2 ? 0.5f : 1.f; // one read only: the server treats it as a hint
		out.push_back(e);
		tr.emitted = true;
	}
	tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(),
				     [&](const Track &tr) { return t - tr.last > kTrackGoneMs; }),
		      tracks_.end());
}

} // namespace tourney
