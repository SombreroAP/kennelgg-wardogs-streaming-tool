"""VOD scan (experimental, 0.30.0): a recording played back as fast as the PC can decode it, read for the
same highlights the plugin clips live, and clipped afterwards - for streams the plugin did not watch.

The plugin sends {"type": "vod_scan", "id", "source", "out", "player"}. The video is split into a few
time slices, each read by its own process: ffmpeg decodes the slice straight through (no seeking) at
the kill feed's read rate, the kill-feed detector from the live pipeline reads it on the video's own
clock, and the HUD's item plate names the guns. Measured on 1080p60 (M5, 28 Sep 2026): decoding runs
at about 22x real time and a kill-feed read costs 11 ms, so a few processes scan a 4-hour VOD in some
10-15 minutes. It never touches OBS, and runs at below-normal priority.

Back to the plugin: vod_progress {done_s, total_s, speed, found} about once a second, vod_done
{moments: [{t, start, end, title, tags, kills, score}]} (or {error}), and after vod_clip {indices},
vod_clip_done {index, path} for each clip cut into the plugin's clip folder, with the same sidecar a
live clip has, so the highlights compilation and Kennel Cut take them like any other.

Sources: a recording on this PC, or a Twitch VOD link - only on the streamer's own channel (the Twitch
channel set in the plugin), read straight from Twitch without downloading it whole; clips from a VOD
fetch only their own few seconds. Measured on a 1080p60 Twitch VOD of Sombrero's (26 Sep 2026, 24 min):
every kill and the 498 m death the plugin had logged live were found, plus the KILL CONFIRMED money of
kills a squad mate finished (the ticker's), at 4.8x on a Mac."""
from __future__ import annotations

import json
import multiprocessing as mp
import os
import queue
import re
import subprocess
import threading
import time

from highlights import TAG_SCORE, ffmpeg_path

READ_FPS = 5.0             # the kill feed's live read rate
FRAME_W, FRAME_H = 1920, 1080  # every source is read at this size: the areas are fractions of it
WARM_S = 20.0              # each slice starts this much early, so a kill at its seam is not cut in two
PRE_S, POST_S = 10.0, 6.0  # a clip: from before the first kill to after the last
MERGE_GAP_S = 8.0          # moments closer than this are one clip
_NOWIN = 0x08000000 if os.name == "nt" else 0          # CREATE_NO_WINDOW
_LOWPRI = 0x00004000 if os.name == "nt" else 0         # BELOW_NORMAL_PRIORITY_CLASS


TWITCH_VOD = re.compile(r"twitch\.tv/(?:[^/]+/)?videos?/(\d+)", re.I)


def resolve_twitch(url: str, own: str) -> dict:
    """A Twitch VOD link -> {media, duration, id, channel, title}: the HLS address ffmpeg reads (the
    1080p or best rendition below it), after checking the VOD is on the streamer's own channel."""
    m = TWITCH_VOD.search(url)
    if not m:
        raise RuntimeError("that is not a Twitch VOD link (twitch.tv/videos/...)")
    if not own:
        raise RuntimeError("set your Twitch channel first (Settings, Clips & replays, Twitch)")
    import yt_dlp
    with yt_dlp.YoutubeDL({"quiet": True, "no_warnings": True, "skip_download": True}) as y:
        info = y.extract_info(f"https://www.twitch.tv/videos/{m.group(1)}", download=False)
    channel = (info.get("uploader_id") or info.get("channel_id") or info.get("uploader") or "").lower()
    if channel.lstrip("@") != own.lower().lstrip("@"):
        # only your own VODs: scanning other people's streams is not what this is for
        raise RuntimeError(f"that VOD is on {info.get('uploader') or channel}'s channel, not yours ({own})")
    fmts = [f for f in info.get("formats") or [] if f.get("vcodec") not in (None, "none") and f.get("url")]
    fmts.sort(key=lambda f: ((f.get("height") or 0) <= 1080, f.get("height") or 0, f.get("fps") or 0))
    if not fmts:
        raise RuntimeError("Twitch gave no playable video for that VOD (sub-only or deleted?)")
    return {"media": fmts[-1]["url"], "duration": float(info.get("duration") or 0), "id": m.group(1),
            "channel": channel, "title": info.get("title") or "", "height": fmts[-1].get("height") or 0}


def ffprobe_path() -> str | None:
    ff = ffmpeg_path()
    if not ff:
        return None
    p = os.path.join(os.path.dirname(ff), "ffprobe" + (".exe" if os.name == "nt" else ""))
    return p if os.path.exists(p) else None


