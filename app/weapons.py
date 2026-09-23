"""Which weapon made a kill, by the game's own name.

Two sources, best first:

1. **Your HUD.** The bottom-right of the screen names what you are holding ("GALIL 5.56",
   "EMERGENCY RESUSCITATOR"). For your own kills that is the answer, read straight off the game.
   The plugin sends that corner as its own small stream (id 4), two frames a second, and the last
   few seconds are kept so a kill is paired with what you held when it happened.
2. **Learned icons.** Every one of your kills also shows that weapon's white silhouette in the
   kill feed. It is saved under icons/learned/<weapon>/ with the name the HUD gave it, so the same
   icon on someone else's row - the kill that downed you, a squad mate's kill - is named too.
   The more you play, the more of the arsenal it knows; nothing is guessed from a generic shape.

The names below are the game's, from the Early Access weapon list (see NAMES_SOURCE). Each has a
class that matches one of the generic kill-feed templates in templates/, so the clip rules that
key on those ("rpg", "sniper", "heli"...) keep working whatever the exact weapon.
"""
from __future__ import annotations

import difflib
import json
import os
import re
import threading
import time
from collections import Counter, deque

import cv2
import numpy as np

try:
    import pytesseract
except ImportError:  # the OCR build always has it; a bare test environment may not
    pytesseract = None

# Where the name sits: the bottom-right item plate, above the health/armour row. Fractions of
# the frame, measured on a 1875x1052 capture of the game (the plate ends at x 0.980, the text
# line is y 0.837-0.852); generous so a different HUD scale or aspect still lands in it.
HUD_ROI = (0.70, 0.815, 0.29, 0.055)
HUD_FPS = 2.0
HISTORY_S = 6.0            # HUD crops kept, seconds
LEARNED_MATCH = 0.85       # a learned icon names a kill from this score: on the streamer's own rows the
                           # same gun scored 0.88-1.00 and the nearest other rifle 0.82 (22 Sep footage)
LEARN_MAX = 12             # samples kept per weapon
LEARN_DUP = 0.95           # a new sample this close to one we have adds nothing

NAMES_SOURCE = ("wardogs.zone/database (read from the game files, build CL501228) and MetaForge, which agree "
                "name for name; checked against Bulkhead's Season 1 changelog (9 Sep 2026)")

# (exact in-game name, category, generic kill-feed template class). The class ties a name to the
# icon classes the clip rules key on; "" = none of them. Vehicle weapons are named for titles but
# never learned: a vehicle kill shows the vehicle's icon, not the gun's.
WEAPONS: list[tuple[str, str, str]] = [
    # assault rifles (A-91, Bushmaster M17S and KH-2002 are the three factions' free rifles)
    ("A-91", "assault rifle", "rifle"),
    ("AK74", "assault rifle", "rifle"),
    ("Bushmaster M17S", "assault rifle", "rifle"),
    ("FAL", "assault rifle", "rifle"),
    ("Galil", "assault rifle", "rifle"),
    ("KH-2002", "assault rifle", "rifle"),
    ("M4", "assault rifle", "rifle"),
    ("T-21", "assault rifle", "rifle"),
    # SMGs
    ("AMP-9", "SMG", "rifle"),
    ("MP5", "SMG", "rifle"),
    ("PP-19 Vityaz", "SMG", "rifle"),
    ("Super-45", "SMG", "rifle"),
    # shotguns
    ("M500", "shotgun", "shotgun"),
    ("MP43", "shotgun", "shotgun"),
    # LMGs
    ("M249 SAW", "LMG", "lmg"),
    ("PKM", "LMG", "lmg"),
    # marksman rifles
    ("BMR-308", "marksman rifle", "sniper"),
    ("SKS", "marksman rifle", "hunting"),
    ("SVD", "marksman rifle", "sniper"),
    # sniper rifles
    ("AMR 50", "sniper rifle", "sniper"),
    ("MK22", "sniper rifle", "sniper"),
    ("Mosin Nagant", "sniper rifle", "boltgun"),
    ("Scout Rifle TD", "sniper rifle", "boltgun"),
    ("SV98", "sniper rifle", "boltgun"),
    # pistols
    ("Deagle", "pistol", "pistol"),
    ("GGX 17", "pistol", "pistol"),
    ("GGX 18", "pistol", "pistol"),
    ("Judge", "pistol", "pistol"),
    ("M1911", "pistol", "pistol"),
    # launchers
    ("9K333 Verba", "launcher", "rpg"),
    ("MAAWS", "launcher", "rpg"),
    ("MGL-40", "launcher", "grenade"),
    ("RPG-7", "launcher", "rpg"),
    # bow
    ("Compound Bow", "bow", ""),
    # throwables and explosives
    ("M67 Frag Grenade", "grenade", "grenade"),
    ("Gold Frag Grenade", "grenade", "grenade"),
    ("C4 Charge", "explosive", "c4"),
    ("Remote Detonator", "explosive", "c4"),
    ("Improvised Explosive Device", "explosive", "c4"),
    ("AT Mine", "explosive", ""),
    ("Claymore", "explosive", ""),
    # melee and build tools
    ("Fists", "melee", ""),
    ("Small Hammer", "tool", "hammer"),
    ("Medium Hammer", "tool", "hammer"),
    ("Large Hammer", "tool", "hammer"),
    # emplacements you build
    ("L81 Mortar", "emplacement", "mortar"),
    ("Talon 9K-SAM", "emplacement", ""),
    ("Vanguard CIWS", "emplacement", ""),
    ("Stingray", "emplacement", ""),
    # vehicle weapons (named in titles, never learned from the icon)
    ("M134D Minigun", "vehicle weapon", ""),
    ("M249 Machine Gun", "vehicle weapon", ""),
    ("2A42 Autocannon", "vehicle weapon", ""),
    ("B-13 Rocket Pods", "vehicle weapon", ""),
    ("L55A1 Cannon", "vehicle weapon", "tank"),
    ("MG3A1 Coaxial Gun", "vehicle weapon", "tank"),
    ("L52 Cannon", "vehicle weapon", "artillery"),
]

