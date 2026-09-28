import streamDeck, { SingletonAction } from "@elgato/streamdeck";
import { bridge, setLogger } from "./bridge.js";
import { t, setLanguage } from "./i18n.js";
setLogger(streamDeck.logger);

/** Keys that mirror a piece of state: title and on/off image follow it. */
class KennelAction extends SingletonAction {
	constructor() {
		super();
		this.unsub = null;
	}
	onWillAppear(ev) {
		if (!this.unsub) this.unsub = bridge.onChange(() => this.refreshAll());
		this.refresh(ev.action, bridge.state);
	}
	refreshAll() {
		for (const a of this.actions) this.refresh(a, bridge.state);
	}
	async refresh(a, state) {
		try {
			if (!state) {
				await a.setTitle(t("OBS?"));
				if (a.setState) await a.setState(0);
				return;
			}
			await this.paint(a, state);
		} catch (e) {
			streamDeck.logger.error(`refresh: ${e}`);
		}
	}
	async paint(a, state) {}
	async onKeyDown(ev) {
		if (!bridge.connected) {
			await ev.action.showAlert();
			return;
		}
		if (!(await this.press(ev))) await ev.action.showAlert();
	}
	async press(ev) { return false; }
}

// ---- the first keys (0.18.4) ------------------------------------------------------------------
class PovAction extends KennelAction {
	manifestId = "com.kennelgg.wardogs.pov";
	target(a, settings) {
		return (settings && settings.who) || "auto"; // "auto" (the live one) or a squad mate's name
	}
	async paint(a, state) {
		const who = this.target(a, await a.getSettings());
		let f = null;
		if (who === "auto")
			f = state.friends.find((x) => x.live) || state.friends.find((x) => x.index === state.activeIndex) || null;
		else
			f = state.friends.find((x) => x.name.toLowerCase() === who.toLowerCase()) || null;
		const shown = state.applied && f && f.index === state.activeIndex;
		let title = f ? f.name : (who === "auto" ? t("nobody") : who);
		if (f && f.live) title += " ●";
		if (who === "auto" && !f) title = t("no squad");
		await a.setTitle(title.length > 12 ? title.slice(0, 11) + "…" : title);
		await a.setState(shown ? 1 : 0);
	}
	async press(ev) {
		const who = this.target(ev.action, await ev.action.getSettings());
		return bridge.control("show", { name: who === "auto" ? "auto" : who });
	}
	async onSendToPlugin(ev) {
		if (ev.payload && ev.payload.event === "squad")
			await streamDeck.ui.current?.sendToPropertyInspector({
				event: "squad", friends: bridge.state ? bridge.state.friends : [], connected: bridge.connected,
			});
	}
}

class CycleAction extends KennelAction {
	manifestId = "com.kennelgg.wardogs.cycle";
	async paint(a, state) {
		const n = state.friends.filter((f) => !f.off).length;
		await a.setTitle(n ? t("next\n%1 up").replace("%1", n) : t("no squad"));
	}
	async press() { return bridge.control("cycle"); }
}

/** A key that sends one command and shows a fixed title (and an OK tick when asked). */
function simple(id, cmd, title, { ok = false, extra = null, lit = null, litTitle = null } = {}) {
	return class extends KennelAction {
		manifestId = `com.kennelgg.wardogs.${id}`;
		async paint(a, state) {
			const on = lit ? !!lit(state) : false;
			await a.setTitle(on && litTitle ? t(litTitle) : t(title));
			if (lit) await a.setState(on ? 1 : 0);
		}
		async press(ev) {
			const args = extra ? extra(await ev.action.getSettings()) : undefined;
			const sent = bridge.control(cmd, args);
			if (sent && ok) await ev.action.showOk();
			return sent;
		}
	};
}

