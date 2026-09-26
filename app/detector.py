"""Track kill-feed rows across frames, decide each row once it has enough reads, then apply
the event rules from config (multi-kill, long range, deaths, vehicles, team kills)."""
import os
import re
import time
from collections import Counter
from dataclasses import dataclass, field

import cv2
import numpy as np

from ocr import read_rows, ocr_rows, sig_iou, prof_corr, vote_distance, name_matches
from colors import relation
import weapons as W


@dataclass
class FeedEvent:
    killer: str          # OCR text of the killer column
    victim: str          # OCR text of the victim column
    distance_m: int
    dist_conf: int       # number of OCR reads supporting distance_m
    icons: list[str]
    killer_rel: str      # me | squad | team | enemy | neutral | unknown
    victim_rel: str
    ts: float
    weapon: str = ""       # what made the kill: the game's name ("Galil", "M67 Frag Grenade", "Havoc"), or
                           # the feed's class for what nothing names ("Grenade", "Helicopter"); "" = an unnamed gun
    weapon_from: str = ""  # "hud" (read off your screen) | "learned" (a learned kill-feed icon) | "feed" (the class)
    weapon_type: str = ""  # gun | grenade | explosive | launcher | mortar | artillery | bow | melee | emplacement |
                           # vehicle | vehicle weapon
    vehicle: str = ""      # the vehicle the kill came from (its name, or its class), when it did

    @property
    def weapon_exact(self) -> bool:
        return self.weapon_from in ("hud", "learned")

    @property
    def killer_me(self): return self.killer_rel == "me"
    @property
    def victim_me(self): return self.victim_rel == "me"


@dataclass
class Trigger:
    kind: str
    title: str
    events: list[FeedEvent]
    tags: list[str] = field(default_factory=list)

    def describe(self) -> str:
        """Human/AI-readable summary for filenames: what happened, distances, weapon, outcome.
        e.g. 'Double kill - 68m 61m - rifle - 2 enemies' or 'Sniped from 315m - headshot - sniper - death'"""
        parts = [self.title]
        ds = [f"{e.distance_m}m" for e in self.events if e.distance_m]
        if ds and not any(p.endswith("m") for p in parts[0].split()):
            parts.append(" ".join(ds))
        weapons = self.weapon_names()
        if weapons:
            parts.append(weapons[0])
        if any("skull" in e.icons for e in self.events) and "headshot" not in self.title.lower():
            parts.append("headshot")
        if any(e.victim_me for e in self.events):
            parts.append("death")
        elif len(self.events) > 1:
            parts.append(f"{len(self.events)} kills")
        return " - ".join(parts)

    def headline(self) -> str:
        """The clip's title as a person would say it, for Twitch and the clip index.
        'Crashed my chopper' stays as it is; 'Double kill' becomes
        'Double kill at 68m and 61m with a rifle'; a death reads 'Died to a headshot at 120m'."""
        t = self.title.strip()
        low = t.lower()
        died = any(e.victim_me for e in self.events)
        ds = [f"{e.distance_m}m" for e in self.events if e.distance_m]
        weapons = self.weapon_names()
        head = any("skull" in e.icons for e in self.events)
        bits = []
        if died and not low.startswith(("died", "killed", "downed", "crashed")):
            bits.append("Died" + (" to a headshot" if head else "") + (f" - {t}" if t else ""))
        else:
            bits.append(t[:1].upper() + t[1:] if t else "Highlight")
            if head and "headshot" not in low:
                bits[0] += ", headshot"
        if ds and not any(ch.isdigit() for ch in t):
            bits.append("at " + (ds[0] if len(ds) == 1 else ", ".join(ds[:-1]) + " and " + ds[-1]))
        if weapons and weapons[0].lower() not in low:
            # the game's names as "the Galil", the feed's classes as "a grenade", in kill order
            said, seen = [], set()
            for e in self.events:
                if not e.weapon or e.weapon.lower() in seen:
                    continue
                seen.add(e.weapon.lower())
                said.append(("the " + e.weapon) if e.weapon_exact else ("a " + e.weapon.lower()))
            if not said:
                said = ["a " + weapons[0]]
            said = said[:3]
            bits.append("with " + (said[0] if len(said) == 1 else ", ".join(said[:-1]) + " and " + said[-1]))
        n = len(self.events)
        if n > 1 and "kill" in low and not any(ch.isdigit() for ch in t):
            bits.append(f"({n} kills)")
        return " ".join(bits)[:100]

    def weapon_names(self) -> list[str]:
        """What made the kills, most-used first: the game's own names where known, the generic
        kill-feed class ("rifle", "rpg") where not."""
        exact = Counter(e.weapon for e in self.events if e.weapon and e.weapon_exact)
        if exact:
            return [n for n, _ in exact.most_common()]
        named = Counter(e.weapon.lower() for e in self.events if e.weapon)
        if named:
            return [n for n, _ in named.most_common()]
        generic = Counter(i for e in self.events for i in e.icons if i not in ("skull", "explosion"))
        return [n.replace("_", " ") for n, _ in generic.most_common()]

    @staticmethod
    def build(kind, title, events, extra=()):
        tags = {kind, *extra}
        for ev in events:
            tags.update(ev.icons)
            if ev.weapon_type:
                tags.add(ev.weapon_type.replace(" ", "-"))
            for w in (ev.weapon.split(" or ") if ev.weapon and ev.weapon_exact else []):
                tags.add(W.slug(w))
                cat = W.category_of(w)
                if cat:
                    tags.add(cat.replace(" ", "-").lower())
            if "skull" in ev.icons:
                tags.add("headshot")
            if ev.victim_rel in ("squad", "team"):
                tags.add("teamkill")
            if ev.victim_me:
                tags.add("death")
        return Trigger(kind, title, events, sorted(tags))


