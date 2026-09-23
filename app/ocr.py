"""Kill-feed OCR. Rows are segmented from a top-hat mask (light UI text on any background),
then each row is read in columns: killer name, distance, victim name. Icons between the
columns are classified by template matching against templates/*.png. Every row also gets a
pixel signature so the detector can tell "same row still on screen" from "new kill",
independent of OCR noise."""
import difflib
from collections import Counter
import glob
import os
import re
from dataclasses import dataclass, field

import cv2
import numpy as np
import pytesseract

from colors import measure, classify

SCALE = 4                     # upscale factor before OCR (feed text is ~9 px tall at 1080p)
REF_FRAME_H = 1080            # every template was cut from 1080p footage; other resolutions are scaled to it
_frame_h = REF_FRAME_H


def set_frame_height(h: int):
    """The capture's frame height. The HUD scales with resolution, so a 1440p feed draws every icon
    a third bigger than the templates: the ROI is scaled back to 1080p size before anything else."""
    global _frame_h
    _frame_h = max(360, int(h or REF_FRAME_H))
    print(f"[ocr] frame height {_frame_h}: icons scaled x{REF_FRAME_H / _frame_h:.3f} to match the templates")
NAME_COL = (0.00, 0.40)       # fraction of ROI width holding "Kennel.gg - Sombrero"
VICTIM_COL = (0.50, 1.00)     # victim name (after the distance)
ICON_COL = (0.20, 0.62)       # weapon / kill-type icons live here (vehicle icons start further left)
SIG_H_PX = 12                 # signature window height in ROI px, centred on the text
SIG_SHAPE = (96, 8)           # (w, h) of the downsampled signature
OCR_CFG = "--psm 7 --oem 3"
# Distance token as tesseract tends to see it: "[68 m]", "(6im)", "[8m", "68 m]"... At least one
# bracket, or a word boundary on both sides, so "Sombrero" can never become "50 m".
_D = "[0-9OoIl|!iSBG]"
DIST_RE = re.compile(rf"(?:[\[\({{]\s*({_D}{{1,4}})\s*m\s*[\]\)}}]?|({_D}{{1,4}})\s*m\s*[\]\)}}]|\b({_D}{{1,4}})\s*m\b)")
CONFUSE = str.maketrans({"i": "1", "l": "1", "I": "1", "|": "1", "!": "1", "O": "0", "o": "0",
                         "S": "5", "B": "8", "G": "6"})
TEMPLATE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "templates")
ICON_MATCH = 0.60             # normalised cross-correlation threshold for an icon template
# Weapon icons are mutually exclusive (one weapon per row): the best-scoring one wins.
# Kill-type icons (skull = headshot, explosion) can appear alongside a weapon.
KILLTYPE_ICONS = ("skull", "explosion")
NAME_MATCH = 0.55             # threshold for templates/name_*.png (your own feed name)
COLOR_BANDS = None            # set from config by main/calibrate; None = colors.DEFAULT_BANDS
WEAPON_READER = None          # weapons.WeaponReader, set by main: names a row's icon from the learned ones


