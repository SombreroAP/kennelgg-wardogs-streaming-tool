"""Talk to the Kennel.gg WARDOGS OBS plugin over its local bridge (ws://127.0.0.1:47820).

Replaces both obs-websocket uses when `capture.backend: bridge` / `obs.mode: bridge`:
- frames: the plugin pushes JPEG crops of the game source at native resolution (binary frames,
  16-byte header "KWF1" + uint16 w + uint16 h + uint64 ts_ms, little endian);
- clips: we send {"type":"clip", ...}; the plugin saves OBS's replay buffer, renames the file with
  the tags and answers with clip_saved {path}. The plugin also sends pov events (downed/reviving/up).
Protocol v1 is documented in the plugin's README. Needs `pip install websocket-client`.
"""
import json
import os
import struct
import threading
import time

import cv2
import numpy as np

try:
    import websocket  # websocket-client
except ImportError as e:  # pragma: no cover
    raise SystemExit("pip install websocket-client") from e


def _app_version() -> str:
    """version.txt next to the exe, written at build time; '' from a source checkout."""
    try:
        import sys
        with open(os.path.join(os.path.dirname(os.path.abspath(sys.argv[0])), "version.txt"), encoding="utf-8") as fh:
            return fh.read().strip()
    except Exception:
        return ""


def _roi_list(r) -> list:
    """ROI as [x, y, w, h] floats, from either a list or a {x,y,w,h} mapping. [] if unusable."""
    try:
        if isinstance(r, dict):
            return [float(r["x"]), float(r["y"]), float(r["w"]), float(r["h"])]
        if isinstance(r, (list, tuple)) and len(r) == 4:
            return [float(v) for v in r]
    except (KeyError, TypeError, ValueError):
        pass
    return []


