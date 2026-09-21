"""Listen to the streamer's microphone (16 kHz mono PCM, sent by the OBS plugin over the bridge).

Two jobs:
  1. Name manual clips. When the plugin saves a clip the streamer asked for, the words said
     from ~8 s before the moment to ~4 s after become the clip's title ("Insane Triple Through
     Smoke"), and the whole sentence goes into the clip's .json. Whisper (faster-whisper, base
     model, int8, CPU) does that; Vosk is the fallback when Whisper is not available.
  2. Commands. Vosk listens all the time (it is light: ~10 % of one core) for the wake word
     followed by a command, and the plugin does the rest: "kennel replay", "kennel clip",
     "kennel show bouga", "kennel back", "kennel dual", "kennel highlights".

Nothing is written to disk and nothing leaves the PC: both models run locally. They are
downloaded once into ProgramData\\Kennel.gg\\ClipHound\\models the first time voice is on.
"""
import difflib
import json
import os
import re
import sys
import threading
import time
import zipfile

import numpy as np

RATE = 16000
RING_S = 30                       # seconds of microphone kept for naming
BEFORE_S, AFTER_S = 8.0, 4.0      # the window around a manual clip that names it
VOICE_AFTER_S = 7.0               # "kennel clip that" ... then the sentence that names it
MAX_TITLE_WORDS = 7
GRACE_S = 1.1          # a command that a longer phrase begins with waits this long for the rest

VOSK_URL = "https://alphacephei.com/vosk/models/vosk-model-small-en-us-0.15.zip"
VOSK_DIR = "vosk-model-small-en-us-0.15"
WHISPER_MODEL = "base"            # ~75 MB int8; "small" is better and 3x slower

# What a command can sound like. Matching is loose: the words after the wake word are scored
# against every phrase (word overlap and a fuzzy ratio), and the best intent wins if it is clear
# enough. "kennel play that back" is a replay; "kennel put bouga on" changes the squad mate.
INTENTS = {
    "replay":   ["replay", "instant replay", "play that back", "play it back", "run it back", "play it again",
                 "rewind", "show the replay", "replay that"],
    "clip_replay": ["clip replay", "clip and replay", "clip then replay", "clip that and replay", "save and replay",
                    "clip it and replay", "replay clip", "clip and play it back"],
    "clip":     ["clip", "clip that", "clip it", "clip this", "save that", "save clip", "save the clip",
                 "record that", "clip him", "clip the last bit"],
    "dual_off": ["dual off", "dual pov off", "stop dual", "single pov", "end dual", "turn off dual"],
    "dual":     ["dual", "dual pov", "dual point of view", "force dual", "split screen", "both povs",
                 "two povs", "dual on", "go dual", "both pov", "both", "both of us", "picture in picture", "two pov"],
    "force":    ["force squad mate", "force squad mate pov", "squad mate pov", "show squad mate", "show my squad mate",
                 "show his pov", "show her pov", "show their pov", "teammate pov", "switch to squad mate",
                 "squad mate point of view", "show squad mate point of view", "go to squad mate"],
    "closest":  ["closest", "nearest", "show closest", "closest squad mate", "nearest squad mate", "who is closest",
                 "whos closest", "show the closest pov", "closest pov", "nearest pov",
                 "show closest squad mate point of view"],
    "change":   ["change squad mate", "next squad mate", "switch squad mate", "other squad mate", "next pov",
                 "change pov", "change point of view", "swap squad mate", "next one", "someone else",
                 "change squad mate point of view", "switch pov", "switch povs", "swap pov", "switch the pov",
                 "switch point of view", "next point of view", "other pov"],
    "me":       ["back", "my pov", "me", "back to me", "my point of view", "show me", "stop", "my screen",
                 "go back", "back to my pov"],
    "highlights": ["highlights", "play highlights", "compilation", "montage", "play the compilation"],
}
# "kennel show bouga", "kennel switch to bouga three four", "kennel put bouga on": a name
NAME_RE = re.compile(r"^(?:show|switch to|swap to|change to|go to|put|watch|pov of|point of view of)\s+(.+?)(?:\s+(?:on|pov|point of view))?$")
NAME_STOP = {"squad", "mate", "squadmate", "teammate", "my", "the", "closest", "nearest", "him", "her", "them", "his",
             "their", "replay", "clip", "dual", "back", "me", "next", "other"}



def models_dir() -> str:
    base = os.path.dirname(os.path.abspath(sys.argv[0]))
    d = os.path.join(base, "models")
    os.makedirs(d, exist_ok=True)
    return d


