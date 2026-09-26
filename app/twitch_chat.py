"""Twitch chat, read over IRC, for the "!replay" trigger and for clips when chat goes wild.

One TLS socket to irc.chat.twitch.tv, logged in with the same token the clips use (it needs the
chat:read scope, so a login made before 0.13.2 has to be redone once), joined to the broadcaster's
channel. A "!replay" from the broadcaster, a moderator, a VIP or a subscriber is sent to the
plugin, which owns the cooldown and answers with replay_result. Nothing is ever written to chat.
"""
from __future__ import annotations

import socket
import ssl
import threading
import time

HOST, PORT = "irc.chat.twitch.tv", 6697
ALLOWED_BADGES = ("broadcaster", "moderator", "vip", "subscriber", "founder")


def _parse(line: str):
    """IRC line with tags -> (tags dict, user, command, channel, text)."""
    tags = {}
    if line.startswith("@"):
        raw, _, line = line[1:].partition(" ")
        for kv in raw.split(";"):
            k, _, v = kv.partition("=")
            tags[k] = v
    user = ""
    if line.startswith(":"):
        prefix, _, line = line[1:].partition(" ")
        user = prefix.split("!")[0]
    cmd, _, rest = line.partition(" ")
    chan, _, text = rest.partition(" :")
    return tags, user, cmd, chan.strip(), text.rstrip("\r\n")


class TwitchChat:
    def __init__(self, cfg_twitch: dict, bridge, enabled_fn, word_fn=lambda: "!replay", hype=None):
        self.tw = cfg_twitch
        self.b = bridge
        self.replay_on = enabled_fn      # () -> bool: the plugin's "chat may trigger replays" switch
        self.word = word_fn              # () -> str: what they type (Settings, Clips), "!replay" by default
        self.hype = hype                 # ChatHype, or None: clips when chat goes wild
        # chat is read while either of them wants it
        self.enabled = lambda: bool(self.replay_on() or (self.hype is not None and self.hype.level()))
        self._stop = False
        self._sock = None
        self._thread = threading.Thread(target=self._run, daemon=True, name="twitch-chat")
        self._last_sent = 0.0
        self.state = "off"

    def start(self):
        self._thread.start()

    def stop(self):
        self._stop = True
        try:
            if self._sock:
                self._sock.close()
        except OSError:
            pass

    def _say(self, text):
        print("[chat] " + text)

    def _run(self):
        backoff = 5
        while not self._stop:
            token = (self.tw.get("access_token") or "").strip()
            login = (self.tw.get("clipper_login") or self.tw.get("broadcaster_login") or "").strip().lower()
            chan = (self.tw.get("broadcaster_login") or "").strip().lower()
            if not token or not chan or not self.enabled():
                self.state = "off"
                time.sleep(5)
                continue
            try:
                self._session(token, login or chan, chan)
                backoff = 5
            except Exception as e:
                self.state = f"error: {e}"
                self._say(f"chat connection lost ({e}); retrying in {backoff} s")
                time.sleep(backoff)
                backoff = min(60, backoff * 2)

    def _session(self, token: str, login: str, chan: str):
        raw = socket.create_connection((HOST, PORT), timeout=20)
        ctx = ssl.create_default_context()
        s = ctx.wrap_socket(raw, server_hostname=HOST)
        self._sock = s
        s.sendall(b"CAP REQ :twitch.tv/tags twitch.tv/commands\r\n")
        s.sendall(f"PASS oauth:{token}\r\n".encode())
        s.sendall(f"NICK {login}\r\n".encode())
        s.sendall(f"JOIN #{chan}\r\n".encode())
        s.settimeout(360)  # Twitch pings every ~5 min; silence longer than that is a dead socket
        buf = b""
        joined = False
        while not self._stop:
            try:
                data = s.recv(4096)
            except socket.timeout:
                raise RuntimeError("no traffic for 6 minutes")
            if not data:
                raise RuntimeError("closed by Twitch")
            buf += data
            while b"\r\n" in buf:
                line, buf = buf.split(b"\r\n", 1)
                text = line.decode("utf-8", "replace")
                if text.startswith("PING"):
                    s.sendall(b"PONG :tmi.twitch.tv\r\n")
                    continue
                tags, user, cmd, where, msg = _parse(text)
                if cmd == "NOTICE" and "authentication failed" in msg.lower():
                    self.state = "login lacks chat permission"
                    self._say("Twitch refused the chat login. Log out and back in to Twitch on the ClipHound "
                              "tab once: the login needs the chat permission added in 0.13.2.")
                    self._stop_for(300)
                    return
                if cmd == "JOIN" and not joined:
                    joined = True
                    self.state = f"reading #{chan}"
                    self._say(f"reading chat in #{chan} as {login}")
                elif cmd == "PRIVMSG":
                    self._message(tags, user, msg)
                if not self.enabled():
                    self._say("chat replays and chat clips both turned off; leaving chat")
                    s.close()
                    return

    def _stop_for(self, seconds: float):
        try:
            self._sock.close()
        except Exception:
            pass
        time.sleep(seconds)

    def _message(self, tags: dict, user: str, msg: str):
        if self.hype is not None:
            try:
                self.hype.feed(tags.get("display-name") or user, msg)
            except Exception as e:
                self._say(f"chat clip check failed: {e}")
        if not self.replay_on():
            return
        m = msg.strip().lower()
        w = (self.word() or "!replay").strip().lower()
        if not (m == w or m.startswith(w + " ")):
            return
        badges = tags.get("badges", "") or ""
        allowed = any(b.split("/")[0] in ALLOWED_BADGES for b in badges.split(",") if b) or tags.get("mod") == "1" \
            or tags.get("subscriber") == "1"
        if not allowed:
            self._say(f"!replay from {user} ignored (not a subscriber, VIP or moderator)")
            return
        if time.time() - self._last_sent < 2.0:
            return  # a burst of !replay lines is one request
        self._last_sent = time.time()
        self._say(f"!replay from {user}")
        self.b.send({"type": "replay", "who": f"twitch:{user}"})
