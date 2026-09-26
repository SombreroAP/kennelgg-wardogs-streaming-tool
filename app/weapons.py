"""Which weapon made a kill, by the game's own name.

Two sources, best first:

1. **Your HUD.** The bottom-right of the screen names what you are holding ("GALIL 5.56",
   "EMERGENCY RESUSCITATOR"). For your own kills that is the answer, read straight off the game.
   The plugin sends that corner as its own small stream (id 4), two frames a second, and the last
   few seconds are kept so a kill is paired with what you held when it happened.
2. **Learned icons.** Every one of your kills also shows that weapon's white silhouette in the
   kill feed. It is saved under icons/learned/<weapon>/ with the name the HUD gave it, so the same
   icon on someone else's row - the kill that downed you, a squad mate's kill - is named too.
   The more you play, the more of the arsenal it knows; nothing is guessed from a generic shape.

The names below are the game's, from the Early Access weapon list (see NAMES_SOURCE). Each has a
class that matches one of the generic kill-feed templates in templates/, so the clip rules that
key on those ("rpg", "sniper", "heli"...) keep working whatever the exact weapon.
"""
from __future__ import annotations

import difflib
import json
import os
import re
import threading
import time
import unicodedata
from collections import Counter, deque

import cv2
import numpy as np

try:
    import pytesseract
except ImportError:  # the OCR build always has it; a bare test environment may not
    pytesseract = None

# Where the name sits: the bottom-right item plate, above the health/armour row. Fractions of
# the frame, measured on a 1875x1052 capture of the game (the plate ends at x 0.980, the text
# line is y 0.837-0.852); generous so a different HUD scale or aspect still lands in it.
HUD_ROI = (0.70, 0.815, 0.29, 0.055)
HUD_FPS = 2.0
LEARNED_MATCH = 0.85       # a learned icon names a kill from this score: on the streamer's own rows the
                           # same gun scored 0.88-1.00 and the nearest other rifle 0.82 (22 Sep footage)
LEARN_MAX = 12             # samples kept per weapon
LEARN_DUP = 0.95           # a new sample this close to one we have adds nothing

NAMES_SOURCE = ("wardogs.zone/database (read from the game files, build CL501228) and MetaForge, which agree "
                "name for name; checked against Bulkhead's Season 1 changelog (9 Sep 2026)")

# (exact in-game name, category, generic kill-feed template class). The class ties a name to the
# icon classes the clip rules key on; "" = none of them. Vehicle weapons are named for titles but
# never learned: a vehicle kill shows the vehicle's icon, not the gun's.
WEAPONS: list[tuple[str, str, str]] = [
    # assault rifles (A-91, Bushmaster M17S and KH-2002 are the three factions' free rifles)
    ("A-91", "assault rifle", "rifle"),
    ("AK74", "assault rifle", "rifle"),
    ("Bushmaster M17S", "assault rifle", "rifle"),
    ("FAL", "assault rifle", "rifle"),
    ("Galil", "assault rifle", "rifle"),
    ("KH-2002", "assault rifle", "rifle"),
    ("M4", "assault rifle", "rifle"),
    ("T-21", "assault rifle", "rifle"),
    # SMGs
    ("AMP-9", "SMG", "rifle"),
    ("MP5", "SMG", "rifle"),
    ("PP-19 Vityaz", "SMG", "rifle"),
    ("Super-45", "SMG", "rifle"),
    # shotguns
    ("M500", "shotgun", "shotgun"),
    ("MP43", "shotgun", "shotgun"),
    # LMGs
    ("M249 SAW", "LMG", "lmg"),
    ("PKM", "LMG", "lmg"),
    # marksman rifles
    ("BMR-308", "marksman rifle", "sniper"),
    ("SKS", "marksman rifle", "hunting"),
    ("SVD", "marksman rifle", "sniper"),
    # sniper rifles
    ("AMR 50", "sniper rifle", "sniper"),
    ("MK22", "sniper rifle", "sniper"),
    ("Mosin Nagant", "sniper rifle", "boltgun"),
    ("Scout Rifle TD", "sniper rifle", "boltgun"),
    ("SV98", "sniper rifle", "boltgun"),
    # pistols
    ("Deagle", "pistol", "pistol"),
    ("GGX 17", "pistol", "pistol"),
    ("GGX 18", "pistol", "pistol"),
    ("Judge", "pistol", "pistol"),
    ("M1911", "pistol", "pistol"),
    # launchers
    ("9K333 Verba", "launcher", "rpg"),
    ("MAAWS", "launcher", "rpg"),
    ("MGL-40", "launcher", "grenade"),
    ("RPG-7", "launcher", "rpg"),
    # bow
    ("Compound Bow", "bow", ""),
    # throwables and explosives
    ("M67 Frag Grenade", "grenade", "grenade"),
    ("Gold Frag Grenade", "grenade", "grenade"),
    ("C4 Charge", "explosive", "c4"),
    ("Remote Detonator", "explosive", "c4"),
    ("Improvised Explosive Device", "explosive", "c4"),
    ("AT Mine", "explosive", ""),
    ("Claymore", "explosive", ""),
    # melee and build tools
    ("Fists", "melee", ""),
    ("Small Hammer", "tool", "hammer"),
    ("Medium Hammer", "tool", "hammer"),
    ("Large Hammer", "tool", "hammer"),
    # emplacements you build
    ("L81 Mortar", "emplacement", "mortar"),
    ("Talon 9K-SAM", "emplacement", ""),
    ("Vanguard CIWS", "emplacement", ""),
    ("Stingray", "emplacement", ""),
    # vehicle weapons (named in titles, never learned from the icon)
    ("M134D Minigun", "vehicle weapon", ""),
    ("M249 Machine Gun", "vehicle weapon", ""),
    ("2A42 Autocannon", "vehicle weapon", ""),
    ("B-13 Rocket Pods", "vehicle weapon", ""),
    ("L55A1 Cannon", "vehicle weapon", "tank"),
    ("MG3A1 Coaxial Gun", "vehicle weapon", "tank"),
    ("L52 Cannon", "vehicle weapon", "artillery"),
]

