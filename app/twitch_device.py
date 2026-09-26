"""Log in to Twitch from the OBS plugin with the device code flow: no developer app for the user,
no client secret. Needs one registered *public* Twitch application for Kennel (client id below).
Register at https://dev.twitch.tv/console/apps: name "Kennel.gg Wardogs Streaming Tool", category
Application Integration, client type Public, redirect http://localhost. Paste its Client ID here."""
import threading
import time

import requests

KENNEL_TWITCH_CLIENT_ID = "bdtbcsrqxvjozcksvhm4eoy1ec0t3e"  # Kennel's public Twitch app (dev.twitch.tv, client type Public)
SCOPES = "clips:edit chat:read channel:manage:broadcast"   # the last one: stream markers (0.20.0)


def client_id(cfg: dict) -> str:
    return (cfg.get("twitch") or {}).get("client_id") or KENNEL_TWITCH_CLIENT_ID


def start_login(cfg: dict, on_status, save):
    """Runs the device flow in a thread. on_status(dict) gets {"state": "code"|"ok"|"error", ...};
    save(cfg) is called with tokens + clipper login written into cfg["twitch"]."""
    cid = client_id(cfg)
    if not cid:
        on_status({"state": "error", "error": "Twitch app id is not set in this build yet"})
        return

    def run():
        try:
            r = requests.post("https://id.twitch.tv/oauth2/device", data={"client_id": cid, "scopes": SCOPES}, timeout=10)
            r.raise_for_status()
            d = r.json()
            on_status({"state": "code", "user_code": d["user_code"], "verification_uri": d["verification_uri"], "expires_in": d.get("expires_in", 1800)})
            deadline = time.time() + d.get("expires_in", 1800)
            interval = d.get("interval", 5)
            while time.time() < deadline:
                time.sleep(interval)
                t = requests.post("https://id.twitch.tv/oauth2/token", data={
                    "client_id": cid, "scopes": SCOPES, "device_code": d["device_code"],
                    "grant_type": "urn:ietf:params:oauth:grant-type:device_code"}, timeout=10)
                if t.status_code == 200:
                    tok = t.json()
                    tw = cfg.setdefault("twitch", {})
                    tw["client_id"] = cid
                    tw["access_token"] = tok["access_token"]
                    tw["refresh_token"] = tok.get("refresh_token", "")
                    tw["scopes"] = list(tok.get("scope") or [])
                    tw["enabled"] = True
                    # who did we log in as?
                    u = requests.get("https://api.twitch.tv/helix/users", headers={"Client-Id": cid, "Authorization": f"Bearer {tok['access_token']}"}, timeout=10)
                    login = ""
                    if u.status_code == 200 and u.json().get("data"):
                        login = u.json()["data"][0]["login"]
                        tw["clipper_login"] = login
                    save(cfg)
                    on_status({"state": "ok", "login": login})
                    return
                msg = (t.json() or {}).get("message", "") if t.headers.get("content-type", "").startswith("application/json") else ""
                if "authorization_pending" in msg or "slow_down" in msg or t.status_code == 400:
                    if "slow_down" in msg:
                        interval += 5
                    continue
                on_status({"state": "error", "error": msg or f"HTTP {t.status_code}"})
                return
            on_status({"state": "error", "error": "code expired - try again"})
        except Exception as e:
            on_status({"state": "error", "error": str(e)})

    threading.Thread(target=run, daemon=True).start()


def logout(cfg: dict, save):
    tw = cfg.setdefault("twitch", {})
    for k in ("access_token", "refresh_token", "clipper_login"):
        tw[k] = ""
    tw["scopes"] = []
    tw["enabled"] = False
    save(cfg)


def status(cfg: dict) -> dict:
    tw = cfg.get("twitch") or {}
    return {"type": "twitch_status", "state": "ok" if tw.get("access_token") else "out",
            "login": tw.get("clipper_login", ""), "enabled": bool(tw.get("enabled")),
            "broadcaster": tw.get("broadcaster_login", ""), "has_app_id": bool(client_id(cfg)),
            # False for a login made before markers were asked for: the plugin says to log in again
            "markers": "channel:manage:broadcast" in (tw.get("scopes") or [])}
