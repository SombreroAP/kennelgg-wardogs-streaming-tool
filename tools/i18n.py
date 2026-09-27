#!/usr/bin/env python3
"""The plugin's translations.

  python3 tools/i18n.py extract   collects every tx("..."), txs("...") and TX_NOOP("...") in src/, plus the
                                  overlay pages' strings (data/overlay/*.html, t("...")), into data/i18n/en.json
  python3 tools/i18n.py check     every data/i18n/<code>.json against en.json: missing strings, and
                                  translations whose %1 %2 placeholders, {name} fields or <tags> differ
  python3 tools/i18n.py overlay   writes data/overlay/i18n.js, the overlay pages' strings in every language
  python3 tools/i18n.py ini       writes data/locale/<locale>.ini (OBS's Tools menu entry and the hotkey
                                  names) from the translations of data/locale/en-US.ini's values

A string is its own key: the English text exactly as the program has it at run time."""
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
I18N = os.path.join(ROOT, "data", "i18n")
LANGS = ["de", "fr", "es", "it", "pt", "pl", "tr", "ru", "uk", "ja", "ko", "zh", "zh-tw"]

_LIT = r'"(?:[^"\\\n]|\\.)*"'
_CALL = re.compile(r'\b(tx|txs|TX_NOOP)\(\s*((?:' + _LIT + r'\s*)+)\)')
_BAD = re.compile(r'\b(tx|txs)\((?!\s*")')
_JS = re.compile(r'\bt\(\s*(' + r"'(?:[^'\\\n]|\\.)*'" + r'|' + _LIT + r')\s*\)')


def _c_unescape(body: str) -> str:
    out, i, b = bytearray(), 0, body
    while i < len(b):
        ch = b[i]
        if ch != "\\":
            out += ch.encode("utf-8")
            i += 1
            continue
        n = b[i + 1]
        i += 2
        if n in "nt\"'\\?":
            out += {"n": b"\n", "t": b"\t", '"': b'"', "'": b"'", "\\": b"\\", "?": b"?"}[n]
        elif n == "u":
            out += chr(int(b[i:i + 4], 16)).encode("utf-8")
            i += 4
        elif n == "U":
            out += chr(int(b[i:i + 8], 16)).encode("utf-8")
            i += 8
        elif n == "x":
            m = re.match(r"[0-9a-fA-F]{1,2}", b[i:])
            out.append(int(m.group(0), 16))
            i += len(m.group(0))
        else:
            raise ValueError(f"escape \\{n}")
    return out.decode("utf-8")


def strings_in_source():
    found, bad = {}, []
    files = sorted(glob.glob(os.path.join(ROOT, "src", "**", "*.cpp"), recursive=True) +
                   glob.glob(os.path.join(ROOT, "src", "**", "*.h"), recursive=True))
    for f in files:
        if f.endswith("i18n.h") or f.endswith("i18n.cpp"):
            continue
        src = open(f, encoding="utf-8").read()
        rel = os.path.relpath(f, ROOT)
        for m in _CALL.finditer(src):
            lits = re.findall(_LIT, m.group(2))
            s = "".join(_c_unescape(l[1:-1]) for l in lits)
            line = src.count("\n", 0, m.start()) + 1
            found.setdefault(s, f"{rel}:{line}")
        for m in _BAD.finditer(src):
            line = src.count("\n", 0, m.start()) + 1
            bad.append(f"{rel}:{line}")
    for f in sorted(glob.glob(os.path.join(ROOT, "data", "overlay", "*.html"))):
        src = open(f, encoding="utf-8").read()
        for m in _JS.finditer(src):
            s = json.loads('"' + m.group(1)[1:-1].replace('"', '\\"') + '"') if m.group(1)[0] == "'" else json.loads(m.group(1))
            found.setdefault(s, "overlay:" + os.path.basename(f))
    for key, val in ini_strings():
        found.setdefault(val, "data/locale/en-US.ini:" + key)
    return found, bad


OBS_LOCALE = {"de": "de-DE", "fr": "fr-FR", "es": "es-ES", "it": "it-IT", "pt": "pt-BR", "pl": "pl-PL",
              "tr": "tr-TR", "ru": "ru-RU", "uk": "uk-UA", "ja": "ja-JP", "ko": "ko-KR", "zh": "zh-CN",
              "zh-tw": "zh-TW"}


def ini_strings():
    out = []
    for line in open(os.path.join(ROOT, "data", "locale", "en-US.ini"), encoding="utf-8"):
        m = re.match(r'([\w.]+)="(.*)"\s*$', line)
        if m:
            out.append((m.group(1), m.group(2)))
    return out


