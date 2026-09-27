"""The kill ticker under the crosshair. When you get a kill the game draws skulls, a boxed running total
("+$1,750") and the lines that make it up ("KILL CONFIRMED +$1,500", "PASSENGER SURVIVED +$250") just
below the centre of the screen. Only kills and what comes with them show there - heals, spotting and
zones are only in the corner under your balance - but the box sits on its own dark background, so it
reads where the corner lines are lost against a white sky.

The plugin sends a crop of that area (stream id 5, five a second, only while session stats are on).
Here the box total is read and followed: it rolls up for a moment, settles, and stays up for a couple of
seconds; a second kill before it goes adds to the same total. Every settled value of a run goes to the
plugin as {"type": "kill_reward", "id": run, "amount": total, "t": start_ms}, and the plugin uses it to
put the right reason on money the corner lines missed. The wallet stays the authority for the total.

Measured on 1440p footage (12-13 Sep): +$1,750 (rolling up through +$52 and +$1,569 first), +$1,500,
+$3,000, +$2,750 read cleanly on every settled frame."""
import re
import threading
import time

import cv2
import numpy as np
import pytesseract

# fractions of a 16:9 frame: the box, the skulls above it and the lines under it
ROI = (0.40, 0.62, 0.20, 0.14)
FPS = 5.0
SETTLE = 2          # the same value this many reads in a row is a settled total
GONE_S = 1.2        # no box for this long: the run is over
_CFG = "--psm 6 -c tessedit_char_whitelist=+$,0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
_AMT = re.compile(r"\+?\$([0-9][0-9,]{1,8})")


def read_total(crop_bgr: np.ndarray):
    """The box total in this crop, or None: the topmost dollar amount (the lines' amounts sit under it)."""
    if crop_bgr is None or crop_bgr.size == 0:
        return None
    g = cv2.cvtColor(crop_bgr, cv2.COLOR_BGR2GRAY)
    s = max(1.0, 450.0 / max(1, g.shape[0]))           # the same text size at 1080p, 1440p and 4K
    g = cv2.resize(g, None, fx=s, fy=s, interpolation=cv2.INTER_CUBIC)
    _, m = cv2.threshold(g, 185, 255, cv2.THRESH_BINARY)  # white text on the box's dark background
    d = pytesseract.image_to_data(255 - m, config=_CFG, output_type=pytesseract.Output.DICT)
    best = None
    for i, t in enumerate(d["text"]):
        mt = _AMT.search((t or "").strip())
        if not mt:
            continue
        try:
            v = int(mt.group(1).replace(",", ""))
        except ValueError:
            continue
        if 50 <= v <= 200000 and (best is None or d["top"][i] < best[0]):
            best = (d["top"][i], v)
    return best[1] if best else None


class Ticker:
    def __init__(self, send):
        self.send = send
        self._cv = threading.Condition()
        self._latest = None
        self.run = 0
        self._active = False
        self._start = 0.0
        self._last_seen = 0.0
        self._cand, self._count, self._settled = None, 0, None
        self._last_end, self._last_total = 0.0, None   # the run that just ended, to join a brief dropout
        threading.Thread(target=self._loop, daemon=True, name="kill-ticker").start()

    def on_crop(self, crop, ts: float):
        with self._cv:
            self._latest = (crop, ts)
            self._cv.notify()

    def _loop(self):
        while True:
            with self._cv:
                while self._latest is None:
                    self._cv.wait(timeout=1.0)
                    if self._latest is None:
                        self._tick(time.time(), None)   # a run ends even when no more crops come
                crop, ts = self._latest
                self._latest = None
            try:
                self._tick(ts, read_total(crop))
            except Exception as e:
                print(f"[ticker] {e}")

    def _tick(self, ts: float, v):
        if v is None:
            if self._active and ts - self._last_seen > GONE_S:
                if self._settled:
                    print(f"[ticker] run {self.run} ended at +${self._settled:,}")
                self._last_end, self._last_total = self._last_seen, self._settled
                self._active = False
                self._cand, self._count, self._settled = None, 0, None
            return
        if not self._active:
            self._active = True
            if self._last_total is not None and ts - self._last_end < 3.0 and v >= self._last_total:
                # the box was lost for a moment (a flash, smoke): the same run, not a new kill
                self._settled = self._last_total
            else:
                self.run += 1
                self._start = ts
        self._last_seen = ts
        if v == self._cand:
            self._count += 1
        else:
            self._cand, self._count = v, 1
        if self._count >= SETTLE and v != self._settled:
            self._settled = v
            self.send({"type": "kill_reward", "id": self.run, "amount": v, "t": int(self._start * 1000)})
