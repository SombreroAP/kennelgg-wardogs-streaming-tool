"""The inventory screen. Repacking magazines means standing still with the inventory open for a
long while, and a stream of that is a stream of nothing; the plugin shows a squad mate's POV
instead until it closes. The screen is told by the "COMBINE AMMO" hint above the storage grid
(top right) and, as a second sign, the "INVENTORY" tab top left. Read off the once-a-second
whole frame with tesseract on two small crops; open after two frames in a row (two seconds),
closed after two frames without it, so a flicker does not flap the stream."""
import difflib
import re

import cv2
import numpy as np
import pytesseract

SCALE = 3
CFG = "--psm 7 --oem 3 -c tessedit_char_whitelist=ABCDEFGHIJKLMNOPQRSTUVWXYZ "
# fractions of a 16:9 frame, from the 1600x900 inventory screenshot
COMBINE_ROI = (0.88, 0.262, 0.10, 0.05)
TAB_ROI = (0.082, 0.010, 0.085, 0.038)


def _read(roi_bgr: np.ndarray) -> str:
    if roi_bgr is None or roi_bgr.size == 0:
        return ""
    up = cv2.resize(roi_bgr, None, fx=SCALE, fy=SCALE, interpolation=cv2.INTER_CUBIC)
    gray = cv2.cvtColor(up, cv2.COLOR_BGR2GRAY)
    m = cv2.adaptiveThreshold(cv2.GaussianBlur(gray, (0, 0), 1.0), 255,
                              cv2.ADAPTIVE_THRESH_GAUSSIAN_C, cv2.THRESH_BINARY, 31, -14)
    inv = cv2.copyMakeBorder(cv2.bitwise_not(m), 12, 12, 12, 12, cv2.BORDER_CONSTANT, value=255)
    return pytesseract.image_to_string(inv, config=CFG).strip()


def _close(text: str, want: str, ratio: float) -> bool:
    t = re.sub(r"[^A-Z ]", " ", text.upper())
    t = re.sub(r"\s+", " ", t).strip()
    if want in t:
        return True
    return bool(t) and difflib.SequenceMatcher(None, t, want).ratio() >= ratio


def is_open(frame: np.ndarray) -> tuple[bool, str]:
    """(open, what was read)."""
    h, w = frame.shape[:2]
    def crop(r):
        x0, y0 = int(w * r[0]), int(h * r[1])
        return frame[y0:y0 + int(h * r[3]), x0:x0 + int(w * r[2])]
    a = _read(crop(COMBINE_ROI))
    if _close(a, "COMBINE AMMO", 0.6) or "COMBINE" in a.upper():
        return True, a
    b = _read(crop(TAB_ROI))
    if _close(b, "INVENTORY", 0.7):
        return True, b
    return False, (a + " | " + b).strip(" |")


OPEN_AFTER_S = 2.0     # the screen has to be up this long before the swap
CLOSE_MISSES = 2       # two crops in a row without it (about 2 s): one unreadable frame is not a close


# The equipment vendor's screen has a backpack grid and its own top bar ("EQUIPMENT VENDOR", tabs up
# to LOOSE AMMO), which half-read as "NEAMMO" and passed for COMBINE AMMO at 0.6: the stream flipped
# to a squad mate and back every few seconds while someone shopped (0.26.6 report). The vendor is
# not the inventory; and a half word is not enough.
_NOT_INVENTORY = ("VENDOR", "EQUIP", "OVERVI", "LOOSE")  # stems: the crop often cuts the word


def read_crops(combine_bgr, tab_bgr) -> tuple[bool, str]:
    """(open, what was read) from the two crops the plugin sends."""
    b = _read(tab_bgr) if tab_bgr is not None else ""
    if any(w in b.upper() for w in _NOT_INVENTORY):
        return False, b
    a = _read(combine_bgr) if combine_bgr is not None else ""
    if _close(a, "COMBINE AMMO", 0.75) or "COMBINE" in a.upper():
        return True, a
    if _close(b, "INVENTORY", 0.75):
        return True, b
    return False, (a + " | " + b).strip(" |")


class Watcher:
    def __init__(self, bridge):
        self.b = bridge
        self.first_hit = 0.0     # when the current run of hits began
        self.misses = 0
        self.open = False
        self.last_ts = 0.0
        self.last_text = ""

    def tick(self, now: float):
        c = self.b.inventory_cfg
        if not c.get("enabled"):
            if self.open:
                self.open = False
            self.first_hit, self.misses = 0.0, 0
            return
        crops = self.b.latest_inv()
        cb = crops.get(2)
        tb = crops.get(3)
        ts = max((cb[1] if cb else 0.0), (tb[1] if tb else 0.0))
        if ts <= self.last_ts or now - ts > 5:
            return                      # nothing new (or the plugin stopped sending)
        self.last_ts = ts
        try:
            hit, text = read_crops(cb[0] if cb else None, tb[0] if tb else None)
        except Exception as e:
            print(f"[inventory] cannot read: {e}")
            return
        self.last_text = text
        if hit:
            self.misses = 0
            if not self.first_hit:
                self.first_hit = now
            if not self.open and now - self.first_hit >= OPEN_AFTER_S:
                self.open = True
                print(f"[inventory] open  ({text})")
                self.b.send({"type": "inventory", "open": True, "text": text})
        else:
            self.misses += 1
            if self.misses >= CLOSE_MISSES:
                self.first_hit = 0.0
                if self.open:
                    self.open = False
                    print(f"[inventory] closed  (read: {text!r})")
                    self.b.send({"type": "inventory", "open": False})

    def maybe_read(self, frame, now: float):
        """Kept for the once-a-second whole frame: no longer used, the crops are quicker."""
        return