class Bridge:
    """One connection shared by the frame source and the clip trigger. Reconnects on its own."""

    def __init__(self, cfg: dict):
        self.url = f"ws://127.0.0.1:{int(cfg.get('port', 47820))}"
        self.fps = float(cfg.get("fps", 3))
        self.ws = None
        self.connected = False
        self._last_err = None   # so the same connection error is not printed every 3 s
        self._frame = None            # latest full frame (BGR), once a second
        self._frame_ts = 0.0
        self._feed = None             # latest kill-feed crop (BGR), at the reading rate
        self._feed_ts = 0.0
        self._inv = {}                # stream id -> (crop, ts): the inventory screen's two tell-tales
        self.on_hud = None            # callable(crop, ts): the HUD's item plate, for weapon names (weapons.py)
        self.feed_roi = _roi_list(cfg.get("roi")) or [0.0, 0.5, 0.25, 0.25]   # fractions of the frame
        self._lock = threading.Lock()
        self._pending = {}            # clip id -> callback(path)
        self._seq = 0
        self.pov_state = "up"
        self.on_pov = None            # callback(state, friend)
        self.game_source = ""
        self.cfg = None              # full config dict (set by main) for app_config / twitch messages
        self.save_cfg = None         # callable(cfg) that writes config.yaml
        self.on_config = None        # callable(cfg) after the plugin changed settings
        self.on_clip_saved = None    # callable(path, msg): every clip the plugin named
        self.on_highlights = None    # callable(msg): build the compilation
        self.on_obs_health = None    # callable(msg): OBS's dropped-frame counters
        self.on_audio = None          # 16 kHz mono int16 microphone PCM (voice.py)
        self.on_voice_config = None
        self.pending_voice_cfg = None
        self.on_voice_name = None
        # the game's NEARBY panel: the plugin says whether to read it, where it is and whose names
        # to look for; nearby_burst is a deadline until which we read it every quarter second
        self.nearby_cfg = {"enabled": False, "roi": [0.80, 0.79, 0.19, 0.14], "names": [], "interval": 0.4}
        self.nearby_burst = 0.0
        self.nearby_test = False     # the plugin's Test read button: read once and report back
        self.vehicle_cfg = {"enabled": False, "roi": [0.86, 0.60, 0.14, 0.25]}   # automatic Dual POV
        self.inventory_cfg = {"enabled": False}   # squad mate POV while the inventory is open
        threading.Thread(target=self._run, daemon=True).start()

    def _on_error(self, _ws, err):
        """Say why we cannot reach the plugin. Silence here is what made this look like a hang: the
        app sat retrying for ever while the dock said \"starting\" and nothing explained either."""
        msg = str(err) or err.__class__.__name__
        if msg != self._last_err:
            self._last_err = msg
            print(f"[bridge] cannot reach the plugin at {self.url}: {msg}")
            print("[bridge] is OBS running with the plugin, and is the ClipHound bridge on "
                  "(Settings, Advanced, ClipHound connection)? Retrying every 3 s.")

    # ---- connection ----
    def _run(self):
        self._lost_at = None
        while True:
            if self._lost_at and time.time() - self._lost_at > 20 and os.environ.get("KENNEL_FROM_OBS"):
                print("[bridge] plugin gone for 20 s and we were started by OBS - exiting")
                os._exit(0)
            try:
                self.ws = websocket.WebSocketApp(self.url, on_open=self._on_open, on_message=self._on_message,
                                                 on_close=self._on_close, on_error=self._on_error)
                self.ws.run_forever(ping_interval=20, ping_timeout=10)
            except Exception as e:
                print(f"[bridge] {e}")
            self.connected = False
            time.sleep(3)

    def _send_app_state(self):
        if self.cfg is None:
            return
        c = self.cfg
        self.send({"type": "app_config", "values": {
            "app_version": _app_version(),
            "player_name": c["detection"].get("player_name", ""),
            "library": (c.get("obs") or {}).get("library", ""),
            "broadcaster": (c.get("twitch") or {}).get("broadcaster_login", ""),
            "twitch_enabled": bool((c.get("twitch") or {}).get("enabled")),
            "fps": (c.get("capture") or {}).get("fps", 3),
            "clip_every_kill": bool(c["detection"].get("clip_every_kill")),
            "multikill_window": float(c["detection"].get("multikill_window_s", 30)),
            "roi": _roi_list((c.get("capture") or {}).get("roi")),
            "nearby": {"enabled": bool(self.nearby_cfg.get("enabled")),
                       "roi": list(self.nearby_cfg.get("roi") or []),
                       "names": list(self.nearby_cfg.get("names") or [])},
        }})
        from twitch_device import status
        self.send(status(c))

    def _on_open(self, ws):
        self.connected = True
        print(f"[bridge] connected to the plugin at {self.url}")
        self._send_app_state()
        self._subscribe()
        self.status("ClipHound watching the kill feed")

    def _on_close(self, ws, *_):
        if self.connected:
            print("[bridge] plugin went away; reconnecting")
            self._lost_at = time.time()
        self.connected = False

    def _on_message(self, ws, msg):
        if isinstance(msg, (bytes, bytearray)):
            if len(msg) >= 4 and msg[:4] == b"KWA1":
                if self.on_audio:
                    self.on_audio(bytes(msg[4:]))
                return
            if len(msg) >= 17 and msg[:4] == b"KWF2":
                # a stream: 0 = the kill-feed crop, 1 = the whole frame
                sid, w, h, ts = struct.unpack_from("<BHHQ", msg, 4)
                frame = cv2.imdecode(np.frombuffer(msg[17:], np.uint8), cv2.IMREAD_COLOR)
                if frame is not None:
                    with self._lock:
                        if sid == 0:
                            self._feed, self._feed_ts = frame, ts / 1000.0
                        elif sid in (2, 3):
                            self._inv[sid] = (frame, ts / 1000.0)   # the inventory reader's two crops
                        elif sid == 4:
                            pass                                     # handed on below, outside the lock
                        else:
                            self._frame, self._frame_ts = frame, ts / 1000.0
                    if sid == 4 and self.on_hud:
                        self.on_hud(frame, ts / 1000.0)
                return
            if len(msg) < 16 or msg[:4] != b"KWF1":
                return
            w, h, ts = struct.unpack_from("<HHQ", msg, 4)
            frame = cv2.imdecode(np.frombuffer(msg[16:], np.uint8), cv2.IMREAD_COLOR)
            if frame is not None:
                with self._lock:
                    self._frame, self._frame_ts = frame, ts / 1000.0
            return
        try:
            o = json.loads(msg)
        except ValueError:
            return
        t = o.get("type")
        if t == "hello":
            print(f"[bridge] plugin {o.get('plugin')} {o.get('version')} (protocol {o.get('protocol')})")
        elif t == "app_config" and self.cfg is not None and "set" in o:
            v = o["set"]
            c = self.cfg
            if "player_name" in v:
                c["detection"]["player_name"] = v["player_name"]
            if "game_lang" in v:
                c["detection"]["game_lang"] = v["game_lang"] or ""   # "" = not known yet (auto, no match so far)
            if "library" in v:
                c.setdefault("obs", {})["library"] = v["library"]
            if "broadcaster" in v:
                c.setdefault("twitch", {})["broadcaster_login"] = v["broadcaster"]
                c["twitch"]["broadcaster_id"] = ""
            if "twitch_enabled" in v:
                c.setdefault("twitch", {})["enabled"] = bool(v["twitch_enabled"])
            if "fps" in v and float(v["fps"]) > 0:
                fps = max(1.0, min(20.0, float(v["fps"])))
                if fps != float(c.setdefault("capture", {}).get("fps", 5)):
                    c["capture"]["fps"] = fps
                    self.fps = fps
                    self._subscribe()      # ask the plugin for frames at the new rate
            if "roi" in v and _roi_list(v["roi"]) and _roi_list(v["roi"])[2] > 0.01:
                r = _roi_list(v["roi"])
                c.setdefault("capture", {})["roi"] = {"x": r[0], "y": r[1], "w": r[2], "h": r[3]}
            if isinstance(v.get("vehicle"), dict):
                vc = v["vehicle"]
                self.vehicle_cfg["enabled"] = bool(vc.get("enabled"))
                roi = _roi_list(vc.get("roi"))
                if roi and roi[2] > 0.01:
                    self.vehicle_cfg["roi"] = roi
            if isinstance(v.get("inventory"), dict):
                on = bool(v["inventory"].get("enabled"))
                if on != self.inventory_cfg.get("enabled"):
                    self.inventory_cfg["enabled"] = on
                    self._subscribe()          # the crop streams come and go with the switch
            if isinstance(v.get("nearby"), dict):
                nb = v["nearby"]
                self.nearby_cfg["enabled"] = bool(nb.get("enabled"))
                self.nearby_cfg["names"] = [str(n) for n in (nb.get("names") or []) if str(n).strip()]
                roi = _roi_list(nb.get("roi"))
                if roi and roi[2] > 0.01:
                    self.nearby_cfg["roi"] = roi
                c.setdefault("nearby", {}).update({"enabled": self.nearby_cfg["enabled"],
                                                   "roi": self.nearby_cfg["roi"]})
            if "clip_every_kill" in v:
                c["detection"]["clip_every_kill"] = bool(v["clip_every_kill"])
            if "multikill_window" in v:
                c["detection"]["multikill_window_s"] = float(v["multikill_window"])
            if "series_window" in v:
                c["detection"]["series_window_s"] = float(v["series_window"])
            if "replay_chat" in v:
                c.setdefault("replay", {})["chat"] = bool(v["replay_chat"])
            for k in ("clip_trim", "clip_trim_lead_s", "run_merge", "run_cut_gaps", "run_gap_s"):
                if k in v:
                    key = {"clip_trim": "trim", "clip_trim_lead_s": "trim_lead_s", "run_merge": "merge",
                           "run_cut_gaps": "cut_gaps", "run_gap_s": "gap_s"}[k]
                    c.setdefault("runs", {})[key] = v[k]
            if "series_window" in v:
                c.setdefault("runs", {})["window_s"] = float(v["series_window"])
            if "replay_word" in v:
                c.setdefault("replay", {})["word"] = str(v["replay_word"]).strip() or "!replay"
            if self.save_cfg:
                self.save_cfg(c)
            print(f"[bridge] settings from the plugin: {v}")
            if self.on_config:
                self.on_config(c)
            self._send_app_state()
        elif t == "twitch_login" and self.cfg is not None:
            from twitch_device import start_login
            start_login(self.cfg, lambda st: self.send({"type": "twitch_status", **st}), lambda c: (self.save_cfg(c) if self.save_cfg else None, self.on_config(c) if self.on_config else None, self._send_app_state()))
        elif t == "twitch_logout" and self.cfg is not None:
            from twitch_device import logout
            logout(self.cfg, lambda c: (self.save_cfg(c) if self.save_cfg else None, self.on_config(c) if self.on_config else None))
            self._send_app_state()
        elif t == "nearby_now":
            self.nearby_burst = time.time() + 3.0
        elif t == "nearby_test":
            self.nearby_test = True
        elif t == "app_state":
            self._send_app_state()
        elif t == "shutdown":
            print("[bridge] OBS is closing - ClipHound exiting")
            os._exit(0)
        elif t == "config":
            self.game_source = o.get("gameSource", "")
            self.pov_state = o.get("povState", "up")
        elif t == "pov":
            self.pov_state = o.get("state", "up")
            print(f"[bridge] pov: {self.pov_state} ({o.get('friend', '')})")
            if self.on_pov:
                self.on_pov(self.pov_state, o.get("friend", ""))
        elif t == "clip_result":
            if not o.get("ok"):
                print(f"[bridge] clip refused: {o.get('error')}")
                self._pending.pop(o.get("id"), None)
        elif t == "replay_result":
            if not o.get("ok"):
                print(f"[chat] replay from {o.get('who')} not played: {o.get('error')}")
        elif t == "highlights_build":
            if self.on_highlights:
                self.on_highlights(o)
        elif t == "obs_health":
            if self.on_obs_health:
                self.on_obs_health(o)
        elif t == "voice_config":
            if self.on_voice_config:
                self.on_voice_config(o)
            else:
                self.pending_voice_cfg = o   # the voice module is not up yet: kept for it
        elif t == "voice_name":
            if self.on_voice_name:
                self.on_voice_name(o.get("path", ""), float(o.get("epoch") or time.time()))
        elif t == "clip_saved":
            print(f"[bridge] clip saved: {o.get('path')}")
            if self.on_clip_saved:
                try:
                    self.on_clip_saved(o.get("path", ""), o)
                except Exception as e:
                    print(f"[bridge] on_clip_saved: {e}")
            # the plugin does not echo our id on clip_saved; pair with the oldest pending request
            if self._pending:
                cid = next(iter(self._pending))
                cb = self._pending.pop(cid)
                if cb:
                    cb(o.get("path", ""), o)

    def _subscribe(self):
        """Two streams. The kill-feed crop at the reading rate (a few hundred kilobytes a second),
        and the whole frame once a second for the minimap team check, the NEARBY panel and the
        vehicle list. The whole frame at the reading rate was fourteen megabytes ten times a
        second, rendered, read back, JPEG-encoded and decoded: most of the CPU the app used."""
        r = self.feed_roi
        import weapons
        streams = [{"id": 0, "fps": self.fps, "roi": [r[0], r[1], r[2], r[3]], "width": 0},
                   {"id": 1, "fps": 1.0, "roi": [0, 0, 1, 1], "width": 0},
                   # the HUD's item plate, bottom right: what you are holding, so your kills get
                   # the weapon's real name (a strip a few hundred pixels wide, twice a second)
                   {"id": 4, "fps": weapons.HUD_FPS, "roi": list(weapons.HUD_ROI), "width": 0}]
        if self.inventory_cfg.get("enabled"):
            # two tiny crops, three times a second: the "COMBINE AMMO" hint and the INVENTORY tab, so
            # the screen closing is seen within a second
            import inventory
            streams += [{"id": 2, "fps": 3.0, "roi": list(inventory.COMBINE_ROI), "width": 0},
                        {"id": 3, "fps": 3.0, "roi": list(inventory.TAB_ROI), "width": 0}]
        self.send({"type": "subscribe", "frames": True, "fps": self.fps, "roi": [0, 0, 1, 1], "width": 0,
                   "streams": streams})

    def set_feed_roi(self, r):
        rl = _roi_list(r)
        if rl and rl[2] > 0.01 and rl != self.feed_roi:
            self.feed_roi = rl
            self._subscribe()

    def send(self, o: dict):
        try:
            if self.ws and self.connected:
                self.ws.send(json.dumps(o))
                return True
        except Exception as e:
            print(f"[bridge] send failed: {e}")
        return False

    def status(self, text: str):
        self.send({"type": "status", "text": text})

    def event(self, text: str, kind: str = "kill"):
        """Something happened in the kill feed (shown in the plugin's dock)."""
        self.send({"type": "event", "kind": kind, "text": text})

    # ---- frames ----
    def latest(self):
        with self._lock:
            return self._frame, self._frame_ts

    def latest_feed(self):
        with self._lock:
            return self._feed, self._feed_ts

    def latest_inv(self):
        """{2: (crop, ts), 3: (crop, ts)} - whatever has arrived."""
        with self._lock:
            return dict(self._inv)

    # ---- clips ----
    def clip(self, title: str, tags: list, source: str = "cliphound", on_saved=None, info: dict | None = None) -> bool:
        self._seq += 1
        cid = f"c{self._seq}"
        self._pending[cid] = on_saved
        info = dict(info or {})
        moments = [float(t) for t in info.pop("moments", []) or []]
        ok = self.send({"type": "clip", "id": cid, "title": title, "tags": list(tags), "source": source,
                        "moments": moments, "info": info})
        if not ok:
            self._pending.pop(cid, None)
            print(f"[bridge] not connected, clip '{title}' lost")
        return ok