def duration_of(source: str) -> float:
    """The video's length in seconds (ffprobe, else ffmpeg's banner)."""
    fp = ffprobe_path()
    if fp:
        r = subprocess.run([fp, "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", source],
                           capture_output=True, text=True, creationflags=_NOWIN)
        try:
            return float(r.stdout.strip())
        except ValueError:
            pass
    r = subprocess.run([ffmpeg_path(), "-hide_banner", "-i", source], capture_output=True, text=True,
                       creationflags=_NOWIN)
    m = re.search(r"Duration: (\d+):(\d+):(\d+(?:\.\d+)?)", r.stderr)
    if not m:
        raise RuntimeError("could not read the video's length")
    return int(m.group(1)) * 3600 + int(m.group(2)) * 60 + float(m.group(3))


def _score(tags: list[str], kills: int) -> float:
    s = 0.0
    for t in tags:
        t = str(t).lower()
        s += TAG_SCORE.get(t, 0.0)
        if t.endswith("m") and t[:-1].isdigit():
            s += min(2.0, int(t[:-1]) / 150.0)
    return s + 0.8 * max(0, kills - 1)


def _box(frac, w=FRAME_W, h=FRAME_H):
    x, y, bw, bh = frac
    return int(w * x), int(h * y), max(1, int(w * bw)), max(1, int(h * bh))