# things you can hold that are not weapons: seeing one of these on the plate means the gun you had is
# put away. Only a positive match counts - unreadable text never clears what you were holding
NOT_WEAPONS = ["Emergency Resuscitator", "M18 Smoke Grenade", "M18 Signal Grenade", "Halligan Bar", "Wrench",
               "Light Drill", "Heavy Drill", "Loudspeaker"]

# Model designations: the same in every language the game is translated into, so a plate reading
# "FUSIL AK74 5,45" or "АК74" (after OCR) still names the gun. Only a whole word (or two words run
# together, "GGX 17") counts, so a code is never found inside another word.
CODES: dict[str, list[str]] = {
    "A-91": ["A91"], "AK74": ["AK74"], "Bushmaster M17S": ["M17S", "BUSHMASTER"], "FAL": ["FAL"],
    "Galil": ["GALIL"], "KH-2002": ["KH2002"], "M4": ["M4"], "T-21": ["T21"],
    "AMP-9": ["AMP9"], "MP5": ["MP5"], "PP-19 Vityaz": ["PP19", "VITYAZ", "ПП19", "ВИТЯЗЬ"], "Super-45": ["SUPER45"],
    "M500": ["M500"], "MP43": ["MP43"], "M249 SAW": ["M249SAW"], "PKM": ["PKM", "ПКМ"],
    "BMR-308": ["BMR308"], "SKS": ["SKS", "СКС"], "SVD": ["SVD", "СВД", "СГД"],
    "AMR 50": ["AMR50", "АМР50"], "MK22": ["MK22"], "Mosin Nagant": ["MOSIN", "NAGANT", "МОСИНА", "МОСІНА"],
    "SV98": ["SV98", "СВ98"],
    "Deagle": ["DEAGLE"], "GGX 17": ["GGX17"], "GGX 18": ["GGX18"], "M1911": ["M1911"],
    "9K333 Verba": ["9K333", "VERBA", "ВЕРБА"], "MAAWS": ["MAAWS"], "MGL-40": ["MGL40"], "RPG-7": ["RPG7", "РПГ7"],
    "M67 Frag Grenade": ["M67"], "C4 Charge": ["C4"], "Claymore": ["CLAYMORE"], "L81 Mortar": ["L81"],
    "Talon 9K-SAM": ["9KSAM", "TALON"], "Vanguard CIWS": ["CIWS", "VANGUARD"], "Stingray": ["STINGRAY"],
    "M134D Minigun": ["M134D", "M134"], "M249 Machine Gun": ["M249"], "2A42 Autocannon": ["2A42"],
    "B-13 Rocket Pods": ["B13"], "L55A1 Cannon": ["L55A1"], "MG3A1 Coaxial Gun": ["MG3A1"], "L52 Cannon": ["L52"],
    # not weapons (both M18 grenades are smoke or signal: either way the gun is put away)
    "M18 Smoke Grenade": ["M18"],
}