# what can be learned from a kill-feed icon: things you hold (and the mortar you fire)
LEARNABLE = {"assault rifle", "SMG", "shotgun", "LMG", "marksman rifle", "sniper rifle", "pistol", "launcher",
             "bow", "grenade", "explosive", "melee", "tool", "emplacement"}


def slug(name: str) -> str:
    """'PP-19 Vityaz' -> 'pp19vityaz': file names and tags."""
    return re.sub(r"[^a-z0-9]", "", name.lower())


BY_SLUG = {slug(n): (n, cat, cls) for n, cat, cls in WEAPONS}


_FOLD = str.maketrans({"1": "I", "0": "O", "L": "I"})


def _norm(s: str) -> str:
    return re.sub(r"[^A-Z0-9]", "", s.upper())


def match_name(text: str) -> str | None:
    """The weapon an OCR'd HUD line names, or None. The plate often carries the calibre after the
    name ("GALIL 5.56"), and OCR drops or doubles a letter; the name has to lead the line and fit
    clearly better than any other."""
    t = _norm(text)
    if len(t) < 2:
        return None
    scored = []
    for n, _, _ in WEAPONS:
        c = _norm(n)
        head = t[:len(c) + 1]
        # OCR swaps I/1 and O/0: compare with those folded together as well
        fc, ft = c.translate(_FOLD), t.translate(_FOLD)
        r = max(difflib.SequenceMatcher(None, c, head).ratio(),
                difflib.SequenceMatcher(None, c, t[:len(c)]).ratio(),
                difflib.SequenceMatcher(None, fc, ft[:len(fc)]).ratio())
        if len(c) <= 3:          # "M4", "FAL", "C4": short names only on an exact lead
            r = 1.0 if t.startswith(c) and (len(t) == len(c) or not t[len(c)].isalpha()) else 0.0
        scored.append((r, n))
    scored.sort(reverse=True)
    best, name = scored[0]
    second = scored[1][0] if len(scored) > 1 else 0.0
    if best >= 0.82 and best - second >= 0.06:
        return name
    return None