# ---- one slice, in its own process ------------------------------------------------------------
def _scan_slice(job: dict, progress, cancel):
    """Reads [start, end) of the source (from start - WARM_S) and returns the triggers in it."""
    import numpy as np
    import detector
    import ocr
    import weapons

    vt = [0.0]                                   # the detectors' clock is the video's
    fake = type("T", (), {"time": staticmethod(lambda: vt[0]), "sleep": staticmethod(lambda s: None),
                          "monotonic": staticmethod(lambda: vt[0])})
    detector.time = ocr.time = fake
    det = detector.KillDetector(dict(job["detection"]))
    det.set_rate(READ_FPS)
    det.only_mine = True                         # other people's kills are not highlights of yours
    det.me = job["detection"].get("player_name", "")
    reader = weapons.WeaponReader(base_dir=job.get("icons") or "icons")
    det.weapons = reader
    ocr.WEAPON_READER = reader
    weapons.set_language(job["detection"].get("game_lang", ""))

    s0 = max(0.0, job["start"] - (WARM_S if job["start"] > 0 else 0.0))
    length = job["end"] - s0
    # only the three small areas leave ffmpeg, stacked in one frame: the kill feed, the HUD's item plate and
    # the kill ticker under the crosshair (whole 1080p frames through a pipe cost more than the reading)
    kx, ky, kw, kh = _box(job["roi"])
    hx, hy, hw, hh = _box(job["hud_roi"])
    tx_, ty, tw, th = _box(job["ticker_roi"])
    W = max(kw, hw, tw)
    H = kh + hh + th
    graph = (f"[0:v]fps={READ_FPS},scale={FRAME_W}:{FRAME_H}:flags=bilinear,split=3[a][b][c];"
             f"[a]crop={kw}:{kh}:{kx}:{ky},pad={W}:{kh}[a2];[b]crop={hw}:{hh}:{hx}:{hy},pad={W}:{hh}[b2];"
             f"[c]crop={tw}:{th}:{tx_}:{ty},pad={W}:{th}[c2];[a2][b2][c2]vstack=inputs=3[out]")
    cmd = [job["ffmpeg"], "-hide_banner", "-loglevel", "error", "-nostdin", "-ss", f"{s0:.3f}", "-i", job["source"],
           "-t", f"{length:.3f}", "-an", "-sn", "-filter_complex", graph, "-map", "[out]",
           "-f", "rawvideo", "-pix_fmt", "bgr24", "-"]
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                         creationflags=_NOWIN | _LOWPRI, bufsize=W * H * 3 * 8)
    import cv2
    import ticker
    runs = []                                    # the ticker's settled totals: (run id, amount, start)
    tick = ticker.Ticker.__new__(ticker.Ticker)  # its run logic only, no thread: fed on the video's clock
    tick.send = lambda o: runs.append((o["id"], o["amount"], o["t"] / 1000.0))
    tick.run, tick._active, tick._start, tick._last_seen = 0, False, 0.0, 0.0
    tick._cand, tick._count, tick._settled = None, 0, None
    tick._last_end, tick._last_total = 0.0, None
    size = W * H * 3
    out, last_plate, last_thumb, i = [], -10.0, None, 0
    try:
        while not cancel.is_set():
            buf = p.stdout.read(size)
            if len(buf) < size:
                break
            t = s0 + i / READ_FPS
            i += 1
            vt[0] = t
            frame = np.frombuffer(buf, dtype=np.uint8).reshape(H, W, 3)
            feed = frame[0:kh, 0:kw]
            plate = frame[kh:kh + hh, 0:hw]
            tbox = frame[kh + hh:H, 0:tw]
            # the item plate: what is in your hands names your kills (once a second, when it changed)
            if t - last_plate >= 1.0:
                g = cv2.resize(cv2.cvtColor(plate, cv2.COLOR_BGR2GRAY), (96, 12),
                               interpolation=cv2.INTER_AREA).astype(np.int16)
                if last_thumb is None or float((np.abs(g - last_thumb) > 25).mean()) >= 0.01:
                    last_thumb = g
                    try:
                        reader._read(plate.copy(), t)
                    except Exception:
                        pass
                last_plate = t
            # the kill ticker: a boxed "+$1,750" only your own kills put there. Read only when there is
            # bright text on it (most frames have nothing, and reading costs)
            if float((cv2.cvtColor(tbox, cv2.COLOR_BGR2GRAY) > 200).mean()) > 0.004:
                try:
                    tick._tick(t, ticker.read_total(tbox))
                except Exception:
                    pass
            else:
                tick._tick(t, None)
            for trig in det.feed_frame(feed):
                if t < job["start"]:
                    continue                     # the warm-up belongs to the slice before
                evs = [e for e in trig.events if getattr(e, "ts", None) is not None]
                first = min((e.ts for e in evs), default=t)
                tags = list(trig.tags)
                for e in trig.events:
                    if getattr(e, "distance_m", 0):
                        tags.append(f"{e.distance_m}m")
                kills = sum(1 for e in trig.events if e.killer_me and not e.victim_me)
                out.append({"t": round(t, 2), "first": round(first, 2), "title": trig.headline(),
                            "kind": trig.kind, "tags": sorted(set(tags)), "kills": kills})
            if i % 10 == 0:
                progress.put((job["idx"], t - s0))
    finally:
        try:
            p.kill()
        except Exception:
            pass
    tick._tick(s0 + length + 5, None)
    # every ticker run is a kill (or a streak of them) of yours: its last total says how big
    last = {}
    for rid, amount, start in runs:
        amt, st, steps = last.get(rid, (0, start, 0))
        # each new settled total in a run is one more kill (a kill's lines settle into one total)
        last[rid] = (max(amount, amt), st, steps + (1 if amount > amt else 0))
    for rid, (amount, start, steps) in last.items():
        if start < job["start"]:
            continue
        kills = max(1, steps)
        name = {1: "Kill", 2: "Double kill", 3: "Triple kill", 4: "Quad kill"}.get(kills, f"{kills} kills")
        out.append({"t": round(start + 2.0, 2), "first": round(start, 2), "title": f"{name} (+${amount:,})",
                    "kind": "ticker", "tags": ["kill", "ticker"] + (["multikill"] if kills > 1 else []),
                    "kills": kills})
    progress.put((job["idx"], length))
    return out


def _use_bundled_tesseract():
    """A slice is its own process: it never ran main.py's setup, so it looked for Tesseract on PATH and did not
    find it (LOG-8AD5). The installer puts it next to ClipHound.exe."""
    import sys
    if not getattr(sys, "frozen", False):
        return
    tess = os.path.join(os.path.dirname(sys.executable), "tesseract", "tesseract.exe")
    if os.path.exists(tess):
        os.environ.setdefault("TESSDATA_PREFIX", os.path.join(os.path.dirname(tess), "tessdata"))
        import pytesseract
        pytesseract.pytesseract.tesseract_cmd = tess


def _slice_entry(job, progress, cancel, results):
    _use_bundled_tesseract()
    try:
        results.put((job["idx"], _scan_slice(job, progress, cancel), None))
    except Exception as e:
        results.put((job["idx"], [], str(e)))