# Translated display names, as the game shows them in its 13 other interface languages (de fr es
# it pt-BR pl tr ru uk ja ko zh-Hans zh-Hant): {English name: [names]}. From wardo.gs, read from the
# game files (build CL-501228, 14 Sep 2026), cross-checked against players' own guides (Bilibili
# wiki, gamerch, wikiwiki.jp, quest-knight.de, esports.ru). A name the same in every language needs
# no entry; the model designations (CODES) cover the guns. Vehicle weapons are English only until a
# translated one is seen in the game.
ALIASES: dict[str, list[str]] = {
    "Compound Bow": ["Compoundbogen", "Arc à poulies", "Arco compuesto", "Arco Compound", "Arco Composto",
                     "Łuk bloczkowy", "Bileşik Yay", "Блочный лук", "Блочний лук", "コンパウンドボウ",
                     "컴파운드 보우", "复合弓", "複合弓"],
    "Scout Rifle TD": ["Scout-Gewehr TD", "Fusil Scout TD", "Rifle Scout TD", "Fucile Scout TD", "Fuzil Scout TD",
                       "Karabin Scout TD", "Gözcü Tüfeği TD", "Винтовка Scout Rifle TD", "Гвинтівка розвідника TD",
                       "スカウトライフルTD", "정찰소총 TD", "TD 侦察步枪", "TD 侦查步枪", "偵察步槍 TD"],
    "Mosin Nagant": ["Винтовка Мосина", "Гвинтівка Мосіна", "모신나강", "莫辛步枪", "莫辛-納甘步槍"],
    "Deagle": ["Desert Eagle", "데저트 이글"],
    "Judge": ["저지", "法官左輪"],
    "Galil": ["갈릴"],
    "Super-45": ["슈퍼-45"],
    "PP-19 Vityaz": ["ПП-19 «Витязь»", "ПП-19 Витязь"],
    "9K333 Verba": ["9K333 «Верба»", "9K333 Верба"],
    "RPG-7": ["Lance-roquettes RPG-7", "РПГ-7"],
    "M67 Frag Grenade": ["M67 Splittergranate", "Grenade à fragmentation M67", "Granada de fragmentación M67",
                         "Granata a frammentazione M67", "Granada de Fragmentação M67", "Granat odłamkowy M67",
                         "M67 Parça Tesirli El Bombası", "Осколочная граната М67", "Осколкова граната M67",
                         "M67フラググレネード", "M67 세열수류탄", "M67 破片手榴弹", "M67 破片手榴彈"],
    "C4 Charge": ["C4-Ladung", "Charge de C4", "Carga de C4", "Cartuccia C4", "Ładunek C4", "C4 Yükü", "Заряд C4",
                  "C4爆薬", "C4 폭약", "C4 炸药包", "C4 炸藥"],
    "Remote Detonator": ["Fernzünder", "Détonateur à distance", "Detonador a distancia", "Detonatore a distanza",
                         "Detonador Remoto", "Zdalny detonator", "Uzaktan Kumandalı Fünye", "Дистанционный детонатор",
                         "Дистанційний детонатор", "遠隔起爆装置", "원격격발기", "遥控引爆器", "遙控引爆器"],
    "Improvised Explosive Device": ["Improvisierter Sprengsatz", "Engin explosif improvisé", "Explosivo improvisado",
                                    "Ordigno esplosivo improvvisato", "Improwizowana bomba", "El Yapımı Patlayıcı",
                                    "Самодельная бомба", "Саморобна вибухівка", "即席爆発装置", "급조폭발물",
                                    "简易爆炸装置", "簡易爆炸裝置"],
    "AT Mine": ["PA-Mine", "Mine antichar", "Mina AT", "Mina anticarro", "Mina przeciwpancerna", "AT Mayını",
                "ПТ-мина", "ПТ міна", "対戦車地雷", "대전차지뢰", "反坦克地雷"],
    "Claymore": ["Claymore-Mine", "Mina Claymore", "Anti Personel Mayını", "Клеймор", "クレイモア", "클레이모어",
                 "阔剑地雷", "闊刀地雷"],
    "Small Hammer": ["Kleiner Hammer", "Petit marteau", "Martillo pequeño", "Martello piccolo", "Martelo Pequeno",
                     "Mały młotek", "Küçük Çekiç", "Малый молоток", "Малий молоток", "小型ハンマー", "소형 망치",
                     "小号锤子", "小型鎚"],
    "Medium Hammer": ["Mittlerer Hammer", "Marteau moyen", "Martillo mediano", "Martello medio", "Martelo Médio",
                      "Średni młotek", "Orta Boy Çekiç", "Средний молоток", "Середній молоток", "中型ハンマー",
                      "중형 망치", "中号锤子", "中型鎚"],
    "Large Hammer": ["Grosser Hammer", "Großer Hammer", "Grand marteau", "Martillo grande", "Martello grande",
                     "Martelo Grande", "Duży młotek", "Büyük Çekiç", "Большой молоток", "Великий молоток",
                     "大型ハンマー", "대형 망치", "大号锤子", "大型鎚"],
    "L81 Mortar": ["L81 Mörser", "Mortier L81", "Mortero L81", "Mortaio L81", "Morteiro L81", "Moździerz L81",
                   "L81 Havan", "Миномет L81", "Міномет L81", "L81迫撃砲", "L81 박격포", "L81迫击炮", "L81 迫擊砲"],
    "Talon 9K-SAM": ["タロン 9K-SAM"],
    "Vanguard CIWS": ["CIWS Vanguard", "뱅가드 CIWS", "ヴァンガード CIWS", "Vanguard 近迫武器系統"],
    "Stingray": ["Stringray", "Стингрей", "スティングレイ", "스팅레이", "魔鬼鱼弹", "刺魟無人機"],
    # not weapons: seeing one means the gun is put away
    "Emergency Resuscitator": ["Notfall-Wiederbelebungsgerät", "Réanimateur d'urgence", "Resucitador de emergencia",
                               "Rianimatore d'emergenza", "Reanimador de Emergência", "Resuscytator awaryjny",
                               "Acil Durum Canlandırma Cihazı", "Экстренный реаниматор",
                               "Екстрений реанімаційний апарат", "緊急レサシテーター", "응급 소생기", "紧急复苏仪",
                               "緊急復甦器"],
    "M18 Smoke Grenade": ["M18 Rauchgranate", "Grenade fumigène M18", "Granada de humo M18", "Granata fumogena M18",
                          "Granada de Fumaça M18", "Granat dymny M18", "M18 Sis Bombası", "Дымовая граната М18",
                          "Димова граната M18", "M18スモークグレネード", "M18 연막수류탄", "M18 烟雾弹", "M18 煙霧彈"],
    "M18 Signal Grenade": ["M18 Signalgranate", "Grenade de signalisation M18", "Granada de señal M18",
                           "Granata di segnalazione M18", "Granada de Sinalização M18", "Granat sygnalizacyjny M18",
                           "M18 Sinyal Bombası", "Сигнальная граната М18", "Сигнальна граната M18",
                           "M18シグナルグレネード", "M18 신호수류탄", "M18 信号手榴弹", "M18 信號彈"],
    "Halligan Bar": ["Halligan-Tool", "Barre Halligan", "Barra Halligan", "Narzędzie Halligan", "Holigan Aleti",
                     "Лом Халлигана", "Лом Halligan", "ハリガンツール", "쇠지렛대", "哈利根铁铤", "三叉撬棒"],
    "Wrench": ["Schraubenschlüssel", "Clé", "Llave inglesa", "Chiave inglese", "Chave Inglesa", "Klucz",
               "İngiliz Anahtarı", "Гаечный ключ", "Гайковий ключ", "レンチ", "렌치", "扳手"],
    "Light Drill": ["Leichter Bohrer", "Perceuse légère", "Taladro ligero", "Perforatrice leggera", "Broca Leve",
                    "Lekka wiertarka", "Hafif Matkap", "Легкая дрель", "Легкий дриль", "軽量ドリル", "경량형 드릴",
                    "轻型钻机", "輕型電鑽"],
    "Heavy Drill": ["Schwerer Bohrer", "Perceuse lourde", "Taladro pesado", "Perforatrice pesante", "Broca Pesada",
                    "Ciężka wiertarka", "Ağır Matkap", "Тяжелая дрель", "Важкий дриль", "ヘビードリル", "대형 드릴",
                    "重型钻机", "重型電鑽"],
    "Loudspeaker": ["Lautsprecher", "Haut-parleur", "Altavoz", "Altoparlante", "Alto-falante", "Głośnik", "Hoparlör",
                    "Громкоговоритель", "Гучномовець", "拡声器", "확성 스피커", "广播喇叭", "喇叭"],
}