@dataclass
class RowRead:
    y: int                    # row top in ROI pixels
    sig: np.ndarray           # bool array SIG_SHAPE[::-1]
    prof: np.ndarray          # normalised column profile of the killer-name text (row identity)
    vprof: np.ndarray         # same for the victim side (tells identical no-distance kills apart)
    _inv: np.ndarray          # inverted upscaled mask of the whole row (for lazy OCR)
    _mask: np.ndarray         # upscaled mask of the row (for icon matching)
    _bgr: np.ndarray          # original-resolution row crop (for dumping / labelling)
    _hsv: np.ndarray          # upscaled HSV of the row (for name colours)
    name: str = ""
    victim: str = ""
    name_color: str = "unknown"
    victim_color: str = "unknown"
    name_is_me: bool = False     # own-name template matched in the killer column
    victim_is_me: bool = False   # own-name template matched right of the icons
    dists: list = field(default_factory=list)   # distance reads this frame (voting in the detector)
    icons: list = field(default_factory=list)
    weapon_icon: np.ndarray | None = None   # the weapon's white silhouette on its own (learning, naming)
    exact: str = ""                         # the game's name for it, when a learned icon matches

    def ocr(self):
        """Read this row on its own. The detector uses ocr_rows() instead, which reads every
        undecided row in one tesseract call - starting tesseract costs far more than the pixels."""
        ocr_rows([self])
        return self

    def fill(self, data: dict, prepped: np.ndarray):
        """Columns out of the words tesseract found in this row: killer name, distance, victim.
        `data` is image_to_data output in this row's coordinates (padded by _prep_row)."""
        W = self._inv.shape[1]
        idx = [i for i in range(len(data["text"])) if data["text"][i].strip()]
        text = " ".join(data["text"][i] for i in idx)
        mid = lambda i: data["left"][i] - PAD_X + data["width"][i] / 2
        self.name = " ".join(data["text"][i] for i in idx if mid(i) < W * NAME_COL[1])
        vx = _victim_span(data, W)
        if vx:
            self.victim = " ".join(data["text"][i] for i in idx
                                   if data["left"][i] - PAD_X >= vx[0] - 4
                                   and data["left"][i] - PAD_X + data["width"][i] <= vx[1] + 4)
        else:
            self.victim = " ".join(data["text"][i] for i in idx if mid(i) > W * 0.5)
        # distance: from the whole-row text; if that fails, re-read just the bracketed word box
        # (one extra call, and only on rows that have something bracket-shaped in them)
        d = _digits(text)
        self.dists = [d] if d else [x for x in (_digits_from_box(prepped, data),) if x]
        M = self._mask.shape[1]
        mcol = lambda c: self._mask[:, int(M * c[0]):int(M * c[1])]
        hcol = lambda c: self._hsv[:, int(M * c[0]):int(M * c[1])]
        # colour comes from the team icon in front of the killer / after the victim (and orange
        # squad text), so measure the whole column on saturated pixels, not just text pixels
        self.name_color = classify(measure(hcol(NAME_COL), mcol(NAME_COL)), COLOR_BANDS)
        vsel = slice(vx[0], M) if vx else slice(int(M * 0.5), M)
        self.victim_color = classify(measure(self._hsv[:, vsel], self._mask[:, vsel]), COLOR_BANDS)
        # icons are pure white; match on a white-pixel mask, which stays clean on busy backgrounds
        white = ((self._hsv[:, :, 2] > 150) & (self._hsv[:, :, 1] < 70)).astype(np.uint8) * 255
        self.icons, self.weapon_icon = match_icons_ex(white[:, int(M * ICON_COL[0]):int(M * ICON_COL[1])])
        if WEAPON_READER is not None and self.weapon_icon is not None:
            self.exact = WEAPON_READER.identify(self.weapon_icon)[0] or ""
        # own-name template: robust where OCR fails (rock, sky, wood backgrounds)
        self.name_is_me = match_name(white[:, :int(M * NAME_COL[1])])
        self.victim_is_me = match_name(white[:, int(M * 0.45):])
        return self


SHEET_CFG = "--psm 6 --oem 3"   # several rows stacked into one image, one line each
SHEET_GAP = 26                  # white space between them


def _stack(imgs: list[np.ndarray]):
    """One tall image holding every row, and each row's (top, bottom) inside it. Rows keep their
    own x coordinates (padding is added on the right only), so word boxes need no rebasing."""
    w = max(i.shape[1] for i in imgs)
    parts, bands, y = [np.full((SHEET_GAP, w), 255, np.uint8)], [], SHEET_GAP
    for im in imgs:
        pad = np.full((im.shape[0], w), 255, np.uint8)
        pad[:, :im.shape[1]] = im
        parts.append(pad)
        bands.append((y, y + im.shape[0]))
        y += im.shape[0] + SHEET_GAP
        parts.append(np.full((SHEET_GAP, w), 255, np.uint8))
    return np.vstack(parts), bands