class Voice:
    def __init__(self, bridge, cfg: dict):
        self.b = bridge
        self.cfg = cfg
        self.enabled = False
        self.wake = "hey kennel"
        self.commands = True
        self.names = True
        self.squad: list[str] = []
        self.allow: list[str] = []      # commands the plugin has on; empty = all
        self.chime = True
        self._armed_until = 0.0         # after a bare "hey kennel": the next words are the command
        self._chimed_at = 0.0
        self._chime_path = ""
        self._chime_vol = 60
        self.tones = True
        self._tone_paths = {}
        self._armed_said = False      # words came after a bare wake but made no command
        self._wake_ok_until = 0.0     # Whisper agreed the wake phrase was said, until
        self._wake_no_until = 0.0     # ... or disagreed: do not ask again for a moment
        self._ring = np.zeros(RATE * RING_S, np.int16)
        self._ring_pos = 0
        self._ring_epoch = 0.0          # wall clock of the newest sample in the ring
        self._lock = threading.Lock()
        self._q: list[bytes] = []
        self._q_cv = threading.Condition()
        self._vosk = None
        self._vosk_state = "off"
        self._whisper = None
        self._whisper_state = "off"
        self._worker = None
        self._loader = None
        self._last_cmd = 0.0
        self._last_clip_cmd = 0.0   # when "kennel clip" was last heard: the name comes after it
        self._last_cmd_name = ""
        self._pending = None        # a command held for GRACE_S: {cmd, name, after, heard, score, deadline}
        self._got = 0               # samples received since the last level report
        self._sq = 0.0              # their energy
        self._first = True
        self._utts = []             # (start, end) of the last sentences heard, by wall clock
        self._utt_start = 0.0       # when the sentence now being said began (0 = none)
        self._grammar_wake = None
        self._report_at = time.time() + 30
        self._status = ""

    # ----- from the plugin -----
    def configure(self, o: dict):
        self.enabled = bool(o.get("enabled", True))
        self.wake = re.sub(r"\s+", " ", str(o.get("wake") or "hey kennel").strip().lower())
        self.commands = bool(o.get("commands", True))
        self.names = bool(o.get("names", True))
        self.squad = [str(n) for n in (o.get("squad") or [])]
        self.allow = [str(c) for c in (o.get("allow") or [])]
        self.chime = bool(o.get("chime", True))
        self.chime_local = bool(o.get("chime_local", True))
        self.tones = bool(o.get("tones", True))
        vol = int(o.get("chime_volume", 60) or 60)
        if vol != self._chime_vol:
            self._chime_vol, self._chime_path, self._tone_paths = vol, "", {}   # new files at the new level
        if self.enabled and self._loader is None:
            self._loader = threading.Thread(target=self._load_models, daemon=True)
            self._loader.start()
        if self.enabled and self._worker is None:
            self._worker = threading.Thread(target=self._run, daemon=True)
            self._worker.start()
        self._say("listening" if self.enabled else "off")

    def feed(self, pcm: bytes):
        """Called from the bridge thread with ~100 ms of 16 kHz mono int16."""
        if not self.enabled or not pcm:
            return
        a = np.frombuffer(pcm, np.int16)
        if self._first:
            self._first = False
            print(f"[voice] microphone audio is arriving from the plugin ({len(a)} samples in the first piece)")
        self._got += len(a)
        self._sq += float(np.dot(a.astype(np.float64), a.astype(np.float64)))
        now = time.time()
        if now >= self._report_at:
            secs = self._got / RATE
            rms = (self._sq / max(1, self._got)) ** 0.5
            db = 20 * np.log10(max(rms, 1e-9) / 32768.0)
            print(f"[voice] mic: {secs:.0f} s received in the last {int(now - self._report_at + 30)} s, level {db:.0f} dBFS"
                  + ("  (silence - is the right source picked, and is it unmuted in Windows?)" if db < -60 else ""))
            self._got, self._sq, self._report_at = 0, 0.0, now + 120
        with self._lock:
            n = len(a)
            end = self._ring_pos + n
            if end <= len(self._ring):
                self._ring[self._ring_pos:end] = a
            else:
                k = len(self._ring) - self._ring_pos
                self._ring[self._ring_pos:] = a[:k]
                self._ring[:n - k] = a[k:]
            self._ring_pos = end % len(self._ring)
            self._ring_epoch = time.time()
        with self._q_cv:
            self._q.append(pcm)
            self._q_cv.notify()

    def name_clip(self, path: str, epoch: float):
        """The plugin saved a manual clip at `epoch`: name it from what was said around then."""
        if not self.enabled or not self.names:
            return
        threading.Thread(target=self._name, args=(path, epoch), daemon=True).start()

    # ----- inside -----
    def _say(self, text: str):
        if text != self._status:
            self._status = text
            print(f"[voice] {text}")
            self.b.send({"type": "voice_status", "text": text})

    def _load_models(self):
        d = models_dir()
        # Vosk: the always-on listener
        try:
            import vosk
            vosk.SetLogLevel(-1)
            vd = os.path.join(d, VOSK_DIR)
            if not os.path.isdir(vd):
                self._say("downloading the command model (40 MB)")
                import requests
                z = vd + ".zip"
                with requests.get(VOSK_URL, stream=True, timeout=60) as r:
                    r.raise_for_status()
                    with open(z, "wb") as fh:
                        for chunk in r.iter_content(1 << 20):
                            fh.write(chunk)
                with zipfile.ZipFile(z) as zf:
                    zf.extractall(d)
                os.remove(z)
            self._vosk = vosk.Model(vd)
            self._vosk_state = "ready"
        except Exception as e:
            self._vosk_state = f"unavailable ({e})"
            print(f"[voice] vosk: {e}")
        # Whisper: the namer
        try:
            from faster_whisper import WhisperModel
            self._say("loading the naming model" if os.path.isdir(os.path.join(d, "whisper")) else
                      "downloading the naming model (75 MB)")
            self._whisper = WhisperModel(WHISPER_MODEL, device="cpu", compute_type="int8",
                                         download_root=os.path.join(d, "whisper"))
            self._whisper_state = "ready"
        except Exception as e:
            self._whisper_state = f"unavailable ({e})"
            print(f"[voice] whisper: {e}")
        if self._vosk is None and self._whisper is None:
            self._say("no speech model could be loaded; see cliphound.log")
        elif self._vosk is None:
            self._say("naming ready; commands unavailable")
        elif self._whisper is None:
            self._say("commands ready; clips named with the small model")
        else:
            self._say("listening: commands and clip names ready")

    def _run(self):
        """Feed the microphone to Vosk and act on the wake word."""
        rec = None
        while True:
            with self._q_cv:
                while not self._q:
                    self._q_cv.wait(0.2)
                    if not self._q:
                        self._flush_pending(time.time())
                chunk = b"".join(self._q)
                self._q.clear()
            if self._vosk is None or not (self.commands or self.names):
                continue   # the listener also marks where sentences end, which names dock clips
            self._flush_pending(time.time())
            if rec is None or self._grammar_wake != self.wake:
                rec = self._recogniser()
            try:
                if rec.AcceptWaveform(chunk):
                    text = json.loads(rec.Result()).get("text", "")
                    now = time.time()
                    if text.strip():
                        # a sentence ended (the listener finalises on a pause): its span names a
                        # clip pressed on the dock or a hotkey
                        self._utts = (self._utts + [(self._utt_start or now - 2.0, now)])[-12:]
                    self._utt_start = 0.0
                    clean = text.replace("[unk]", " unk ").strip()
                    if clean.replace("unk", "").strip():
                        print(f"[voice] heard: {text[:120]}")   # what the model makes of you, [unk] = not a command
                        self._heard(clean)
                else:
                    # a command should not wait for a pause in the talking: look at the partial too
                    part = json.loads(rec.PartialResult()).get("partial", "").replace("[unk]", " unk ").strip()
                    if part and not self._utt_start:
                        self._utt_start = time.time() - 0.5   # the first words came a moment before the partial
                    if part.replace("unk", "").strip() and self._heard(part, partial=True):
                        rec.Reset()
            except Exception as e:
                print(f"[voice] recogniser: {e}")
                rec = None

    def _wake_confirmed(self, now: float) -> bool:
        if now < self._wake_ok_until:
            return True
        if now < self._wake_no_until:
            return False
        if self._whisper is None:
            return True
        try:
            audio = self._window(now - 3.0, now)
            if len(audio) < RATE // 2:
                return True
            f = audio.astype(np.float32) / 32768.0
            segs, _ = self._whisper.transcribe(f, language="en", beam_size=3, vad_filter=False,
                                               condition_on_previous_text=False,
                                               initial_prompt=self.wake.split()[-1].lower())   # one word: enough to
                                               # spell it right, not enough to make Whisper hear it everywhere
            text = " ".join(s.text.strip() for s in segs).lower()
            wake_words = self.wake.split()
            last, lead = wake_words[-1], (wake_words[-2] if len(wake_words) > 1 else "")
            words = re.sub(r"[^a-z0-9' ]", " ", text).split()
            # how Whisper tends to spell "kennel" in different accents; the listener has already
            # heard the wake phrase, this is the second opinion, so near-homophones count
            alike = {"kennel", "kennels", "kenel", "kettle", "kernel", "kendall", "cannel", "canal", "kenneth",
                     "kenna", "kenno", "kennell", "kenle"} if last == "kennel" else set()
            ok = False
            for k, w in enumerate(words):
                if w == last or w in alike or (len(w) >= 4 and difflib.SequenceMatcher(None, w, last).ratio() >= 0.7):
                    if not lead or (k > 0 and self._lead_ok(words[k - 1], lead)):
                        ok = True
            print(f"[voice] wake check: {'yes' if ok else 'no'}  <- {text!r}")
            if ok:
                self._wake_ok_until = now + 6.0
            else:
                self._wake_no_until = now + 0.6   # a "no" on a partial is looked at again at the final
            return ok
        except Exception as e:
            print(f"[voice] wake check: {e}")
            return True

    def _play_chime(self, kind: str = "wake"):
        """Sounds for the streamer: "wake" (a soft two-note chime: listening), "ok" (a rising pair:
        the command was taken), "fail" (a low falling pair: the words made no command). Through
        this PC's speakers, the stream, or both, as the plugin says; never recorded."""
        if kind == "wake" and not self.chime:
            return
        if kind != "wake" and not self.tones:
            return
        self.b.send({"type": "voice_chime", "kind": kind})   # the plugin plays it into the stream, if that is where it goes
        if not self.chime_local:
            return
        try:
            import winsound
        except ImportError:
            return
        try:
            path = self._tone_paths.get(kind)
            if not path:
                path = os.path.join(models_dir(), f"{kind}-{self._chime_vol}.wav")
                if not os.path.exists(path):
                    import wave
                    sr = 22050

                    def tone(freq, ms, vol):
                        n = int(sr * ms / 1000)
                        t = np.arange(n) / sr
                        env = np.minimum(1.0, np.minimum(t / 0.01, (n / sr - t) / 0.06))
                        return np.sin(2 * np.pi * freq * t) * env * vol
                    k = max(0.05, min(1.0, self._chime_vol / 100.0)) * 0.3   # 100 % is still soft, -10 dBFS
                    if kind == "ok":
                        a = np.concatenate([tone(880, 80, k), tone(1174.66, 140, k * 0.9), np.zeros(int(sr * 0.04))])
                    elif kind == "fail":
                        a = np.concatenate([tone(392, 120, k), tone(293.66, 200, k * 0.9), np.zeros(int(sr * 0.04))])
                    else:
                        a = np.concatenate([tone(659.25, 110, k), tone(987.77, 160, k * 0.9), np.zeros(int(sr * 0.05))])
                    with wave.open(path, "wb") as w:
                        w.setnchannels(1)
                        w.setsampwidth(2)
                        w.setframerate(sr)
                        w.writeframes((a * 32767).astype(np.int16).tobytes())
                self._tone_paths[kind] = path
            winsound.PlaySound(path, winsound.SND_FILENAME | winsound.SND_ASYNC | winsound.SND_NODEFAULT)
        except Exception as e:
            print(f"[voice] chime: {e}")

    def _recogniser(self):
        """Vosk with a grammar: the wake word and every way of asking, everything else [unk].
        Free-form, the small model made "kendall replay" and "kennel club that" of a clear
        "kennel replay" and "kennel clip that"; told what can be said, it gets them right and
        turns ordinary talk into [unk]. A name after "show" is [unk] too; Whisper reads it."""
        from vosk import KaldiRecognizer
        self._grammar_wake = self.wake
        phrases = {"[unk]", "show [unk]", "switch to [unk]", "clip that [unk]"}
        for wk in self.wakes():
            phrases |= {wk, f"{wk} show [unk]", f"{wk} switch to [unk]", f"{wk} change to [unk]", f"{wk} clip that [unk]",
                        f"{wk} clip [unk]", f"{wk} switch", f"{wk} change", f"{wk} swap", f"{wk} next", "switch", "change",
                        "swap", "next"}
            for ps in INTENTS.values():
                for p in ps:
                    for q in (p, p.replace("povs", "p o v s").replace("pov", "p o v")):
                        phrases.add(f"{wk} {q}")
                        phrases.add(q)   # on its own: counts only in the moment after the wake phrase
        try:
            rec = KaldiRecognizer(self._vosk, RATE, json.dumps(sorted(phrases)))
            print(f"[voice] listening for {len(phrases)} phrases after '{self.wake}'")
        except Exception as e:
            print(f"[voice] grammar not accepted ({e}); listening free-form")
            rec = KaldiRecognizer(self._vosk, RATE)
        rec.SetWords(False)
        return rec

    def _name_after(self, cmd: str):
        """'kennel show [unk]': the name was not in the small model's world. Whisper hears the
        last few seconds and the words after show / switch to are the name."""
        try:
            audio = self._window(time.time() - 4.0, time.time())
            if self._whisper is None or len(audio) < RATE // 2:
                return
            f = audio.astype(np.float32) / 32768.0
            segs, _ = self._whisper.transcribe(f, language="en", beam_size=2, vad_filter=False,
                                               condition_on_previous_text=False,
                                               initial_prompt=f"{self.wake}, show " + ", ".join(self.squad[:6]))
            text = " ".join(s.text.strip() for s in segs).lower()
            t = re.sub(r"[^a-z0-9' ]", " ", text)
            # the last ask in the window is the one that just fired
            found = re.findall(r"\b(?:show|switch to|change to|swap to|go to|put)\s+((?:(?!\b(?:show|switch|change|swap|go|put|" +
                               re.escape(self.wake.split()[-1]) + r")\b)[a-z0-9' ])+?)(?:\s+(?:on|pov|point of view))?\s*(?=$|\b" +
                               re.escape(self.wake.split()[-1]) + r"\b)", t)
            name = self.digits(found[-1].strip()) if found else ""
            print(f"[voice] name after '{cmd}': {name!r}  <- {text!r}")
            if name:
                self.b.send({"type": "voice", "cmd": "change", "name": name, "heard": text})
        except Exception as e:
            print(f"[voice] name: {e}")

    def _heard(self, text: str, partial: bool = False) -> bool:
        if not self.commands:
            return False
        t = " " + re.sub(r"[^a-z0-9' ]", " ", text.lower()) + " "
        t = re.sub(r"\s+", " ", t)
        t = t.replace(" p o v s ", " povs ").replace(" p o v ", " pov ").replace(" pee oh vee ", " pov ")
        # the wake phrase as the model heard it: "hey kennel", or just "kennel", or "kennels" / "kenel"
        ws = t.split()
        wi = -1
        wake_words = self.wake.split()
        last, lead = wake_words[-1], (wake_words[-2] if len(wake_words) > 1 else "")
        for k, w in enumerate(ws):
            if w == last or (len(w) >= 4 and difflib.SequenceMatcher(None, w, last).ratio() >= 0.8):
                # a wake phrase of two words needs both: "dog kennel" is not "hey kennel"
                if lead and not (k > 0 and self._lead_ok(ws[k - 1], lead)):
                    continue
                wi = k
        now = time.time()
        if self._pending:
            # a command is held for a moment: more words may complete it ("clip" ... "and replay"
            # is "clip and replay"). The hold starts from the first partial result, so the
            # listener's own wait for silence is not added on top; the same words again (the
            # final result) keep it held, words still being said extend it, and a different ask
            # with its own wake phrase lets it go as it is
            p = self._pending
            if wi >= 0:
                after_now = " ".join(w for w in ws[wi + 1:] if w != "unk").strip()
                if after_now == p["after"]:
                    return not partial
                if after_now.startswith(p["after"] + " "):
                    c2, n2, s2 = self.intent(after_now)
                    if c2 and c2 != p["cmd"]:
                        self._pending = None
                        self._armed_until = 0.0
                        return self._fire(c2, n2, after_now, s2, now)
                    p["after"], p["heard"] = after_now, text
                    p["deadline"] = max(p["deadline"], now + 0.7)
                    return not partial
            elif partial or now <= p["deadline"]:
                new = " ".join(w for w in ws if w != "unk").strip()
                # the listener re-sends the growing ask ("clip", "clip that"): that is the same ask
                # grown, not two asks; only words that do not start with it are added on
                combined = new if new.startswith(p["after"]) else (p["after"] + " " + new).strip()
                c2, n2, s2 = self.intent(combined)
                if c2 and c2 != p["cmd"]:
                    self._pending = None
                    self._armed_until = 0.0
                    return self._fire(c2, n2, combined, s2, now)
                if partial:
                    p["deadline"] = max(p["deadline"], now + 0.7)
                    return False
            if not partial:
                self._flush_pending(now, force=True)
        if wi < 0:
            # no wake phrase in this: it counts only in the few seconds after a bare "hey kennel"
            ws = [w for w in ws if w != "unk"]
            if now > self._armed_until or not ws:
                return False
            after = " ".join(ws).strip()
        else:
            # the listener is told what can be said, so it will hear "hey kennel" in "hey can you"
            # or "get to the". A second opinion: Whisper on the last two seconds has to hear it too
            if not self._wake_confirmed(now):
                return False
            after = " ".join(w for w in ws[wi + 1:] if w != "unk").strip()
            if not after:
                # "hey kennel" on its own: chime, and take the next few seconds' words as the command
                if now - self._chimed_at > 2.0:
                    self._chimed_at = now
                    self._armed_until = now + 6.0
                    self._play_chime()
                return False
        cmd, name, score = self.intent(after)
        if not cmd and not partial and after in ("switch", "change", "swap", "next", "swap to", "switch to", "change to"):
            cmd, name, score = "change", "", 1.0   # no name said: the next squad mate with a picture
        if not cmd and not partial and after not in ("show", "switch to", "change to", "clip that", "clip"):
            # a whole sentence after the wake phrase, and it made no command: say so
            if wi >= 0 or now <= self._armed_until:
                print(f"[voice] not understood: {after!r}")
                self._armed_until = 0.0
                self._play_chime("fail")
            return False
        if not cmd and not partial and after in ("show", "switch to", "change to", "clip that", "clip"):
            cmd, name, score = ("clip", "", 1.0) if after.startswith("clip") else ("change", "", 1.0)
            if cmd == "change":
                threading.Thread(target=self._name_after, args=(after,), daemon=True).start()
                self._last_cmd = time.time()
                return True
        if not cmd:
            return False
        if partial:
            # half a sentence is not a command: "show my" is not "show me". Only a whole phrase,
            # word for word, fires early; anything with a name or a fuzzy fit waits for the end
            if score < 0.87 or name or after.split()[0] in ("show", "switch", "change", "swap", "go", "put", "watch"):
                return False
            # ...and not a phrase that a longer one begins with: "clip" is the start of "clip and
            # replay", so acting on it mid-sentence turned "clip and replay" into a plain clip.
            # It is held from here, so the wait for the final result is not added to the hold
            if self._starts_longer(after):
                if not self._pending:
                    self._pending = {"cmd": cmd, "name": name, "after": after, "heard": text, "score": score,
                                     "deadline": now + GRACE_S}
                    self._armed_until = 0.0
                return False
        if not partial and self._starts_longer(after) and not name:
            # "clip", "dual", "replay": a longer ask may follow after a breath. Hold it a moment
            self._pending = {"cmd": cmd, "name": name, "after": after, "heard": text, "score": score,
                             "deadline": now + GRACE_S}
            self._armed_until = 0.0
            return True
        self._armed_until = 0.0
        return self._fire(cmd, name, text, score, now)

    def _flush_pending(self, now: float, force: bool = False):
        p = self._pending
        if p and (force or now > p["deadline"]):
            self._pending = None
            self._fire(p["cmd"], p["name"], p["heard"], p["score"], now)

    def _fire(self, cmd: str, name: str, text: str, score: float, now: float) -> bool:
        if cmd == "clip_replay" and self.allow and not ("clip" in self.allow and "replay" in self.allow):
            return True                # one of the two halves is switched off
        if self.allow and cmd not in self.allow and cmd not in ("me", "highlights", "clip_replay"):
            return True                # switched off in the plugin: swallow it, say nothing
        if now - self._last_cmd < 2.0 and cmd == self._last_cmd_name:
            return True                # the same command, heard twice (partial then final)
        self._last_cmd = now
        self._last_cmd_name = cmd
        if cmd in ("clip", "clip_replay"):
            self._last_clip_cmd = now
        print(f"[voice] command: {cmd} {name!r} ({score:.2f})  <- {text!r}")
        self.b.send({"type": "voice", "cmd": cmd, "name": name, "heard": text})
        self._play_chime("ok")
        return True

    @staticmethod
    def _starts_longer(after: str) -> bool:
        a = after.strip() + " "
        for ps in INTENTS.values():
            for p in ps:
                if p != after.strip() and p.startswith(a):
                    return True
        return False

    @classmethod
    def intent(cls, after: str) -> tuple[str, str, float]:
        """(cmd, name, score) for the words after the wake word; ("", "", 0) when nothing fits."""
        after = after.strip()
        words = after.split()
        # a squad mate by name first: "show bouga three four", "switch to carranco"
        m = NAME_RE.match(after)
        if m:
            cand = m.group(1).strip()
            if cand and not all(w in NAME_STOP for w in cand.split()):
                # but "show my squad mate" / "show closest" are intents, not names
                if not any(w in ("squad", "mate", "squadmate", "teammate", "closest", "nearest", "replay", "me")
                           for w in cand.split()):
                    return "change", cls.digits(cand), 1.0
        best, best_cmd = 0.0, ""
        head = " ".join(words[:6])
        for cmd, phrases in INTENTS.items():
            for ph in phrases:
                pw = ph.split()
                # every word of the phrase present, in order, near the start
                if all(w in words for w in pw):
                    idx = [words.index(w) for w in pw]
                    # at the very start, or one word in and then most of what was said: "me" in
                    # the middle of "mate me someone" is not "back to me"
                    if idx == sorted(idx) and (idx[0] == 0 or (idx[0] == 1 and len(pw) * 2 >= len(words))):
                        # the phrase that explains the most words wins ("change squad mate point of
                        # view" over "squad mate point of view")
                        sc = 0.85 + 0.02 * len(pw)
                        if sc > best:
                            best, best_cmd = sc, cmd
                        continue
                sc = difflib.SequenceMatcher(None, head[:len(ph) + 4], ph).ratio()
                if sc > best:
                    best, best_cmd = sc, cmd
        if best >= 0.8:
            return best_cmd, "", min(best, 1.0)
        return "", "", best

    def _window(self, t0: float, t1: float) -> np.ndarray:
        """Microphone samples between two wall-clock times, from the ring."""
        with self._lock:
            newest = self._ring_epoch
            ring = self._ring.copy()
            pos = self._ring_pos
        # the ring ends at `newest`; sample s is (len - s) samples before it
        lin = np.concatenate([ring[pos:], ring[:pos]])       # oldest .. newest
        n = len(lin)
        a = int(n - (newest - t0) * RATE)
        z = int(n - (newest - t1) * RATE)
        a, z = max(0, a), max(0, min(n, z))
        return lin[a:z] if z > a else lin[:0]

    def _name(self, path: str, epoch: float):
        # asked by voice ("kennel clip that ..."): the sentence that follows names it, so the
        # window runs from the command onwards and waits for it. Asked from the dock or a
        # hotkey: what was being said around the moment
        by_voice = abs(epoch - self._last_clip_cmd) < 4.0
        if by_voice:
            t0, t1 = self._last_clip_cmd - 1.0, self._last_clip_cmd + VOICE_AFTER_S
        else:
            # pressed on the dock or a hotkey: the last sentence you said. If you were still
            # talking at the press, that sentence, once it ends (up to four seconds)
            # the listener's first partial for a sentence comes up to a second after it began, so
            # a press as you start talking has to give it that moment before deciding
            just_finished = any(epoch - 1.0 <= e <= epoch + 0.3 for _, e in self._utts)
            if not just_finished:
                # not right after a sentence: give one that is starting up to 1.5 s to show
                deadline = epoch + 1.5
                while not self._utt_start and time.time() < deadline:
                    time.sleep(0.1)
            if self._utt_start and self._utt_start <= epoch + 1.5:
                deadline = time.time() + 4.0
                while self._utt_start and time.time() < deadline:
                    time.sleep(0.1)
            last = next(((s, e) for s, e in reversed(self._utts) if e <= epoch + 4.5 and e >= epoch - 20.0), None)
            if last:
                t0, t1 = last[0] - 0.4, last[1] + 0.3
            else:
                t0, t1 = epoch - BEFORE_S, epoch + AFTER_S   # nothing said lately: the old window
        wait = t1 - time.time() + 0.3
        if wait > 0:
            time.sleep(min(wait, VOICE_AFTER_S + 1))
        audio = self._window(t0, t1)
        if len(audio) < RATE:
            self.b.send({"type": "clip_name", "path": path, "title": "", "text": ""})
            return
        text = ""
        try:
            if self._whisper is not None:
                f = audio.astype(np.float32) / 32768.0
                segs, _ = self._whisper.transcribe(f, language="en", beam_size=2, vad_filter=True,
                                                   condition_on_previous_text=False,
                                                   initial_prompt=f"{self.wake}, clip that.")
                text = " ".join(s.text.strip() for s in segs).strip()
            elif self._vosk is not None:
                from vosk import KaldiRecognizer
                rec = KaldiRecognizer(self._vosk, RATE)
                rec.AcceptWaveform(audio.tobytes())
                text = json.loads(rec.FinalResult()).get("text", "")
        except Exception as e:
            print(f"[voice] naming: {e}")
        title = self.title_from(text, self.wake.split()[-1])
        print(f"[voice] clip name: {title!r}  <- {text!r}")
        self.b.send({"type": "clip_name", "path": path, "title": title, "text": text})

    def wakes(self) -> list[str]:
        """The wake phrase, whole. "Kennel" on its own used to count too, and "dog kennel" in
        conversation woke it; now it takes "hey kennel", both words."""
        return [self.wake]

    @staticmethod
    def _lead_ok(w: str, lead: str) -> bool:
        """The word before "kennel" has to be the "hey": as said, or as the listeners tend to
        write it ("hay", "hi", "a")."""
        if w == lead or w == "unk":
            return True                # "[unk] kennel": something was said first; Whisper decides what
        if lead == "hey":
            return w in ("hay", "hi", "a", "hey", "okay", "ok", "yo", "oi", "eh", "ay", "hae", "hei")
        return len(w) >= 3 and difflib.SequenceMatcher(None, w, lead).ratio() >= 0.75

    @staticmethod
    def digits(name: str) -> str:
        """'bouga three four' -> 'bouga34': names carry numbers, recognisers say them as words."""
        nums = {"zero": "0", "oh": "0", "one": "1", "two": "2", "three": "3", "four": "4", "five": "5",
                "six": "6", "seven": "7", "eight": "8", "nine": "9"}
        out = []
        for w in name.split():
            if w in nums and out and (out[-1][-1].isdigit() or len(out[-1]) > 0):
                if out[-1][-1].isdigit():
                    out[-1] += nums[w]
                else:
                    out[-1] += nums[w]
            elif w in nums:
                out.append(nums[w])
            else:
                out.append(w)
        return " ".join(out)

    @staticmethod
    def title_from(text: str, wake: str = "kennel") -> str:
        wake = wake.split()[-1] if wake else "kennel"   # "hey kennel" -> "kennel": "hey" may be misheard
        """A few words fit for a file name: the command words and filler dropped, Title Case."""
        t = text.lower()
        t = re.sub(r"\bp\s?[.]?\s?v\b", "pov", t)   # "PV", "P V" as the recognisers write it
        t = re.sub(r"[^a-z0-9' ]", " ", t)
        # "kennel clip that <what it was>": what follows the ask is the title, whatever came before
        # the ask can arrive as "kennel clip that", or with the wake word misheard ("then I'll
        # clip that", "can I clip that"): the clip words are the anchor, the wake word optional
        ask = re.compile(r"(?:\b" + re.escape(wake) + r"\b\s*)?\b(clip|clipped|save|record)\s+"
                         r"(that and replay|it and replay|and replay|then replay|replay|that|this|it|him|her|them)\b\s*")
        m = None
        for m in ask.finditer(t):
            pass
        if m and t[m.end():].strip():
            t = t[m.end():]
        else:
            t = ask.sub(" ", t)
            t = re.sub(r"\b" + re.escape(wake) + r"\b\s*(clip|replay|show|back|dual|highlights)?\s*", " ", t)
        filler = {"uh", "um", "like", "yeah", "okay", "ok", "oh", "so", "just", "bro", "dude", "man", "guys", "chat",
                  "holy", "wow", "lets", "let's", "please"}
        words = [w for w in t.split() if w not in filler]
        # leading noise ("that was", "oh my god") goes; the little words inside a phrase stay
        lead = {"that", "this", "it", "was", "is", "my", "god", "no", "way", "there", "we", "and", "the", "a", "hey",
                "so", "then"}
        while words and words[0] in lead:
            words.pop(0)
        if not words:
            return ""
        words = words[:MAX_TITLE_WORDS] if len(words) > MAX_TITLE_WORDS else words
        return " ".join("POV" if w == "pov" else w.capitalize() for w in words)[:48].strip()
