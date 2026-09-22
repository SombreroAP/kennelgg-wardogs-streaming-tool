"""The end-of-stream highlights compilation, built on the streaming PC while it streams.

Two halves. As each clip is saved, its action is cut out into a small cached segment (one at a
time, low priority, held back whenever the plugin reports OBS dropping frames). When the plugin
asks (Play highlights with nothing built yet, or the stream ending), the session's segments are
picked and ordered, an intro and outro card are made, everything is joined, a music track is
laid under it if the highlights folder has a music/ subfolder, and the file lands in the
highlights folder. Only ffmpeg is needed: it ships next to ClipHound.exe.
"""
from __future__ import annotations

import datetime
import glob
import hashlib
import json
import os
import shutil
import subprocess
import sys
import threading
import time

PRE_S, POST_S = 5.0, 3.0            # a segment runs from this long before the first kill to this long after the last
ASSUME_FROM_END = 7.0               # a clip with no offsets: the kill is about this far before the end
MAX_SEGMENTS = 12
CARD_INTRO_S, CARD_OUTRO_S = 2.2, 2.6
W, H, FPS = 1920, 1080, 60
MUSIC_VOLUME = 0.18
TAG_SCORE = {"multikill": 3.0, "headshot": 2.0, "longrange": 2.0, "rpg": 1.5, "heli": 1.5, "tank": 1.5, "c4": 1.5,
             "highlight": 2.0, "manual": 1.0, "funny": 1.0, "death": -2.0, "teamkill": -3.0, "downed": -1.0}
_NOWIN = 0x08000000 if os.name == "nt" else 0          # CREATE_NO_WINDOW
_LOWPRI = 0x00004000 if os.name == "nt" else 0         # BELOW_NORMAL_PRIORITY_CLASS


def _base_dir() -> str:
    return os.path.dirname(sys.executable) if getattr(sys, "frozen", False) else os.path.dirname(os.path.abspath(__file__))


def ffmpeg_path() -> str | None:
    for cand in (os.path.join(_base_dir(), "ffmpeg", "ffmpeg.exe"), os.path.join(_base_dir(), "ffmpeg.exe"),
                 shutil.which("ffmpeg")):
        if cand and os.path.exists(cand):
            return cand
    return None


def _font() -> str | None:
    for cand in (os.path.join(_base_dir(), "fonts", "SairaCondensed-Bold.ttf"),
                 os.path.join(_base_dir(), "..", "data", "overlay", "SairaCondensed-Bold.ttf"),
                 r"C:\Windows\Fonts\arialbd.ttf", "/System/Library/Fonts/Supplemental/Arial Bold.ttf"):
        if os.path.exists(cand):
            return cand
    return None


def _ff_escape(s: str) -> str:
    """A path or text inside an ffmpeg filter option."""
    return s.replace("\\", "/").replace(":", "\\:").replace("'", "\\'").replace(",", "\\,").replace("%", "\\%")


