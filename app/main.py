"""ClipHound: OCR the WARDOGS kill feed -> Twitch clip + tagged OBS replay."""
import os
import sys
import time

# Frozen (installer) build: work from the exe's folder so config.yaml, debug/ and the library
# paths resolve, and use the bundled Tesseract.
class _Tee:
    """stdout/stderr to the console and to cliphound.log with timestamps (the plugin's Logs button reads it)."""
    def __init__(self, stream, path):
        self.stream, self.f, self.at_line_start = stream, open(path, "a", encoding="utf-8", buffering=1), True
    def write(self, s):
        try:
            if self.stream is not None:
                self.stream.write(s)
        except Exception:
            pass
        try:
            import time as _t
            for part in s.splitlines(True):
                if self.at_line_start and part.strip():
                    self.f.write(_t.strftime("%H:%M:%S ") + part)
                else:
                    self.f.write(part)
                self.at_line_start = part.endswith("\n")
        except Exception:
            pass
    def flush(self):
        try:
            self.stream.flush(); self.f.flush()
        except Exception:
            pass
    def __getattr__(self, n):
        if self.stream is None:
            raise AttributeError(n)
        return getattr(self.stream, n)


if getattr(sys, "frozen", False):
    os.chdir(os.path.dirname(sys.executable))
    # one ClipHound at a time (the plugin may try to start it again)
    try:
        import ctypes
        _mutex = ctypes.windll.kernel32.CreateMutexW(None, False, "Local\\KennelClipHound")
        if ctypes.windll.kernel32.GetLastError() == 183:
            try:
                with open("cliphound.log", "a", encoding="utf-8") as fh:
                    fh.write(time.strftime("%H:%M:%S") + " [app] another ClipHound is already running on this PC - "
                             "this one exits. If the plugin says it is connected but nothing works, close the "
                             "other copy (Task Manager, ClipHound.exe) and press Start ClipHound on the dock.\n")
            except Exception:
                pass
            raise SystemExit(0)
    except SystemExit:
        raise
    except Exception:
        pass
    try:
        _lp = "cliphound.log"
        if os.path.exists(_lp) and os.path.getsize(_lp) > 2_000_000:
            os.replace(_lp, "cliphound.log.old")
        sys.stdout = _Tee(sys.stdout, _lp)
        sys.stderr = _Tee(sys.stderr, _lp)
        print(f"=== ClipHound started {__import__('time').strftime('%Y-%m-%d %H:%M:%S')} ===")
    except Exception as _e:
        print(f"[log] could not open cliphound.log: {_e}")
    _tess = os.path.join(os.path.dirname(sys.executable), "tesseract", "tesseract.exe")
    if os.path.exists(_tess):
        os.environ["TESSDATA_PREFIX"] = os.path.join(os.path.dirname(_tess), "tessdata")
        try:
            import pytesseract
            pytesseract.pytesseract.tesseract_cmd = _tess
        except ImportError:
            pass
    if not os.path.exists("config.yaml") and os.path.exists("config.default.yaml"):
        import shutil
        shutil.copy("config.default.yaml", "config.yaml")   # settings come from the OBS plugin's ClipHound tab
    if True:   # developer flags only; the installer and the plugin never pass these
        if "--token" in sys.argv:
            import get_token  # noqa: F401  (runs the Twitch login flow on import)
            raise SystemExit(0)
        if "--setup" in sys.argv:
            import setup as _setup
            sys.argv.remove("--setup")
            _setup.main()
            raise SystemExit(0)
import threading
import time

import cv2
import yaml

from capture import RoiCapture
from detector import KillDetector

DRY = "--dry-run" in sys.argv


