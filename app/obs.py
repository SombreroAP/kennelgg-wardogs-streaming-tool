"""OBS on the streaming PC via obs-websocket v5 (TCP 4455 on the network).
Saves the replay buffer (backtrack) or runs a timed recording. Reconnects if the link drops."""
import csv
import json
import os
import re
import threading
import time
import obsws_python as obs


INDEX_COLS = ["created", "file", "title", "tags", "kind", "distance_m", "killer", "victim", "icons",
              "weapons", "weapon_types", "vehicles", "kills", "kill_times_s_from_end", "kill_epochs", "end_epoch"]
KILL_COLS = ["file", "created", "kill", "epoch", "time", "s_from_end", "killer", "victim", "killer_rel",
             "victim_rel", "distance_m", "headshot", "weapon", "weapon_type", "weapon_source", "vehicle"]


def _csv_with_header(path: str, cols: list[str]):
    """Open `path` for appending rows of `cols`. A file with an older, shorter header is brought up to
    date first (its rows padded), so the columns never shift under a reader."""
    import csv
    if os.path.exists(path):
        with open(path, newline="", encoding="utf-8") as f:
            rows = list(csv.reader(f))
        if rows and rows[0] != cols and all(c in cols for c in rows[0]):
            old = rows[0]
            tmp = path + ".tmp"
            with open(tmp, "w", newline="", encoding="utf-8") as f:
                w = csv.writer(f)
                w.writerow(cols)
                for r in rows[1:]:
                    d = dict(zip(old, r))
                    w.writerow([d.get(c, "") for c in cols])
            os.replace(tmp, path)
        new = not rows
    else:
        new = True
    f = open(path, "a", newline="", encoding="utf-8")
    w = csv.writer(f)
    if new:
        w.writerow(cols)
    return f, w