/** An on/off key: lit while it is on, the title says which. */
function toggle(id, cmd, word, isOn) {
	return class extends KennelAction {
		manifestId = `com.kennelgg.wardogs.${id}`;
		async paint(a, state) {
			const on = !!isOn(state);
			await a.setTitle(t(word) + "\n" + (on ? t("on") : t("off")));
			await a.setState(on ? 1 : 0);
		}
		async press() { return bridge.control(cmd); }
	};
}

const ClipAction = simple("clip", "clip", "clip", { ok: true });
const ReplayAction = simple("replay", "replay", "replay", { lit: (s) => s.replayPlaying, litTitle: "playing" });
const ClipReplayAction = simple("clipreplay", "clip_replay", "clip +\nreplay", { ok: true });
const VoiceAction = toggle("voice", "voice_toggle", "voice", (s) => s.voice);
const DualAction = toggle("dual", "dual_toggle", "dual", (s) => s.dual);

class MeAction extends KennelAction {
	manifestId = "com.kennelgg.wardogs.me";
	async paint(a, state) { await a.setTitle(state.applied ? t("back\nto me") : t("my POV")); }
	async press() { return bridge.control("me"); }
}

// ---- session stats (0.31.0) -------------------------------------------------------------------
const SessionAction = toggle("session", "session_overlay_toggle", "stats", (s) => s.session && s.session.overlay && s.session.overlay.on);

// what a stat key can show: the session object's field and how to write it
const money = (n) => (n < 0 ? "-$" : "$") + Math.abs(Math.round(n || 0)).toLocaleString("en-US");
const signed = (n) => (Math.round(n || 0) > 0 ? "+" : "") + money(n);
const STATS = {
	net: ["Session balance", (s) => signed(s.net)],
	kda: ["K / D / A", (s) => s.kdaText || "0 / 0 / 0"],
	kills: ["Kills", (s) => String(s.kills || 0)],
	deaths: ["Deaths", (s) => String(s.deaths || 0)],
	kd: ["K/D", (s) => (s.kd || 0).toFixed(s.kd >= 10 ? 0 : 1)],
	revives: ["Revives", (s) => String(s.revives || 0)],
	heals: ["Heals", (s) => String(s.heals || 0)],
	spots: ["Spots", (s) => String(s.spots || 0)],
	supplies: ["Supplies", (s) => String(s.supplies || 0)],
	builds: ["Built", (s) => String(s.builds || 0)],
	transports: ["Passengers", (s) => String(s.transports || 0)],
	headshots: ["Headshots", (s) => String(s.headshots || 0)],
	permin: ["$ / min", (s) => s.perMinText || "-"],
	earned: ["Earned", (s) => money(s.earned)],
	spent: ["Spent", (s) => money(s.spent)],
	medicCash: ["Medic $", (s) => money(s.medicCash)],
	reconCash: ["Recon $", (s) => money(s.reconCash)],
	logisticsCash: ["Logistics $", (s) => money(s.logisticsCash)],
	buildCash: ["Builder $", (s) => money(s.buildCash)],
	transportCash: ["Transport $", (s) => money(s.transportCash)],
	objectiveCash: ["Objective $", (s) => money(s.objectiveCash)],
	combatCash: ["Combat $", (s) => money(s.combatCash)],
};
const esc = (s) => String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
/** The key drawn as an image: the stat's name small on top, its value large, coloured by sign. */
function statImage(label, value, colour) {
	const size = value.length > 7 ? 30 : value.length > 5 ? 38 : 48;
	const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 144 144" width="144" height="144">` +
		`<rect width="144" height="144" rx="18" fill="#1b1d1a"/><rect x="0" y="0" width="6" height="144" fill="#c99a3b"/>` +
		`<text x="72" y="44" text-anchor="middle" font-family="Arial, sans-serif" font-size="17" font-weight="700" fill="#9a978f">${esc(label.toUpperCase())}</text>` +
		`<text x="72" y="98" text-anchor="middle" font-family="Arial, sans-serif" font-size="${size}" font-weight="700" fill="${colour}">${esc(value)}</text></svg>`;
	return "data:image/svg+xml;base64," + Buffer.from(svg).toString("base64");
}
class StatAction extends KennelAction {
	manifestId = "com.kennelgg.wardogs.stat";
	async paint(a, state) {
		const settings = await a.getSettings();
		const id = STATS[settings.stat] ? settings.stat : "net";
		const s = state.session || {};
		const value = STATS[id][1](s);
		let colour = "#ece7db";
		if (id === "net") colour = (s.net || 0) > 0 ? "#5fd07a" : (s.net || 0) < 0 ? "#ef5a4c" : "#ece7db";
		else if (id.endsWith("Cash") || id === "earned" || id === "permin") colour = "#a9b86e";
		else if (id === "spent") colour = "#e07a6a";
		await a.setTitle("");
		await a.setImage(statImage(t(STATS[id][0]), value, colour));
	}
	async press(ev) { await ev.action.showOk(); return true; } // a display: nothing to do
}