# Vehicles by their game names (wardogs.zone / MetaForge, build CL501228), with the kill-feed icon
# class they show as. If the HUD plate ever names one, a kill the feed shows as that vehicle's is
# named after it; otherwise it keeps the class ("Helicopter", "Tank", "Vehicle").
VEHICLES: list[tuple[str, str]] = [
    ("MH-6", "heli"), ("AH-6M [Miniguns]", "heli"), ("AH-6R [Rockets]", "heli"), ("Havoc", "heli"),
    ("Z20 Lakota", "heli"), ("Z20 Lakota [Miniguns]", "heli"),
    ("L2A6", "tank"), ("Flakpanzer Gepard", "tank"), ("M113 APC SV", "tank"), ("SPH-2", "artillery"),
    ("Bobcat", "car"), ("Dune Buggy", "car"), ("Humvee", "car"), ("Humvee [M249]", "car"), ("Humvee [Minigun]", "car"),
    ("Kodiak", "car"), ("Kodiak [M249]", "car"), ("Kodiak [Pickup]", "car"), ("URAL", "car"),
    ("Ural Defender", "car"), ("Ural Defender [M249]", "car"),
]
VEHICLE_CLASS = {n: c for n, c in VEHICLES}

# What a kill-feed icon class is called when nothing names the exact weapon or vehicle, and what
# kind of thing made the kill. Guns without a name stay blank: a gun is never guessed.
CLASS_LABEL = {"grenade": "Grenade", "c4": "C4", "rpg": "Rocket", "mortar": "Mortar", "artillery": "Artillery",
               "heli": "Helicopter", "tank": "Tank", "car": "Vehicle", "hammer": "Hammer", "explosion": "Explosion"}
CLASS_TYPE = {"rifle": "gun", "lmg": "gun", "shotgun": "gun", "sniper": "gun", "boltgun": "gun", "hunting": "gun",
              "pistol": "gun", "grenade": "grenade", "c4": "explosive", "rpg": "launcher", "mortar": "mortar",
              "artillery": "artillery", "heli": "vehicle", "tank": "vehicle", "car": "vehicle", "hammer": "melee",
              "explosion": "explosive"}
CATEGORY_TYPE = {"assault rifle": "gun", "SMG": "gun", "shotgun": "gun", "LMG": "gun", "marksman rifle": "gun",
                 "sniper rifle": "gun", "pistol": "gun", "bow": "bow", "launcher": "launcher", "grenade": "grenade",
                 "explosive": "explosive", "melee": "melee", "tool": "melee", "emplacement": "emplacement",
                 "vehicle weapon": "vehicle weapon"}


def weapon_type(name: str) -> str:
    """gun | grenade | explosive | launcher | mortar | bow | melee | emplacement | vehicle weapon | vehicle"""
    if name in VEHICLE_CLASS:
        return "vehicle"
    return CATEGORY_TYPE.get(category_of(name), "")


# what can be learned from a kill-feed icon: things you hold (and the mortar you fire)
LEARNABLE = {"assault rifle", "SMG", "shotgun", "LMG", "marksman rifle", "sniper rifle", "pistol", "launcher",
             "bow", "grenade", "explosive", "melee", "tool", "emplacement"}


def slug(name: str) -> str:
    """'PP-19 Vityaz' -> 'pp19vityaz': file names and tags."""
    return re.sub(r"[^a-z0-9]", "", name.lower())


BY_SLUG = {slug(n): (n, cat, cls) for n, cat, cls in WEAPONS}


_FOLD = str.maketrans({"1": "I", "0": "O", "L": "I"})
# Cyrillic capitals that look like Latin ones: Russian writes М4, АК-74, МК22 with them, and OCR
# returns whichever alphabet it likes. Folded to one form on both sides of every comparison
_LOOKALIKE = str.maketrans({"А": "A", "В": "B", "Е": "E", "К": "K", "М": "M", "Н": "H", "О": "O", "Р": "P",
                            "С": "C", "Т": "T", "Х": "X", "У": "Y", "І": "I", "Ł": "L", "Ø": "O", "İ": "I",
                            "ı": "I", "Ё": "E"})