def _update_library_index(lib: str, rec: dict):
    """In the library root: index.csv (a row per clip, the weapons and every kill's time in it),
    kills.csv (a row per kill: when, how far into the clip, who, and what made it) and
    resolve_metadata.csv (DaVinci Resolve import format: File Name, Description, Keywords, Comments).

    Times: every kill's epoch (the moment its kill-feed row appeared) and, when the plugin says when
    the file ends, its offset before that end, to a tenth of a second - so an editor can land on it.
    Weapons are the game's names where something named them (your HUD, a learned icon); an
    explosive or a vehicle kill is never given the name of a gun."""
    evs = rec.get("events") or []
    end = rec.get("end_epoch")
    offs = [round(end - e["ts"], 1) if end and e.get("ts") else "" for e in evs]
    f, w = _csv_with_header(os.path.join(lib, "index.csv"), INDEX_COLS)
    with f:
        w.writerow([rec.get("created"), rec.get("file"), rec.get("title"), " ".join(rec.get("tags", [])),
                    rec.get("kind"), rec.get("distance_m"), rec.get("killer"), rec.get("victim"),
                    " ".join(rec.get("icons", [])),
                    "|".join(e.get("weapon", "") for e in evs), "|".join(e.get("weapon_type", "") for e in evs),
                    "|".join(e.get("vehicle", "") for e in evs), len(evs) or rec.get("kills", ""),
                    "|".join(str(o) for o in offs), "|".join(f"{e['ts']:.3f}" for e in evs if e.get("ts")),
                    f"{end:.3f}" if end else ""])
    if evs:
        f, w = _csv_with_header(os.path.join(lib, "kills.csv"), KILL_COLS)
        with f:
            for k, (e, o) in enumerate(zip(evs, offs), 1):
                w.writerow([rec.get("file"), rec.get("created"), k, f"{e['ts']:.3f}" if e.get("ts") else "",
                            e.get("time", ""), o, e.get("killer", ""), e.get("victim", ""), e.get("killer_rel", ""),
                            e.get("victim_rel", ""), e.get("distance_m", ""), "yes" if e.get("headshot") else "",
                            e.get("weapon", ""), e.get("weapon_type", ""), e.get("weapon_from", ""), e.get("vehicle", "")])
    rm = os.path.join(lib, "resolve_metadata.csv")
    new = not os.path.exists(rm)
    with open(rm, "a", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        if new:
            w.writerow(["File Name", "Description", "Keywords", "Comments", "Scene", "Shot"])
        w.writerow([os.path.basename(rec.get("file", "")), rec.get("description", rec.get("title")), ",".join(rec.get("tags", [])),
                    f"{rec.get('kind','')} {rec.get('distance_m','')}m killer={rec.get('killer','')} victim={rec.get('victim','')}".strip(),
                    rec.get("created", "")[:10], rec.get("kind", "")])


def _safe_name(s: str) -> str:
    s = re.sub(r"[^A-Za-z0-9 _\-\[\]#@.]+", "", s)
    return re.sub(r"\s+", " ", s).strip()[:180]        # Windows path budget


class OBS:
    def __init__(self, cfg):
        self.cfg = cfg
        self.cl = None
        self._lock = threading.Lock()
        self._connect()

    def _connect(self):
        try:
            self.cl = obs.ReqClient(host=self.cfg["host"], port=self.cfg["port"],
                                    password=self.cfg["password"], timeout=3)
            ver = self.cl.get_version()
            print(f"[obs] connected to {self.cfg['host']}:{self.cfg['port']} - OBS {ver.obs_version} "
                  f"(websocket {ver.obs_web_socket_version})")
            if self.cfg["mode"] == "replay" and not self.cl.get_replay_buffer_status().output_active:
                print("[obs] replay buffer is NOT running - starting it")
                self.cl.start_replay_buffer()
        except Exception as e:
            self.cl = None
            print(f"[obs] cannot reach OBS at {self.cfg['host']}:{self.cfg['port']} ({e}); "
                  f"retrying every {self.cfg['reconnect_s']}s")
            threading.Timer(self.cfg["reconnect_s"], self._connect).start()

    def trigger(self, title: str, tags: list[str] | None = None, info: dict | None = None):
        """title: short clip title. info['description'] (if given) is the long filename summary."""
        self._info = info or {}
        with self._lock:
            if not self.cl:
                print(f"[obs] not connected, backtrack for '{title}' lost")
                return
            try:
                if self.cfg["mode"] == "replay":
                    self.cl.save_replay_buffer()
                    print(f"[obs] replay buffer saved for '{title}'")
                    threading.Timer(3.0, self._rename_last_replay, args=(title, tags or [])).start()
                else:
                    if self.cl.get_record_status().output_active:
                        print("[obs] already recording, skip")
                        return
                    self.cl.start_record()
                    print(f"[obs] recording {self.cfg['record_seconds']}s for '{title}'")
                    threading.Timer(self.cfg["record_seconds"], self._stop).start()
            except Exception as e:
                print(f"[obs] request failed ({e}); reconnecting")
                self.cl = None
                self._connect()

    def _rename_last_replay(self, title: str, tags: list[str]):
        """OBS names replays by timestamp; rename to '<stamp> <title> [tag1 tag2].mkv' so the
        files are searchable, and log them to replays.jsonl next to clips.jsonl."""
        try:
            path = self.cl.get_last_replay_buffer_replay().saved_replay_path
        except Exception as e:
            print(f"[obs] could not get replay path: {e}")
            return
        if not path or not os.path.exists(path):
            print(f"[obs] replay path not found: {path!r} (OBS on another machine? see README)")
            return
        folder, fname = os.path.split(path)
        stem, ext = os.path.splitext(fname)
        lib = self.cfg.get("library") or ""
        if lib:
            folder = os.path.join(lib, time.strftime("%Y-%m-%d"))
            os.makedirs(folder, exist_ok=True)
        desc = self._info.get("description") or title
        moment = self.cfg.get("replay_delay_s", 4.0) + self._info.get("decision_lag_s", 3.0)
        # e.g. "Replay 2026-09-08 20-14-33 - Double kill - 68m 61m - rifle - 2 kills [multikill rifle] @-7s.mkv"
        new = os.path.join(folder, _safe_name(f"{stem} - {desc} [{' '.join(tags)}] @-{moment:.0f}s") + ext)
        try:
            os.rename(path, new)
        except OSError as e:
            print(f"[obs] rename failed ({e}); keeping {path}")
            new = path
        rec = {"file": new, "title": title, "description": desc, "tags": tags,
               "created": time.strftime("%Y-%m-%d %H:%M:%S"),
               "moment_s_from_end": round(moment, 1),      # the kill is about this far before the clip ends
               **{k: v for k, v in getattr(self, "_info", {}).items() if k != "description"}}
        with open("replays.jsonl", "a", encoding="utf-8") as f:
            f.write(json.dumps(rec, ensure_ascii=False) + "\n")
        with open(os.path.splitext(new)[0] + ".json", "w", encoding="utf-8") as f:
            json.dump(rec, f, ensure_ascii=False, indent=1)          # sidecar next to the clip
        if lib:
            _update_library_index(lib, rec)
        print(f"[obs] replay -> {new}")

    def _stop(self):
        try:
            self.cl.stop_record()
            print("[obs] recording stopped")
        except Exception as e:
            print(f"[obs] stop failed: {e}")