def overlay_strings():
    out = []
    for f in sorted(glob.glob(os.path.join(ROOT, "data", "overlay", "*.html"))):
        src = open(f, encoding="utf-8").read()
        for m in _JS.finditer(src):
            s = json.loads('"' + m.group(1)[1:-1].replace('"', '\\"') + '"') if m.group(1)[0] == "'" else json.loads(m.group(1))
            if s not in out:
                out.append(s)
    return out


_PH = re.compile(r"%\d+|\{[a-z_]+\}|</?[a-zA-Z][^>]*>|&[a-z]+;")


def _marks(s):
    return sorted(_PH.findall(s))


def check():
    en = json.load(open(os.path.join(I18N, "en.json"), encoding="utf-8"))
    ok = True
    for code in LANGS:
        p = os.path.join(I18N, code + ".json")
        if not os.path.exists(p):
            print(f"{code}: no file")
            ok = False
            continue
        tr = json.load(open(p, encoding="utf-8"))
        missing = [k for k in en if not tr.get(k)]
        extra = [k for k in tr if k not in en]
        wrong = [k for k in en if tr.get(k) and _marks(k) != _marks(tr[k])]
        print(f"{code}: {len(en) - len(missing)}/{len(en)} translated, {len(wrong)} with changed placeholders/tags, {len(extra)} stale")
        for k in wrong[:15]:
            print(f"   {k!r}\n-> {tr[k]!r}")
        if missing or wrong:
            ok = False
    return ok


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "extract"
    if cmd == "extract":
        found, bad = strings_in_source()
        os.makedirs(I18N, exist_ok=True)
        with open(os.path.join(I18N, "en.json"), "w", encoding="utf-8") as fh:
            json.dump({k: "" for k in found}, fh, ensure_ascii=False, indent=0)
        print(f"{len(found)} strings -> data/i18n/en.json")
        if "-v" in sys.argv:
            for k, where in found.items():
                print(f"{where}\t{k!r}")
        for b in bad:
            print(f"not a literal (use txv on TX_NOOP text): {b}")
    elif cmd == "check":
        sys.exit(0 if check() else 1)
    elif cmd == "overlay":
        keys = overlay_strings()
        table = {}
        for code in LANGS:
            p = os.path.join(I18N, code + ".json")
            tr = json.load(open(p, encoding="utf-8")) if os.path.exists(p) else {}
            table[code] = {k: tr[k] for k in keys if tr.get(k)}
        js = ("// Generated by tools/i18n.py overlay from data/i18n/*.json - do not edit.\n"
              "// t(\"English\") in the overlay pages: the page's &lang= translation, else the English.\n"
              "var KENNEL_I18N = " + json.dumps(table, ensure_ascii=False, separators=(",", ":")) + ";\n"
              "var KENNEL_LANG = (new URLSearchParams(location.search).get(\"lang\") || \"en\").toLowerCase();\n"
              "function t(s) { var d = KENNEL_I18N[KENNEL_LANG]; return (d && d[s]) || s; }\n"
              "document.documentElement.lang = KENNEL_LANG === \"zh-tw\" ? \"zh-Hant\" : KENNEL_LANG === \"zh\" ? \"zh-Hans\" : KENNEL_LANG;\n"
              "// the brand fonts are Latin only: each page's font lists fall back through --cjk to the Windows font\n"
              "// that has this language's letters (and the right Han shapes for Japanese vs Chinese)\n"
              "document.documentElement.style.setProperty(\"--cjk\", ({\n"
              "  ja: '\"Yu Gothic UI\", \"Yu Gothic\", Meiryo, \"MS Gothic\", \"Segoe UI\"',\n"
              "  ko: '\"Malgun Gothic\", \"Segoe UI\"',\n"
              "  zh: '\"Microsoft YaHei UI\", \"Microsoft YaHei\", SimHei, \"Segoe UI\"',\n"
              "  \"zh-tw\": '\"Microsoft JhengHei UI\", \"Microsoft JhengHei\", \"Segoe UI\"'\n"
              "})[KENNEL_LANG] || '\"Segoe UI\"');\n")
        open(os.path.join(ROOT, "data", "overlay", "i18n.js"), "w", encoding="utf-8").write(js)
        print(f"{len(keys)} overlay strings -> data/overlay/i18n.js")
    elif cmd == "ini":
        for code, loc in OBS_LOCALE.items():
            p = os.path.join(I18N, code + ".json")
            tr = json.load(open(p, encoding="utf-8")) if os.path.exists(p) else {}
            lines = [f'{k}="{(tr.get(v) or v).replace(chr(34), chr(39))}"' for k, v in ini_strings()]
            open(os.path.join(ROOT, "data", "locale", loc + ".ini"), "w", encoding="utf-8").write("\n".join(lines) + "\n")
        print(f"{len(OBS_LOCALE)} locale files -> data/locale/")


if __name__ == "__main__":
    main()