def ocr_rows(rows: list["RowRead"]) -> list["RowRead"]:
    """Read every row in one tesseract call, then fill each one's columns. This is the whole
    per-frame OCR cost of the detector: one call, not four per row."""
    if not rows:
        return rows
    imgs = [_prep_row(r._inv) for r in rows]
    sheet, bands = _stack(imgs)
    d = pytesseract.image_to_data(sheet, config=SHEET_CFG, output_type=pytesseract.Output.DICT)
    keys = ("text", "left", "top", "width", "height", "conf")
    per = [{k: [] for k in keys} for _ in rows]
    for i, word in enumerate(d["text"]):
        if not word.strip():
            continue
        centre = d["top"][i] + d["height"][i] / 2
        for j, (y0, y1) in enumerate(bands):
            if y0 - SHEET_GAP / 2 <= centre <= y1 + SHEET_GAP / 2:
                for k in keys:
                    per[j][k].append(d[k][i] if k != "top" else d[k][i] - y0)
                break
    for r, data, img in zip(rows, per, imgs):
        r.fill(data, img)
    return rows


def binarize(roi_bgr: np.ndarray):
    """Return (upscaled gray, tophat mask). Tophat keeps small bright features = HUD text."""
    f = SCALE * REF_FRAME_H / _frame_h   # 4x of the 1080p size, whatever the feed's resolution
    up = cv2.resize(roi_bgr, None, fx=f, fy=f, interpolation=cv2.INTER_CUBIC)
    gray = cv2.cvtColor(up, cv2.COLOR_BGR2GRAY)
    binarize.last_up = up
    k = cv2.getStructuringElement(cv2.MORPH_RECT, (5 * SCALE, 5 * SCALE))
    th = cv2.morphologyEx(gray, cv2.MORPH_TOPHAT, k)
    _, mask = cv2.threshold(th, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    return gray, mask


def segment_rows(mask: np.ndarray, min_h_px: int = 6, pad_px: int = 3) -> list[tuple[int, int]]:
    """Row spans (y0, y1) in *ROI* pixels, from the horizontal projection of the name column."""
    h, w = mask.shape
    col = mask[:, int(w * NAME_COL[0]):int(w * NAME_COL[1])] > 0
    proj = col.sum(1)
    thr = col.shape[1] * 0.06
    rows, on, y0 = [], False, 0
    for y, v in enumerate(proj):
        if v > thr and not on:
            on, y0 = True, y
        elif v <= thr and on:
            on = False
            rows.append((y0, y))
    if on:
        rows.append((y0, h))
    out = []
    for y0, y1 in rows:
        if (y1 - y0) >= min_h_px * SCALE:
            out.append((max(0, y0 // SCALE - pad_px), min(h // SCALE, y1 // SCALE + pad_px)))
    return out


def _ocr(img) -> str:
    return pytesseract.image_to_string(img, config=OCR_CFG).strip()


def _digits(text: str) -> str | None:
    m = DIST_RE.search(text)
    if not m:
        return None
    g = next(g for g in m.groups() if g).translate(CONFUSE)
    return g if g.isdigit() else None


def _prep_row(inv: np.ndarray) -> np.ndarray:
    """Thicken strokes slightly and pad so brackets at the crop edge survive."""
    img = cv2.erode(inv, np.ones((2, 2), np.uint8))
    return cv2.copyMakeBorder(img, 8, 8, 16, 16, cv2.BORDER_CONSTANT, value=255)


def _ocr_data(img):
    d = pytesseract.image_to_data(img, config=OCR_CFG, output_type=pytesseract.Output.DICT)
    return " ".join(w for w in d["text"] if w), d


PAD_X, PAD_Y = 16, 8          # padding added by _prep_row (word boxes are in padded coords)


def _victim_span(data, W) -> tuple[int, int] | None:
    """x-range (row coords) of the word boxes after the distance bracket, i.e. the victim's
    name, skipping the trailing platform icon (a short box at the end)."""
    idx = [i for i, w in enumerate(data["text"]) if w and re.search(r"[\]\)}]", w)]
    if idx:
        i0 = idx[-1]
        boxes = [(data["left"][i], data["width"][i]) for i in range(i0 + 1, len(data["text"]))
                 if data["text"][i].strip() and data["width"][i] > 8]
    else:   # no distance (vehicle crash rows): the victim is whatever text sits right of centre
        boxes = [(data["left"][i], data["width"][i]) for i in range(len(data["text"]))
                 if data["text"][i].strip() and data["width"][i] > 8 and data["left"][i] - PAD_X > W * 0.5]
    if not boxes:
        return None
    if len(boxes) > 1 and boxes[-1][1] < 12 * SCALE:   # trailing icon box
        boxes = boxes[:-1]
    x0 = max(0, boxes[0][0] - PAD_X)
    x1 = min(W, boxes[-1][0] + boxes[-1][1] - PAD_X)
    return (x0, x1) if x1 - x0 > 8 else None


def _digits_from_box(img, data) -> str | None:
    for i, w in enumerate(data["text"]):
        if re.search(r"[\[\(\]\)]", w) or re.fullmatch(rf"{_D}+m?", w):
            x, y, ww, hh = data["left"][i], data["top"][i], data["width"][i], data["height"][i]
            crop = img[max(0, y - 6):y + hh + 6, max(0, x - 10):x + ww + 10]
            crop = cv2.resize(crop, None, fx=2, fy=2, interpolation=cv2.INTER_CUBIC)
            d = _digits(_ocr(crop))
            if d:
                return d
    return None


def read_rows(roi_bgr: np.ndarray) -> list[RowRead]:
    """Segment rows and compute signatures. No OCR yet - call RowRead.ocr() on the ones you need."""
    gray, mask = binarize(roi_bgr)
    hsv = cv2.cvtColor(binarize.last_up, cv2.COLOR_BGR2HSV)
    H, W = mask.shape
    inv = cv2.bitwise_not(mask)                     # dark text on white for tesseract
    out = []
    for y0, y1 in segment_rows(mask):
        Y0, Y1 = y0 * SCALE, y1 * SCALE
        # full-width signature, vertically centred on the name text so a 1-2 px row jitter
        # doesn't change it; compared frame-to-frame by the detector
        band = mask[Y0:Y1, :int(W * NAME_COL[1])]
        ys = np.where(band.sum(1) > 0)[0]
        cy = (ys.mean() if len(ys) else band.shape[0] / 2) + Y0
        h = SIG_H_PX * SCALE
        S0 = int(max(0, min(H - h, cy - h / 2)))
        sig = cv2.resize(mask[S0:S0 + h], SIG_SHAPE, interpolation=cv2.INTER_AREA) > 64
        # y0/y1 are in the 1080p-sized units the mask was cut in; the colour crop is in the feed's
        # own pixels, so a 1440p feed needs them scaled (without this every row but the top one
        # was saved as a strip of background)
        k = _frame_h / REF_FRAME_H
        b0, b1 = int(y0 * k), int(np.ceil(y1 * k))
        out.append(RowRead(y=y0, sig=sig, prof=name_profile(mask[Y0:Y1]), vprof=name_profile(mask[Y0:Y1], (0.5, 1.0)), _inv=inv[Y0:Y1], _mask=mask[Y0:Y1], _bgr=roi_bgr[b0:b1].copy(),
                           _hsv=hsv[Y0:Y1]))
    return out


def name_profile(row_mask: np.ndarray, cols: tuple[float, float] = (0.05, NAME_COL[1])) -> np.ndarray:
    """Column-sum profile of the killer-name glyphs, resampled to 128 and normalised. The feed
    inserts new rows at the top and pushes the rest down, so a slot can change owner without
    blanking; pixel IoU can't tell (~0.5 either way) but this profile can (swap ~0.1, same
    row 0.5-1.0)."""
    W = row_mask.shape[1]
    p = (row_mask[:, int(W * cols[0]):int(W * cols[1])] > 0).sum(0).astype(np.float32)
    p = cv2.resize(p.reshape(1, -1), (128, 1), interpolation=cv2.INTER_AREA).ravel()
    p -= p.mean()
    n = np.linalg.norm(p)
    return p / n if n else p


def prof_corr(a: np.ndarray, b: np.ndarray) -> float:
    return float(np.dot(a, b))


def sig_iou(a: np.ndarray, b: np.ndarray, cols: tuple[float, float] = (0.0, 1.0)) -> float:
    """IoU of two signatures, optionally restricted to a horizontal fraction."""
    w = a.shape[1]
    a, b = a[:, int(w * cols[0]):int(w * cols[1])], b[:, int(w * cols[0]):int(w * cols[1])]
    inter = np.logical_and(a, b).sum()
    union = np.logical_or(a, b).sum()
    return inter / union if union else 0.0


MAX_DIST_M = 999


def vote_distance(reads: list[str]) -> tuple[int | None, int]:
    """Pick the distance from noisy reads -> (metres, support). A partial read ('8' for '68')
    supports any longer candidate it is a prefix/suffix of, so '68','8','8','6' -> (68, 4).
    Multi-digit candidates need at least two supporting reads; otherwise fall back to the
    plain majority. Support is the number of reads behind the answer - use it as confidence."""
    reads = [r for r in reads if r and int(r) <= MAX_DIST_M]
    if not reads:
        return None, 0
    support = lambda c: sum(r == c or c.startswith(r) or c.endswith(r) for r in reads)
    cands = [c for c in set(reads) if len(c) >= 2 and support(c) >= 2]
    if cands:
        best = max(cands, key=lambda c: (support(c), len(c)))
        return int(best), support(best)
    best, n = Counter(reads).most_common(1)[0]
    return int(best), n


def name_matches(text: str, name: str, min_ratio: float = 0.72) -> bool:
    """Fuzzy: does `name` appear in OCR `text`? ('Kennel.gg-Sambrezo' still matches 'Sombrero').
    Also accepts a clean fragment of at least 5 characters ('pPOOH' for 'wOnderPOOH') at a
    stricter ratio, for rows where the background eats half the name."""
    t, n = text.lower(), (name or "").strip().lower()
    if len(n) < 2:
        return False      # no name set: nothing is "me" (an empty needle is found in every string)
    if n in t:
        return True
    L = len(n)
    if any(difflib.SequenceMatcher(None, t[i:i + L], n).ratio() >= min_ratio
           for i in range(max(1, len(t) - L + 1))):
        return True
    words = [w for w in re.split(r"[^a-z0-9]+", t) if len(w) >= 5]
    for w in words:
        for j in range(0, L - len(w) + 1):
            if difflib.SequenceMatcher(None, w, n[j:j + len(w)]).ratio() >= 0.8:
                return True
    return False


# ---- icons ---------------------------------------------------------------------------------
_templates: dict[str, np.ndarray] | None = None


def load_templates() -> dict[str, np.ndarray]:
    """templates/<icon>.png : binary (white-on-black) icon masks at ROI resolution x SCALE,
    cut from the white pixels of a row (see README "Icons")."""
    global _templates
    if _templates is None:
        _templates = {}
        for p in glob.glob(os.path.join(TEMPLATE_DIR, "*.png")):
            t = cv2.imread(p, cv2.IMREAD_GRAYSCALE)
            if t is not None:
                _templates[os.path.splitext(os.path.basename(p))[0]] = t
    return _templates


def _scores(mask_band: np.ndarray, names) -> dict[str, float]:
    out = {}
    for name in names:
        t = load_templates()[name]
        if t.shape[0] > mask_band.shape[0] or t.shape[1] > mask_band.shape[1]:
            continue
        out[name] = float(cv2.matchTemplate(mask_band, t, cv2.TM_CCOEFF_NORMED).max())
    return out


def _base(name: str) -> str:
    n = re.sub(r"_\d+$", "", name)                 # tank_2.png -> "tank" (variants of one icon)
    return "skull" if n == "skull_small" else n     # the small skull is a headshot too, not a weapon


def _clusters(mask_band: np.ndarray, gap: int = 6 * SCALE // 2, min_area: int = 40):
    """The icons in a band as (x0, y0, x1, y1) boxes: white blobs, with pieces closer than `gap`
    horizontally joined into one icon (a rifle's magazine is its own blob)."""
    n, _, stats, _ = cv2.connectedComponentsWithStats((mask_band > 0).astype(np.uint8), connectivity=8)
    boxes = []
    for i in range(1, n):
        x, y, w, h, a = stats[i]
        if a >= min_area:
            boxes.append([x, y, x + w, y + h])
    boxes.sort()
    out = []
    for bx in boxes:
        if out and bx[0] <= out[-1][2] + gap:
            out[-1][2] = max(out[-1][2], bx[2])
            out[-1][1] = min(out[-1][1], bx[1])
            out[-1][3] = max(out[-1][3], bx[3])
        else:
            out.append(bx)
    return out


def _size_sim(tw: int, th: int, bw: int, bh: int) -> float:
    """How alike a template and an icon box are in size, 0..1. A small template sliding over a
    slice of a big icon can correlate well; this is what stops it winning."""
    return (min(tw, bw) / max(tw, bw)) * (min(th, bh) / max(th, bh))


def match_icons(mask_band: np.ndarray) -> list[str]:
    return match_icons_ex(mask_band)[0]


def match_icons_ex(mask_band: np.ndarray) -> tuple[list[str], np.ndarray | None]:
    """The icons in this band: at most one weapon, plus skull / explosion if drawn beside it.

    Each icon is found as a blob cluster and scored against every template on its own, with the
    correlation weighted by how well the template's size fits the icon: templates were cut from
    real icons, so the right one is about the same size. That is what keeps a pistol from being
    read off the body of a rifle, or a thin grenade-launcher tube off a sniper's barrel."""
    tm = load_templates()
    names = [n for n in tm if not n.startswith("name_")]
    hits, weapons, crops = [], {}, {}
    unknown = None   # a gun-shaped icon no generic template knows
    for x0, y0, x1, y1 in _clusters(mask_band):
        bw, bh = x1 - x0, y1 - y0
        if bh < 8 or bw < 8:
            continue
        pad = 6
        crop = mask_band[max(0, y0 - pad):y1 + pad, max(0, x0 - pad):x1 + pad]
        best_base, best_v = None, 0.0
        for n in names:
            t = tm[n]
            th, tw = t.shape
            sim = _size_sim(tw, th, bw, bh)
            if sim < 0.2:
                continue                      # not remotely the same size
            # give the template room: pad the crop up to the template if the icon is smaller
            c = crop
            if th > c.shape[0] or tw > c.shape[1]:
                c = np.zeros((max(th, c.shape[0]), max(tw, c.shape[1])), np.uint8)
                c[:crop.shape[0], :crop.shape[1]] = crop
            ncc = float(cv2.matchTemplate(c, t, cv2.TM_CCOEFF_NORMED).max())
            v = ncc * (sim ** 0.5)
            if v > best_v:
                best_base, best_v = _base(n), v
        if best_base is None or best_v < ICON_MATCH:
            # no generic template fits: keep it if it is shaped like a gun (wide, one to four
            # pieces; a word of name text breaks into a piece per letter), so the learned icons
            # can still name it and your own kills can still teach it
            box = mask_band[y0:y1, x0:x1]
            np_, _, pst, _ = cv2.connectedComponentsWithStats((box > 0).astype(np.uint8), connectivity=8)
            pieces = np_ - 1
            widest = int(pst[1:, cv2.CC_STAT_WIDTH].max()) if pieces else 0
            # a gun is one body most of the icon's width long; "[KNL" is four letters side by side
            if 60 <= bw <= 220 and 12 <= bh <= 60 and bw >= 1.6 * bh and pieces <= 4 and widest >= 0.55 * bw:
                if unknown is None or bw > unknown.shape[1]:
                    unknown = box.copy()
            continue
        if best_base in KILLTYPE_ICONS:
            if best_base not in hits:
                hits.append(best_base)
        else:
            if best_v > weapons.get(best_base, 0.0):
                weapons[best_base] = best_v
                crops[best_base] = mask_band[y0:y1, x0:x1].copy()   # tight: the silhouette alone
    if weapons:
        w = max(weapons, key=weapons.get)
        hits.append(w)
        return hits, crops[w]
    return hits, unknown


def match_name(mask_band: np.ndarray) -> bool:
    """Does one of templates/name_*.png (your own feed name) appear in this band?"""
    names = [n for n in load_templates() if n.startswith("name_")]
    return any(v >= NAME_MATCH for v in _scores(mask_band, names).values())