def main():
    cfg = load_config()
    bridge = None
    if cfg["capture"].get("backend", "obs") == "bridge" or cfg["obs"].get("mode") == "bridge":
        from bridge import Bridge
        bridge = Bridge({**(cfg.get("bridge") or {}), "fps": cfg["capture"]["fps"], "roi": cfg["capture"].get("roi")})
    if bridge is not None:
        bridge.cfg = cfg
        bridge.save_cfg = save_config
    if cfg["capture"].get("backend", "obs") == "bridge":
        from bridge import BridgeRoiCapture
        cap = BridgeRoiCapture(cfg["capture"], bridge)
    elif cfg["capture"].get("backend", "obs") == "obs":
        from capture_obs import ObsRoiCapture
        cap = ObsRoiCapture(cfg["capture"], cfg["obs"])
    else:
        cap = RoiCapture(cfg["capture"])
    import ocr
    ocr.COLOR_BANDS = {k: tuple(v) for k, v in (cfg["detection"].get("colors") or {}).items()} or None
    det = KillDetector(cfg["detection"], dump_rows="debug/rows" if cfg["capture"].get("debug_dump") else None,
                       harvest_dir=os.path.join("icons", "harvest"))
    det.set_rate(cfg["capture"]["fps"])
    # the game's own weapon names: read off your HUD on your kills, learned from the kill-feed
    # icon they leave, and used to name everyone else's kills with that icon
    import weapons
    reader = weapons.WeaponReader(base_dir="icons")
    det.weapons = reader
    ocr.WEAPON_READER = reader
    weapons.set_language(cfg["detection"].get("game_lang", ""))
    if bridge is not None:
        bridge.on_hud = reader.on_hud
        reader.on_change = lambda name: bridge.send({"type": "holding", "name": name})
    print(f"[capture] ROI {cap.box} @ {cfg['capture']['fps']} fps, deciding a row on {det.votes} reads "
          f"({det.min_reads} if it goes away early)   dry-run={DRY}")

    tw = ob = None
    if not DRY and cfg["twitch"]["enabled"] and cfg["twitch"].get("access_token"):
        try:
            from twitch import Twitch
            tw = Twitch(cfg["twitch"], lambda _sec: save_config(cfg))
        except Exception as e:
            print(f"[twitch] not ready ({e}); log in from the OBS plugin (Settings, Clips & replays, Twitch clips)")
            if bridge is not None:
                bridge.send({"type": "twitch_status", "state": "error",
                             "error": f"{e}  -  no Twitch clips will be made until you log in again "
                                      f"(Settings, Clips & replays, Twitch clips: Log in with Twitch)."})
    if not DRY and cfg["obs"]["enabled"] and cfg["obs"].get("mode") == "bridge":
        from bridge import BridgeOBS
        ob = BridgeOBS(cfg["obs"], bridge)
    elif not DRY and cfg["obs"]["enabled"]:
        from obs import OBS
        ob = OBS(cfg["obs"])

    state = {"tw": tw, "ob": ob}

    def apply_live(c):
        # settings changed from the OBS plugin: name, library, Twitch, clip rules, the areas we read
        was = det.me
        det.me = c["detection"].get("player_name", det.me)
        if not (det.me or "").strip():
            print("[config] no player name set: kill-feed rows cannot be matched to you. "
                  "Enter your in-game name in the plugin (Settings > ClipHound).")
        elif det.me != was:
            print(f"[config] player name: {det.me!r}")
        det.cfg["clip_every_kill"] = bool(c["detection"].get("clip_every_kill"))
        try:
            import weapons
            weapons.set_language(c["detection"].get("game_lang", ""))
        except Exception as e:
            print(f"[weapons] language: {e}")
        det.cfg["multikill_window_s"] = float(c["detection"].get("multikill_window_s", 30))
        det.set_rate(c["capture"].get("fps", 5))
        if hasattr(cap, "set_roi"):
            cap.set_roi(c["capture"].get("roi"))
        try:
            if not DRY and c["twitch"].get("enabled") and c["twitch"].get("access_token"):
                from twitch import Twitch
                state["tw"] = Twitch(c["twitch"], lambda _sec: save_config(c))
                print(f"[twitch] ready as {c['twitch'].get('clipper_login', '?')} for channel {c['twitch'].get('broadcaster_login', '?')}")
            else:
                state["tw"] = None
            try:
                start_chat(c)
            except Exception as ce:
                print(f"[chat] could not start: {ce}")
        except Exception as e:
            print(f"[twitch] not ready: {e}")
            if bridge is not None:
                bridge.send({"type": "twitch_status", "state": "error",
                             "error": f"{e}  -  no Twitch clips will be made until you log in again "
                                      f"(Settings, Clips & replays, Twitch clips: Log in with Twitch)."})
            state["tw"] = None
    chat = {"c": None}

    def start_chat(c):
        if bridge is None:
            return
        if chat["c"] is not None:
            chat["c"].stop()
            chat["c"] = None
        if c["twitch"].get("enabled") and c["twitch"].get("access_token"):
            from twitch_chat import TwitchChat
            chat["c"] = TwitchChat(c["twitch"], bridge, lambda: bool((c.get("replay") or {}).get("chat", True)),
                                   lambda: str((c.get("replay") or {}).get("word") or "!replay"))
            chat["c"].start()

    try:
        start_chat(cfg)
    except Exception as ce:
        print(f"[chat] could not start: {ce}")

    if bridge is not None:
        bridge.on_config = apply_live
        try:
            print("[highlights] starting")
            from highlights import Highlights
            hl = Highlights(bridge, cfg)
            bridge.on_highlights = hl.on_build
            bridge.on_obs_health = hl.on_obs_health
            from runs import Runs
            runs = Runs(bridge, cfg, hl.ff, hl.encoder_args)

            def _on_clip(path, o):
                hl.on_clip_saved(path, o)
                runs.on_clip_saved(path, o)
            bridge.on_clip_saved = _on_clip
        except Exception as e:
            print(f"[highlights] not available: {e}")
        try:
            from voice import Voice
            vo = Voice(bridge, cfg)
            bridge.on_audio = vo.feed
            bridge.on_voice_config = vo.configure
            bridge.on_voice_name = vo.name_clip
            if bridge.pending_voice_cfg:         # the plugin's settings came before this module was up
                vo.configure(bridge.pending_voice_cfg)
                bridge.pending_voice_cfg = None
            bridge.send({"type": "voice_ready"})  # ...and ask for them again either way
        except Exception as e:
            print(f"[voice] not available: {e}")

    run = {"last": 0.0, "n": 0}

    def fire(trig):
        tw, ob = state["tw"], state["ob"]
        print(f"\n*** {trig.kind.upper()}: {trig.title}   tags={trig.tags} ***\n")
        if bridge is not None:
            bridge.event(f"{trig.title}  [{', '.join(trig.tags)}]", "trigger")
        # clips this close together are one run of rolling highlights. A Twitch clip's title cannot
        # be changed once it exists, so the first keeps its plain title and the rest say which part
        # they are; the plugin renames the files on disk to "[1 of 3]", "[2 of 3]", "[3 of 3]".
        now = time.time()
        window = float(cfg["detection"].get("series_window_s", 45))
        run["n"] = run["n"] + 1 if now - run["last"] <= window else 1
        run["last"] = now
        twitch_title = trig.headline() + (f" - part {run['n']}" if run["n"] > 1 else "")
        if tw:
            threading.Timer(cfg["twitch"]["clip_delay_s"], lambda: _safe(tw.create_clip, twitch_title, trig.tags)).start()
        if ob:
            ev = trig.events[-1] if trig.events else None
            info = {"kind": trig.kind, "description": trig.describe(),
                    "decision_lag_s": 10 / cfg["capture"]["fps"] + 0.5,   # VOTES reads + fade-in
                    "distance_m": ev.distance_m if ev else 0,
                    "killer": ev.killer if ev else "", "victim": ev.victim if ev else "",
                    "icons": ev.icons if ev else [], "kills": len(trig.events),
                    "weapon": (trig.weapon_names() or [""])[0],
                    "weapons": [e.weapon for e in trig.events],
                    # when each kill-feed row first appeared (epoch seconds): the plugin turns these
                    # into "seconds before the end of the file", so an edit can land on the kill
                    "moments": [e.ts for e in trig.events if e.ts],
                    # one entry per kill: ts = the moment its kill-feed row appeared (epoch seconds,
                    # this PC's clock, the same one the plugin stamps the file's end with)
                    "events": [{"ts": e.ts, "time": time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime(e.ts)) +
                                f".{int((e.ts % 1) * 1000):03d}",
                                "killer": e.killer, "victim": e.victim, "distance_m": e.distance_m,
                                "icons": list(e.icons), "headshot": "skull" in e.icons,
                                "weapon": e.weapon, "weapon_from": e.weapon_from, "weapon_type": e.weapon_type,
                                "vehicle": e.vehicle, "killer_rel": e.killer_rel, "victim_rel": e.victim_rel}
                               for e in trig.events]}
            threading.Timer(cfg["obs"]["replay_delay_s"], lambda: _safe(ob.trigger, trig.headline(), trig.tags, info)).start()

    if cfg["capture"].get("debug_dump"):
        os.makedirs("debug", exist_ok=True)
    from colors import detect_my_team
    watcher = None
    if bridge is not None:
        from nearby import Watcher
        watcher = Watcher(bridge)
        from vehicle import Watcher as VehicleWatcher
        vehicle = VehicleWatcher(bridge)
        from inventory import Watcher as InventoryWatcher
        inventory = InventoryWatcher(bridge)
    last_team_check = 0.0
    last_full_ts = -1.0
    while True:
        t0 = time.time()
        period = 1.0 / max(1.0, float(cfg["capture"]["fps"]))   # the plugin can change the rate live
        # team colour: re-check the minimap every 30 s while in auto (it changes each match)
        if cfg["detection"].get("my_team", "auto") == "auto" and t0 - last_team_check > 30 and hasattr(cap, "full_frame"):
            last_team_check = t0
            dc = cfg["detection"]
            team = detect_my_team(cap.full_frame(), dc.get("team_icon_roi"), dc.get("minimap_roi"), ocr.COLOR_BANDS)
            if team and team != det.my_team:
                print(f"[team] you are on the {team} team")
                det.my_team = team
        # who is near you (bottom-right NEARBY panel): read only while the plugin asks for it,
        # which is from the moment you go down until you are back up
        if watcher is not None and hasattr(cap, "full_frame"):
            test = bridge is not None and bridge.nearby_test
            if bridge is not None:
                bridge.nearby_test = False
            # the whole frame arrives once a second: read it once, not at every kill-feed tick
            fts = cap.full_frame_ts() if hasattr(cap, "full_frame_ts") else t0
            if test or fts != last_full_ts:
                last_full_ts = fts
                watcher.maybe_read(cap.full_frame(), t0, force=test)
                vehicle.maybe_read(cap.full_frame(), t0)
        if bridge is not None:
            inventory.tick(t0)   # its own crops, three a second; nothing when the switch is off
        roi = cap.grab()
        if cfg["capture"].get("debug_dump"):
            cv2.imwrite("debug/roi.png", roi)
        for trig in det.feed_frame(roi):
            fire(trig)
        if bridge is not None and hasattr(det, "last_new_events"):
            for ev in det.last_new_events:
                try:
                    who = f"{ev.killer} > {ev.victim}" + (f" {ev.distance_m}m" if getattr(ev, "distance_m", 0) else "")
                    if getattr(ev, "weapon", ""):
                        who += f" ({ev.weapon})"
                    bridge.event(who, "kill")
                except Exception:
                    pass
        time.sleep(max(0, period - (time.time() - t0)))