// the bar's ready-made choices, as the OBS plugin names them (Session::presets)
const ROLES = ["fragger", "medic", "recon", "logistics", "builder", "driver", "objective", "all-round"];
const ROLE_NAMES = { fragger: "Fragger", medic: "Medic", recon: "Recon", logistics: "Logistics", builder: "Builder",
	driver: "Driver", objective: "Objective", "all-round": "All-round", custom: "custom" };
class RoleAction extends KennelAction {
	manifestId = "com.kennelgg.wardogs.role";
	async paint(a, state) {
		const settings = await a.getSettings();
		const want = settings.role || "cycle";
		const cur = state.role || "custom";
		if (want === "cycle") {
			await a.setTitle(t("role") + "\n" + t(ROLE_NAMES[cur] || cur));
			await a.setState(0);
		} else {
			await a.setTitle(t(ROLE_NAMES[want] || want));
			await a.setState(cur === want ? 1 : 0);
		}
	}
	async press(ev) {
		const settings = await ev.action.getSettings();
		const want = settings.role || "cycle";
		if (want !== "cycle") return bridge.control("role", { role: want });
		const cur = (bridge.state && bridge.state.role) || "custom";
		const next = ROLES[(ROLES.indexOf(cur) + 1) % ROLES.length];
		return bridge.control("role", { role: next });
	}
}

/** Reset needs holding: a stray tap mid-stream must not throw the session away. */
class ResetAction extends KennelAction {
	manifestId = "com.kennelgg.wardogs.reset";
	async paint(a) { await a.setTitle(t("reset\n(hold)")); }
	async onKeyDown(ev) { this.down = Date.now(); }
	async onKeyUp(ev) {
		if (!bridge.connected) return ev.action.showAlert();
		if (Date.now() - (this.down || 0) < 800) return ev.action.showAlert();
		if (bridge.control("session_reset")) await ev.action.showOk();
		else await ev.action.showAlert();
	}
}

const StatsImageAction = simple("statsimage", "stats_image", "stats\nimage", { ok: true });
class ClipTagAction extends KennelAction {
	manifestId = "com.kennelgg.wardogs.cliptag";
	async paint(a) {
		const tag = (await a.getSettings()).tag || "highlight";
		await a.setTitle(t("clip") + "\n" + t(tag));
	}
	async press(ev) {
		const tag = (await ev.action.getSettings()).tag || "highlight";
		const sent = bridge.control("clip_tag", { tag });
		if (sent) await ev.action.showOk();
		return sent;
	}
}
class ClosestAction extends KennelAction {
	manifestId = "com.kennelgg.wardogs.closest";
	async paint(a, state) {
		await a.setTitle(t("closest") + (state.closest ? "\n" + state.closest.slice(0, 10) : ""));
	}
	async press() { return bridge.control("closest"); }
}
const AutoAction = toggle("auto", "auto_toggle", "auto", (s) => s.enabled);
const MagPackAction = toggle("magpack", "invswitch_toggle", "mag pack", (s) => s.invSwitch);
const DualAutoAction = toggle("dualauto", "dualauto_toggle", "dual auto", (s) => s.dualAuto);
const StingerAction = toggle("stingers", "stingers_toggle", "stingers", (s) => s.stingers);
const NameTagAction = toggle("nametag", "nametag_toggle", "name tag", (s) => s.nameTag);
const PopoutsAction = toggle("popouts", "popouts_toggle", "pop-outs", (s) => s.popoutsShown);
const HighlightsAction = simple("highlights", "highlights", "highlights", { lit: (s) => s.highlightsBuilding, litTitle: "building" });
const SendLogsAction = simple("sendlogs", "send_logs", "send\nlogs", { ok: true });