def merge(triggers: list[dict], duration: float) -> list[dict]:
    """Triggers to clip-sized moments: close ones become one, the best title wins."""
    ms = []
    for tr in sorted(triggers, key=lambda x: x["first"]):
        start = max(0.0, tr["first"] - PRE_S)
        end = min(duration, tr["t"] + POST_S)
        score = _score(tr["tags"], tr["kills"])
        if ms and start <= ms[-1]["end"] + MERGE_GAP_S:
            m = ms[-1]
            m["end"] = max(m["end"], end)
            m["tags"] = sorted(set(m["tags"]) | set(tr["tags"]))
            m["kills"] = max(m["kills"], tr["kills"])
            m["kill_times"] = sorted(set(m["kill_times"]) | {tr["first"], tr["t"]})
            if score >= m["score"]:
                m["title"], m["score"], m["t"] = tr["title"], score, tr["t"]
            continue
        ms.append({"t": tr["t"], "start": round(start, 2), "end": round(end, 2), "title": tr["title"],
                   "tags": tr["tags"], "kills": tr["kills"], "score": score,
                   "kill_times": sorted({tr["first"], tr["t"]})})
    for m in ms:
        m["score"] = round(m["score"], 2)
    return ms


def hms(s: float) -> str:
    s = int(s)
    return f"{s // 3600}:{s % 3600 // 60:02d}:{s % 60:02d}" if s >= 3600 else f"{s // 60}:{s % 60:02d}"


