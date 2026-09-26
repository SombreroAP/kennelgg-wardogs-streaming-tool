"""Chat going wild -> a clip.

Every chat message is fed in. A moment is "hot" when, in the last WINDOW seconds, enough different
people wrote enough messages - several times the channel's usual rate - or when a few people ask
for a clip outright ("clip", "clip it", "!clip"). Chat reacts a few seconds after the thing it is
reacting to (stream delay plus typing), so the moment handed back is a little before the burst
began; the replay buffer reaches well past that. One clip per COOLDOWN at most.

Commands other than a clip request, the broadcaster's own lines and the usual bots are ignored,
so a !discord spam or a bot's timer never looks like a reaction.
"""
from __future__ import annotations

import re
import time
from collections import Counter, deque

BOTS = {"nightbot", "streamelements", "streamlabs", "moobot", "fossabot", "soundalerts", "wizebot",
        "botrixoficial", "sery_bot", "kofistreambot", "commanderroot", "streamstickers", "blerp",
        "own3d", "pretzelrocks", "infokennel"}
# level -> (different people, messages, times the usual rate). 1 = only big moments, 3 = small ones too
LEVELS = {1: (6, 10, 4.0), 2: (4, 7, 3.0), 3: (3, 5, 2.5)}
WINDOW = 10.0        # seconds a burst is counted over
HISTORY = 300.0      # the usual rate is measured over the last five minutes
COOLDOWN = 60.0
REACTION_S = 5.0     # how far before the first message of a burst the moment itself was
ASK = re.compile(r"^\s*!?clip(\s+(it|that|this))?\b", re.I)
WORD = re.compile(r"[A-Za-z0-9_']{2,}")


class ChatHype:
    def __init__(self, level_fn, fire, me: str = ""):
        self.level = level_fn        # () -> 0 off, 1-3 as in LEVELS
        self.fire = fire             # (title, tags, moment_epoch, info) -> None
        self.me = (me or "").lower()
        self.msgs: deque = deque()   # (t, user, text)
        self.last = 0.0

    def feed(self, user: str, text: str, t: float | None = None):
        """One chat line. Returns the clip title when this line tipped chat over, else None."""
        t = time.time() if t is None else t
        u = (user or "").lower()
        text = (text or "").strip()
        if not u or not text or u in BOTS or u == self.me:
            return None
        asking = bool(ASK.match(text))
        if text.startswith("!") and not asking:
            return None
        self.msgs.append((t, u, text, asking))
        while self.msgs and t - self.msgs[0][0] > HISTORY:
            self.msgs.popleft()
        lvl = int(self.level() or 0)
        if lvl not in LEVELS or t - self.last < COOLDOWN:
            return None
        need_users, need_msgs, times = LEVELS[lvl]
        recent = [m for m in self.msgs if t - m[0] <= WINDOW]
        older = len(self.msgs) - len(recent)
        covered = (t - self.msgs[0][0]) - WINDOW
        usual = older / covered * WINDOW if covered >= 60 else 0.0   # messages per WINDOW, normally
        people = {m[1] for m in recent}
        askers = {m[1] for m in recent if m[3]}
        # the first minute after joining, the usual rate is not known: a busy chat would look like a
        # burst, so only an outright request for a clip counts until then
        burst = covered >= 60 and len(people) >= need_users and len(recent) >= max(need_msgs, times * usual)
        asked = len(askers) >= max(2, need_users - 2)
        if not (burst or asked):
            return None
        self.last = t
        # the word most of them wrote ("KEKW", "W", "NO WAY"): once per person, three people at least
        said = Counter()
        for p in people:
            words = {w.lower() for m in recent if m[1] == p for w in WORD.findall(m[2])}
            said.update(words)
        common = [(w, n) for w, n in said.most_common(3) if n >= 3 and w not in ("the", "and", "you", "that")]
        word = ""
        if common:
            w = common[0][0]
            # keep the way most people wrote it ("KEKW", not "kekw")
            spelled = Counter(x for m in recent for x in WORD.findall(m[2]) if x.lower() == w)
            word = spelled.most_common(1)[0][0] if spelled else w
        title = "Chat asked for a clip" if asked and not burst else "Chat went wild"
        if word and not asked:
            title += f" ({word})"
        first = min(m[0] for m in recent)
        info = {"kind": "chat", "description": f"{len(recent)} messages from {len(people)} people in "
                                               f"{int(WINDOW)} s" + (f", {len(askers)} asking for a clip" if askers else ""),
                "chat_messages": len(recent), "chat_people": len(people), "chat_word": word,
                "moments": [first - REACTION_S]}
        tags = ["chat"] + (["asked"] if asked else [])
        try:
            self.fire(title, tags, first - REACTION_S, info)
        except Exception as e:
            print(f"[chat] could not ask for the clip: {e}")
        return title
