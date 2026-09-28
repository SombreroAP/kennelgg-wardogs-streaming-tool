"""Work out the streamer's in-game name from the kill feed.

The feed shows a distance ("[68 m]") only on rows involving the streamer, so the one name that is on
every row with a distance is theirs. ClipHound needs that name to tell your kills and deaths from
everyone else's; a streamer who never set it (or set their Discord name) got no kills, no deaths and no
kill clips at all (27 Sep 2026 report: 135 of their own rows, none counted).
"""
from __future__ import annotations

import re
from collections import Counter, defaultdict

MIN_ROWS = 3       # rows with a distance before a guess
MIN_SHARE = 0.6    # the name has to be on this share of them


def _key(text: str) -> tuple[str, str]:
    """(match key, display form) of one feed column: the clan tag and OCR junk around it removed."""
    t = (text or "").strip()
    # "[Sun]AdventuringBear", "[Sun}AdventuringBear": the tag in brackets goes
    t = re.sub(r"^.*?[\[\(\{][^\]\)\}]{0,8}[\]\)\}]\s*", "", t) if re.search(r"[\[\(\{]", t[:3]) else t
    t = re.sub(r"\s*\d+\s*m\s*$", "", t)            # a distance read into the column
    t = re.sub(r"^[^A-Za-z0-9]+|[^A-Za-z0-9]+$", "", t)
    return re.sub(r"[^a-z0-9]", "", t.lower()), t


class NameLearner:
    def __init__(self) -> None:
        self.rows = 0
        self.keys: Counter = Counter()
        self.shown: dict[str, Counter] = defaultdict(Counter)
        self.sent = ""

    def add(self, killer: str, victim: str, distance_m: int) -> str | None:
        """One decided feed row. Returns a name the first time the guess settles (or changes)."""
        if not distance_m:
            return None
        self.rows += 1
        seen = set()
        for col in (killer, victim):
            k, shown = _key(col)
            if len(k) >= 3 and k not in seen:
                seen.add(k)
                self.keys[k] += 1
                self.shown[k][shown] += 1
        if self.rows < MIN_ROWS or not self.keys:
            return None
        k, n = self.keys.most_common(1)[0]
        if n < MIN_ROWS or n < MIN_SHARE * self.rows:
            return None
        name = self.shown[k].most_common(1)[0][0]
        if name == self.sent:
            return None
        self.sent = name
        return name