@dataclass
class _Row:
    sig: np.ndarray
    prof: np.ndarray
    vprof: np.ndarray
    y: int
    first: float
    last: float
    reads: list = field(default_factory=list)
    read_since: float = 0.0      # when the first OCR read of this row happened
    done: bool = False
    crop: np.ndarray | None = None


class KillDetector:
    SIG_MATCH = 0.40           # IoU vs the previous frame above this = same row still on screen
    PROF_MATCH = 0.45          # ...and the killer-name profile must correlate (catches slot swaps)
    Y_MATCH = 8                # ...and within this many ROI px vertically (same slot)
    Y_SHIFT = 120              # ...or up to this far down: a new kill pushes older rows down a slot
    ROW_TTL_S = 0.6            # a row must be seen every frame or two; the feed blanks ~0.5 s
    FADE_IN_S = 0.35           # rows fade in; OCR is garbage before this
    DUP_S = 15.0               # same slot + same distance + same roles within this = same kill
    # A row is decided on the CLOCK, not on a number of reads: 0.75 s after we start reading it,
    # with however many reads that turned out to be. OCR speed then changes the accuracy a little
    # instead of changing how long you wait for the clip.
    DECIDE_S = 0.75            # decide this long after the first read of a row
    VANISH_S = 0.40            # a row that goes away early (inventory opened, pushed off by a multi-kill)
    VOTES = 7                  # cap: never more OCR reads than this per row
    MIN_VOTES = 3              # floor: never decide on fewer than this (except the "it was me" shortcut)

    def __init__(self, cfg, dump_rows: str | None = None, harvest_dir: str | None = None):
        self.cfg = cfg
        self.set_rate(float(cfg.get("fps", 5) or 5))
        self.me = cfg["player_name"]
        self.my_team = cfg.get("my_team", "auto")      # 'red' | 'blue' | 'green' | 'auto' (set by main from the HUD)
        self.rules = cfg.get("rules") or []
        self.dump_rows = dump_rows
        # every decided row's picture, at the size it was read, kept up to a cap: the icon templates
        # are cut from these, at the streamer's own resolution
        self.harvest_dir = harvest_dir
        self._harvested = 0
        if harvest_dir:
            try:
                os.makedirs(harvest_dir, exist_ok=True)
                self._harvested = len([f for f in os.listdir(harvest_dir) if f.endswith('.png')])
            except OSError:
                self.harvest_dir = None
        self._rows: list[_Row] = []
        self._decided: list[tuple[float, int, int, tuple]] = []   # (ts, y, dist, roles)
        self._my_kills: list[FeedEvent] = []
        self._last_trigger = float('-inf')
        self._multikill_fired_for = 0
        self._n = 0

    def set_rate(self, fps: float):
        """Frames per second we are being fed. Decisions are timed, not counted, so a faster feed
        means a faster clip rather than more OCR per row."""
        self.fps = max(1.0, float(fps))
        self.votes = int(max(self.MIN_VOTES, min(self.VOTES, round(self.DECIDE_S * self.fps))))
        self.min_reads = int(max(2, min(self.votes, round(self.VANISH_S * self.fps))))

    # ---- frame in ---------------------------------------------------------------
    def feed_frame(self, roi_bgr) -> list[Trigger]:
        now = time.time()
        events = []
        pending = []                     # rows still undecided: read together, in one call
        for rd in read_rows(roi_bgr):
            best, best_iou = None, 0.0
            for r in self._rows:
                dy = rd.y - r.y
                if r.last == now or dy < -self.Y_MATCH or dy > self.Y_SHIFT:
                    continue
                iou = sig_iou(r.sig, rd.sig)
                # a row that moved down a slot must match its name profile strongly, not just its shape
                need_prof = self.PROF_MATCH if abs(dy) <= self.Y_MATCH else max(self.PROF_MATCH, 0.7)
                if iou > best_iou and prof_corr(r.prof, rd.prof) >= need_prof:
                    best, best_iou = r, iou
            if best is None or best_iou < self.SIG_MATCH:
                best = _Row(sig=rd.sig, prof=rd.prof, vprof=rd.vprof, y=rd.y, first=now, last=now)
                self._rows.append(best)
            best.last, best.sig, best.prof, best.y = now, rd.sig, rd.prof, rd.y
            if best.done or now - best.first < self.FADE_IN_S:
                continue
            pending.append((best, rd))               # only undecided rows cost tesseract time
        ocr_rows([rd for _, rd in pending])
        for best, rd in pending:
            best.reads.append(rd)
            if best.crop is None:
                best.crop = rd._bgr
            if best.read_since == 0.0:
                best.read_since = now
            enough = (len(best.reads) >= self.votes or
                      (len(best.reads) >= self.MIN_VOTES and now - best.read_since >= self.DECIDE_S))
            if enough:
                best.done = True
                ev = self._decide(best)
                if ev:
                    events.append(ev)
        # rows that vanish: HUD noise if they had almost no reads, otherwise (pushed off the feed by
        # a multi-kill, or faded) decide them with what we have
        keep = []
        for r in self._rows:
            # a row with our own name in it is worth deciding on two reads: those are the kills and
            # deaths we clip, and the feed can be covered (inventory, map) a moment after it appears
            mine = len(r.reads) >= 2 and any(x.name_is_me or x.victim_is_me for x in r.reads)
            if now - r.last < self.ROW_TTL_S:
                keep.append(r)
            elif not r.done and (len(r.reads) >= self.min_reads or mine):
                r.done = True
                ev = self._decide(r)
                if ev:
                    events.append(ev)
        self._rows = keep
        self.last_new_events = events   # every decided feed row (the plugin's dock shows them)
        return self._apply_rules(events, now)

    def _decide(self, row: _Row) -> FeedEvent | None:
        names = [r.name for r in row.reads]
        victims = [r.victim for r in row.reads]
        # majority of reads normally; crash rows (vehicle + explosion icons) sit on whatever the
        # crash site looks like, so there two agreeing reads are enough
        icon_reads = sum((r.icons for r in row.reads), [])
        crash = "explosion" in icon_reads
        need = 2 if crash else (len(names) // 2 + 1)
        killer_me = (sum(name_matches(n, self.me) for n in names) >= need
                     or sum(r.name_is_me for r in row.reads) >= 2)
        victim_me = (sum(name_matches(v, self.me) for v in victims) >= need
                     or sum(r.victim_is_me for r in row.reads) >= 2)
        top = Counter(names).most_common(1)[0][0]
        if len(re.sub(r"[^A-Za-z0-9]", "", top)) < 4 and not crash and not (killer_me or victim_me):
            return None                                # no readable killer name: HUD/texture noise
        kcol = Counter(r.name_color for r in row.reads).most_common(1)[0][0]
        vcol = Counter(r.victim_color for r in row.reads).most_common(1)[0][0]
        team = self.my_team if self.my_team in ("red", "blue", "green") else "unknown"
        killer_rel = "me" if killer_me else relation(kcol, team)
        victim_rel = "me" if victim_me else relation(vcol, team)
        if not (killer_me or victim_me or "squad" in (killer_rel, victim_rel)):
            return None                                # someone else's kill
        dist, conf = vote_distance(sum((r.dists for r in row.reads), []))
        dist = dist or 0
        # weapon icon: majority of reads; skull / explosion are small and flicker on busy
        # backgrounds, so 30 % of reads (at least 2) is enough
        cnt = Counter(sum((r.icons for r in row.reads), []))
        icons = [i for i, c in cnt.items()
                 if (c >= max(2, 0.3 * len(row.reads)) if i in ("skull", "explosion") else c * 2 > len(row.reads))]
        roles = (killer_rel, victim_rel)
        # the feed shifts rows down as new ones arrive, so identity is (roles, distance, icons)
        # inside a window, not the y position
        self._decided = [x for x in self._decided if row.first - x[0] < self.DUP_S]
        key = (roles, dist, tuple(sorted(icons)))
        if any(x[1] == key and prof_corr(x[2], row.vprof) >= 0.8 for x in self._decided):
            return None                                # same kill re-acquired after a shift / dropped frame
        self._decided.append((row.first, key, row.vprof))
        weapon, weapon_from, weapon_kind, vehicle = self._weapon(row, killer_rel == "me", icons, dist)
        ev = FeedEvent(killer=Counter(names).most_common(1)[0][0], victim=Counter(victims).most_common(1)[0][0],
                       distance_m=dist, dist_conf=conf, icons=icons,
                       killer_rel=killer_rel, victim_rel=victim_rel, ts=row.first,
                       weapon=weapon, weapon_from=weapon_from, weapon_type=weapon_kind, vehicle=vehicle)
        print(f"[feed] {killer_rel}({kcol}) {ev.killer!r} -> {victim_rel}({vcol}) {ev.victim!r}  "
              f"{dist} m (x{conf}) icons={icons}" + (f" weapon={weapon} ({weapon_from})" if weapon else "") +
              f" reads={sum((r.dists for r in row.reads), [])}")
        if self.dump_rows is not None and row.crop is not None:
            os.makedirs(self.dump_rows, exist_ok=True)
            self._n += 1
            cv2.imwrite(os.path.join(self.dump_rows, f"row_{self._n:03d}_{dist}m.png"), row.crop)
        if self.harvest_dir and row.crop is not None and self._harvested < 600 and (killer_rel == "me" or victim_rel == "me"):
            try:
                tag = "-".join(icons) if icons else "none"
                if weapon:
                    tag += "~" + W.slug(weapon)       # the exact weapon, for curating templates later
                name = f"{time.strftime('%Y%m%d-%H%M%S')}_{tag}_{dist}m_{row.crop.shape[1]}x{row.crop.shape[0]}.png"
                cv2.imwrite(os.path.join(self.harvest_dir, name), row.crop)
                self._harvested += 1
            except Exception:
                pass
        return ev

    # the generic kill-feed classes a gun can never be: if the HUD says Galil and the icon is a
    # chopper or C4, the kill was not the gun in your hands (you switched, or were in a vehicle)
    NOT_A_GUN = {"heli", "tank", "car", "c4", "rpg", "grenade", "mortar", "artillery", "hammer"}
    VEHICLES = {"heli", "tank", "car", "artillery"}
    # kills that land after the thing is out of your hands: a grenade explodes, C4 is set off, a
    # rocket or a mortar round flies, while the gun is already back up. The HUD's current weapon is
    # never the answer for these; the thrown or fired one, if the plate showed it a moment before, is
    DELAYED = {"grenade", "c4", "rpg", "mortar"}
    DELAYED_WINDOW_S = 15.0
    # fists, a knife or a tool reach this far at most: a kill further away, or one the feed draws
    # with a gun's icon, was not made with them - the plate showed them for a moment (a weapon
    # switch, a holster) while the gun made the kill
    MELEE_MAX_M = 5
    GUN_ICONS = {k for k, v in W.CLASS_TYPE.items() if v == "gun"}

    def _weapon(self, row, my_kill: bool, icons: list[str], dist: int = 0) -> tuple[str, str, str, str]:
        """(weapon, source, type, vehicle) for a kill: what made it, by the game's own name.

        source: "hud" (read off your screen at your kill), "learned" (a kill-feed icon learned from
        your kills), "feed" (the kill-feed icon's class only: Grenade, Helicopter...), "" (a gun the
        feed shows but nothing names - a gun is never guessed). type: gun, grenade, explosive,
        launcher, mortar, artillery, bow, melee, emplacement, vehicle, vehicle weapon.

        What the feed shows decides which source may answer:
          - a vehicle (chopper, tank, car, artillery): the mounted gun if you were on one, else the
            vehicle by name, else its class - never the gun you last held;
          - a grenade, C4, rocket or mortar round (or a bare explosion): the explosive your screen
            showed in the seconds before - never the gun that is back in your hands;
          - a gun: the gun in your hands (your kill) or a learned icon (anyone's)."""
        reader = getattr(self, "weapons", None)
        with_icon = [r for r in row.reads if getattr(r, "weapon_icon", None) is not None]
        icon = max(with_icon, key=lambda r: r.weapon_icon.size).weapon_icon if with_icon else None
        generic = [i for i in icons if i not in ("skull", "explosion")]
        g = generic[0] if generic else ("explosion" if "explosion" in icons else "")
        label, gtype = W.CLASS_LABEL.get(g, ""), W.CLASS_TYPE.get(g, "")
        feed = (label, "feed", gtype, "") if label else ("", "", gtype, "")

        # ---- vehicles
        if g in self.VEHICLES:
            if my_kill and reader is not None:
                latest = reader.recent(row.first, lambda n: True, 30.0)
                veh = reader.vehicle_at(row.first)
                if latest and W.weapon_type(latest) == "vehicle weapon":
                    return latest, "hud", "vehicle weapon", veh or label
                if veh:
                    return veh, "hud", "vehicle", veh
            return label, "feed", "vehicle", label

        # ---- explosives that land after they have left your hands
        if g in self.DELAYED or g == "explosion":
            if my_kill and reader is not None:
                classes = {"grenade", "c4", "rpg", "mortar"} if g == "explosion" else {g}
                thrown = reader.recent(row.first, lambda n: W.class_of(n) in classes, self.DELAYED_WINDOW_S)
                if thrown:
                    return thrown, "hud", W.weapon_type(thrown), ""
            return feed

        # ---- a gun, or no icon at all
        if my_kill and reader is not None:
            held = reader.held_at(row.first)
            if held and W.class_of(held) in self.DELAYED and g and g not in self.NOT_A_GUN:
                # the plate already shows the grenade you are pulling, but a gun made the kill
                held = reader.recent(row.first, lambda n: W.class_of(n) not in self.DELAYED and
                                     W.weapon_type(n) in ("gun", "bow", "melee"), self.DELAYED_WINDOW_S)
            if held and W.weapon_type(held) == "vehicle weapon" and g:
                held = None            # the feed shows a hand-held gun: not the mounted one
            if held and W.weapon_type(held) == "melee" and (dist > self.MELEE_MAX_M or g in self.GUN_ICONS):
                # "Fists" at 71 m with a rifle icon: the gun you held just before made the kill
                gun = reader.recent(row.first, lambda n: W.weapon_type(n) in ("gun", "bow"), 120.0)
                print(f"[weapons] not {held} at {dist} m (feed icon {g or 'none'}): "
                      + (f"{gun}, held just before" if gun else "no gun on the plate before it"))
                if not gun:
                    return feed
                return gun, "hud", W.weapon_type(gun), ""   # not learned from: the plate was not on it
            if held:
                if not (g and g in self.NOT_A_GUN and g != W.class_of(held)):
                    reader.learn(held, icon)
                return held, "hud", W.weapon_type(held), ""
            return feed
        exact = Counter(r.exact for r in row.reads if getattr(r, "exact", ""))
        if exact:
            name, votes = exact.most_common(1)[0]
            first = name.split(" or ")[0]
            if votes * 2 >= len(with_icon or row.reads) and W.weapon_type(first) in ("gun", "bow", "launcher", ""):
                return name, "learned", W.weapon_type(first) or "gun", ""
        return feed

    # ---- events -> triggers ----------------------------------------------------------
    def _apply_rules(self, events: list[FeedEvent], now) -> list[Trigger]:
        out = []
        win = self.cfg["multikill_window_s"]
        self._my_kills = [k for k in self._my_kills if now - k.ts <= win]
        if not self._my_kills:
            self._multikill_fired_for = 0
        last = getattr(self, "_last_kill_key", None)
        for ev in events:
            my_kill = ev.killer_me and ev.victim_rel not in ("me", "squad", "team")
            if my_kill:
                # the same feed row is sometimes decided twice (with and without the weapon icon);
                # one kill = one entry
                key = (ev.victim.strip().lower()[:12], ev.distance_m)
                if last and last[0] == key and now - last[1] < 4:
                    continue
                self._last_kill_key = last = (key, now)
                self._my_kills.append(ev)
            matched = False
            for rule in self.rules:
                if self._rule_hits(rule, ev):
                    title = rule["title"].format(dist=ev.distance_m, killer=ev.killer, victim=ev.victim)
                    out.append(Trigger.build(rule.get("kind", "rule"), title, [ev], rule.get("tags", ())))
                    matched = True
                    break                                  # first matching rule wins
            if not matched and my_kill and self.cfg.get("clip_every_kill"):
                title = f"Kill {ev.distance_m}m" if ev.distance_m else "Kill"
                out.append(Trigger.build("kill", title, [ev], ("kill",)))
        n = len(self._my_kills)
        if n >= self.cfg["multikill_min"] and n > self._multikill_fired_for:
            names = {2: "Double", 3: "Triple", 4: "Quad", 5: "Penta"}.get(n, f"{n}x")
            out.append(Trigger.build("multikill", f"{names} kill ({n} players)", list(self._my_kills), ("multikill",)))
            self._multikill_fired_for = n
        # cooldown: one rule trigger per cooldown_s (a multikill upgrade is always allowed);
        # several rows decided in the same frame share one clip
        rule_ok = now - self._last_trigger >= self.cfg["cooldown_s"]
        keep = []
        for t in out:
            if t.kind == "multikill":
                keep.append(t)
            elif rule_ok:
                keep.append(t)
                rule_ok = False
        if keep:
            self._last_trigger = now
        return keep

    @staticmethod
    def _rule_hits(rule: dict, ev: FeedEvent) -> bool:
        # killer / victim in a rule: me | squad | team | friendly (squad or team) | enemy | other (not me) | any
        def ok(want, rel, name=""):
            return {"any": True, "me": rel == "me", "squad": rel == "squad", "team": rel == "team",
                    "friendly": rel in ("squad", "team"), "enemy": rel == "enemy",
                    "other": rel != "me",
                    # nobody readable in that column: an environmental death has no killer
                    "none": rel != "me" and len(re.sub(r"[^A-Za-z0-9]", "", name)) < 3}[want]
        if not ok(rule.get("killer", "any"), ev.killer_rel, ev.killer) or not ok(rule.get("victim", "any"), ev.victim_rel, ev.victim):
            return False
        if ev.distance_m < rule.get("min_dist", 0):
            return False
        if "min_dist" in rule and ev.dist_conf < rule.get("min_conf", 3):
            return False                               # distance too uncertain for a range rule
        need = rule.get("icon")
        if need and not any(i.startswith(need) for i in ev.icons):
            return False
        return True
