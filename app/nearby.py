"""Read the game's NEARBY panel (bottom right of the HUD): which squad mates are near you and
how far away they are, in metres.

The panel is a short right-aligned list - one row per player, the name then a small chip with
the distance - and it stays on screen while you are down, which is exactly when the OBS plugin
wants it: it shows the POV of whoever is closest, because that is who is coming to revive you.

Only rows whose name matches one of the squad mates configured in the plugin are reported, so
OCR noise cannot make the plugin switch to somebody who is not there. Kept deliberately cheap:
one tesseract call per row, a second small one only when the distance chip did not come out.
"""
import difflib
import os
import re

import cv2
import numpy as np
import pytesseract

from ocr import name_matches

SCALE = 3                     # upscale before OCR; the panel's text is ~14 px tall at 1080p
NAME_CFG = "--psm 6 --oem 3"                                   # the names, stacked, one line each
DIST_CFG = "--psm 11 --oem 3 -c tessedit_char_whitelist=0123456789m"   # the distance chips, stacked
ONE_CFG = "--psm 8 --oem 3 -c tessedit_char_whitelist=0123456789m"     # one chip on its own (fallback)
DIST_RE = re.compile(r"([0-9OoIl|!iSBG]{1,3})\s*[mM]")
CONFUSE = str.maketrans({"i": "1", "l": "1", "I": "1", "|": "1", "!": "1", "O": "0", "o": "0",
                         "S": "5", "B": "8", "G": "6"})
MAX_M = 999
GAP = 24                      # white space between stacked rows so tesseract keeps them apart
JUNK = ("nearby", "squad", "cerca", "proximite", "aproximite", "proximit", "aproximit")    # the panel's own header in English, Spanish, French (letters only)