// ---- Stream Deck + dials ----------------------------------------------------------------------
/** Squad dial: turn to pick a squad mate, press to show them (again: back to you). */
class SquadDial extends SingletonAction {
	manifestId = "com.kennelgg.wardogs.squaddial";
	onWillAppear(ev) {
		if (!this.unsub) this.unsub = bridge.onChange(() => { for (const a of this.actions) this.draw(a); });
		this.draw(ev.action);
	}
	async draw(a) {
		const s = bridge.state;
		if (!s) return a.setFeedback({ title: t("Squad"), value: t("OBS?") });
		const f = s.friends.find((x) => x.index === s.activeIndex);
		await a.setFeedback({ title: s.applied ? t("on stream") : t("Squad"),
			value: f ? f.name + (f.live ? " ●" : "") : t("no squad"),
			indicator: { value: s.applied ? 100 : 0 } });
	}
	async onDialRotate(ev) { bridge.control(ev.payload.ticks > 0 ? "cycle_pick" : "cycle_pick_prev"); }
	async onDialDown(ev) { bridge.control("show", { name: (bridge.state && bridge.state.active) || "auto" }); }
	async onTouchTap(ev) { bridge.control("me"); }
}
/** Whoosh dial: turn for the stingers' whoosh volume, press to mute it or bring it back. */
class WhooshDial extends SingletonAction {
	manifestId = "com.kennelgg.wardogs.whooshdial";
	onWillAppear(ev) {
		if (!this.unsub) this.unsub = bridge.onChange(() => { for (const a of this.actions) this.draw(a); });
		this.draw(ev.action);
	}
	async draw(a) {
		const s = bridge.state;
		const v = s && s.whooshOn ? s.whooshVolume : 0;
		await a.setFeedback({ title: t("Whoosh"), value: s ? (s.whooshOn ? `${v}%` : t("off")) : t("OBS?"), indicator: { value: v } });
	}
	async onDialRotate(ev) { bridge.control("whoosh_volume", { delta: ev.payload.ticks * 5 }); }
	async onDialDown() { bridge.control("whoosh_toggle"); }
}

for (const A of [PovAction, CycleAction, ClipAction, ReplayAction, ClipReplayAction, VoiceAction, DualAction, MeAction,
	SessionAction, StatAction, RoleAction, ResetAction, StatsImageAction, ClipTagAction, ClosestAction, AutoAction,
	MagPackAction, DualAutoAction, StingerAction, NameTagAction, PopoutsAction, HighlightsAction, SendLogsAction,
	SquadDial, WhooshDial])
	streamDeck.actions.registerAction(new A());

// the bridge port lives in the plugin's global settings (47820 unless the OBS plugin was changed)
streamDeck.settings.getGlobalSettings().then((g) => bridge.setPort(g && g.port)).catch(() => {});
streamDeck.settings.onDidReceiveGlobalSettings((ev) => bridge.setPort(ev.settings && ev.settings.port));

// the key titles follow the OBS plugin's language (Settings, General, Plugin language)
bridge.onChange((state) => { if (state && state.lang) setLanguage(state.lang); });
bridge.start();
streamDeck.connect().then(() => {
	try { setLanguage(streamDeck.info.application.language); } catch (e) {}
});