class Highlights:
    def __init__(self, bridge, cfg: dict):
        self.b = bridge
        self.cfg = cfg
        self.ff = ffmpeg_path()
        self.work = os.path.join(_base_dir(), "work", "highlights")
        os.makedirs(self.work, exist_ok=True)
        self.encoder = None            # decided on the first render: h264_nvenc or libx264
        self.queue: list[dict] = []    # clips waiting for a segment
        self.paused_until = 0.0        # OBS was dropping frames: hold off until then
        self._lagged = None
        self._lock = threading.Lock()
        self._busy = False
        self._building = False
        threading.Thread(target=self._worker, daemon=True, name="highlights").start()
        print(f"[highlights] ffmpeg: {self.ff or 'NOT FOUND - no compilation can be made'}; cache {self.work}")

    # ---- what the plugin tells us -------------------------------------------------------------
    def on_clip_saved(self, path: str, o: dict):
        if not path or not self.ff:
            return
        with self._lock:
            self.queue.append({"path": path, "title": o.get("title", ""), "tags": list(o.get("tags") or [])})

    def on_obs_health(self, o: dict):
        """{lagged, total, dropped}: counters since OBS started. A rise in lagged or dropped frames
        since the last report means OBS is short of GPU or network right now: back off."""
        lagged = int(o.get("lagged", 0)) + int(o.get("dropped", 0))
        if self._lagged is not None and lagged > self._lagged:
            self.paused_until = time.time() + 30.0
            print(f"[highlights] OBS dropped {lagged - self._lagged} frame(s): segment work paused 30 s")
        self._lagged = lagged

    def on_build(self, o: dict):
        """{clips:[{path,title,tags,when}], out, player, max}: build the compilation now."""
        if self._building:
            self._say("Highlights: already building.")
            return
        threading.Thread(target=self._build, args=(o,), daemon=True, name="highlights-build").start()

    # ---- segments ------------------------------------------------------------------------------
    def _worker(self):
        while True:
            time.sleep(1.0)
            if time.time() < self.paused_until or self._building:
                continue
            with self._lock:
                job = self.queue.pop(0) if self.queue else None
            if not job:
                continue
            try:
                self._busy = True
                self.segment(job["path"], job.get("tags") or [])
            except Exception as e:
                print(f"[highlights] segment failed for {job['path']}: {e}")
            finally:
                self._busy = False

    def _probe_duration(self, path: str) -> float:
        # ffmpeg only: no ffprobe in the bundle. The duration line is on stderr.
        r = subprocess.run([self.ff, "-hide_banner", "-i", path], capture_output=True, text=True, creationflags=_NOWIN)
        for line in r.stderr.splitlines():
            line = line.strip()
            if line.startswith("Duration:"):
                hms = line.split()[1].rstrip(",")
                h, m, s = hms.split(":")
                return int(h) * 3600 + int(m) * 60 + float(s)
        return 0.0

    def _window(self, path: str, side_of: str | None = None) -> tuple[float, float]:
        """(start, length) of the action inside the file, from the plugin's sidecar (side_of: another
        file's sidecar, for the vertical twin of a clip - the two were saved at the same instant)."""
        dur = self._probe_duration(path)
        if dur <= 0:
            raise RuntimeError("could not read the file's length")
        first = last = None
        side = os.path.splitext(side_of or path)[0] + ".json"
        if os.path.exists(side):
            try:
                d = json.load(open(side, encoding="utf-8"))
                last = d.get("moment_s_from_end")
                first = d.get("first_s_from_end", last)
            except (OSError, ValueError):
                pass
        if last is None:
            first = last = ASSUME_FROM_END
        start = max(0.0, dur - float(first) - PRE_S)
        end = min(dur, dur - float(last) + POST_S)
        if end - start < 3.0:
            end = min(dur, start + 8.0)
        return start, end - start

    def encoder_args(self) -> list[str]:
        return self._pick_encoder()

    def _pick_encoder(self) -> list[str]:
        if self.encoder is None:
            r = subprocess.run([self.ff, "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                                "color=c=black:s=256x256:r=30:d=0.2", "-c:v", "h264_nvenc", "-f", "null", "-"],
                               capture_output=True, text=True, creationflags=_NOWIN)
            self.encoder = "h264_nvenc" if r.returncode == 0 else "libx264"
            print(f"[highlights] encoder: {self.encoder}")
        if self.encoder == "h264_nvenc":
            return ["-c:v", "h264_nvenc", "-preset", "p4", "-tune", "hq", "-rc", "vbr", "-cq", "21", "-b:v", "0",
                    "-maxrate", "40M", "-bufsize", "80M", "-profile:v", "high", "-pix_fmt", "yuv420p"]
        return ["-c:v", "libx264", "-preset", "veryfast", "-crf", "20", "-pix_fmt", "yuv420p"]

    def _seg_path(self, path: str, start: float, length: float, w: int = W, h: int = H) -> str:
        st = os.stat(path)
        key = hashlib.sha1(f"{path}|{st.st_size}|{int(st.st_mtime)}|{start:.1f}|{length:.1f}|{w}x{h}@{FPS}".encode()).hexdigest()[:16]
        return os.path.join(self.work, f"seg-{key}.mp4")

    def segment(self, path: str, tags: list[str], vertical: bool = False, side_of: str | None = None) -> str:
        """The action cut out of the file. vertical: the portrait twin of a clip, sized 9:16, its window
        taken from the landscape clip's sidecar (side_of)."""
        w, h = (H, W) if vertical else (W, H)
        start, length = self._window(path, side_of)
        out = self._seg_path(path, start, length, w, h)
        if os.path.exists(out) and os.path.getsize(out) > 0:
            return out
        vf = (f"scale={w}:{h}:force_original_aspect_ratio=decrease,pad={w}:{h}:(ow-iw)/2:(oh-ih)/2,"
              f"fps={FPS},format=yuv420p")
        cmd = [self.ff, "-hide_banner", "-loglevel", "error", "-y", "-ss", f"{start:.2f}", "-i", path, "-t", f"{length:.2f}",
               "-vf", vf, *self._pick_encoder(), "-c:a", "aac", "-b:a", "160k", "-ar", "48000", "-ac", "2",
               "-movflags", "+faststart", out + ".part.mp4"]
        t0 = time.time()
        r = subprocess.run(cmd, capture_output=True, text=True, creationflags=_NOWIN | _LOWPRI)
        if r.returncode != 0:
            raise RuntimeError(r.stderr.strip()[-300:])
        os.replace(out + ".part.mp4", out)
        print(f"[highlights] segment {os.path.basename(path)} [{start:.1f}s +{length:.1f}s] in {time.time() - t0:.1f}s")
        return out

    # ---- the compilation -----------------------------------------------------------------------
    def _card(self, lines: list[str], seconds: float, name: str, vertical: bool = False) -> str:
        w, h = (H, W) if vertical else (W, H)
        name = name + ("-v" if vertical else "")
        out = os.path.join(self.work, f"card-{hashlib.sha1((name + '|'.join(lines)).encode()).hexdigest()[:12]}.mp4")
        if os.path.exists(out):
            return out
        font = _font()
        vf = []
        if font:
            fe = _ff_escape(font)
            ys = [h * 0.40, h * 0.55] if len(lines) > 1 else [h * 0.46]
            sizes = [84, 40] if vertical else [110, 48]
            for i, text in enumerate(lines[:2]):
                col = "0xC99A3B" if i == 0 else "0xECE7DB"
                vf.append(f"drawtext=fontfile='{fe}':text='{_ff_escape(text)}':fontcolor={col}:fontsize={sizes[i]}:"
                          f"x=(w-text_w)/2:y={int(ys[i])}")
        cmd = [self.ff, "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i", f"color=c=0x1c1f1d:s={w}x{h}:r={FPS}:d={seconds}",
               "-f", "lavfi", "-i", f"anullsrc=r=48000:cl=stereo:d={seconds}"]
        if vf:
            cmd += ["-vf", ",".join(vf)]
        cmd += [*self._pick_encoder(), "-c:a", "aac", "-b:a", "160k", "-shortest", out]
        r = subprocess.run(cmd, capture_output=True, text=True, creationflags=_NOWIN | _LOWPRI)
        if r.returncode != 0 and vf:
            # no drawtext in this ffmpeg: a plain card is better than no compilation
            print(f"[highlights] card text failed ({r.stderr.strip()[-120:]}); plain card")
            cmd = [c for c in cmd if c not in ("-vf", ",".join(vf))]
            r = subprocess.run(cmd, capture_output=True, text=True, creationflags=_NOWIN | _LOWPRI)
        if r.returncode != 0:
            raise RuntimeError("card: " + r.stderr.strip()[-200:])
        return out

    @staticmethod
    def _score(c: dict) -> float:
        s = 0.0
        for t in c.get("tags") or []:
            t = str(t).lower()
            s += TAG_SCORE.get(t, 0.0)
            if t.endswith("m") and t[:-1].isdigit():
                s += min(2.0, int(t[:-1]) / 150.0)   # distance
        side = os.path.splitext(c["path"])[0] + ".json"
        try:
            d = json.load(open(side, encoding="utf-8"))
            s += 0.8 * max(0, int(d.get("kills", 1)) - 1)
        except (OSError, ValueError):
            pass
        return s

    def _say(self, text: str):
        print("[highlights] " + text)
        try:
            self.b.send({"type": "highlights_status", "text": text})
            self.b.status(text)
        except Exception:
            pass

    def _build(self, o: dict):
        self._building = True
        try:
            self._build_inner(o)
        except Exception as e:
            print(f"[highlights] build failed: {e}")
            self.b.send({"type": "highlights_ready", "ok": False, "error": str(e)[:300]})
        finally:
            self._building = False

    def _build_inner(self, o: dict):
        if not self.ff:
            raise RuntimeError("ffmpeg is not installed next to ClipHound")
        clips = [c for c in (o.get("clips") or []) if c.get("path") and os.path.exists(c["path"])]
        out_dir = o.get("out") or os.path.join(os.path.dirname(clips[0]["path"]) if clips else self.work, "highlights")
        os.makedirs(out_dir, exist_ok=True)
        if not clips:
            raise RuntimeError("no clips from this session to build from")
        limit = int(o.get("max") or MAX_SEGMENTS)
        ranked = sorted(clips, key=self._score, reverse=True)[:limit]
        # the best opens, the second best closes, the rest in the order they happened
        if len(ranked) >= 3:
            best, second, rest = ranked[0], ranked[1], ranked[2:]
            rest.sort(key=lambda c: c.get("when") or c["path"])
            order = [best, *rest, second]
        else:
            order = ranked
        n = len(order)
        segs = []
        for i, c in enumerate(order):
            self._say(f"Highlights: cutting {i + 1} of {n}...")
            try:
                segs.append(self.segment(c["path"], c.get("tags") or []))
            except Exception as e:
                print(f"[highlights] skipped {c['path']}: {e}")
        if not segs:
            raise RuntimeError("no clip could be cut")
        player = (o.get("player") or "").strip() or "HIGHLIGHTS"
        when = datetime.datetime.now()
        intro = [player.upper(), f"HIGHLIGHTS  ·  {when.strftime('%d %b %Y').upper()}"]
        outro = ["kennel.gg", "The Kennel  ·  WARDOGS community"]
        parts = [self._card(intro, CARD_INTRO_S, "intro"), *segs, self._card(outro, CARD_OUTRO_S, "outro")]
        final = os.path.join(out_dir, f"Highlights {when.strftime('%Y-%m-%d %H-%M')}.mp4")
        self._join(parts, final, "")
        # the vertical canvas's own compilation, from the clips' portrait twins, when the plugin
        # runs the vertical scene: named "... [vertical]" next to the landscape one
        final_v = ""
        vclips = [c for c in order if c.get("pathV") and os.path.exists(c["pathV"])]
        if o.get("vertical") and vclips:
            segs_v = []
            for i, c in enumerate(vclips):
                self._say(f"Highlights: cutting vertical {i + 1} of {len(vclips)}...")
                try:
                    segs_v.append(self.segment(c["pathV"], c.get("tags") or [], vertical=True, side_of=c["path"]))
                except Exception as e:
                    print(f"[highlights] skipped vertical {c['pathV']}: {e}")
            if segs_v:
                parts_v = [self._card(intro, CARD_INTRO_S, "intro", True), *segs_v,
                           self._card(outro, CARD_OUTRO_S, "outro", True)]
                final_v = final[:-4] + " [vertical].mp4"
                try:
                    self._join(parts_v, final_v, "-v")
                except Exception as e:
                    print(f"[highlights] vertical compilation failed: {e}")
                    final_v = ""
        self._say(f"Highlights ready: {os.path.basename(final)} ({len(segs)} clips)")
        self.b.send({"type": "highlights_ready", "ok": True, "path": final, "pathV": final_v, "clips": len(segs)})

    def _join(self, parts: list[str], final: str, tag: str):
        """Concatenate the parts into `final`, with a music track under it when there is one."""
        out_dir = os.path.dirname(final)
        self._say("Highlights: joining..." if not tag else "Highlights: joining the vertical one...")
        lst = os.path.join(self.work, f"concat{tag}.txt")
        with open(lst, "w", encoding="utf-8") as f:
            for p in parts:
                f.write("file '" + p.replace("\\", "/").replace("'", "'\\''") + "'\n")
        joined = os.path.join(self.work, f"joined{tag}.mp4")
        r = subprocess.run([self.ff, "-hide_banner", "-loglevel", "error", "-y", "-f", "concat", "-safe", "0", "-i", lst,
                            "-c", "copy", "-movflags", "+faststart", joined], capture_output=True, text=True,
                           creationflags=_NOWIN | _LOWPRI)
        if r.returncode != 0:
            raise RuntimeError("join: " + r.stderr.strip()[-200:])
        music = []
        for folder in (os.path.join(out_dir, "music"), os.path.join(_base_dir(), "music")):
            for ext in ("*.mp3", "*.m4a", "*.wav", "*.flac"):
                music += glob.glob(os.path.join(folder, ext))
            if music:
                break  # the folder next to the clips wins over the one shipped next to ClipHound
        music = sorted(music)
        if music:
            track = music[int(time.time()) % len(music)]
            self._say(f"Highlights: music - {os.path.basename(track)}")
            r = subprocess.run([self.ff, "-hide_banner", "-loglevel", "error", "-y", "-i", joined, "-stream_loop", "-1", "-i", track,
                                "-filter_complex",
                                f"[1:a]volume={MUSIC_VOLUME},afade=t=in:d=1.5[m];[0:a][m]amix=inputs=2:duration=first:dropout_transition=2,"
                                f"afade=t=out:st={max(0.0, self._probe_duration(joined) - 2.0):.2f}:d=2[a]",
                                "-map", "0:v", "-map", "[a]", "-c:v", "copy", "-c:a", "aac", "-b:a", "192k", "-shortest",
                                "-movflags", "+faststart", final], capture_output=True, text=True, creationflags=_NOWIN | _LOWPRI)
            if r.returncode != 0:
                print(f"[highlights] music pass failed ({r.stderr.strip()[-160:]}); without music")
                shutil.copyfile(joined, final)
        else:
            shutil.copyfile(joined, final)