def _norm(s: str) -> str:
    """Upper case, accents off, full-width forms folded, Cyrillic look-alikes made Latin, and only
    letters and digits kept - of any script, so 复合弓 and Блочный лук survive."""
    s = unicodedata.normalize("NFKC", s).upper().translate(_LOOKALIKE)
    s = "".join(ch for ch in unicodedata.normalize("NFKD", s) if not unicodedata.combining(ch))
    return re.sub(r"[\W_]", "", s.translate(_LOOKALIKE))


def _score_names(t: str, names, first: str | None = None) -> list[tuple[float, str]]:
    """`t`: the line from some word on, normalised; `first`: that word on its own, normalised."""
    scored = []
    for n in names:
        c = _norm(n)
        head = t[:len(c) + 1]
        # OCR swaps I/1 and O/0: compare with those folded together as well
        fc, ft = c.translate(_FOLD), t.translate(_FOLD)
        r = max(difflib.SequenceMatcher(None, c, head).ratio(),
                difflib.SequenceMatcher(None, c, t[:len(c)]).ratio(),
                difflib.SequenceMatcher(None, fc, ft[:len(fc)]).ratio())
        if len(c) <= 3:          # "M4", "FAL", "C4": short names only as a word of their own
            lead = first if first is not None else t
            r = 1.0 if lead == c or (first is None and t.startswith(c) and
                                     (len(t) == len(c) or not t[len(c)].isalpha())) else 0.0
        scored.append((r, n))
    scored.sort(reverse=True)
    return scored


# calibres the plate shows after the name; OCR often drops the space before one ("M45.56" is the
# M4 in 5.56), so a known calibre at the end of a word is split off first
CALIBRES = ["5.56", "5.45", "7.62", "7.92", "9X19", "9X18", "9X39", "9MM", ".338", ".308", ".300", ".45", ".50",
            "12GA", "12G", "40MM", "4.6", "5.7", "5,56", "5,45", "7,62"]


def _split_calibre(word: str) -> list[str]:
    w = word.upper()
    for c in sorted(CALIBRES, key=len, reverse=True):
        if w.endswith(c) and len(w) > len(c):
            return [word[:-len(c)], word[-len(c):]]
    return [word]


def _weapon_names() -> list[str]:
    return [n for n, _, _ in WEAPONS]


def match_item(text: str) -> tuple[str | None, bool]:
    """(name, is_weapon) for an OCR'd plate line, or (None, False).

    Two ways in, whatever the game's language: the whole name (English, or a translation in
    ALIASES) starting at any word, since scenery can put junk in front of it; or the gun's model
    designation (CODES) as a word of its own. The calibre after it ("GALIL 5.56") is ignored, and
    the fit has to be clearly better than any other name."""
    words = [p for w in re.split(r"\s+", text.strip()) if w for p in _split_calibre(w)]
    weapons = set(_weapon_names())
    pool = [(n, n) for n in _weapon_names()] + [(n, n) for n in NOT_WEAPONS] + [(n, n) for n, _ in VEHICLES] + \
           [(a, n) for n, al in ALIASES.items() for a in al]
    best = (0.0, None)
    for i in range(len(words)):
        t = _norm(" ".join(words[i:]))
        if len(t) < 2:
            continue
        scores = []
        first = _norm(words[i])
        for alias, canon in pool:
            r = _score_names(t, [alias], first)[0][0]
            scores.append((r, canon))
        scores.sort(reverse=True)
        r, canon = scores[0]
        second = next((x for x, c in scores[1:] if c != canon), 0.0)
        if r >= 0.82 and r - second >= 0.06 and r > best[0]:
            best = (r, canon)
    if best[1]:
        return best[1], best[1] in weapons
    # a CJK name read with its last glyph wrong ("复合" and a stray mark for 复合弓): the rest of it
    # still names one item, provided no other name starts the same way
    t_all = _norm(text)
    if t_all and any(ord(ch) > 0x2E80 for ch in t_all):
        found = set()
        for alias, canon in pool:
            a = _norm(alias)
            if len(a) >= 3 and any(ord(ch) > 0x2E80 for ch in a) and a[:-1] in t_all:
                found.add(canon)
        if len(found) == 1:
            canon = found.pop()
            return canon, canon in weapons
    # the model designation on its own: a word, or two words run together ("GGX 17")
    toks = [_norm(w) for w in words]
    cands = set(toks) | {a + b for a, b in zip(toks, toks[1:])}
    folded = {c.translate(_FOLD) for c in cands}
    hits = []
    for canon, codes in CODES.items():
        for code in codes:
            code = _norm(code)
            if code in cands or (len(code) >= 5 and code.translate(_FOLD) in folded):
                hits.append((len(code), canon))
                break
    if not hits:
        return None, False
    hits.sort(reverse=True)
    if len(hits) > 1 and hits[0][0] == hits[1][0] and hits[0][1] != hits[1][1]:
        return None, False                       # two codes of the same length: no answer beats a wrong one
    canon = hits[0][1]
    return canon, canon in weapons


def match_name(text: str) -> str | None:
    """The weapon an OCR'd HUD line names, or None."""
    name, weapon = match_item(text)
    return name if weapon else None


# ---- the game's language ----------------------------------------------------------------------
# Latin-script languages (English, German, French, Spanish, Italian, Portuguese, Polish, Turkish)
# are read with the English model ClipHound ships: the plate's letters come out with the accents
# off, which is how the names are compared anyway. Cyrillic and CJK need their own model, fetched
# once, on first use, from Tesseract's own repository (a few MB each), not shipped in the installer.
PACKS = {"ru": "rus", "uk": "ukr", "ja": "jpn", "ko": "kor", "zh": "chi_sim", "zh-tw": "chi_tra"}
PACK_URL = "https://github.com/tesseract-ocr/tessdata_fast/raw/main/{}.traineddata"
_lang = ""
_pack_ready: dict[str, bool] = {}


