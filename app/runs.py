"""What happens to a clip file after the plugin has named it.

Three things, each a setting in the plugin (Settings, Clips):

  trim      the 45 s file is cut down so it starts a few seconds before the first kill it was
            made for (10 s by default). A stream copy: no re-encode, no quality loss, done in
            under a second. The cut lands on the keyframe before the wanted point, so the lead
            is never shorter than asked. The end of the file is left alone, so the offsets in
            the name and the sidecar stay right.
  merge     clips made within the run window of each other (the plugin's "[1 of 3]" runs) are
            joined into one file of continuous action, the overlap between them cut once. Where
            two clips overlap, the seam is placed by lining their audio up, not by the clock,
            so nothing repeats or skips. One re-encode on the GPU.
  cut gaps  inside a merged run, stretches with nothing happening are cut out (off by default):
            every kill keeps some seconds before and after it, and a gap longer than the setting
            is dropped.

Everything works from the plugin's JSON sidecars: when each kill happened, and when the file ended.
"""
from __future__ import annotations

import json
import os
import re
import subprocess
import threading
import time

import numpy as np

_NOWIN = 0x08000000 if os.name == "nt" else 0
_LOWPRI = 0x00004000 if os.name == "nt" else 0


class Runs:
    def __init__(self, bridge, cfg: dict, ff: str | None, encoder_args):
        self.b = bridge
        self.cfg = cfg
        self.ff = ff
        self.encoder_args = encoder_args     # () -> list[str], shared with the highlights builder
        self._lock = threading.Lock()
        self._run: list[dict] = []           # clips of the run in progress
        self._last_end = 0.0
        self._timer: threading.Timer | None = None
        self._busy = threading.Lock()

    # ---- settings the plugin pushes -----------------------------------------------------------
    def _s(self, key, default):
        return (self.cfg.get("runs") or {}).get(key, default)

    # ---- one clip landed -----------------------------------------------------------------------
    def on_clip_saved(self, path: str, o: dict):
        if not path or not self.ff:
            return
        threading.Thread(target=self._handle, args=(path,), daemon=True, name="runs").start()

    def _handle(self, path: str):
        time.sleep(2.5)  # let the writer close it and the plugin finish its rename
        path = self._find(path)
        if not path:
            return
        # nothing is cut yet: a run is merged from the full files first, so the merged clip is
        # continuous, and only then is each clip trimmed. A lone clip waits the same window.
        self._track(path, self._sidecar(path))

    @staticmethod
    def _find(path: str) -> str | None:
        """The file, or where the plugin's run rename moved it ("name [2 of 3].mp4")."""
        if os.path.exists(path):
            return path
        base, ext = os.path.splitext(path)
        folder = os.path.dirname(path)
        try:
            for f in os.listdir(folder):
                if f.startswith(os.path.basename(base)) and f.endswith(ext) and re.search(r"\[\d+ of \d+\]" + re.escape(ext) + "$", f):
                    return os.path.join(folder, f)
        except OSError:
            pass
        return None

    @staticmethod
    def _sidecar(path: str) -> dict:
        p = os.path.splitext(path)[0] + ".json"
        try:
            return json.load(open(p, encoding="utf-8"))
        except (OSError, ValueError):
            return {}

    @staticmethod
    def _save_sidecar(path: str, d: dict):
        p = os.path.splitext(path)[0] + ".json"
        try:
            json.dump(d, open(p, "w", encoding="utf-8"), indent=2)
        except OSError:
            pass

    def _duration(self, path: str) -> float:
        r = subprocess.run([self.ff, "-hide_banner", "-i", path], capture_output=True, text=True, creationflags=_NOWIN)
        for line in r.stderr.splitlines():
            line = line.strip()
            if line.startswith("Duration:"):
                h, m, s = line.split()[1].rstrip(",").split(":")
                return int(h) * 3600 + int(m) * 60 + float(s)
        return 0.0

    # ---- trim ----------------------------------------------------------------------------------
    def trim(self, path: str, side: dict) -> str | None:
        if side.get("trimmed"):
            return None
        first = side.get("first_s_from_end", side.get("moment_s_from_end"))
        if first is None:
            return None
        dur = self._duration(path)
        lead = float(self._s("trim_lead_s", 10))
        start = dur - float(first) - lead
        if dur <= 0 or start < 1.5:
            return None  # nothing worth cutting
        base, ext = os.path.splitext(path)
        out = base + ".trim" + ext
        # -ss before -i on a stream copy starts at the keyframe at or before the point: the lead
        # comes out a little longer than asked, never shorter
        cmd = [self.ff, "-hide_banner", "-loglevel", "error", "-y", "-ss", f"{start:.2f}", "-i", path, "-c", "copy",
               "-avoid_negative_ts", "make_zero", "-movflags", "+faststart", out]
        r = subprocess.run(cmd, capture_output=True, text=True, creationflags=_NOWIN | _LOWPRI)
        if r.returncode != 0 or not os.path.exists(out) or os.path.getsize(out) < 1000:
            raise RuntimeError(r.stderr.strip()[-200:] or "no output")
        new_dur = self._duration(out)
        cur = self._find(path) or path   # the plugin may have renamed it while we cut
        # Windows will not replace a file another program is reading (the highlights builder, a player):
        # wait for it a little, and leave the clip as it was rather than a stray .trim file (logs: WinError 5)
        for attempt in range(6):
            try:
                os.replace(out, cur)
                break
            except PermissionError:
                if attempt == 5:
                    try:
                        os.remove(out)
                    except OSError:
                        pass
                    raise
                time.sleep(2)
                cur = self._find(path) or cur
        side["trimmed"] = True
        side["trimmed_from_s"] = round(dur, 2)
        side["duration_s"] = round(new_dur, 2)
        self._save_sidecar(cur, side)
        print(f"[runs] trimmed {os.path.basename(cur)}: {dur:.1f}s -> {new_dur:.1f}s (starts {lead:.0f}s before the first kill)")
        return cur

    # ---- runs ----------------------------------------------------------------------------------
    def _track(self, path: str, side: dict):
        end = float(side.get("end_epoch") or os.path.getmtime(path))
        window = float(self._s("window_s", 45))
        with self._lock:
            if self._run and end - self._last_end > window:
                run, self._run = self._run, []
                threading.Thread(target=self._finish, args=(run,), daemon=True).start()
            self._run.append({"path": path, "end": end, "side": side})
            self._last_end = end
            if self._timer:
                self._timer.cancel()
            self._timer = threading.Timer(window + 15, self._flush)
            self._timer.daemon = True
            self._timer.start()

    def _flush(self):
        with self._lock:
            run, self._run = self._run, []
        if run:
            self._finish(run)

    def _finish(self, run: list[dict]):
        with self._busy:
            # the plugin may have renamed them into "[n of N]" meanwhile
            for c in run:
                c["path"] = self._find(c["path"]) or c["path"]
            run = [c for c in run if os.path.exists(c["path"])]
            if len(run) >= 2 and self._s("merge", True):
                try:
                    self.merge(run)
                except Exception as e:
                    print(f"[runs] merge failed: {e}")
            if self._s("trim", True):
                for c in run:
                    try:
                        self.trim(c["path"], c["side"])
                    except Exception as e:
                        print(f"[runs] trim failed for {os.path.basename(c['path'])}: {e}")

    # ---- the seam: line two overlapping clips up by their sound ---------------------------------
    def _audio(self, path: str, start: float, length: float) -> np.ndarray:
        r = subprocess.run([self.ff, "-hide_banner", "-loglevel", "error", "-ss", f"{max(0.0, start):.3f}", "-i", path, "-t",
                            f"{length:.3f}", "-vn", "-ac", "1", "-ar", "8000", "-f", "s16le", "-"],
                           capture_output=True, creationflags=_NOWIN)
        return np.frombuffer(r.stdout, dtype=np.int16).astype(np.float32)

    def _align(self, a_path: str, a_dur: float, b_path: str, guess: float) -> float:
        """Where in B does the end of A sit. `guess` from the clocks; the answer from the sound,
        within +-1.5 s of it. Returns B's position that equals A's end."""
        probe = 4.0
        ref = self._audio(a_path, a_dur - probe, probe)                   # the last 4 s of A
        win_start = max(0.0, guess - probe - 1.5)
        hay = self._audio(b_path, win_start, probe + 3.0)                 # 7 s of B around the guess
        if len(ref) < 8000 or len(hay) <= len(ref):
            return guess
        ref = ref - ref.mean()
        hay = hay - hay.mean()
        # normalised cross-correlation: the raw one is won by whatever is loudest, not by what
        # matches, so every lag is divided by the energy of the window it compares against
        n = len(hay) + len(ref)
        fa = np.fft.rfft(hay, n)
        fb = np.fft.rfft(ref[::-1], n)
        corr = np.fft.irfft(fa * fb, n)[len(ref) - 1:len(hay)]
        cs = np.concatenate(([0.0], np.cumsum(hay.astype(np.float64) ** 2)))
        energy = np.sqrt(np.maximum(cs[len(ref):len(hay) + 1] - cs[:len(hay) - len(ref) + 1], 1e-6))
        ncc = corr[:len(energy)] / (np.linalg.norm(ref) * energy + 1e-6)
        i = int(np.argmax(ncc))
        peak = float(ncc[i])
        found = win_start + i / 8000.0 + probe
        if peak < 0.35:
            print(f"[runs] seam: sound match weak ({peak:.2f}), using the clock")
            return guess
        print(f"[runs] seam: sound puts A's end at {found:.2f}s in B (clock said {guess:.2f}s, match {peak:.2f})")
        return found

    # ---- merge ---------------------------------------------------------------------------------
    def merge(self, run: list[dict]):
        run = sorted(run, key=lambda c: c["end"])
        durs = [self._duration(c["path"]) for c in run]
        # each clip's absolute start, from its end time and length
        starts = [c["end"] - d for c, d in zip(run, durs)]
        # the pieces: A whole; B from the point that equals A's end; C from the point that equals
        # B's end; a clip that does not overlap the previous one starts a new piece with a cut
        pieces = []       # (path, in_s, out_s, abs_start_of_piece)
        timeline = 0.0    # seconds of merged output so far
        events = []       # (merged_time, event dict)
        prev_end_abs = None
        for i, (c, d) in enumerate(zip(run, durs)):
            in_s = 0.0
            if prev_end_abs is not None and starts[i] < prev_end_abs:
                guess = prev_end_abs - starts[i]
                in_s = self._align(run[i - 1]["path"], durs[i - 1], c["path"], guess)
                in_s = min(max(0.0, in_s), d - 0.5)
            pieces.append((c["path"], in_s, d, timeline))
            for ev in c["side"].get("events") or []:
                ts = ev.get("ts")
                if ts:
                    pos = float(ts) - starts[i]
                    if pos >= in_s:
                        events.append((timeline + (pos - in_s), ev))
            timeline += d - in_s
            prev_end_abs = c["end"]
        # optional: cut the dead space
        keep = None
        if self._s("cut_gaps", False) and events:
            pre, post, gap = float(self._s("pre_s", 4)), float(self._s("post_s", 5)), float(self._s("gap_s", 12))
            times = sorted(t for t, _ in events)
            ranges = []
            for t in times:
                a, b = max(0.0, t - pre), min(timeline, t + post)
                if ranges and a - ranges[-1][1] <= gap:
                    ranges[-1][1] = max(ranges[-1][1], b)
                else:
                    ranges.append([a, b])
            keep = ranges
        # cut list in source terms
        segs = []  # (path, in, out)
        for path, in_s, out_s, t0 in pieces:
            if keep is None:
                segs.append((path, in_s, out_s))
                continue
            for a, b in keep:
                lo, hi = max(a, t0), min(b, t0 + (out_s - in_s))
                if hi - lo > 0.3:
                    segs.append((path, in_s + (lo - t0), in_s + (hi - t0)))
        first = run[0]
        folder = os.path.dirname(first["path"])
        base = re.sub(r"\s*\[\d+ of \d+\]$", "", os.path.splitext(os.path.basename(first["path"]))[0])
        base = re.sub(r"\s*@-\d+(?:\.\d+)?s$", "", base)
        out = os.path.join(folder, f"{base} [run of {len(run)}].mp4")
        work = out + ".parts"
        os.makedirs(work, exist_ok=True)
        parts = []
        for k, (path, a, b) in enumerate(segs):
            p = os.path.join(work, f"p{k:02d}.mp4")
            cmd = [self.ff, "-hide_banner", "-loglevel", "error", "-y", "-ss", f"{a:.3f}", "-i", path, "-t", f"{b - a:.3f}",
                   *self.encoder_args(), "-c:a", "aac", "-b:a", "160k", "-ar", "48000", "-ac", "2", p]
            r = subprocess.run(cmd, capture_output=True, text=True, creationflags=_NOWIN | _LOWPRI)
            if r.returncode != 0:
                raise RuntimeError(r.stderr.strip()[-200:])
            parts.append(p)
        lst = os.path.join(work, "list.txt")
        with open(lst, "w", encoding="utf-8") as f:
            for p in parts:
                f.write("file '" + p.replace("\\", "/").replace("'", "'\\''") + "'\n")
        r = subprocess.run([self.ff, "-hide_banner", "-loglevel", "error", "-y", "-f", "concat", "-safe", "0", "-i", lst,
                            "-c", "copy", "-movflags", "+faststart", out], capture_output=True, text=True,
                           creationflags=_NOWIN | _LOWPRI)
        if r.returncode != 0:
            raise RuntimeError("join: " + r.stderr.strip()[-200:])
        for p in parts + [lst]:
            try:
                os.remove(p)
            except OSError:
                pass
        try:
            os.rmdir(work)
        except OSError:
            pass
        total = self._duration(out)
        # a sidecar for the run: every kill, as seconds from the end of the merged file
        evs = []
        if keep is None:
            for t, ev in events:
                evs.append({**ev, "s_from_end": round(total - t, 2)})
        side = {"run_of": len(run), "sources": [os.path.basename(c["path"]) for c in run], "duration_s": round(total, 2),
                "events": evs, "cut_gaps": keep is not None, "kills": sum(int(c["side"].get("kills", 1)) for c in run)}
        if evs:
            side["moment_s_from_end"] = min(e["s_from_end"] for e in evs)
            side["first_s_from_end"] = max(e["s_from_end"] for e in evs)
        self._save_sidecar(out, side)
        msg = f"Run merged: {os.path.basename(out)} ({len(run)} clips, {total:.0f} s{', dead space cut' if keep else ''})"
        print("[runs] " + msg)
        try:
            self.b.status(msg)
            self.b.send({"type": "event", "kind": "run", "text": msg})
        except Exception:
            pass