def save_config(c):
    """Atomic write: never leave a half-written config.yaml behind."""
    tmp = "config.yaml.tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        yaml.safe_dump(c, f, sort_keys=False, allow_unicode=True)
    os.replace(tmp, "config.yaml")


def load_config():
    try:
        with open("config.yaml", encoding="utf-8") as f:
            c = yaml.safe_load(f)
        if not isinstance(c, dict) or "detection" not in c:
            raise ValueError("config.yaml is not a ClipHound config")
        # a config.yaml survives upgrades, so rules added in a newer build are merged in by kind
        try:
            if os.path.exists("config.default.yaml"):
                with open("config.default.yaml", encoding="utf-8") as f:
                    d = yaml.safe_load(f) or {}
                det_c, det_d = c.setdefault("detection", {}), (d.get("detection") or {})
                have = {r.get("kind") for r in (det_c.get("rules") or [])}
                new = [r for r in (det_d.get("rules") or []) if r.get("kind") and r.get("kind") not in have]
                if new:
                    det_c.setdefault("rules", []).extend(new)
                    print(f"[config] rules added from this build: {', '.join(r['kind'] for r in new)}")
                    save_config(c)
        except Exception as e:
            print(f"[config] could not merge new rules: {e}")
        return c
    except Exception as e:
        print(f"[config] config.yaml is unreadable ({e}); starting from defaults")
        try:
            os.replace("config.yaml", time.strftime("config.broken-%Y%m%d-%H%M%S.yaml"))
        except Exception:
            pass
        src = "config.default.yaml" if os.path.exists("config.default.yaml") else None
        if src is None:
            raise
        import shutil
        shutil.copy(src, "config.yaml")
        with open("config.yaml", encoding="utf-8") as f:
            return yaml.safe_load(f)


def _safe(fn, *a):
    try:
        fn(*a)
    except Exception as e:
        print(f"[error] {fn.__qualname__}: {e}")


if __name__ == "__main__":
    try:
        main()
    except SystemExit:
        raise
    except Exception:
        import traceback
        traceback.print_exc()
        raise