def _mask_tophat(gray_up: np.ndarray) -> np.ndarray:
    """Small bright features (HUD text) on a darker background."""
    k = cv2.getStructuringElement(cv2.MORPH_RECT, (5 * SCALE, 5 * SCALE))
    th = cv2.morphologyEx(gray_up, cv2.MORPH_TOPHAT, k)
    _, m = cv2.threshold(th, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    return m


def _mask_local(gray_up: np.ndarray) -> np.ndarray:
    """Pixels brighter than their surroundings. Survives a bright scene behind the panel, where
    the tophat's global Otsu split turns the whole crop into one blob."""
    return cv2.adaptiveThreshold(cv2.GaussianBlur(gray_up, (0, 0), 1.0), 255,
                                 cv2.ADAPTIVE_THRESH_GAUSSIAN_C, cv2.THRESH_BINARY, 31, -12)


def _usable(mask: np.ndarray, spans: list) -> bool:
    """Do these look like rows of a name list, rather than one blob of background?"""
    return bool(spans) and all(y1 - y0 < mask.shape[0] * 0.35 for y0, y1 in spans)


def _rows_of(gray_up: np.ndarray):
    """(mask, spans), from whichever way of finding the text works on this frame: bright-on-dark
    normally, brighter-than-its-surroundings when the scene behind the panel is light."""
    best = None
    for mask in (_mask_tophat(gray_up), _mask_local(gray_up)):
        spans = _row_spans(mask)
        rank = (0 if _usable(mask, spans) else 1, float((mask > 0).mean()))
        if best is None or rank < best[0]:
            best = (rank, mask, spans)
    return best[1], best[2]


def _row_spans(mask: np.ndarray, min_h: int = 5, pad: int = 2) -> list[tuple[int, int]]:
    """(y0, y1) of each text row in upscaled pixels."""
    proj = (mask > 0).sum(1)
    thr = max(3.0, mask.shape[1] * 0.015)
    out, on, y0 = [], False, 0
    for y, v in enumerate(proj):
        if v > thr and not on:
            on, y0 = True, y
        elif v <= thr and on:
            on = False
            if y - y0 >= min_h * SCALE:
                out.append((max(0, y0 - pad * SCALE), min(mask.shape[0], y + pad * SCALE)))
    if on and mask.shape[0] - y0 >= min_h * SCALE:
        out.append((max(0, y0 - pad * SCALE), mask.shape[0]))
    return out[-8:]                       # a squad is four; the panel is never a screenful


def _sheet(crops: list[np.ndarray]) -> tuple[np.ndarray, list[tuple[int, int]]]:
    """Stack row crops into one image with white gaps, so all of them are read in a single
    tesseract call (starting tesseract costs far more than the pixels do). Returns the sheet
    and each row's (top, bottom) inside it."""
    w = max(c.shape[1] for c in crops) + 24
    parts, bands, y = [], [], GAP
    parts.append(np.full((GAP, w), 255, np.uint8))
    for c in crops:
        pad = np.full((c.shape[0], w), 255, np.uint8)
        pad[:, 12:12 + c.shape[1]] = c
        parts.append(pad)
        bands.append((y, y + c.shape[0]))
        y += c.shape[0] + GAP
        parts.append(np.full((GAP, w), 255, np.uint8))
    return np.vstack(parts), bands


def _read_sheet(crops: list[np.ndarray], cfg: str) -> list[str]:
    """One OCR call for all the crops; the text of each comes back in the same order."""
    if not crops:
        return []
    sheet, bands = _sheet(crops)
    d = pytesseract.image_to_data(sheet, config=cfg, output_type=pytesseract.Output.DICT)
    out = [""] * len(bands)
    for i, word in enumerate(d["text"]):
        if not word.strip():
            continue
        mid = d["top"][i] + d["height"][i] / 2
        for j, (y0, y1) in enumerate(bands):
            if y0 - GAP / 2 <= mid <= y1 + GAP / 2:
                out[j] = (out[j] + " " + word).strip()
                break
    return out


def _ocr_one(crop: np.ndarray) -> str:
    img = cv2.copyMakeBorder(crop, 12, 12, 18, 18, cv2.BORDER_CONSTANT, value=255)
    return pytesseract.image_to_string(img, config=ONE_CFG).strip()


def _metres(text: str, tail: bool = False) -> int | None:
    m = DIST_RE.search(text)
    if not m and tail:
        m = re.search(r"([0-9]{1,3})\s*$", text.strip())   # "MasterBaiter 9": the m was not read
    if not m:
        return None
    g = m.group(1).translate(CONFUSE)
    return int(g) if g.isdigit() and int(g) <= MAX_M else None


def _chip(gray_row: np.ndarray):
    """The distance sits in a small solid chip at the right end of the row: dark text on a light
    box, the opposite way round from the name. Eroding the bright pixels rubs out the thin glyphs
    and leaves the chip. The rightmost survivor is the chip. Returns (x, y, w, h) or None."""
    _, th = cv2.threshold(gray_row, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    k = max(3, (SCALE * 3) // 2)
    er = cv2.erode(th, np.ones((k, k), np.uint8))
    n, _lab, stats, _c = cv2.connectedComponentsWithStats(er, 8)
    H, W = gray_row.shape[:2]
    best = None
    for i in range(1, n):
        x, y, w, h, _area = stats[i]
        if w < 4 * SCALE or h < 3 * SCALE:
            continue                       # a stray blob
        if w > W * 0.8 or h > H * 0.95:
            continue                       # a bright background, not the chip
        if x + w < W * 0.45:
            continue                       # the chip is at the right end of the row
        if best is None or x > best[0]:
            best = (x, y, w, h)
    return best


def _looks_like_chip(bw: np.ndarray) -> bool:
    """A real distance chip is a light box with a little dark text in it. Anything else the
    erosion found (a bright wall, a muzzle flash) is not, and reading it wastes a tesseract run."""
    if bw is None or bw.size == 0:
        return False
    dark = float((bw < 128).mean())
    return 0.03 <= dark <= 0.55


def _ocr_row(inv_row: np.ndarray) -> str:
    """Whole row, chip and all, as one line. Only used when the distance came out of nothing else."""
    img = cv2.copyMakeBorder(inv_row, 12, 12, 18, 18, cv2.BORDER_CONSTANT, value=255)
    return pytesseract.image_to_string(img, config="--psm 7 --oem 3").strip()


def _sig(crop: np.ndarray) -> np.ndarray:
    """Cheap fingerprint of a name crop, so an unchanged name is not read again next frame."""
    return cv2.resize(crop, (128, 16), interpolation=cv2.INTER_AREA) < 128


def _cached(prev: list, sig: np.ndarray) -> str:
    """The text read for a name that looked like this last frame. Compared on the glyph pixels
    only (overlap, not equality): the background behind the panel moves, and most of the crop is
    background, so plain pixel equality calls two different names the same."""
    for s, t in prev:
        if s.shape != sig.shape:
            continue
        union = np.logical_or(s, sig).sum()
        if union and np.logical_and(s, sig).sum() / union >= 0.75:
            return t
    return ""


def _matches(text: str, name: str) -> bool:
    """Is this row's text the squad mate called `name`? Fuzzy, then a plain letters-and-digits
    comparison for the names OCR mangles the punctuation of."""
    if name_matches(text, name):
        return True
    a = re.sub(r"[^a-z0-9]", "", text.lower())
    b = re.sub(r"[^a-z0-9]", "", name.lower())
    if len(b) >= 4 and (b in a or (len(a) >= 4 and a in b)):
        return True
    return len(b) >= 4 and difflib.SequenceMatcher(None, a, b).ratio() >= 0.7


def read(roi_bgr: np.ndarray, names: list[str], cache: dict | None = None,
         debug: bool = False) -> tuple[list[dict], dict]:
    """([{name, dist, match}] nearest first, notes). Only rows whose name matched one of `names`
    are returned; `notes` says what was seen, which the plugin's Test read button shows.

    Two tesseract calls for the whole panel: one for the names, one for the distance chips.
    Pass a dict as `cache` and names that have not changed since the last frame are not read
    again, which leaves one call per frame in the steady state."""
    notes = {"rows": 0, "chips": 0, "texts": [], "dists": []}
    if roi_bgr is None or roi_bgr.size == 0 or not names:
        return [], notes
    # SCALE is for 1080p text (~14 px): a 1440p or 4K frame has bigger text already, and tripling it again
    # read "[LEL]notcal 998m / 771m / 831m" for a squad mate 75 m away (4K log). Scale to the 1080p size
    import ocr
    f = SCALE * ocr.REF_FRAME_H / max(360, ocr._frame_h)
    up = cv2.resize(roi_bgr, None, fx=f, fy=f, interpolation=cv2.INTER_CUBIC if f >= 1 else cv2.INTER_AREA)
    gray = cv2.cvtColor(up, cv2.COLOR_BGR2GRAY)
    mask, spans = _rows_of(gray)
    nameCrops, chipCrops, rowCrops = [], [], []
    notes["rows"] = len(spans)
    for y0, y1 in spans:
        g, m = gray[y0:y1], mask[y0:y1]
        box = _chip(g)
        bw = None
        if box:
            cx, cy, cw, ch = box
            crop = g[max(0, cy - 2):cy + ch + 2, max(0, cx - 3):cx + cw + 3]
            crop = cv2.resize(crop, None, fx=2, fy=2, interpolation=cv2.INTER_CUBIC)
            _, bw = cv2.threshold(crop, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
            if not _looks_like_chip(bw):
                bw, box = None, None       # a bright blob, not the chip: fall back to the row
        chipCrops.append(bw)
        if box:
            nameCrops.append(cv2.bitwise_not(m[:, :max(8, box[0] - 2)]))  # the name is left of it
            rowCrops.append(cv2.bitwise_not(m))
        else:
            # no chip: the name crop is the whole row, so the metres are in the text we get back
            nameCrops.append(cv2.bitwise_not(m))
            rowCrops.append(None)
    notes["chips"] = sum(c is not None for c in chipCrops)
    if not nameCrops:
        return [], notes

    sigs = [_sig(c) for c in nameCrops]
    prev = (cache or {}).get("rows", [])
    texts: list[str] = [""] * len(sigs)
    todo = []
    for i, sig in enumerate(sigs):
        hit = _cached(prev, sig) if cache is not None else ""
        if hit:
            texts[i] = hit
        else:
            todo.append(i)
    for i, t in zip(todo, _read_sheet([nameCrops[i] for i in todo], NAME_CFG)):
        texts[i] = t
    if cache is not None:                             # only ever holds the rows on screen now
        cache["rows"] = [(sig, t) for sig, t in zip(sigs, texts) if t]

    have = [c for c in chipCrops if c is not None]
    read_dists = _read_sheet(have, DIST_CFG)
    dists, it = [], iter(read_dists)
    for c in chipCrops:
        dists.append(next(it) if c is not None else "")
    notes["texts"] = [t for t in texts if t]
    notes["dists"] = [d for d in dists]
    if debug:
        try:
            cv2.imwrite("nearby_debug.png", up)
            if nameCrops:
                sheet, _b = _sheet(nameCrops)
                cv2.imwrite("nearby_names.png", sheet)
            notes["saved"] = os.path.abspath("nearby_debug.png")
        except Exception as e:
            notes["saved"] = f"could not save: {e}"

    seen: dict[str, dict] = {}
    for i, (text, dtext) in enumerate(zip(texts, dists)):
        if not text or re.sub(r"[^a-z]", "", text.lower()) in JUNK:
            continue
        match = next((n for n in names if n and _matches(text, n)), "")
        if not match:
            continue
        dist = _metres(dtext, tail=True)               # the chip can only read as digits and m
        if dist is None and chipCrops[i] is not None:  # the stacked read missed it: try it alone
            dist = _metres(_ocr_one(chipCrops[i]), tail=True)
        if dist is None:
            dist = _metres(text, tail=True)            # the metres may be in the row text
        if dist is None and rowCrops[i] is not None:
            # last resort: read the whole row, chip and all, and take the number off the end
            dist = _metres(_ocr_row(rowCrops[i]), tail=True)
        unknown = dist is None
        if unknown:
            dist = 998                                 # nearby but unreadable: still better than nothing
        who = re.sub(r"[^A-Za-z0-9 ._-]", "", text).strip()
        old = seen.get(match.lower())
        if old is None or dist < old["dist"]:
            seen[match.lower()] = {"name": who or match, "dist": dist, "match": match,
                                   "unknown": unknown}
    return sorted(seen.values(), key=lambda e: e["dist"]), notes


class Watcher:
    """Reads the panel from the plugin's frames as often as the plugin asked for, and sends the
    list back over the bridge whenever it changes."""

    def __init__(self, bridge):
        self.b = bridge
        self.last = 0.0
        self.sent: list[dict] = []
        self.sent_at = 0.0
        self.warned = False
        self.misses = 0
        self.diag_at = 0.0
        self.cache: dict = {}
        self.known: dict = {}         # squad mate -> (last distance that was read, when)

    HOLD_S = 12.0                 # how long a distance stays usable after the last time it was read

    def _hold(self, found: list, now: float) -> list:
        """Keep the last distance we actually read for a squad mate. One frame in several the
        little chip cannot be read, and a squad mate who is nearby with an unknown distance is
        worse than useless for picking the closest one - so use what they were last time."""
        for e in found:
            k = e["match"].lower()
            if e.get("unknown"):
                prev = self.known.get(k)
                if prev and now - prev[1] <= self.HOLD_S:
                    e["dist"], e["unknown"], e["held"] = prev[0], False, True
            else:
                self.known[k] = (e["dist"], now)
        for k in [k for k, v in self.known.items() if now - v[1] > 120]:
            self.known.pop(k, None)
        found.sort(key=lambda e: e["dist"])
        return found

    def maybe_read(self, frame, now: float, force: bool = False):
        c = self.b.nearby_cfg
        if frame is None or not c.get("enabled") or not c.get("names"):
            return
        # Nothing is read while you are alive: the plugin asks (nearby_now) the moment the damage
        # log appears, which starts a burst, and the burst is held while a squad mate is on screen.
        busy = now < self.b.nearby_burst or self.b.pov_state in ("downed", "reviving")
        if not force and not busy:
            return
        if not force and now - self.last < float(c.get("interval", 0.4)):
            return
        self.last = now
        h, w = frame.shape[:2]
        r = c.get("roi") or [0.80, 0.79, 0.19, 0.14]
        x0, y0 = int(w * r[0]), int(h * r[1])
        crop = frame[y0:y0 + int(h * r[3]), x0:x0 + int(w * r[2])]
        try:
            found, notes = read(crop, list(c["names"]), self.cache, debug=force)
        except Exception as e:
            if not self.warned:
                self.warned = True
                print(f"[nearby] cannot read the panel: {e}")
            return
        found = self._hold(found, now)
        if found:
            self.misses = 0
        else:
            # a single bad frame is normal (something bright behind the panel, a fade): only tell
            # the plugin the list is empty once we have missed three in a row
            self.misses += 1
            if self.misses < 3 and self.sent:
                return
            if now - self.diag_at > 10:
                self.diag_at = now
                print(f"[nearby] nothing matched: {notes['rows']} rows, {notes['chips']} chips, "
                      f"read {notes['texts']} {notes['dists']}, looking for {list(c['names'])}")
        if force or found != self.sent or now - self.sent_at > 5:
            if found != self.sent:
                print("[nearby] " + (", ".join(f"{e['match']} {e['dist']}m" + (" (held)" if e.get("held") else "")
                                               for e in found) or "nobody"))
            self.sent, self.sent_at = found, now
            self.b.send({"type": "nearby", "list": found})
        if force:
            print(f"[nearby] test read: {notes['rows']} rows, {notes['chips']} chips, "
                  f"texts={notes['texts']} dists={notes['dists']} -> {[e['match'] for e in found]}")
            self.b.send({"type": "nearby_test_result", "rows": notes["rows"], "chips": notes["chips"],
                         "texts": notes["texts"], "dists": notes["dists"],
                         "names": list(c["names"]), "saved": notes.get("saved", ""),
                         "found": [{"match": e["match"], "dist": e["dist"]} for e in found]})
