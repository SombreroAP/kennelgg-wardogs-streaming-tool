"""Twitch Helix: create a clip, and mark the stream for the VOD. Title is recorded locally (see
README on titles)."""
import json
import time
import requests

HELIX = "https://api.twitch.tv/helix"


class Twitch:
    def __init__(self, cfg, on_tokens=None):
        self.cfg = cfg
        # called with the twitch section after a refresh, so the new tokens reach config.yaml
        self.on_tokens = on_tokens
        self.h = {"Client-Id": cfg["client_id"], "Authorization": f"Bearer {cfg['access_token']}"}
        self.broadcaster_id = cfg.get("broadcaster_id") or self._user_id(cfg["broadcaster_login"])
        self.scopes = self._scopes()
        self._mark_warned = ""

    def _scopes(self) -> list:
        """What the saved login may do (a login from before 0.20.0 cannot place markers)."""
        try:
            r = requests.get("https://id.twitch.tv/oauth2/validate",
                             headers={"Authorization": f"OAuth {self.cfg['access_token']}"}, timeout=10)
            if r.status_code == 401 and self.cfg.get("refresh_token"):
                self._refresh()
                r = requests.get("https://id.twitch.tv/oauth2/validate",
                                 headers={"Authorization": f"OAuth {self.cfg['access_token']}"}, timeout=10)
            if r.status_code == 200:
                sc = list(r.json().get("scopes") or [])
                self.cfg["scopes"] = sc
                return sc
        except Exception as e:
            print(f"[twitch] could not check the login's permissions: {e}")
        return list(self.cfg.get("scopes") or [])

    @property
    def can_mark(self) -> bool:
        return "channel:manage:broadcast" in self.scopes

    def create_marker(self, description: str) -> bool:
        """A stream marker at this moment, for the VOD: Twitch lists them on the video's timeline and
        in its Highlighter. Only while live, and only for the broadcaster or one of their editors."""
        if not self.can_mark:
            self._warn_mark("scope", "Twitch markers need one more permission: log in to Twitch again from the "
                                     "plugin (Settings, Clips & replays, Twitch clips) once.")
            return False
        body = {"user_id": self.broadcaster_id, "description": (description or "")[:140]}
        r = requests.post(f"{HELIX}/streams/markers", headers=self.h, json=body, timeout=10)
        if r.status_code == 401 and self.cfg.get("refresh_token"):
            self._refresh()
            r = requests.post(f"{HELIX}/streams/markers", headers=self.h, json=body, timeout=10)
        if r.status_code == 404:
            self._warn_mark("offline", "[twitch] no marker: the channel is not live")
            return False
        if r.status_code in (401, 403):
            self._warn_mark("editor", f"[twitch] no marker: {self.cfg.get('clipper_login', 'this account')} is not "
                                      f"{self.cfg.get('broadcaster_login', 'the channel')} or one of its editors "
                                      f"(make it an editor on Twitch, or log in as the channel)")
            return False
        if r.status_code >= 400:
            self._warn_mark(str(r.status_code), f"[twitch] no marker: HTTP {r.status_code} {r.text[:120]}")
            return False
        self._mark_warned = ""
        print(f"[twitch] marker: {body['description']}")
        return True

    def _warn_mark(self, key, text):
        if self._mark_warned != key:        # once per kind of failure, not once per clip
            self._mark_warned = key
            print(text)

    def _get(self, path, **params):
        r = requests.get(f"{HELIX}/{path}", headers=self.h, params=params, timeout=10)
        if r.status_code == 401 and self.cfg.get("refresh_token"):
            self._refresh()
            r = requests.get(f"{HELIX}/{path}", headers=self.h, params=params, timeout=10)
        r.raise_for_status()
        return r.json()

    def _refresh(self):
        data = {"grant_type": "refresh_token", "refresh_token": self.cfg["refresh_token"], "client_id": self.cfg["client_id"]}
        if self.cfg.get("client_secret"):
            data["client_secret"] = self.cfg["client_secret"]
        r = requests.post("https://id.twitch.tv/oauth2/token", data=data, timeout=10)
        if r.status_code == 400:
            # the stored refresh token has been used already or revoked - only a fresh login fixes it
            raise RuntimeError("Twitch refused the saved login (400). Open the plugin's Settings, Clips & replays, Twitch clips "
                               "and press Log in to Twitch again - no clips can be made until you do.")
        r.raise_for_status()
        tok = r.json()
        self.cfg["access_token"] = tok["access_token"]
        self.cfg["refresh_token"] = tok.get("refresh_token", self.cfg["refresh_token"])
        self.h["Authorization"] = f"Bearer {tok['access_token']}"
        # Twitch rotates the refresh token: the one we just used is dead. Write the new pair out now
        # or the next start fails with 400 and no clip is ever made again.
        if self.on_tokens:
            try:
                self.on_tokens(self.cfg)
                print("[twitch] token refreshed and saved")
                return
            except Exception as e:
                print(f"[twitch] token refreshed but NOT saved ({e}) - log in again if clips stop")
                return
        print("[twitch] token refreshed but there is nowhere to save it - log in again if clips stop")

    def _user_id(self, login):
        d = self._get("users", login=login)
        return d["data"][0]["id"]

    def create_clip(self, title: str, tags: list[str] | None = None, _with_title: bool = True) -> dict | None:
        # Twitch names the clip after the stream unless told otherwise; the title goes with the
        # request (Helix accepts one of up to 100 characters). Length is Twitch's to decide: the API
        # takes the seconds leading up to the request, and its edit page trims after the fact.
        params = {
            "broadcaster_id": self.broadcaster_id,
            "has_delay": str(self.cfg.get("has_delay", False)).lower(),
        }
        if _with_title and title:
            params["title"] = title[:100]
        r = requests.post(f"{HELIX}/clips", headers=self.h, params=params, timeout=10)
        if r.status_code == 401 and self.cfg.get("refresh_token"):
            self._refresh()
            return self.create_clip(title, tags, _with_title)
        if r.status_code == 400 and _with_title:
            print("[twitch] Twitch refused the title; making the clip without one")
            return self.create_clip(title, tags, _with_title=False)
        if r.status_code == 404:
            print("[twitch] 404: channel is not live, no clip made")
            return None
        r.raise_for_status()
        clip = r.json()["data"][0]
        clip["title"] = title
        clip["tags"] = sorted(tags or [])
        clip["url"] = f"https://clips.twitch.tv/{clip['id']}"
        clip["created"] = time.strftime("%Y-%m-%d %H:%M:%S")
        with open("clips.jsonl", "a", encoding="utf-8") as f:
            f.write(json.dumps(clip, ensure_ascii=False) + "\n")
        print(f"[twitch] clip created: {clip['url']}  title='{title}'  tags={clip['tags']}  edit: {clip['edit_url']}")
        return clip