def read_plate(crop_bgr: np.ndarray) -> str:
    """The text on the item plate: light capitals on a dark plate."""
    if pytesseract is None or crop_bgr is None or crop_bgr.size == 0:
        return ""
    g = cv2.cvtColor(crop_bgr, cv2.COLOR_BGR2GRAY)
    scale = max(1.0, 40.0 / max(1, g.shape[0] // 3))   # the text line ~13 px at 1080p: bring it to ~40
    g = cv2.resize(g, None, fx=scale, fy=scale, interpolation=cv2.INTER_CUBIC)
    _, th = cv2.threshold(g, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    inv = cv2.bitwise_not(th)
    cfg = "--psm 6 --oem 3 -c tessedit_char_whitelist=ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-x "
    try:
        return pytesseract.image_to_string(inv, config=cfg)
    except Exception:
        return ""


class WeaponReader:
    """Keeps the HUD corner's last few seconds and names what you held at a moment; saves and
    serves the learned kill-feed icons."""

    def __init__(self, base_dir: str = "icons"):
        self.hist: deque = deque()
        self._lock = threading.Lock()
        self._cache: dict[float, str | None] = {}
        self.dir = os.path.join(base_dir, "learned")
        self.learned: dict[str, list[np.ndarray]] = {}
        # weapons found to draw the same kill-feed icon (the game gives some guns one damage type,
        # and so perhaps one icon): slug -> the other slugs it cannot be told from
        self.shared: dict[str, set[str]] = {}
        self._last_name = None
        self._clash: dict[str, int] = {}
        self.load()

    # ---- the HUD ---------------------------------------------------------------------------
    def on_hud(self, crop: np.ndarray, ts: float):
        with self._lock:
            self.hist.append((ts, crop))
            while self.hist and ts - self.hist[0][0] > HISTORY_S:
                old = self.hist.popleft()
                self._cache.pop(old[0], None)

    def _name_of(self, ts: float, crop) -> str | None:
        if ts not in self._cache:
            text = read_plate(crop)
            name = None
            for line in text.splitlines():
                name = match_name(line)
                if name:
                    break
            self._cache[ts] = name
        return self._cache[ts]

    def held_at(self, ts: float) -> str | None:
        """What you were holding at `ts` (epoch seconds): the name most of the HUD frames from two
        seconds before to half a second after agree on."""
        with self._lock:
            frames = [(t, c) for t, c in self.hist if ts - 2.0 <= t <= ts + 0.5]
        # the three nearest the moment: each is a tesseract call, and the clip is waiting
        frames = sorted(frames, key=lambda f: abs(f[0] - ts))[:3]
        names = [n for n in (self._name_of(t, c) for t, c in frames) if n]
        if not names:
            return None
        name, votes = Counter(names).most_common(1)[0]
        if votes * 2 < len(names):
            return None           # switched weapons in that window: no answer beats a wrong one
        if name != self._last_name:
            print(f"[weapons] holding: {name}")
            self._last_name = name
        return name

    # ---- learned icons -----------------------------------------------------------------------
    def _shared_path(self):
        return os.path.join(self.dir, "shared.json")

    def load(self):
        self.learned = {}
        try:
            with open(self._shared_path(), encoding="utf-8") as f:
                self.shared = {k: set(v) for k, v in json.load(f).items() if k in BY_SLUG}
        except (OSError, ValueError):
            self.shared = {}
        if not os.path.isdir(self.dir):
            return
        for s in os.listdir(self.dir):
            d = os.path.join(self.dir, s)
            if s not in BY_SLUG or not os.path.isdir(d):
                continue
            ims = []
            for f in sorted(os.listdir(d)):
                if f.endswith(".png"):
                    m = cv2.imread(os.path.join(d, f), cv2.IMREAD_GRAYSCALE)
                    if m is not None:
                        ims.append(m)
            if ims:
                self.learned[s] = ims
        if self.learned:
            print(f"[weapons] learned icons: " +
                  ", ".join(f"{BY_SLUG[s][0]} x{len(v)}" for s, v in sorted(self.learned.items())))

    def identify(self, icon: np.ndarray | None) -> tuple[str | None, float]:
        """The learned weapon this kill-feed icon is, and how sure: (name, score) or (None, best)."""
        if icon is None or not self.learned:
            return None, 0.0
        ih, iw = icon.shape
        scores = {}
        for s, ims in self.learned.items():
            best = 0.0
            for t in ims:
                th, tw = t.shape
                sim = (min(tw, iw) / max(tw, iw)) * (min(th, ih) / max(th, ih))
                if sim < 0.45:
                    continue
                a, b = (icon, t) if (ih >= th and iw >= tw) else (t, icon)
                if b.shape[0] > a.shape[0] or b.shape[1] > a.shape[1]:
                    pad = np.zeros((max(a.shape[0], b.shape[0]), max(a.shape[1], b.shape[1])), np.uint8)
                    pad[:a.shape[0], :a.shape[1]] = a
                    a = pad
                v = float(cv2.matchTemplate(a, b, cv2.TM_CCOEFF_NORMED).max()) * (sim ** 0.5)
                best = max(best, v)
            scores[s] = best
        ranked = sorted(scores.items(), key=lambda kv: -kv[1])
        if not ranked:
            return None, 0.0
        s, v = ranked[0]
        partners = self.shared.get(s, set())
        second = next((sc for k, sc in ranked[1:] if k not in partners), 0.0)
        if v >= LEARNED_MATCH and v - second >= 0.03:
            if partners:
                # the same icon as another weapon: say both rather than pick one
                names = sorted([BY_SLUG[s][0]] + [BY_SLUG[p][0] for p in partners if p in BY_SLUG])
                return " or ".join(names), v
            return BY_SLUG[s][0], v
        return None, v

    def learn(self, name: str, icon: np.ndarray | None) -> bool:
        """Keep this kill-feed icon as `name`'s, unless it adds nothing or clearly is another
        weapon we already know (the HUD can change a moment before the row shows)."""
        if icon is None or icon.size == 0 or name not in (n for n, _, _ in WEAPONS):
            return False
        if category_of(name) not in LEARNABLE:
            return False
        s = slug(name)
        other, v = self.identify(icon)
        if other and name not in other.split(" or ") and v >= 0.92:
            # the HUD says one weapon and the icon is, near enough exactly, another we learned:
            # either you switched just before the kill, or the two share an icon. Twice for the
            # same pair and they are taken to share it
            o = slug(other.split(" or ")[0])
            key = "|".join(sorted([s, o]))
            self._clash[key] = self._clash.get(key, 0) + 1
            if self._clash[key] >= 2 and o not in self.shared.get(s, set()):
                self.shared.setdefault(s, set()).add(o)
                self.shared.setdefault(o, set()).add(s)
                try:
                    os.makedirs(self.dir, exist_ok=True)
                    with open(self._shared_path(), "w", encoding="utf-8") as f:
                        json.dump({k: sorted(v) for k, v in self.shared.items()}, f, indent=1)
                except OSError:
                    pass
                print(f"[weapons] {name} and {BY_SLUG[o][0]} draw the same kill-feed icon: named as both")
            else:
                print(f"[weapons] not learning {name}: the icon is {other} ({v:.2f})")
            return False
        have = self.learned.get(s, [])
        if len(have) >= LEARN_MAX:
            return False
        for t in have:
            if t.shape == icon.shape and float(cv2.matchTemplate(icon, t, cv2.TM_CCOEFF_NORMED).max()) >= LEARN_DUP:
                return False
        d = os.path.join(self.dir, s)
        try:
            os.makedirs(d, exist_ok=True)
            cv2.imwrite(os.path.join(d, f"{time.strftime('%Y%m%d-%H%M%S')}_{len(have)}.png"), icon)
        except OSError:
            return False
        self.learned.setdefault(s, []).append(icon)
        print(f"[weapons] learned {name}'s kill-feed icon ({len(self.learned[s])} sample(s))")
        return True

    def summary(self) -> dict:
        """For the plugin's log and a future Settings view: what is known, by name."""
        return {BY_SLUG[s][0]: len(v) for s, v in self.learned.items()}


def category_of(name: str) -> str:
    return BY_SLUG.get(slug(name), (name, "", ""))[1]


def class_of(name: str) -> str:
    return BY_SLUG.get(slug(name), (name, "", ""))[2]


if __name__ == "__main__":
    for t in ["GALIL 5.56", "GAL1L 5.56", "M4 5.56", "M249 SAW", "PP-19 VITYAZ 9X19", "EMERGENCY RESUSCITATOR",
              "MOSIN NAGANT 7.62", "SCOUT RIFLE TD", "FAL 7.62", "DEAGLE .50", "RPG-7", "AK74 5.45", "SMALL HAMMER",
              "9K333 VERBA", "M67 FRAG GRENADE", "M249 MACHINE GUN", "C4 CHARGE", "M4A1", "SKS 7.62", "SVD 7.62",
              "GGX 17 9MM", "GGX 18", "MK22 .338", "L81 MORTAR", "FISTS", "M134D MINIGUN", "A-91 5.56", "T-21"]:
        print(f"{t!r:28} -> {match_name(t)}")