class VodScan:
    """One scan at a time, from the plugin's messages."""

    def __init__(self, bridge, cfg: dict):
        self.b = bridge
        self.cfg = cfg
        self.cancel = None
        self.busy = False
        self.last = None            # the finished scan: {id, source, moments, duration}

    # ---- messages ------------------------------------------------------------------------------
    def on_message(self, o: dict):
        t = o.get("type")
        if t == "vod_scan":
            if self.busy:
                self.b.send({"type": "vod_done", "id": o.get("id"), "error": "a scan is already running"})
                return
            threading.Thread(target=self._run, args=(o,), daemon=True, name="vod-scan").start()
        elif t == "vod_cancel" and self.cancel is not None:
            self.cancel.set()
        elif t == "vod_clip":
            threading.Thread(target=self._clip, args=(o,), daemon=True, name="vod-clip").start()
        elif t == "stream" and o.get("state") == "started" and self.cancel is not None:
            # going live: the scan gives the PC back
            self.cancel.set()

    # ---- the scan ------------------------------------------------------------------------------
    def _run(self, o: dict):
        sid, source = o.get("id"), (o.get("source") or "").strip()
        self.busy = True
        self.cancel = mp.Event()
        try:
            ff = ffmpeg_path()
            if not ff:
                raise RuntimeError("ffmpeg is missing from ClipHound's folder")
            twitch = None
            if TWITCH_VOD.search(source):
                own = ((self.cfg.get("twitch") or {}).get("broadcaster_login") or "").strip()
                twitch = resolve_twitch(source, own)
                print(f"[vodscan] Twitch VOD {twitch['id']} ({twitch['height']}p): {twitch['title']}")
                source, dur = twitch["media"], twitch["duration"]
            elif not os.path.exists(source):
                raise RuntimeError("the file is not there")
            else:
                dur = duration_of(source)
            if dur < 10:
                raise RuntimeError("the video is too short")
            det_cfg = dict(self.cfg.get("detection") or {})
            if o.get("player"):
                det_cfg["player_name"] = o["player"]
            if not (det_cfg.get("player_name") or "").strip():
                raise RuntimeError("your in-game name is not set (Settings, General)")
            roi = (self.cfg.get("capture") or {}).get("roi") or {}
            import weapons
            import ticker
            n = max(1, min(6, (os.cpu_count() or 4) // 2, int(dur // 120) or 1))
            step = dur / n
            jobs = [{"idx": k, "source": source, "ffmpeg": ff, "start": k * step, "end": min(dur, (k + 1) * step),
                     "detection": det_cfg, "icons": os.path.abspath("icons"),
                     "roi": (float(roi.get("x", 0)), float(roi.get("y", 0.42)), float(roi.get("w", 0.24)),
                             float(roi.get("h", 0.16))),
                     "hud_roi": tuple(weapons.HUD_ROI), "ticker_roi": tuple(ticker.ROI)} for k in range(n)]
            print(f"[vodscan] {os.path.basename(source)}: {hms(dur)} in {n} slices")
            ctx = mp.get_context("spawn")
            progress, results = ctx.Queue(), ctx.Queue()
            cancel = ctx.Event()
            self.cancel = cancel
            procs = [ctx.Process(target=_slice_entry, args=(j, progress, cancel, results), daemon=True) for j in jobs]
            for p in procs:
                p.start()
            done = {j["idx"]: 0.0 for j in jobs}
            got, trig, errors, t0, last = {}, [], [], time.time(), 0.0
            while len(got) < n:
                try:
                    idx, secs = progress.get(timeout=0.5)
                    done[idx] = max(done[idx], secs)
                except queue.Empty:
                    pass
                while True:
                    try:
                        idx, out, err = results.get_nowait()
                    except queue.Empty:
                        break
                    got[idx] = True
                    trig += out
                    if err:
                        errors.append(err)
                if time.time() - last >= 1.0:
                    last = time.time()
                    total_done = min(dur, sum(done.values()))
                    el = max(0.1, time.time() - t0)
                    self.b.send({"type": "vod_progress", "id": sid, "done_s": round(total_done, 1),
                                 "total_s": round(dur, 1), "speed": round(total_done / el, 1),
                                 "found": len(merge(trig, dur))})
                if cancel.is_set() and all(not p.is_alive() for p in procs):
                    break
            for p in procs:
                p.join(timeout=5)
            if cancel.is_set():
                raise RuntimeError("stopped")
            if errors and not trig:
                raise RuntimeError(errors[0])
            moments = merge(trig, dur)
            self.last = {"id": sid, "source": source, "moments": moments, "duration": dur,
                         "name": (f"Twitch {twitch['id']}" if twitch else
                                  os.path.splitext(os.path.basename(source))[0])}
            el = time.time() - t0
            print(f"[vodscan] done in {el:.0f} s ({dur / max(1, el):.1f}x): {len(moments)} moments")
            self.b.send({"type": "vod_done", "id": sid, "moments": moments, "duration": round(dur, 1),
                         "seconds": round(el, 1), "twitch_id": twitch["id"] if twitch else ""})
        except Exception as e:
            print(f"[vodscan] {e}")
            self.b.send({"type": "vod_done", "id": sid, "error": str(e)})
        finally:
            self.busy = False
            self.cancel = None

    # ---- clips ---------------------------------------------------------------------------------
    def _clip(self, o: dict):
        sc = self.last
        if not sc or sc["id"] != o.get("id"):
            self.b.send({"type": "vod_clip_done", "id": o.get("id"), "error": "scan it again first"})
            return
        out_dir = o.get("out") or os.path.join(os.path.dirname(sc["source"]), "clips")
        os.makedirs(out_dir, exist_ok=True)
        import highlights
        enc = highlights.Highlights.__new__(highlights.Highlights)
        enc.ff, enc.encoder = ffmpeg_path(), None
        stem = sc.get("name") or "VOD"
        for idx in o.get("indices") or []:
            try:
                m = sc["moments"][int(idx)]
            except (IndexError, ValueError, TypeError):
                continue
            safe = re.sub(r'[\\/:*?"<>|]+', "", m["title"]).strip()[:80] or "Highlight"
            path = os.path.join(out_dir, f"{safe} - {stem} @ {hms(m['start']).replace(':', '-')}.mp4")
            length = m["end"] - m["start"]
            cmd = [enc.ff, "-hide_banner", "-loglevel", "error", "-y", "-ss", f"{m['start']:.2f}", "-i", sc["source"],
                   "-t", f"{length:.2f}", *enc._pick_encoder(), "-c:a", "aac", "-b:a", "160k",
                   "-movflags", "+faststart", path]
            r = subprocess.run(cmd, capture_output=True, text=True, creationflags=_NOWIN | _LOWPRI)
            if r.returncode != 0:
                self.b.send({"type": "vod_clip_done", "id": o.get("id"), "index": idx,
                             "error": r.stderr.strip()[-200:]})
                continue
            # the same sidecar a live clip has: where the action is, counted from the end
            kt = m.get("kill_times") or [m["t"]]
            side = {"title": m["title"], "tags": m["tags"], "kills": max(1, m["kills"]), "source": "vod",
                    "vod": stem, "vod_start_s": m["start"],
                    "first_s_from_end": round(m["end"] - min(kt), 2), "moment_s_from_end": round(m["end"] - max(kt), 2)}
            with open(os.path.splitext(path)[0] + ".json", "w", encoding="utf-8") as fh:
                json.dump(side, fh, indent=1)
            print(f"[vodscan] clip: {os.path.basename(path)}")
            self.b.send({"type": "vod_clip_done", "id": o.get("id"), "index": idx, "path": path})
        self.b.send({"type": "vod_clip_done", "id": o.get("id"), "all": True})