def _packs_dir() -> str:
    import sys
    return os.path.join(os.path.dirname(os.path.abspath(sys.argv[0])), "models", "tessdata")


def set_language(lang: str | None):
    """The game's interface language, from the plugin ("" when not known yet)."""
    global _lang
    lang = (lang or "").strip().lower()
    if lang == _lang:
        return
    _lang = lang
    print(f"[weapons] game language: {lang or 'not known yet (English letters)'}")
    code = PACKS.get(lang)
    if code and not _pack_ready.get(code):
        threading.Thread(target=_fetch_pack, args=(code,), daemon=True, name="tessdata").start()


def _fetch_pack(code: str):
    d = _packs_dir()
    try:
        os.makedirs(d, exist_ok=True)
        # the model and English side by side: the plate mixes the two ("M67 破片手榴弹")
        src = os.path.join(os.environ.get("TESSDATA_PREFIX", ""), "eng.traineddata")
        eng = os.path.join(d, "eng.traineddata")
        if not os.path.exists(eng) and os.path.exists(src):
            import shutil
            shutil.copyfile(src, eng)
        path = os.path.join(d, f"{code}.traineddata")
        if not os.path.exists(path):
            print(f"[weapons] downloading the {code} reading model for weapon names (first time only)")
            import requests
            with requests.get(PACK_URL.format(code), stream=True, timeout=60) as r:
                r.raise_for_status()
                with open(path + ".part", "wb") as fh:
                    for chunk in r.iter_content(1 << 20):
                        fh.write(chunk)
            os.replace(path + ".part", path)
        _pack_ready[code] = os.path.exists(eng) and os.path.exists(path)
        print(f"[weapons] {code} reading model {'ready' if _pack_ready[code] else 'incomplete (English only)'}")
    except Exception as e:
        print(f"[weapons] could not get the {code} reading model ({e}); weapon names read in English letters")


