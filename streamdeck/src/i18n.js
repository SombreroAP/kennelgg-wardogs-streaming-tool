// Key titles in the OBS plugin's language (the state message carries it as "lang"), else Stream Deck's
// own. The table is generated from the plugin's translations (i18n-table.js); anything missing is English.
import { TABLE } from "./i18n-table.js";

let lang = "en";
const SD_TO_PLUGIN = { zh_CN: "zh", zh_TW: "zh-tw", de: "de", es: "es", fr: "fr", ja: "ja", ko: "ko", en: "en" };

export function setLanguage(code) {
	if (!code) return;
	const c = SD_TO_PLUGIN[code] || String(code).toLowerCase();
	lang = TABLE[c] ? c : "en";
}
export function language() { return lang; }
export function t(s) {
	const d = TABLE[lang];
	return (d && d[s]) || s;
}