class BridgeRoiCapture:
    """Frame source with the same interface as ObsRoiCapture / RoiCapture, fed by the plugin."""

    def __init__(self, cap_cfg: dict, bridge: Bridge):
        self.b = bridge
        self.r = cap_cfg["roi"]
        self.upscale = float(cap_cfg.get("upscale", 2.0))
        print("[capture] waiting for the first frame from the plugin (is OBS running with the plugin?)")
        self.b.set_feed_roi([self.r["x"], self.r["y"], self.r["w"], self.r["h"]])
        frame = self._wait_frame()
        h, w = frame.shape[:2]
        self.box = (int(w * self.r["x"]), int(h * self.r["y"]), int(w * self.r["w"]), int(h * self.r["h"]))
        print(f"[capture] plugin source '{self.b.game_source}' is {w}x{h}; ROI x,y,w,h = {self.box}")
        import ocr
        ocr.set_frame_height(h)  # the icon templates were cut at 1080p; a 1440p feed is scaled to match

    def set_roi(self, r):
        """Kill-feed area changed in the plugin: re-cut the crop from the next frame on."""
        rl = _roi_list(r)
        if not rl or rl[2] <= 0.01:
            return
        self.r = {"x": rl[0], "y": rl[1], "w": rl[2], "h": rl[3]}
        f = getattr(self, "_last", None)
        if f is None:
            f, _ = self.b.latest()
        if f is None:
            return
        h, w = f.shape[:2]
        self.box = (int(w * rl[0]), int(h * rl[1]), int(w * rl[2]), int(h * rl[3]))
        print(f"[capture] kill-feed ROI is now x,y,w,h = {self.box}")
        self.b.set_feed_roi(rl)   # the plugin crops it for us from now on

    def _wait_frame(self):
        while True:
            f, _ = self.b.latest()
            if f is not None:
                return f
            time.sleep(0.5)

    def grab(self) -> np.ndarray:
        # the plugin sends the kill-feed crop itself; the whole frame is the fallback
        c, cts = self.b.latest_feed()
        if c is not None and time.time() - cts <= 5:
            f, ts = self.b.latest()
            if f is not None:
                self._last = f
            return c
        f, ts = self.b.latest()
        if f is None or time.time() - ts > 5:
            f = self._wait_frame()
        self._last = f
        x, y, w, h = self.box
        return f[y:y + h, x:x + w]

    def full_frame(self) -> np.ndarray:
        f, _ = self.b.latest()
        if f is not None:
            self._last = f
        return self._last if getattr(self, "_last", None) is not None else self._wait_frame()

    def full_frame_ts(self) -> float:
        return self.b.latest()[1]

    def preprocess(self, bgr: np.ndarray) -> np.ndarray:
        if self.upscale != 1.0:
            bgr = cv2.resize(bgr, None, fx=self.upscale, fy=self.upscale, interpolation=cv2.INTER_CUBIC)
        gray = cv2.cvtColor(bgr, cv2.COLOR_BGR2GRAY)
        _, th = cv2.threshold(gray, 170, 255, cv2.THRESH_BINARY)
        th = cv2.bitwise_not(th)
        return cv2.medianBlur(th, 3)


class BridgeOBS:
    """Clip trigger with the same .trigger() as obs.OBS, but the plugin saves and names the file."""

    def __init__(self, obs_cfg: dict, bridge: Bridge):
        self.cfg = obs_cfg
        self.b = bridge

    def trigger(self, title: str, tags=None, info=None):
        info = info or {}
        tags = list(tags or [])
        if info.get("kind") and info["kind"] not in tags:
            tags.insert(0, info["kind"])
        if info.get("distance_m"):
            tags.append(f"{int(info['distance_m'])}m")
        if self.b.pov_state != "up":
            tags.append("downed")
        lib = self.cfg.get("library")

        def saved(path, _o):
            if lib and path:
                from obs import _update_library_index
                rec = dict(info)
                rec.update({"created": time.strftime("%Y-%m-%dT%H:%M:%S"), "file": path, "title": title, "tags": tags})
                if _o.get("end_epoch"):
                    rec["end_epoch"] = float(_o["end_epoch"])   # when the plugin asked for the save: the file's end
                try:
                    _update_library_index(lib, rec)
                except Exception as e:
                    print(f"[bridge] library index: {e}")

        self.b.clip(title, tags, "cliphound", on_saved=saved, info=info)