def _prep(gray: np.ndarray, scale: float) -> np.ndarray:
    g = cv2.resize(gray, None, fx=scale, fy=scale, interpolation=cv2.INTER_CUBIC)
    _, th = cv2.threshold(g, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    # dark text on white with room round it: grey input read far worse for every script tried
    return cv2.copyMakeBorder(cv2.bitwise_not(th), 20, 20, 20, 20, cv2.BORDER_CONSTANT, value=255)


def read_plate(crop_bgr: np.ndarray) -> str:
    """The text on the item plate: light capitals on a dark plate."""
    if pytesseract is None or crop_bgr is None or crop_bgr.size == 0:
        return ""
    g = cv2.cvtColor(crop_bgr, cv2.COLOR_BGR2GRAY)
    scale = max(1.0, 40.0 / max(1, g.shape[0] // 3))   # the text line ~13 px at 1080p: bring it to ~40
    code = PACKS.get(_lang)
    try:
        if code and _pack_ready.get(code):
            # no letter list: it would shut out the alphabet the name is in
            cfg = f'--tessdata-dir "{_packs_dir()}" --oem 1 --psm 6'
            out = pytesseract.image_to_string(_prep(g, scale), lang=f"{code}+eng", config=cfg)
            if code in ("jpn", "kor", "chi_sim", "chi_tra"):
                # a few dense glyphs: a second, larger read catches most of what the first misses
                # (36 of 39 test plates read with both, 33 with one)
                out += "\n" + pytesseract.image_to_string(_prep(g, scale * 1.25), lang=f"{code}+eng", config=cfg)
            return out
        cfg = "--psm 6 --oem 3 -c tessedit_char_whitelist=ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,-x "
        return pytesseract.image_to_string(_prep(g, scale), config=cfg)
    except Exception:
        return ""


class WeaponReader:
    """Knows what you are holding, from the HUD, all the time; saves and serves the learned
    kill-feed icons.

    The item plate is watched continuously in the background: a crop that has not changed is not
    read again, and at most one read a second is made, so it costs next to nothing between weapon
    switches. Every clear reading goes on a timeline, and a kill takes the last one before it. That
    way a kill is named even when the plate has faded by the time it happens: the game shows it
    when you switch, and what you switched to is what you are still holding."""

    def __init__(self, base_dir: str = "icons"):
        self._cv = threading.Condition()
        self._latest = None                        # the newest HUD crop, (crop, ts), not read yet
        self._thumb = None                         # the last crop that was read, small and grey
        self._last_read = 0.0
        self.timeline: deque = deque(maxlen=400)   # (ts, name); "" = the plate shows something that is not a weapon
        self.dir = os.path.join(base_dir, "learned")
        self.learned: dict[str, list[np.ndarray]] = {}
        # weapons found to draw the same kill-feed icon (the game gives some guns one damage type,
        # and so perhaps one icon): slug -> the other slugs it cannot be told from
        self.shared: dict[str, set[str]] = {}
        self._last_name = None
        self._clash: dict[str, int] = {}
        self.on_change = None                      # callable(name): the dock shows what you are holding
        self.load()
        threading.Thread(target=self._watch, daemon=True, name="hud-plate").start()

    # ---- the HUD ---------------------------------------------------------------------------
    def on_hud(self, crop: np.ndarray, ts: float):
        with self._cv:
            self._latest = (crop, ts)
            self._cv.notify()

    def _watch(self):
        while True:
            with self._cv:
                while self._latest is None:
                    self._cv.wait()
                crop, ts = self._latest
                self._latest = None
            try:
                g = cv2.cvtColor(crop, cv2.COLOR_BGR2GRAY)
                thumb = cv2.resize(g, (96, 12), interpolation=cv2.INTER_AREA).astype(np.int16)
                # changed = enough cells changed a lot: a plate appearing is a small part of the
                # crop, and an average over the whole of it hid that
                if self._thumb is not None and float((np.abs(thumb - self._thumb) > 25).mean()) < 0.01:
                    continue                       # the same plate (or the same nothing) as last time
                wait = 1.0 - (time.time() - self._last_read)
                if wait > 0:
                    time.sleep(wait)              # one read a second at most; the newest crop is read next
                self._thumb = thumb
                self._last_read = time.time()
                self._read(crop, ts)
            except Exception as e:
                print(f"[weapons] HUD read: {e}")

    def _read(self, crop, ts: float):
        text = read_plate(crop)
        lines = [l for l in text.splitlines() if l.strip()]
        if any(ord(ch) > 0x2E80 for ch in text):
            lines.append("".join(lines))       # CJK: a name split over two lines, joined back up
        for line in lines:
            name, weapon = match_item(line)
            if name:
                # a medkit or a tool: the gun is put away (""); a vehicle is kept by name
                self._note(ts, name if weapon or name in VEHICLE_CLASS else "")
                return
        # blank or unreadable says nothing about what is in your hands: keep the last reading

    def _note(self, ts: float, name: str):
        if self.timeline and self.timeline[-1][1] == name:
            return                                 # the same weapon: the entry keeps when it started
        self.timeline.append((ts, name))
        print(f"[weapons] holding: {name or '(no weapon)'}")
        if self.on_change:
            try:
                self.on_change(name)
            except Exception:
                pass

    def held_at(self, ts: float) -> str | None:
        """What you were holding at `ts` (epoch seconds): the last weapon the plate showed before
        then. None when the plate has not been read yet this session, or showed a non-weapon."""
        cutoff = ts + 0.2        # the plate can land a moment after the switch that preceded the kill
        name = None
        for t, n in reversed(self.timeline):
            if t <= cutoff:
                name = n
                break
        if name in VEHICLE_CLASS:
            return None          # in a vehicle: no gun in your hands
        return name or None

    def vehicle_at(self, ts: float, within: float = 180.0) -> str | None:
        """The vehicle the plate last named, if you have not been seen on foot with a gun since."""
        cutoff = ts + 0.2
        for t, n in reversed(self.timeline):
            if t > cutoff:
                continue
            if ts - t > within:
                return None
            if n in VEHICLE_CLASS:
                return n
            if n and weapon_type(n) not in ("vehicle weapon", ""):
                return None      # a gun in your hands after it: out of the vehicle
        return None

    def recent(self, ts: float, pred, within: float) -> str | None:
        """The most recent weapon the plate showed in the `within` seconds up to `ts` that `pred`
        accepts: the grenade you threw a moment ago, though the gun is back in your hands."""
        cutoff = ts + 0.2
        until = cutoff                             # when the entry after this one began
        for t, n in reversed(self.timeline):
            if t > cutoff:
                until = t
                continue
            if ts - until > within:
                break                              # put away longer ago than that
            if n and pred(n):
                return n
            until = t
        return None

    def current(self) -> str | None:
        return (self.timeline[-1][1] or None) if self.timeline else None

    # ---- learned icons -----------------------------------------------------------------------
    def _shared_path(self):
        return os.path.join(self.dir, "shared.json")

    def _purge_melee_once(self):
        """Before 0.26.0 a gun kill was credited to the fists whenever the plate showed them at that
        moment, and their "icon" was learned from rifle kills: those samples go, once."""
        mark = os.path.join(self.dir, ".melee-purged-0.26")
        if not os.path.isdir(self.dir) or os.path.exists(mark):
            return
        import shutil
        gone = []
        for name, cat, _ in WEAPONS:
            if cat in ("melee", "tool"):
                d = os.path.join(self.dir, slug(name))
                if os.path.isdir(d):
                    shutil.rmtree(d, ignore_errors=True)
                    gone.append(name)
        try:
            with open(self._shared_path(), encoding="utf-8") as f:
                sh = json.load(f)
            melee = {slug(n) for n, c, _ in WEAPONS if c in ("melee", "tool")}
            sh = {k: [x for x in v if x not in melee] for k, v in sh.items() if k not in melee}
            with open(self._shared_path(), "w", encoding="utf-8") as f:
                json.dump({k: v for k, v in sh.items() if v}, f, indent=1)
        except (OSError, ValueError):
            pass
        try:
            with open(mark, "w") as f:
                f.write("melee icons learned from gun kills removed\n")
        except OSError:
            pass
        if gone:
            print(f"[weapons] forgot the learned icons of {', '.join(gone)} (learned from gun kills before 0.26.0)")

    def load(self):
        self._purge_melee_once()
        self.learned = {}
        try:
            with open(self._shared_path(), encoding="utf-8") as f:
                self.shared = {k: set(v) for k, v in json.load(f).items() if k in BY_SLUG}
        except (OSError, ValueError):
            self.shared = {}
        if not os.path.isdir(self.dir):
            return
        for s in os.listdir(self.dir):
            d = os.path.join(self.dir, s)
            if s not in BY_SLUG or not os.path.isdir(d):
                continue
            ims = []
            for f in sorted(os.listdir(d)):
                if f.endswith(".png"):
                    m = cv2.imread(os.path.join(d, f), cv2.IMREAD_GRAYSCALE)
                    if m is not None:
                        ims.append(m)
            if ims:
                self.learned[s] = ims
        if self.learned:
            print(f"[weapons] learned icons: " +
                  ", ".join(f"{BY_SLUG[s][0]} x{len(v)}" for s, v in sorted(self.learned.items())))

    def identify(self, icon: np.ndarray | None) -> tuple[str | None, float]:
        """The learned weapon this kill-feed icon is, and how sure: (name, score) or (None, best)."""
        if icon is None or not self.learned:
            return None, 0.0
        ih, iw = icon.shape
        scores = {}
        for s, ims in self.learned.items():
            best = 0.0
            for t in ims:
                th, tw = t.shape
                sim = (min(tw, iw) / max(tw, iw)) * (min(th, ih) / max(th, ih))
                if sim < 0.45:
                    continue
                a, b = (icon, t) if (ih >= th and iw >= tw) else (t, icon)
                if b.shape[0] > a.shape[0] or b.shape[1] > a.shape[1]:
                    pad = np.zeros((max(a.shape[0], b.shape[0]), max(a.shape[1], b.shape[1])), np.uint8)
                    pad[:a.shape[0], :a.shape[1]] = a
                    a = pad
                v = float(cv2.matchTemplate(a, b, cv2.TM_CCOEFF_NORMED).max()) * (sim ** 0.5)
                best = max(best, v)
            scores[s] = best
        ranked = sorted(scores.items(), key=lambda kv: -kv[1])
        if not ranked:
            return None, 0.0
        s, v = ranked[0]
        partners = self.shared.get(s, set())
        second = next((sc for k, sc in ranked[1:] if k not in partners), 0.0)
        if v >= LEARNED_MATCH and v - second >= 0.03:
            if partners:
                # the same icon as another weapon: say both rather than pick one
                names = sorted([BY_SLUG[s][0]] + [BY_SLUG[p][0] for p in partners if p in BY_SLUG])
                return " or ".join(names), v
            return BY_SLUG[s][0], v
        return None, v

    def learn(self, name: str, icon: np.ndarray | None) -> bool:
        """Keep this kill-feed icon as `name`'s, unless it adds nothing or clearly is another
        weapon we already know (the HUD can change a moment before the row shows)."""
        if icon is None or icon.size == 0 or name not in (n for n, _, _ in WEAPONS):
            return False
        if category_of(name) not in LEARNABLE:
            return False
        s = slug(name)
        other, v = self.identify(icon)
        if other and name not in other.split(" or ") and v >= 0.92:
            # the HUD says one weapon and the icon is, near enough exactly, another we learned:
            # either you switched just before the kill, or the two share an icon. Twice for the
            # same pair and they are taken to share it
            o = slug(other.split(" or ")[0])
            key = "|".join(sorted([s, o]))
            self._clash[key] = self._clash.get(key, 0) + 1
            if self._clash[key] >= 2 and o not in self.shared.get(s, set()):
                self.shared.setdefault(s, set()).add(o)
                self.shared.setdefault(o, set()).add(s)
                try:
                    os.makedirs(self.dir, exist_ok=True)
                    with open(self._shared_path(), "w", encoding="utf-8") as f:
                        json.dump({k: sorted(v) for k, v in self.shared.items()}, f, indent=1)
                except OSError:
                    pass
                print(f"[weapons] {name} and {BY_SLUG[o][0]} draw the same kill-feed icon: named as both")
            else:
                print(f"[weapons] not learning {name}: the icon is {other} ({v:.2f})")
            return False
        have = self.learned.get(s, [])
        if len(have) >= LEARN_MAX:
            return False
        for t in have:
            if t.shape == icon.shape and float(cv2.matchTemplate(icon, t, cv2.TM_CCOEFF_NORMED).max()) >= LEARN_DUP:
                return False
        d = os.path.join(self.dir, s)
        try:
            os.makedirs(d, exist_ok=True)
            cv2.imwrite(os.path.join(d, f"{time.strftime('%Y%m%d-%H%M%S')}_{len(have)}.png"), icon)
        except OSError:
            return False
        self.learned.setdefault(s, []).append(icon)
        print(f"[weapons] learned {name}'s kill-feed icon ({len(self.learned[s])} sample(s))")
        return True

    def summary(self) -> dict:
        """For the plugin's log and a future Settings view: what is known, by name."""
        return {BY_SLUG[s][0]: len(v) for s, v in self.learned.items()}


def category_of(name: str) -> str:
    return BY_SLUG.get(slug(name), (name, "", ""))[1]


def class_of(name: str) -> str:
    return BY_SLUG.get(slug(name), (name, "", ""))[2]


if __name__ == "__main__":
    for t in ["GALIL 5.56", "GAL1L 5.56", "M4 5.56", "M249 SAW", "PP-19 VITYAZ 9X19", "EMERGENCY RESUSCITATOR",
              "MOSIN NAGANT 7.62", "SCOUT RIFLE TD", "FAL 7.62", "DEAGLE .50", "RPG-7", "AK74 5.45", "SMALL HAMMER",
              "9K333 VERBA", "M67 FRAG GRENADE", "M249 MACHINE GUN", "C4 CHARGE", "M4A1", "SKS 7.62", "SVD 7.62",
              "GGX 17 9MM", "GGX 18", "MK22 .338", "L81 MORTAR", "FISTS", "M134D MINIGUN", "A-91 5.56", "T-21"]:
        print(f"{t!r:28} -> {match_name(t)}")
