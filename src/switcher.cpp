#include "switcher.h"
#include <util/platform.h>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

static std::string urlEncode(const std::string &s)
{
	static const char *hex = "0123456789ABCDEF";
	std::string o;
	for (unsigned char c : s) {
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
			o += (char)c;
		else {
			o += '%';
			o += hex[c >> 4];
			o += hex[c & 15];
		}
	}
	return o;
}

static std::string lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
	return s;
}

std::string Switcher::webUrl(const Friend &f)
{
	if (f.kind == FriendKind::VdoNinja)
		// the viewer asks for the friend's chosen quality; WebRTC settles lower by itself on a weak link
		return "https://vdo.ninja/?view=" + urlEncode(f.channel) +
		       "&solo&cleanoutput&autostart&noaudio=0&maxvideobitrate=" + std::to_string(f.vdoKbps) +
		       "&codec=" + f.vdoCodec + "&scale=100&buffer=0&height=" + std::to_string(f.vdoHeight) +
		       "&framerate=" + std::to_string(f.vdoFps);
	if (f.kind == FriendKind::Kick) {
		std::string ch = lower(f.channel);
		if (!ch.empty() && ch[0] == '@')
			ch.erase(0, 1);
		return "https://player.kick.com/" + urlEncode(ch) + "?autoplay=true&muted=false";
	}
	if (f.kind == FriendKind::YouTube) {
		// channel is a channel ID (UC...) - then the channel's current live stream - or a video ID
		std::string id = f.channel;
		if (id.rfind("UC", 0) == 0 && id.size() >= 20)
			return "https://www.youtube.com/embed/live_stream?channel=" + urlEncode(id) +
			       "&autoplay=1&mute=0";
		return "https://www.youtube.com/embed/" + urlEncode(id) + "?autoplay=1&mute=0";
	}
	std::string ch = lower(f.channel);
	if (!ch.empty() && ch[0] == '@')
		ch.erase(0, 1);
	return "https://player.twitch.tv/?channel=" + urlEncode(ch) + "&parent=twitch.tv&muted=false&autoplay=true";
}

std::string Switcher::vdoPushUrl(const Friend &f)
{
	// what the friend opens: share the game window/screen with system audio, no mic, at the chosen quality
	int w = f.vdoHeight * 16 / 9;
	return "https://vdo.ninja/?push=" + urlEncode(f.channel) +
	       "&screenshare&audiodevice=0&quality=0&stereo&maxvideobitrate=" + std::to_string(f.vdoKbps) +
	       "&height=" + std::to_string(f.vdoHeight) + "&width=" + std::to_string(w) +
	       "&framerate=" + std::to_string(f.vdoFps) + "&codec=" + f.vdoCodec + "&label=" + urlEncode(f.channel);
}

std::string Switcher::overlayUrl(const Config &cfg, const std::string &friendName)
{
	char *p = obs_module_file("overlay/overlay.html");
	std::string path = p ? p : "";
	bfree(p);
	std::replace(path.begin(), path.end(), '\\', '/');
	std::string q;
	if (cfg.lookName) {
		q += "name=" + urlEncode(friendName.empty() ? "friend" : friendName) +
		     "&label=" + urlEncode(cfg.lookLabel);
		if (cfg.lookPlate)
			q += "&plate=1";
		q += "&pos=" + urlEncode(cfg.lookPos.empty() ? "ml" : cfg.lookPos);
	}
	if (cfg.lookCam)
		q += "&cam=1";
	if (cfg.lookGrain)
		q += "&grain=" + std::to_string(std::clamp(cfg.grainAmount, 0, 100));
	if (cfg.lookVignette)
		q += "&vig=1";
	if (cfg.lookMark)
		q += "&mark=1";
	if (!q.empty() && q[0] == '&')
		q.erase(0, 1);
	return "file:///" + path + "?" + q;
}

std::vector<std::string> Switcher::sceneNames()
{
	std::vector<std::string> out;
	char **names = obs_frontend_get_scene_names();
	if (!names)
		return out;
	for (char **n = names; *n; n++)
		out.emplace_back(*n);
	bfree(names);
	return out;
}

std::vector<std::pair<std::string, std::string>> Switcher::inputs()
{
	std::vector<std::pair<std::string, std::string>> out;
	obs_enum_sources(
		[](void *data, obs_source_t *src) {
			auto *o = (std::vector<std::pair<std::string, std::string>> *)data;
			if (obs_source_get_type(src) == OBS_SOURCE_TYPE_INPUT)
				o->emplace_back(obs_source_get_name(src), obs_source_get_id(src));
			return true;
		},
		&out);
	std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return lower(a.first) < lower(b.first); });
	return out;
}

std::vector<std::pair<std::string, std::string>> Switcher::listProperty(const char *kind, const char *prop)
{
	// Cached for a few seconds: this creates a real source of that kind for a moment, and a second
	// window capture of the same window makes the first one flicker while it is up.
	static std::map<std::string, std::pair<uint64_t, std::vector<std::pair<std::string, std::string>>>> cache;
	std::string key = std::string(kind) + "/" + prop;
	uint64_t now = os_gettime_ns();
	auto c = cache.find(key);
	if (c != cache.end() && now - c->second.first < 5000000000ULL)
		return c->second.second;

	std::vector<std::pair<std::string, std::string>> out;
	obs_source_t *tmp = obs_source_create_private(kind, "kennel-probe", nullptr);
	if (!tmp)
		return out;
	obs_properties_t *props = obs_source_properties(tmp);
	obs_property_t *p = props ? obs_properties_get(props, prop) : nullptr;
	if (p && obs_property_get_type(p) == OBS_PROPERTY_LIST) {
		size_t n = obs_property_list_item_count(p);
		for (size_t i = 0; i < n; i++) {
			const char *name = obs_property_list_item_name(p, i),
				   *val = obs_property_list_item_string(p, i);
			if (name && val && *val)
				out.emplace_back(name, val);
		}
	}
	if (props)
		obs_properties_destroy(props);
	obs_source_release(tmp);
	cache[key] = {now, out};
	return out;
}

bool Switcher::kindAvailable(const char *kind)
{
	const char *id;
	for (size_t i = 0; obs_enum_input_types(i, &id); i++)
		if (strcmp(id, kind) == 0)
			return true;
	return false;
}

namespace {
/// True if every key we are about to write already holds that value. Updating a window capture
/// tears its capture down and starts it again - do that on every save and it flickers - and it is
/// pointless when nothing changed.
bool alreadySet(obs_source_t *src, obs_data_t *want)
{
	obs_data_t *have = obs_source_get_settings(src);
	if (!have)
		return false;
	bool same = true;
	for (obs_data_item_t *it = obs_data_first(want); it && same; obs_data_item_next(&it)) {
		const char *k = obs_data_item_get_name(it);
		switch (obs_data_item_gettype(it)) {
		case OBS_DATA_STRING: {
			const char *a = obs_data_item_get_string(it), *b = obs_data_get_string(have, k);
			same = a && b && strcmp(a, b) == 0;
			break;
		}
		case OBS_DATA_NUMBER:
			same = obs_data_item_numtype(it) == OBS_DATA_NUM_INT
				       ? obs_data_item_get_int(it) == obs_data_get_int(have, k)
				       : obs_data_item_get_double(it) == obs_data_get_double(have, k);
			break;
		case OBS_DATA_BOOLEAN:
			same = obs_data_item_get_bool(it) == obs_data_get_bool(have, k);
			break;
		default:
			same = false;
		}
	}
	obs_data_release(have);
	return same;
}
} // namespace

std::string Switcher::createInScene(const Config &cfg, const char *kind, const std::string &name, obs_data_t *settings,
				    bool fullCanvas, bool visible, bool toBottom)
{
	if (!kindAvailable(kind))
		return std::string("source type '") + kind + "' is not available in this OBS";
	obs_source_t *ss = sceneSource(cfg);
	if (!ss)
		return "no scene";
	obs_scene_t *scene = obs_scene_from_source(ss);
	obs_source_t *src = obs_get_source_by_name(name.c_str());
	if (src && obs_source_removed(src)) {
		// deleted a moment ago but something still holds it: it keeps the name, and a scene will
		// not take a removed source. Move the ghost aside and make a fresh one.
		obs_source_set_name(src, (name + " (gone)").c_str());
		obs_source_release(src);
		src = nullptr;
	}
	if (src) {
		if (settings && !alreadySet(src, settings))
			obs_source_update(src, settings);
	} else {
		src = obs_source_create(kind, name.c_str(), settings, nullptr);
		if (!src) {
			obs_source_release(ss);
			return "could not create '" + name + "' (OBS refused a " + kind + " source)";
		}
		if (log)
			log("Added '" + name + "' to OBS.");
	}
	obs_sceneitem_t *item = obs_scene_find_source(scene, name.c_str());
	bool fresh = !item;
	if (!item)
		item = obs_scene_add(scene, src);
	if (!item) {
		std::string sceneName = obs_source_get_name(ss) ? obs_source_get_name(ss) : "?";
		obs_source_release(src);
		obs_source_release(ss);
		return "could not add '" + name + "' to the scene '" + sceneName + "'";
	}
	if (item) {
		if (obs_sceneitem_visible(item) != visible)
			obs_sceneitem_set_visible(item, visible);
		// Only place it the first time. After that it is the streamer's to move, and re-applying
		// the full-canvas transform on every save snapped it back under them.
		if (fullCanvas && fresh) {
			struct obs_video_info ovi;
			obs_get_video_info(&ovi);
			struct vec2 pos = {0, 0}, bounds = {(float)ovi.base_width, (float)ovi.base_height};
			obs_sceneitem_set_pos(item, &pos);
			obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
			obs_sceneitem_set_bounds(item, &bounds);
		}
		if (toBottom && fresh)
			obs_sceneitem_set_order(item, OBS_ORDER_MOVE_BOTTOM);
	}
	obs_source_release(src);
	obs_source_release(ss);
	return item ? "" : "could not add '" + name + "' to the scene";
}

/// The sources this plugin made for one squad mate, taken out of every scene and deleted. Only ever
/// ours: a squad mate set up as "an OBS source you already have" keeps their source, and the shared
/// browser source everyone uses when feeds are not preloaded is left alone.
std::vector<std::string> Switcher::friendSourceNames(const Config &cfg, const Friend &f)
{
	std::vector<std::string> names;
	if (f.ownsSources()) { // Discord: we created these
		if (!f.source.empty())
			names.push_back(f.source);
		if (!f.baseSource.empty() && f.baseSource != f.source) // parked while a pop-out is bound
			names.push_back(f.baseSource);
		if (!f.audioSource.empty())
			names.push_back(f.audioSource);
	}
	if (f.isWeb()) {
		std::string web = cfg.webSourceFor(f);
		if (web != Config::webSourceName()) // the per-squad-mate one, not the shared one
			names.push_back(web);
	}
	return names;
}

/// Discord's sound is not handled since 0.10.3: it hands OBS one mix for the whole call. Take the
/// Application Audio Captures earlier builds made for Discord squad mates out of every scene and
/// delete them, once. Returns how many went.
int Switcher::removeDiscordAudio(Config &cfg)
{
	std::vector<std::string> names{Friend::discordCallAudioName()};
	for (auto &f : cfg.friends)
		if (f.kind == FriendKind::Discord && !f.audioSource.empty()) {
			if (std::find(names.begin(), names.end(), f.audioSource) == names.end())
				names.push_back(f.audioSource);
			f.audioSource.clear();
		}
	int gone = 0;
	for (const auto &name : names) {
		obs_source_t *src = obs_get_source_by_name(name.c_str());
		if (!src)
			continue;
		struct obs_frontend_source_list scenes = {};
		obs_frontend_get_scenes(&scenes);
		for (size_t i = 0; i < scenes.sources.num; i++) {
			obs_scene_t *scene = obs_scene_from_source(scenes.sources.array[i]);
			if (obs_sceneitem_t *it = scene ? obs_scene_find_source(scene, name.c_str()) : nullptr)
				obs_sceneitem_remove(it);
		}
		obs_frontend_source_list_free(&scenes);
		obs_source_remove(src);
		obs_source_release(src);
		gone++;
	}
	return gone;
}

int Switcher::removeFriendSources(const Config &cfg, const Friend &f)
{
	int gone = 0;
	for (const auto &name : friendSourceNames(cfg, f)) {
		// the shared Discord capture belongs to everyone watching the call; it goes with the last
		// of them, not the first
		int users = 0;
		for (const auto &g : cfg.friends)
			if (g.source == name || g.audioSource == name)
				users++;
		if (users > 1)
			continue;
		obs_source_t *src = obs_get_source_by_name(name.c_str());
		if (!src)
			continue;
		struct obs_frontend_source_list scenes = {};
		obs_frontend_get_scenes(&scenes);
		for (size_t i = 0; i < scenes.sources.num; i++) {
			obs_scene_t *scene = obs_scene_from_source(scenes.sources.array[i]);
			if (obs_sceneitem_t *it = scene ? obs_scene_find_source(scene, name.c_str()) : nullptr)
				obs_sceneitem_remove(it);
		}
		obs_frontend_source_list_free(&scenes);
		obs_source_remove(src); // OBS lets it go once nothing holds it
		obs_source_release(src);
		gone++;
		if (log)
			log("Removed the source '" + name + "'.");
	}
	return gone;
}

/// Builds before 0.7.0 named everything "Kennel ..."; it is all "Kennel.gg ..." now. Rename what is
/// there rather than make it again, so nobody's scenes fill with duplicates. Returns how many moved.
int Switcher::migrateNames(Config &cfg)
{
	int moved = 0;
	auto rename = [&](const std::string &from, const std::string &to) {
		if (from == to)
			return;
		obs_source_t *src = obs_get_source_by_name(from.c_str());
		if (!src)
			return;
		obs_source_t *clash = obs_get_source_by_name(to.c_str());
		if (clash) { // both exist: leave the old one alone, the new one wins
			obs_source_release(clash);
			obs_source_release(src);
			return;
		}
		obs_source_set_name(src, to.c_str());
		obs_source_release(src);
		moved++;
	};
	rename("Kennel web", Config::webSourceName());
	rename("Kennel look", Config::overlaySourceName());
	rename("Kennel look (vertical)", Config::overlaySourceNameV());
	rename("Kennel dual", Config::dualSceneName());
	rename("Kennel dual feed", Config::dualFeedName());
	bool cfgChanged = false;
	for (auto &f : cfg.friends) {
		rename("Kennel web - " + f.name, std::string(Config::webSourceName()) + " - " + f.name);
		for (std::string *field : {&f.source, &f.audioSource}) {
			if (field->rfind("Kennel · ", 0) == 0) {
				std::string to = "Kennel.gg · " + field->substr(std::string("Kennel · ").size());
				rename(*field, to);
				*field = to;
				cfgChanged = true;
			}
		}
	}
	if (cfgChanged)
		cfg.save();
	if (moved && log)
		log("Renamed " + std::to_string(moved) + " source(s) from \"Kennel ...\" to \"Kennel.gg ...\".");
	return moved;
}

std::string Switcher::createFriendSources(const Config &cfg, Friend &f)
{
	std::string base = "Kennel.gg · " + (f.name.empty() ? std::string("squad mate") : f.name);
	if (f.kind == FriendKind::Discord) {
		// "Any Discord window" means the share is watched inside Discord's own window, and every
		// squad mate set up that way is looking at the same window: one capture between them, not
		// one each. Five captures of one window cost five times the GPU and all showed the same
		// picture anyway. When one of them pops their share out, bindPopout gives them their own.
		bool shared = f.sharesDiscordCall();
		std::string video = shared ? Friend::discordCallSourceName() : base;
		obs_data_t *st = obs_data_create();
		obs_data_set_string(st, "window", f.channel.c_str());
		obs_data_set_int(st, "method", 2); // Windows 10 capture: survives the window being covered
		// exact title unless they chose "any Discord window": every Discord window string ends in
		// Discord.exe, and matching on the executable is how a slot ended up on the wrong window
		obs_data_set_int(st, "priority", shared ? 2 : 1); // 2 = by executable, 1 = this title only
		obs_data_set_bool(st, "cursor", false);
		obs_data_set_bool(st, "client_area", true);
		std::string e = createInScene(cfg, "window_capture", video, st, true, false);
		obs_data_release(st);
		if (!e.empty())
			return e;
		f.source = video;
		return "";
	}
	return "";
}

#ifdef _WIN32
/// OBS's own spelling of a window: "title:class:exe", with '#' and ':' inside the title escaped the
/// way libobs does it (window-helpers.c encode_dstr), '#' first so an escape is never re-escaped.
static std::string obsWindowString(const std::string &title, const std::string &cls, const std::string &exe)
{
	std::string t;
	for (char c : title) {
		if (c == '#')
			t += "#22";
		else if (c == ':')
			t += "#3A";
		else
			t += c;
	}
	return t + ":" + cls + ":" + exe;
}

static std::string narrow(const wchar_t *w)
{
	if (!w || !*w)
		return "";
	int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
	std::string out(n > 0 ? n - 1 : 0, '\0');
	if (n > 0)
		WideCharToMultiByte(CP_UTF8, 0, w, -1, &out[0], n, nullptr, nullptr);
	return out;
}

static BOOL CALLBACK popoutEnum(HWND hwnd, LPARAM lp)
{
	auto *out = reinterpret_cast<std::vector<Switcher::Popout> *>(lp);
	if (!IsWindowVisible(hwnd))
		return TRUE;
	LONG style = GetWindowLongW(hwnd, GWL_STYLE), ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
	if ((style & WS_CHILD) || (ex & WS_EX_TOOLWINDOW))
		return TRUE;
	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);
	if (!pid)
		return TRUE;
	HANDLE ph = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!ph)
		return TRUE;
	wchar_t path[MAX_PATH] = {};
	DWORD len = MAX_PATH;
	bool ok = QueryFullProcessImageNameW(ph, 0, path, &len) != 0;
	CloseHandle(ph);
	if (!ok)
		return TRUE;
	std::string exe = narrow(path);
	size_t slash = exe.find_last_of("\\/");
	if (slash != std::string::npos)
		exe = exe.substr(slash + 1);
	if (_stricmp(exe.c_str(), "Discord.exe") != 0)
		return TRUE;
	wchar_t cls[128] = {}, title[512] = {};
	GetClassNameW(hwnd, cls, 128);
	GetWindowTextW(hwnd, title, 512);
	Switcher::Popout p;
	p.title = narrow(title);
	p.cls = narrow(cls);
	if (p.title.empty() || p.cls.rfind("Chrome_WidgetWin_", 0) != 0)
		return TRUE;
	// Discord's main window ("#squad | Kennel.gg - Discord", or plain "Discord") is not a pop-out
	std::string t = p.title;
	if (t == "Discord" || (t.size() > 10 && t.compare(t.size() - 10, 10, " - Discord") == 0))
		return TRUE;
	p.window = obsWindowString(p.title, p.cls, exe);
	p.minimized = IsIconic(hwnd) != 0;
	p.hwnd = reinterpret_cast<uintptr_t>(hwnd);
	out->push_back(p);
	return TRUE;
}

static const int kSliver = 12; // pixels of the pop-out left on screen: enough for Chromium to call it visible
static const int kPopW = 1920, kPopH = 1080; // a pop-out's shape: 16:9, so Discord draws no letterbox bars

/// 16:9 at 1920x1080, or the largest 16:9 that fits `maxW` x `maxH` when that is smaller.
static void popoutSize(int maxW, int maxH, int &w, int &h)
{
	w = std::min(kPopW, maxW);
	h = w * 9 / 16;
	if (h > maxH) {
		h = maxH;
		w = h * 16 / 9;
	}
}

bool Switcher::tuckPopout(const Popout &p, int slot)
{
	HWND hwnd = reinterpret_cast<HWND>(p.hwnd);
	if (!hwnd || !IsWindow(hwnd) || IsIconic(hwnd))
		return false;
	HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
	MONITORINFO mi = {};
	mi.cbSize = sizeof(mi);
	if (!GetMonitorInfoW(mon, &mi))
		return false;
	RECT r = {};
	GetWindowRect(hwnd, &r);
	if (r.right - r.left <= 0 || r.bottom - r.top <= 0)
		return false;
	// the window can hang off the screen, so only its height has to fit; the shape is the point
	int w, h;
	popoutSize(kPopW, mi.rcMonitor.bottom - mi.rcMonitor.top, w, h);
	int x = mi.rcMonitor.right - kSliver;
	// each one lower than the last, so every sliver keeps a stretch nothing else sits on
	const int step = 220;
	int y = std::clamp((int)mi.rcMonitor.top + slot * step, (int)mi.rcMonitor.top,
			   (int)std::max(mi.rcMonitor.top, mi.rcMonitor.bottom - step));
	bool topmost = (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
	if (topmost && r.left == x && r.top == y && r.right - r.left == w && r.bottom - r.top == h)
		return false; // already where and how it should be
	SetWindowPos(hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_NOOWNERZORDER);
	return true;
}

struct MonList {
	std::vector<RECT> rects;
};
static BOOL CALLBACK monEnum(HMONITOR mon, HDC, LPRECT, LPARAM lp)
{
	MONITORINFO mi = {};
	mi.cbSize = sizeof(mi);
	if (GetMonitorInfoW(mon, &mi))
		reinterpret_cast<MonList *>(lp)->rects.push_back(mi.rcWork);
	return TRUE;
}

std::vector<std::string> Switcher::monitors()
{
	MonList ml;
	EnumDisplayMonitors(nullptr, nullptr, monEnum, reinterpret_cast<LPARAM>(&ml));
	std::vector<std::string> out;
	for (const RECT &r : ml.rects)
		out.push_back(std::to_string(r.right - r.left) + "x" + std::to_string(r.bottom - r.top) + " at " +
			      std::to_string(r.left) + "," + std::to_string(r.top));
	return out;
}

bool Switcher::parkPopout(const Popout &p, int mon, int slot, int total)
{
	HWND hwnd = reinterpret_cast<HWND>(p.hwnd);
	if (!hwnd || !IsWindow(hwnd) || IsIconic(hwnd))
		return false;
	MonList ml;
	EnumDisplayMonitors(nullptr, nullptr, monEnum, reinterpret_cast<LPARAM>(&ml));
	if (mon < 0 || mon >= (int)ml.rects.size())
		return false;
	const RECT &m = ml.rects[mon];
	RECT r = {};
	GetWindowRect(hwnd, &r);
	if (r.right - r.left <= 0 || r.bottom - r.top <= 0)
		return false;
	const int gap = 8;
	// 16:9 and as large as the screen allows: a portrait screen gets them at its full width
	int w, h;
	popoutSize((m.right - m.left) - 2 * gap, (m.bottom - m.top) - 2 * gap, w, h);
	// stacked when they fit; when they do not, staggered so each keeps a band of its own showing,
	// never piled on one spot where the top one would hide the rest from Discord
	int avail = (m.bottom - m.top) - 2 * gap;
	int step = h + gap;
	if (total > 1 && slot >= 0 && total * h + (total - 1) * gap > avail)
		step = std::max(160, (avail - h) / (total - 1));
	int x = m.left + gap, y = m.top + gap + std::max(0, slot) * step;
	if (y + h > m.bottom + h / 2) // never mostly off the bottom
		y = std::max((int)m.top + gap, (int)m.bottom - h / 2);
	bool topmost = (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
	if (topmost && r.left == x && r.top == y && r.right - r.left == w && r.bottom - r.top == h)
		return false;
	SetWindowPos(hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_NOOWNERZORDER);
	return true;
}

void Switcher::untuckPopout(const Popout &p)
{
	HWND hwnd = reinterpret_cast<HWND>(p.hwnd);
	if (!hwnd || !IsWindow(hwnd))
		return;
	HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
	MONITORINFO mi = {};
	mi.cbSize = sizeof(mi);
	if (!GetMonitorInfoW(mon, &mi))
		return;
	RECT r = {};
	GetWindowRect(hwnd, &r);
	int w = r.right - r.left;
	int x = std::max((int)mi.rcMonitor.left, (int)mi.rcMonitor.right - w - 40);
	SetWindowPos(hwnd, HWND_NOTOPMOST, x, r.top, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}
#endif

#ifndef _WIN32
bool Switcher::tuckPopout(const Popout &, int)
{
	return false;
}
bool Switcher::parkPopout(const Popout &, int, int, int)
{
	return false;
}
void Switcher::untuckPopout(const Popout &) {}
std::vector<std::string> Switcher::monitors()
{
	return {};
}
#endif

std::vector<Switcher::Popout> Switcher::discordPopouts()
{
	std::vector<Popout> out;
#ifdef _WIN32
	EnumWindows(popoutEnum, reinterpret_cast<LPARAM>(&out));
#endif
	return out;
}

std::string Switcher::bindPopout(const Config &cfg, Friend &f, const Popout &p)
{
	std::string mine = "Kennel.gg · " + (f.name.empty() ? std::string("squad mate") : f.name) + " pop-out";
	obs_data_t *st = obs_data_create();
	obs_data_set_string(st, "window", p.window.c_str());
	obs_data_set_int(st, "method", 2);
	obs_data_set_int(st, "priority", 1); // exact title: this window and no other
	obs_data_set_bool(st, "cursor", false);
	obs_data_set_bool(st, "client_area", true);
	std::string e = createInScene(cfg, "window_capture", mine, st, true, false);
	obs_data_release(st);
	if (!e.empty())
		return e;
	if (!f.onPopout())
		f.baseSource = f.source; // where to go back to; their audio capture stays as it is
	f.source = mine;
	f.popout = p.window;
	return "";
}

void Switcher::unbindPopout(const Config &cfg, Friend &f)
{
	if (!f.onPopout())
		return;
	std::string mine = f.source;
	f.source = !f.baseSource.empty()   ? f.baseSource
		   : f.sharesDiscordCall() ? std::string(Friend::discordCallSourceName())
					   : "Kennel.gg · " + (f.name.empty() ? std::string("squad mate") : f.name);
	f.baseSource.clear();
	f.popout.clear();
	f.popoutMissingMs = 0;
	// The capture itself stays: hidden, still in the scene, still bound to that exact title. When the
	// pop-out comes back OBS re-hooks it by itself, and binding again is a rename, not a re-create.
	// Deleting it here left ghosts that blocked the name for the next bind. It goes with the slot.
	hideEverywhere(mine);
	(void)cfg;
}

std::string Switcher::createGameCapture(Config &cfg)
{
	obs_data_t *st = obs_data_create();
	obs_data_set_string(st, "capture_mode", "any_fullscreen");
	obs_data_set_bool(st, "capture_audio", false);
	std::string e = createInScene(cfg, "game_capture", "Game", st, true, true, true);
	obs_data_release(st);
	if (e.empty())
		cfg.gameSource = "Game";
	return e;
}

obs_source_t *Switcher::sceneSource(const Config &cfg)
{
	if (!cfg.sceneName.empty()) {
		obs_source_t *s = obs_get_source_by_name(cfg.sceneName.c_str());
		if (s)
			return s;
	}
	return obs_frontend_get_current_scene();
}

std::string Switcher::ensureBrowserSource(obs_scene_t *scene, const char *name, const std::string &url,
					  bool rerouteAudio, int width, int height)
{
	struct obs_video_info ovi;
	obs_get_video_info(&ovi);
	if (width > 0 && height > 0) { // a canvas of another shape
		ovi.base_width = (uint32_t)width;
		ovi.base_height = (uint32_t)height;
	}
	obs_sceneitem_t *item = obs_scene_find_source(scene, name);
	obs_source_t *src = obs_get_source_by_name(name);
	if (!src) {
		obs_data_t *st = obs_data_create();
		obs_data_set_string(st, "url", url.c_str());
		obs_data_set_int(st, "width", ovi.base_width);
		obs_data_set_int(st, "height", ovi.base_height);
		obs_data_set_bool(st, "reroute_audio", rerouteAudio);
		obs_data_set_bool(st, "shutdown", false);
		obs_data_set_bool(st, "restart_when_active", false);
		src = obs_source_create("browser_source", name, st, nullptr);
		obs_data_release(st);
		if (!src)
			return std::string("could not create browser source '") + name +
			       "' (is the Browser Source available in this OBS?)";
		if (log)
			log(std::string("Added browser source '") + name + "'.");
	} else {
		obs_data_t *cur = obs_source_get_settings(src);
		std::string curUrl = obs_data_get_string(cur, "url");
		obs_data_release(cur);
		if (curUrl != url) {
			obs_data_t *st = obs_data_create();
			obs_data_set_string(st, "url", url.c_str());
			obs_data_set_int(st, "width", ovi.base_width);
			obs_data_set_int(st, "height", ovi.base_height);
			obs_data_set_bool(st, "reroute_audio", rerouteAudio);
			obs_data_set_bool(st, "shutdown", false);
			obs_source_update(src, st);
			obs_data_release(st);
		}
	}
	if (!item) {
		item = obs_scene_add(scene, src);
		if (item) {
			obs_sceneitem_set_visible(item, false);
			struct vec2 pos = {0, 0}, bounds = {(float)ovi.base_width, (float)ovi.base_height};
			obs_sceneitem_set_pos(item, &pos);
			obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
			obs_sceneitem_set_bounds(item, &bounds);
		}
	}
	obs_source_release(src);
	return item ? "" : std::string("could not add '") + name + "' to the scene";
}

std::string Switcher::ensureHideFilter(obs_source_t *src)
{
	obs_source_t *f = obs_source_get_filter_by_name(src, Config::hideFilterName());
	if (!f) { // made by a build before 0.7.0: keep it, under its new name
		f = obs_source_get_filter_by_name(src, "Kennel hide");
		if (f)
			obs_source_set_name(f, Config::hideFilterName());
	}
	if (f) {
		obs_source_release(f);
		return "";
	}
	obs_data_t *st = obs_data_create();
	obs_data_set_double(st, "opacity", 0.0);
	f = obs_source_create_private("color_filter_v2", Config::hideFilterName(), st);
	obs_data_release(st);
	if (!f)
		return "could not create the hide filter";
	obs_source_filter_add(src, f);
	obs_source_release(f);
	return "";
}

int Switcher::hideEverywhere(const std::string &sourceName)
{
	struct Ctx {
		const std::string *name;
		int hidden = 0;
	} ctx{&sourceName};
	// every scene OBS has, on every canvas: obs_enum_scenes walks the main canvas only, and a
	// vertical canvas's scenes are real scenes on a canvas of their own
	auto perScene = [](void *param, obs_source_t *ss) {
		auto *c = (Ctx *)param;
		obs_scene_t *scene = obs_scene_from_source(ss);
		if (!scene)
			return true;
		obs_scene_enum_items(
			scene,
			[](obs_scene_t *, obs_sceneitem_t *item, void *p) {
				auto *c = (Ctx *)p;
				obs_source_t *src = obs_sceneitem_get_source(item);
				if (src && obs_source_get_name(src) && *c->name == obs_source_get_name(src) &&
				    obs_sceneitem_visible(item)) {
					obs_sceneitem_set_visible(item, false);
					c->hidden++;
				}
				return true;
			},
			c);
		return true;
	};
	obs_enum_scenes(perScene, &ctx);
	std::pair<Ctx *, bool (*)(void *, obs_source_t *)> both(&ctx, perScene);
	obs_enum_canvases(
		[](void *param, obs_canvas_t *cv) {
			auto *b = (std::pair<Ctx *, bool (*)(void *, obs_source_t *)> *)param;
			if (!(obs_canvas_get_flags(cv) & MAIN))
				obs_canvas_enum_scenes(cv, b->second, b->first);
			return true;
		},
		&both);
	return ctx.hidden;
}

/// Scene items in the plugin's scene, top of the stack first: name and source type.
/// A Discord screen share arrives inside Discord's own window: flat grey chrome down the sides and
/// black letterboxing around the picture. This renders one frame of the source, walks in from each
/// edge while the whole row (or column) is one flat colour, and crops the scene item to what is left,
/// so the stream shows the game and nothing else. Called each time their feed goes up, so it follows
/// the window being resized.
/// A cheap fingerprint of what a source is showing right now. Sampling it quickly and counting how
/// often it changes measures the feed's real frame rate - the only way to tell "the network is not
/// delivering" apart from "this PC is not drawing it".
uint64_t Switcher::feedHash(const std::string &sourceName)
{
	obs_source_t *src = obs_get_source_by_name(sourceName.c_str());
	if (!src)
		return 0;
	std::vector<uint8_t> bgra;
	int w = 0, h = 0, ls = 0;
	uint64_t hash = 0;
	if (hashCap_.grab(src, 64, bgra, w, h, ls) && w > 0 && h > 0) {
		hash = 1469598103934665603ULL;
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w * 4; x += 7) { // every other pixel or so: plenty to tell frames apart
				hash ^= bgra[(size_t)y * ls + x];
				hash *= 1099511628211ULL;
			}
	}
	obs_source_release(src);
	return hash;
}

std::string Switcher::trimToContent(const Config &cfg, const Friend &f)
{
	if (f.source.empty())
		return "";
	obs_source_t *ss = sceneSource(cfg);
	if (!ss)
		return "no scene";
	obs_scene_t *scene = obs_scene_from_source(ss);
	obs_sceneitem_t *item = obs_scene_find_source(scene, f.source.c_str());
	obs_source_t *src = obs_get_source_by_name(f.source.c_str());
	std::string err;
	if (!item || !src) {
		err = "'" + f.source + "' is not in the scene";
	} else if (!f.trim) {
		struct obs_sceneitem_crop none = {0, 0, 0, 0};
		obs_sceneitem_set_crop(item, &none);
	} else {
		const int W = 320;
		std::vector<uint8_t> bgra;
		int w = 0, h = 0, ls = 0;
		if (!trimCap_.grab(src, W, bgra, w, h, ls) || w < 32 || h < 32) {
			err = "no picture from '" + f.source + "' yet";
		} else {
			auto lum = [&](int x, int y) {
				const uint8_t *p = &bgra[(size_t)y * ls + (size_t)x * 4];
				return (int)(0.114f * p[0] + 0.587f * p[1] + 0.299f * p[2]);
			};
			// a row or column of window chrome is one flat colour; the picture never is
			const int kFlat = 10;
			auto flatRow = [&](int y) {
				int lo = 255, hi = 0;
				for (int x = 0; x < w; x++) {
					int v = lum(x, y);
					lo = std::min(lo, v);
					hi = std::max(hi, v);
				}
				return hi - lo <= kFlat;
			};
			auto flatCol = [&](int x) {
				int lo = 255, hi = 0;
				for (int y = 0; y < h; y++) {
					int v = lum(x, y);
					lo = std::min(lo, v);
					hi = std::max(hi, v);
				}
				return hi - lo <= kFlat;
			};
			int top = 0, bottom = 0, left = 0, right = 0;
			const int maxTB = h / 3, maxLR = w / 3; // never eat into the picture itself
			while (top < maxTB && flatRow(top))
				top++;
			while (bottom < maxTB && flatRow(h - 1 - bottom))
				bottom++;
			while (left < maxLR && flatCol(left))
				left++;
			while (right < maxLR && flatCol(w - 1 - right))
				right++;
			uint32_t sw = obs_source_get_width(src), sh = obs_source_get_height(src);
			struct obs_sceneitem_crop crop = {(int)std::lround((double)left * sw / w),
							  (int)std::lround((double)top * sh / h),
							  (int)std::lround((double)right * sw / w),
							  (int)std::lround((double)bottom * sh / h)};
			struct obs_sceneitem_crop had = {0, 0, 0, 0};
			obs_sceneitem_get_crop(item, &had);
			if (crop.left != had.left || crop.top != had.top || crop.right != had.right ||
			    crop.bottom != had.bottom) {
				obs_sceneitem_set_crop(item, &crop);
				if (log && (crop.left || crop.top || crop.right || crop.bottom))
					log("Trimmed the borders off " + f.name + "'s feed (" +
					    std::to_string(crop.left) + "/" + std::to_string(crop.top) + "/" +
					    std::to_string(crop.right) + "/" + std::to_string(crop.bottom) + " px).");
			}
		}
	}
	if (src)
		obs_source_release(src);
	obs_source_release(ss);
	return err;
}

/// Scenes on the other canvases. OBS 31.1+ keeps every extra canvas (Aitum Stream Suite's
/// portrait one, say) with its own scene list, which obs_enum_scenes and obs_get_source_by_name
/// never look at - so they are walked canvas by canvas. Scenes an older vertical plugin keeps
/// outside the scene list (main canvas, not in the front end) come last, with no canvas.
std::vector<std::pair<std::string, std::string>> Switcher::otherCanvasScenes()
{
	std::vector<std::pair<std::string, std::string>> out;
	obs_enum_canvases(
		[](void *param, obs_canvas_t *cv) {
			if (obs_canvas_get_flags(cv) & MAIN)
				return true;
			auto *o = (std::vector<std::pair<std::string, std::string>> *)param;
			const char *cn = obs_canvas_get_name(cv);
			std::pair<std::string, std::vector<std::pair<std::string, std::string>> *> ctx(cn ? cn : "", o);
			obs_canvas_enum_scenes(
				cv,
				[](void *p, obs_source_t *ss) {
					auto *c = (std::pair<std::string,
							     std::vector<std::pair<std::string, std::string>> *> *)p;
					const char *n = obs_source_get_name(ss);
					if (n)
						c->second->emplace_back(c->first, n);
					return true;
				},
				&ctx);
			return true;
		},
		&out);
	std::vector<std::string> front = sceneNames();
	std::pair<std::vector<std::string> *, std::vector<std::pair<std::string, std::string>> *> ctx(&front, &out);
	obs_enum_scenes(
		[](void *param, obs_source_t *ss) {
			auto *pr = (std::pair<std::vector<std::string> *,
					      std::vector<std::pair<std::string, std::string>> *> *)param;
			const char *n = obs_source_get_name(ss);
			if (n && std::find(pr->first->begin(), pr->first->end(), n) == pr->first->end())
				pr->second->emplace_back("", n);
			return true;
		},
		&ctx);
	std::sort(out.begin(), out.end());
	return out;
}

obs_source_t *Switcher::verticalSceneSource(const Config &cfg)
{
	if (cfg.sceneV.empty())
		return nullptr;
	obs_source_t *ss = nullptr;
	if (!cfg.canvasV.empty()) {
		obs_canvas_t *cv = obs_get_canvas_by_name(cfg.canvasV.c_str());
		if (cv) {
			ss = obs_canvas_get_source_by_name(cv, cfg.sceneV.c_str());
			obs_canvas_release(cv);
		}
	}
	if (!ss) {
		// the scene may have moved canvas, or be an older plugin's: any OTHER canvas that has it.
		// Never the main one: a vertical scene often carries the same name as the main scene
		// ("GAMING" on both), and resolving to the main copy put the portrait swap on the wrong
		// canvas with nothing to show for it
		std::pair<const char *, obs_source_t *> ctx(cfg.sceneV.c_str(), nullptr);
		obs_enum_canvases(
			[](void *p, obs_canvas_t *cv) {
				auto *c = (std::pair<const char *, obs_source_t *> *)p;
				if (obs_canvas_get_flags(cv) & MAIN)
					return true;
				c->second = obs_canvas_get_source_by_name(cv, c->first);
				return c->second == nullptr;
			},
			&ctx);
		ss = ctx.second;
	}
	if (!ss && cfg.sceneV != cfg.sceneName)
		ss = obs_get_source_by_name(cfg.sceneV.c_str()); // an older vertical plugin's scene, main canvas
	if (ss && !obs_scene_from_source(ss)) {
		obs_source_release(ss);
		ss = nullptr;
	}
	return ss;
}

/// The same swap, in the vertical scene: the squad mate's source full-canvas in portrait, the look
/// overlay over it in its portrait form. The video source is the very same one the main canvas
/// shows - a source can sit in any number of scenes - so nothing is decoded twice.
std::string Switcher::applyVertical(const Config &cfg, bool on)
{
	if (!cfg.verticalOn())
		return "";
	obs_source_t *ss = verticalSceneSource(cfg);
	obs_scene_t *scene = ss ? obs_scene_from_source(ss) : nullptr;
	if (!scene) {
		if (ss)
			obs_source_release(ss);
		return "vertical scene '" + cfg.sceneV + "' is not there (on a canvas other than the main one)";
	}
	// the portrait canvas's size: a scene reports its canvas's base size
	uint32_t cw = obs_source_get_width(ss), ch = obs_source_get_height(ss);
	if (cw == 0 || ch == 0) {
		cw = 1080;
		ch = 1920;
	}
	{
		// say once which scene, on which canvas, at what size: the thing to check in a log
		std::string cvName = "?";
		if (obs_canvas_t *cv = obs_source_get_canvas(ss)) {
			cvName = obs_canvas_get_name(cv) ? obs_canvas_get_name(cv) : "?";
			obs_canvas_release(cv);
		}
		std::string where = "'" + cfg.sceneV + "' on canvas '" + cvName + "' (" + std::to_string(cw) + "x" +
				    std::to_string(ch) + ")";
		if (where != lastVerticalWhere_ && log) {
			lastVerticalWhere_ = where;
			log("Vertical: the scene is " + where + ".");
		}
	}
	std::string err;
	const Friend *f = cfg.active();
	// every squad mate whose source exists gets a scene item here, so the vertical scene carries
	// the same squad as the main one from the moment it is switched on; only the active one shows
	for (const Friend &g : cfg.friends) {
		std::string name = cfg.sourceFor(g);
		bool isActive = f && &g == f;
		obs_source_t *src = obs_get_source_by_name(name.c_str());
		if (!src) {
			if (isActive && on)
				err = "'" + name + "' does not exist yet";
			continue;
		}
		obs_sceneitem_t *item = obs_scene_find_source(scene, name.c_str());
		bool fresh = !item;
		if (!item)
			item = obs_scene_add(scene, src);
		if (item) {
			if (fresh) {
				// full height of the portrait canvas, centred: the sides of a 16:9 feed fall away
				struct vec2 pos = {0, 0}, bounds = {(float)cw, (float)ch};
				obs_sceneitem_set_pos(item, &pos);
				obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_OUTER);
				obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
				obs_sceneitem_set_bounds(item, &bounds);
				if (log)
					log("Vertical: added " + g.name + "'s feed to '" + cfg.sceneV + "'.");
			}
			bool show = on && isActive;
			if (show)
				moveToTop(item);
			if (show && !obs_sceneitem_visible(item) && log)
				log("Vertical: showing " + g.name + "'s feed.");
			obs_sceneitem_set_visible(item, show);
		}
		obs_source_release(src);
	}
	// the look overlay, portrait
	const char *lookName = Config::overlaySourceNameV();
	if (on && (cfg.lookName || cfg.lookCam || cfg.lookGrain || cfg.lookVignette)) {
		std::string e = ensureBrowserSource(scene, lookName,
						    overlayUrl(cfg, f ? f->name : "") + "&v=1&vtop=" +
							    std::to_string(std::clamp(cfg.lookTopV, 0, 90)),
						    false, (int)cw, (int)ch);
		if (!e.empty() && err.empty())
			err = e;
		if (obs_sceneitem_t *it = obs_scene_find_source(scene, lookName)) {
			moveToTop(it);
			obs_sceneitem_set_visible(it, true);
		}
	} else
		hideEverywhere(lookName);
	obs_source_release(ss);
	raiseOnTopV(cfg); // the streamer's camera and alerts over whatever we just showed, here too
	return err;
}

std::vector<std::pair<std::string, std::string>> Switcher::sceneItems(const Config &cfg)
{
	std::vector<std::pair<std::string, std::string>> out;
	obs_source_t *ss = sceneSource(cfg);
	if (!ss)
		return out;
	obs_scene_enum_items(
		obs_scene_from_source(ss),
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			auto *v = (std::vector<std::pair<std::string, std::string>> *)param;
			obs_source_t *s = obs_sceneitem_get_source(item);
			if (s && obs_source_get_name(s))
				v->emplace_back(obs_source_get_name(s),
						obs_source_get_id(s) ? obs_source_get_id(s) : "");
			return true;
		},
		&out);
	std::reverse(out.begin(), out.end()); // obs enumerates bottom-up
	obs_source_release(ss);
	return out;
}

/// The streamer's own face cam and alerts belong over the top of everything we put in the scene.
/// Called after anything that adds a source or changes the order.
void Switcher::raiseOnTop(const Config &cfg)
{
	if (cfg.onTop.empty())
		return;
	obs_source_t *ss = sceneSource(cfg);
	if (!ss)
		return;
	obs_scene_t *scene = obs_scene_from_source(ss);
	// last first, so the first one in the list ends up the topmost
	for (auto it = cfg.onTop.rbegin(); it != cfg.onTop.rend(); ++it) {
		if (*it == cfg.gameSource || it->rfind("Kennel", 0) == 0)
			continue; // the game itself over the squad mate would undo the swap
		if (obs_sceneitem_t *item = obs_scene_find_source(scene, it->c_str()))
			moveToTop(item);
	}
	obs_source_release(ss);
}

void Switcher::raiseOnTopV(const Config &cfg)
{
	if (!cfg.verticalOn())
		return;
	obs_source_t *ss = verticalSceneSource(cfg);
	if (!ss)
		return;
	obs_scene_t *scene = obs_scene_from_source(ss);
	const auto &names = cfg.onTopV.empty() ? cfg.onTop : cfg.onTopV;
	for (auto it = names.rbegin(); it != names.rend(); ++it) {
		if (*it == cfg.gameSource || it->rfind("Kennel", 0) == 0)
			continue; // the game itself over the squad mate would undo the swap
		if (obs_sceneitem_t *item = obs_scene_find_source(scene, it->c_str()))
			moveToTop(item);
	}
	obs_source_release(ss);
}

std::vector<std::pair<std::string, std::string>> Switcher::sceneItemsV(const Config &cfg)
{
	std::vector<std::pair<std::string, std::string>> out;
	obs_source_t *ss = verticalSceneSource(cfg);
	if (!ss)
		return out;
	obs_scene_enum_items(
		obs_scene_from_source(ss),
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			auto *o = (std::vector<std::pair<std::string, std::string>> *)param;
			obs_source_t *src = obs_sceneitem_get_source(item);
			if (src && obs_source_get_name(src)) {
				const char *id = obs_source_get_unversioned_id(src);
				o->emplace_back(obs_source_get_name(src), id ? id : "");
			}
			return true;
		},
		&out);
	obs_source_release(ss);
	std::reverse(out.begin(), out.end()); // top of the scene first, as the main list is
	return out;
}

static bool looksOnTop(const std::string &name, const std::string &id)
{
	if (name.rfind("Kennel", 0) == 0)
		return false;
	std::string n = name;
	std::transform(n.begin(), n.end(), n.begin(), ::tolower);
	bool cam = id == "dshow_input" || id == "av_capture_input" || id == "av_capture_input_v2" ||
		   id == "macos-avcapture" || id == "v4l2_input";
	bool alert = n.find("alert") != std::string::npos || n.find("streamlabs") != std::string::npos ||
		     n.find("streamelement") != std::string::npos || n.find("stream element") != std::string::npos;
	return cam || alert;
}

std::vector<std::string> Switcher::guessOnTopV(const Config &cfg)
{
	std::vector<std::string> out;
	for (const auto &[name, id] : sceneItemsV(cfg))
		if (looksOnTop(name, id) &&
		    name != cfg.gameSource) // a capture card looks like a camera, but it is the game
			out.push_back(name);
	return out;
}

/// A first guess at what the streamer would want kept on top: their camera, and alert overlays.
std::vector<std::string> Switcher::guessOnTop(const Config &cfg)
{
	std::vector<std::string> out;
	for (const auto &[name, id] : sceneItems(cfg)) {
		if (name.rfind("Kennel", 0) == 0) // our own sources are what it sits on top of
			continue;
		std::string n = name, i2 = id;
		std::transform(n.begin(), n.end(), n.begin(), ::tolower);
		bool cam = i2 == "dshow_input" || i2 == "av_capture_input" || i2 == "av_capture_input_v2" ||
			   i2 == "macos-avcapture" || i2 == "v4l2_input";
		bool alert = n.find("alert") != std::string::npos || n.find("streamlabs") != std::string::npos ||
			     n.find("streamelement") != std::string::npos ||
			     n.find("stream element") != std::string::npos;
		if ((cam || alert) && name != cfg.gameSource) // a capture card looks like a camera, but it is the game
			out.push_back(name);
	}
	return out;
}

std::string Switcher::updateLook(const Config &cfg, bool on)
{
	obs_source_t *ss = sceneSource(cfg);
	if (!ss)
		return "no scene";
	obs_scene_t *scene = obs_scene_from_source(ss);
	std::string err;
	if (on) {
		const Friend *f = cfg.active();
		err = ensureBrowserSource(scene, Config::overlaySourceName(), overlayUrl(cfg, f ? f->name : ""), false);
		obs_sceneitem_t *item = obs_scene_find_source(scene, Config::overlaySourceName());
		if (item) {
			moveToTop(item);
			obs_sceneitem_set_visible(item, true);
			raiseOnTop(cfg);
		}
	} else {
		int n = hideEverywhere(Config::overlaySourceName());
		if (log && n > 1)
			log("Look overlay: hid " + std::to_string(n) + " scene items (it was in more than one place).");
	}
	obs_source_release(ss);
	return err;
}

void Switcher::armWarm(const Config &cfg)
{
	if (cfg.preloadFeeds) {
		// Every browser feed loaded and playing behind the scenes, so a swap is instant - a Twitch
		// or VDO.Ninja page takes seconds to come up and is worth the wait. A window capture is
		// back in well under a second, so only the squad mate we would actually show is kept warm.
		const Friend *act = cfg.active();
		for (const auto &f : cfg.friends) {
			if (f.isWeb() || (act && f.name == act->name))
				armOne(cfg, f);
			else
				hideEverywhere(cfg.sourceFor(f)); // stops it receiving
		}
		raiseOnTop(cfg);
		// the shared browser source is not used in this mode; make sure it is not left showing
		hideEverywhere(Config::webSourceName());
		return;
	}
	const Friend *f = cfg.active();
	if (f)
		armOne(cfg, *f);
	raiseOnTop(cfg);
	// per-squad-mate sources from a previous preloading session must not sit on top of the scene
	for (const auto &other : cfg.friends)
		if (other.isWeb())
			hideEverywhere(std::string(Config::webSourceName()) + " - " + other.name);
}

/// One feed in warm mode: in the scene, playing, fully transparent and muted.
void Switcher::armOne(const Config &cfg, const Friend &f)
{
	std::string name = cfg.sourceFor(f);
	if (!dualInner_.empty() && name == dualInner_)
		return; // it is showing in the dual window right now; warm would blank it
	obs_source_t *ss = sceneSource(cfg);
	if (!ss)
		return;
	obs_scene_t *scene = obs_scene_from_source(ss);
	if (f.isWeb())
		ensureBrowserSource(scene, name.c_str(), webUrl(f), true);
	obs_source_t *src = obs_get_source_by_name(name.c_str());
	obs_sceneitem_t *item = obs_scene_find_source(scene, name.c_str());
	if (src && item) {
		ensureHideFilter(src);
		obs_source_t *hf = obs_source_get_filter_by_name(src, Config::hideFilterName());
		if (hf) {
			obs_source_set_enabled(hf, true);
			obs_source_release(hf);
		}
		obs_source_set_muted(src, true);
		// A browser feed goes on playing while its scene item is hidden, so hide it and let it play;
		// a Discord capture stays up so its picture is there the moment it is needed.
		bool keepUp = !f.isWeb() && f.kind == FriendKind::Discord;
		obs_sceneitem_set_visible(item, keepUp);
	} else if (log)
		log("Warm feed: '" + name + "' is not in the scene.");
	if (!f.audioSource.empty()) {
		obs_source_t *a = obs_get_source_by_name(f.audioSource.c_str());
		obs_sceneitem_t *ai = obs_scene_find_source(scene, f.audioSource.c_str());
		if (a && ai) {
			obs_source_set_muted(a, true);
			obs_sceneitem_set_visible(ai, false);
		}
		if (a)
			obs_source_release(a);
	}
	if (src)
		obs_source_release(src);
	obs_source_release(ss);
}

void Switcher::applyFriendAudio(const Config &cfg, bool showing)
{
	const Friend *a = cfg.active();
	std::vector<std::string> keep; // what carries the sound of the one on screen
	if (showing && cfg.friendAudio && a) {
		keep.push_back(cfg.sourceFor(*a));
		if (!a->audioSource.empty())
			keep.push_back(a->audioSource);
	}
	auto setMute = [](const std::string &name, bool mute) {
		if (name.empty())
			return;
		obs_source_t *src = obs_get_source_by_name(name.c_str());
		if (!src)
			return;
		if (obs_source_muted(src) != mute)
			obs_source_set_muted(src, mute);
		obs_source_release(src);
	};
	// everyone else first, then the one on screen: a Discord slot shares its capture and its call
	// audio with the other Discord slots, and the shared source must end up unmuted
	for (const auto &f : cfg.friends)
		for (const std::string &n : {cfg.sourceFor(f), f.audioSource})
			if (std::find(keep.begin(), keep.end(), n) == keep.end())
				setMute(n, true);
	for (const auto &n : keep)
		setMute(n, false);
}

std::string Switcher::playMedia(const Config &cfg, const std::string &path, int scalePct, int volumePct, bool frame,
				const std::string &pathV)
{
	obs_data_t *st = obs_data_create();
	obs_data_set_string(st, "local_file", path.c_str());
	obs_data_set_bool(st, "is_local_file", true);
	obs_data_set_bool(st, "looping", false);
	obs_data_set_bool(st, "restart_on_activate", false);
	obs_data_set_bool(st, "close_when_inactive", true);
	obs_data_set_bool(st, "clear_on_media_end", true);
	// software decode: the GPU decoder refused a 1440p60 recording at load ("more than 32 decode
	// surfaces"), and a ten-second replay is nothing for the CPU
	obs_data_set_bool(st, "hw_decode", false);
	obs_data_set_int(st, "speed_percent", 100);
	std::string e = createInScene(cfg, "ffmpeg_source", Config::replaySourceName(), st, false, false);
	obs_data_release(st);
	if (!e.empty())
		return e;
	obs_source_t *ss = sceneSource(cfg);
	if (!ss)
		return "no scene";
	obs_scene_t *scene = obs_scene_from_source(ss);
	obs_sceneitem_t *item = obs_scene_find_source(scene, Config::replaySourceName());
	obs_source_t *src = obs_get_source_by_name(Config::replaySourceName());
	if (!item || !src) {
		if (src)
			obs_source_release(src);
		obs_source_release(ss);
		return "the replay source is not in the scene";
	}
	struct obs_video_info ovi;
	obs_get_video_info(&ovi);
	float f = std::clamp(scalePct, 10, 100) / 100.0f;
	float w = ovi.base_width * f, h = ovi.base_height * f;
	struct vec2 pos = {(ovi.base_width - w) / 2.0f, (ovi.base_height - h) / 2.0f}, bounds = {w, h};
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
	obs_sceneitem_set_bounds(item, &bounds);
	obs_sceneitem_set_pos(item, &pos);
	obs_source_set_volume(src, std::clamp(volumePct, 0, 100) / 100.0f);
	obs_source_set_muted(src, volumePct <= 0);
	obs_source_media_restart(src);
	obs_sceneitem_set_visible(item, true);
	moveToTop(item);
	// the frame: the look page in replay mode, rendered at the replay's own size and laid exactly
	// over it, so its edge and "Instant replay" tag sit on the picture
	if (frame) {
		char *pp = obs_module_file("overlay/overlay.html");
		std::string page = pp ? pp : "";
		bfree(pp);
		std::replace(page.begin(), page.end(), '\\', '/');
		std::string url = "file:///" + page + "?replay=1&rlabel=" + urlEncode(cfg.replayLabel);
		std::string e2 = ensureBrowserSource(scene, Config::replayFrameName(), url, false, (int)w, (int)h);
		if (e2.empty()) {
			if (obs_sceneitem_t *fi = obs_scene_find_source(scene, Config::replayFrameName())) {
				obs_sceneitem_set_bounds_type(fi, OBS_BOUNDS_SCALE_INNER);
				obs_sceneitem_set_bounds(fi, &bounds);
				obs_sceneitem_set_pos(fi, &pos);
				obs_sceneitem_set_visible(fi, true);
				moveToTop(fi);
			}
		} else if (log)
			log("Replay frame: " + e2);
	} else
		hideEverywhere(Config::replayFrameName());
	raiseOnTop(cfg); // camera and alerts back over it
	obs_source_release(src);
	obs_source_release(ss);
	if (cfg.verticalOn()) {
		std::string ev = playMediaVertical(cfg, scalePct, frame, pathV);
		if (!ev.empty() && log)
			log("Replay (vertical): " + ev);
	}
	return "";
}

std::string Switcher::playMediaVertical(const Config &cfg, int scalePct, bool frame, const std::string &pathV)
{
	obs_source_t *ss = verticalSceneSource(cfg);
	if (!ss)
		return "vertical scene '" + cfg.sceneV + "' is not there";
	obs_scene_t *scene = obs_scene_from_source(ss);
	uint32_t cw = obs_source_get_width(ss), ch = obs_source_get_height(ss);
	if (cw == 0 || ch == 0) {
		cw = 1080;
		ch = 1920;
	}
	obs_source_t *src = nullptr;
	const char *srcName = Config::replaySourceName();
	bool portrait = !pathV.empty();
	if (portrait) {
		// the vertical canvas's own clip of the moment (Aitum's vertical Backtrack), in a media
		// source of its own: full height, portrait
		srcName = Config::replaySourceNameV();
		obs_data_t *st = obs_data_create();
		obs_data_set_string(st, "local_file", pathV.c_str());
		obs_data_set_bool(st, "is_local_file", true);
		obs_data_set_bool(st, "looping", false);
		obs_data_set_bool(st, "restart_on_activate", false);
		obs_data_set_bool(st, "close_when_inactive", true);
		obs_data_set_bool(st, "clear_on_media_end", true);
		obs_data_set_bool(st, "hw_decode", false);
		obs_data_set_int(st, "speed_percent", 100);
		src = obs_get_source_by_name(srcName);
		if (!src) {
			src = obs_source_create("ffmpeg_source", srcName, st, nullptr);
		} else
			obs_source_update(src, st);
		obs_data_release(st);
		if (!src) {
			obs_source_release(ss);
			return "could not make the vertical replay source";
		}
		hideEverywhere(Config::replaySourceName()); // not the horizontal one as well
	} else {
		src = obs_get_source_by_name(srcName);
		hideEverywhere(Config::replaySourceNameV());
	}
	if (!src) {
		obs_source_release(ss);
		return "no replay source";
	}
	// a scene item for it: the horizontal clip full width at the chosen scale, 16:9, centred;
	// the portrait clip full height at the chosen scale, 9:16, centred
	obs_sceneitem_t *item = obs_scene_find_source(scene, srcName);
	if (!item)
		item = obs_scene_add(scene, src);
	float f = std::clamp(scalePct, 10, 100) / 100.0f;
	float w, h;
	if (portrait) {
		h = ch * f;
		w = h * 9.0f / 16.0f;
	} else {
		w = cw * f;
		h = w * 9.0f / 16.0f;
	}
	struct vec2 pos = {(cw - w) / 2.0f, (ch - h) / 2.0f}, bounds = {w, h};
	if (item) {
		obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
		obs_sceneitem_set_bounds(item, &bounds);
		obs_sceneitem_set_pos(item, &pos);
		obs_sceneitem_set_visible(item, true);
		moveToTop(item);
	}
	if (portrait) {
		obs_source_set_muted(src, true); // the horizontal one carries the sound, if any
		obs_source_media_restart(src);
	}
	if (frame) {
		char *pp = obs_module_file("overlay/overlay.html");
		std::string page = pp ? pp : "";
		bfree(pp);
		std::replace(page.begin(), page.end(), '\\', '/');
		std::string url =
			"file:///" + page + "?replay=1&rlabel=" + urlEncode(cfg.replayLabel) + (portrait ? "&v=1" : "");
		std::string e2 = ensureBrowserSource(scene, Config::replayFrameNameV(), url, false, (int)w, (int)h);
		if (e2.empty()) {
			if (obs_sceneitem_t *fi = obs_scene_find_source(scene, Config::replayFrameNameV())) {
				obs_sceneitem_set_bounds_type(fi, OBS_BOUNDS_SCALE_INNER);
				obs_sceneitem_set_bounds(fi, &bounds);
				obs_sceneitem_set_pos(fi, &pos);
				obs_sceneitem_set_visible(fi, true);
				moveToTop(fi);
			}
		}
	} else
		hideEverywhere(Config::replayFrameNameV());
	raiseOnTopV(cfg);
	obs_source_release(src);
	obs_source_release(ss);
	return "";
}

std::string Switcher::playSound(const Config &cfg, const std::string &path, int volumePct)
{
	obs_data_t *st = obs_data_create();
	obs_data_set_string(st, "local_file", path.c_str());
	obs_data_set_bool(st, "is_local_file", true);
	obs_data_set_bool(st, "looping", false);
	obs_data_set_bool(st, "restart_on_activate", false);
	obs_data_set_bool(st, "close_when_inactive", true);
	obs_data_set_bool(st, "clear_on_media_end", true);
	obs_data_set_bool(st, "hw_decode", false);
	// a sound has no picture: the item exists so the source is active and in the mix
	std::string e = createInScene(cfg, "ffmpeg_source", Config::chimeSourceName(), st, false, true);
	obs_data_release(st);
	if (!e.empty())
		return e;
	obs_source_t *src = obs_get_source_by_name(Config::chimeSourceName());
	if (!src)
		return "no chime source";
	obs_source_set_volume(src, std::clamp(volumePct, 0, 100) / 100.0f);
	obs_source_set_muted(src, false);
	obs_source_set_monitoring_type(src, OBS_MONITORING_TYPE_NONE);
	obs_source_media_restart(src);
	obs_source_release(src);
	return "";
}

int64_t Switcher::mediaDurationMs() const
{
	obs_source_t *src = obs_get_source_by_name(Config::replaySourceName());
	if (!src)
		return 0;
	int64_t d = obs_source_media_get_duration(src);
	obs_source_release(src);
	return d;
}

void Switcher::seekMedia(int64_t ms)
{
	for (const char *n : {Config::replaySourceName(), Config::replaySourceNameV()}) {
		obs_source_t *src = obs_get_source_by_name(n);
		if (!src)
			continue;
		obs_source_media_set_time(src, ms);
		obs_source_release(src);
	}
}

bool Switcher::mediaEnded() const
{
	obs_source_t *src = obs_get_source_by_name(Config::replaySourceName());
	if (!src)
		return true;
	enum obs_media_state st = obs_source_media_get_state(src);
	obs_source_release(src);
	return st == OBS_MEDIA_STATE_ENDED || st == OBS_MEDIA_STATE_STOPPED || st == OBS_MEDIA_STATE_ERROR ||
	       st == OBS_MEDIA_STATE_NONE;
}

int Switcher::mediaState() const
{
	obs_source_t *src = obs_get_source_by_name(Config::replaySourceName());
	if (!src)
		return OBS_MEDIA_STATE_NONE;
	int st = (int)obs_source_media_get_state(src);
	obs_source_release(src);
	return st;
}

int64_t Switcher::mediaTimeMs() const
{
	obs_source_t *src = obs_get_source_by_name(Config::replaySourceName());
	if (!src)
		return 0;
	int64_t t = obs_source_media_get_time(src);
	obs_source_release(src);
	return t;
}

void Switcher::stopMedia(const Config &cfg)
{
	obs_source_t *src = obs_get_source_by_name(Config::replaySourceName());
	if (src) {
		obs_source_media_stop(src);
		// and forget the file: a media source saved with a file in it opens and decodes that file
		// again the next time the scene collection loads, for nothing
		obs_data_t *st = obs_data_create();
		obs_data_set_string(st, "local_file", "");
		obs_source_update(src, st);
		obs_data_release(st);
		obs_source_release(src);
	}
	if (obs_source_t *v = obs_get_source_by_name(Config::replaySourceNameV())) {
		obs_source_media_stop(v);
		obs_data_t *st = obs_data_create();
		obs_data_set_string(st, "local_file", "");
		obs_source_update(v, st);
		obs_data_release(st);
		obs_source_release(v);
	}
	hideEverywhere(Config::replaySourceName());
	hideEverywhere(Config::replaySourceNameV());
	hideEverywhere(Config::replayFrameName());
	hideEverywhere(Config::replayFrameNameV());
	(void)cfg;
}

/// Your own POV, and nothing else: every squad mate's video and audio hidden in every scene.
int Switcher::hideAllFriends(const Config &cfg)
{
	int n = hideEverywhere(Config::webSourceName());
	for (const auto &f : cfg.friends) {
		if (f.isWeb())
			n += hideEverywhere(std::string(Config::webSourceName()) + " - " + f.name);
		else if (!f.source.empty())
			n += hideEverywhere(f.source);
		if (!f.audioSource.empty())
			n += hideEverywhere(f.audioSource);
		std::string nm = cfg.sourceFor(f);
		obs_source_t *src = obs_get_source_by_name(nm.c_str());
		if (src) {
			obs_source_set_muted(src, true);
			obs_source_release(src);
		}
	}
	n += hideEverywhere(Config::overlaySourceName());
	n += hideEverywhere(Config::overlaySourceNameV());
	return n;
}

/// OBS is closing: take the dual-POV window out of the scene and release its private scene and
/// browser page while obs-browser is still loaded.
void Switcher::shutdown()
{
	if (!dualScene_)
		return;
	obs_source_t *dualSrc = obs_scene_get_source(dualScene_);
	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_scene_t *scene = obs_scene_from_source(scenes.sources.array[i]);
		obs_sceneitem_t *it = scene ? obs_scene_find_source(scene, Config::dualSceneName()) : nullptr;
		if (it)
			obs_sceneitem_remove(it);
	}
	obs_frontend_source_list_free(&scenes);
	(void)dualSrc;
	obs_scene_release(dualScene_);
	dualScene_ = nullptr;
	obs_source_t *b = obs_get_source_by_name(Config::dualFeedName());
	if (b) {
		obs_source_remove(b);
		obs_source_release(b);
	}
}

std::string Switcher::applyDual(const Config &cfg, bool on, bool rearm)
{
	const Friend *f = cfg.dual();
	if (on && !f)
		return "no squad mate chosen for the dual POV";
	obs_source_t *ss = sceneSource(cfg);
	if (!ss)
		return "no scene";
	obs_scene_t *scene = obs_scene_from_source(ss);
	obs_sceneitem_t *item = obs_scene_find_source(scene, Config::dualSceneName());
	if (!on) {
		if (item)
			obs_sceneitem_set_visible(item, false);
		hideEverywhere(Config::dualSceneName());
		// their capture went into the window bare; back to warm-and-hidden unless the swap has it
		dualInner_.clear();
		if (rearm && f && !f->isWeb() && cfg.keepWarm)
			armOne(cfg, *f);
		obs_source_release(ss);
		return "";
	}
	struct obs_video_info ovi;
	obs_get_video_info(&ovi);
	// a private nested scene holds the feed at full size; the main scene shows that scene small.
	// Private, so it never appears in the scene list and the "hide every squad mate" sweep does
	// not reach inside it.
	// Private scenes cannot be looked up by name, so it is kept here for the life of the plugin and
	// released in shutdown(); creating one per call leaked a browser page each time, and CEF then
	// crashed when OBS closed with those pages still alive.
	if (!dualScene_) {
		dualScene_ = obs_scene_create_private(Config::dualSceneName());
		if (log)
			log("Added the dual-POV window '" + std::string(Config::dualSceneName()) + "'.");
	}
	obs_scene_t *dual = dualScene_;
	obs_source_t *dualSrc = obs_scene_get_source(dual);
	obs_source_get_ref(dualSrc);
	std::string err;
	std::string inner;
	if (f->isWeb()) {
		inner = Config::dualFeedName();
		err = ensureBrowserSource(dual, inner.c_str(), webUrl(*f), true);
		obs_source_t *b = obs_get_source_by_name(inner.c_str());
		if (b) {
			obs_source_set_muted(b, true); // the window is picture only
			obs_source_release(b);
		}
	} else {
		inner = f->source;
		obs_source_t *src = obs_get_source_by_name(inner.c_str());
		if (!src)
			err = "source '" + inner + "' not found";
		else {
			if (!obs_scene_find_source(dual, inner.c_str())) {
				obs_sceneitem_t *it = obs_scene_add(dual, src);
				struct vec2 pos = {0, 0}, bounds = {(float)ovi.base_width, (float)ovi.base_height};
				obs_sceneitem_set_pos(it, &pos);
				obs_sceneitem_set_bounds_type(it, OBS_BOUNDS_SCALE_INNER);
				obs_sceneitem_set_bounds(it, &bounds);
			}
			// The same capture is what the warm state keeps in the main scene, transparent under
			// its hide filter - and a filter is on the source, so it made the window transparent
			// too. Take the filter off and hide the main-scene item instead: rendering inside the
			// window keeps the capture just as warm.
			obs_source_t *hf = obs_source_get_filter_by_name(src, Config::hideFilterName());
			if (hf) {
				obs_source_set_enabled(hf, false);
				obs_source_release(hf);
			}
			if (obs_sceneitem_t *main = obs_scene_find_source(scene, inner.c_str()))
				obs_sceneitem_set_visible(main, false);
			obs_source_release(src);
			dualInner_ = inner;
		}
	}
	// a small frame and their name over the feed, drawn by the same page as the main look, told
	// to keep everything small because the window is
	bool look = cfg.dualLook && cfg.lookEnabled() && err.empty();
	std::string lookName = Config::dualLookName();
	if (look) {
		std::string url = overlayUrl(cfg, f->name);
		url += (url.find('?') == std::string::npos ? "?" : "&") + std::string("small=1&frame=1&scale=") +
		       std::to_string(std::clamp(cfg.dualNameScale, 25, 400));
		std::string e2 = ensureBrowserSource(dual, lookName.c_str(), url, true);
		if (!e2.empty() && log)
			log("Dual POV look: " + e2);
	}
	// only the chosen feed, and the look over it, are inside the window
	struct Keep {
		std::string feed, look;
		bool showLook;
	} keep{inner, lookName, look};
	obs_scene_enum_items(
		dual,
		[](obs_scene_t *, obs_sceneitem_t *it, void *p) {
			auto *k = (Keep *)p;
			obs_source_t *s = obs_sceneitem_get_source(it);
			std::string n = s ? obs_source_get_name(s) : "";
			obs_sceneitem_set_visible(it, n == k->feed || (k->showLook && n == k->look));
			if (k->showLook && n == k->look)
				obs_sceneitem_set_order(it, OBS_ORDER_MOVE_TOP);
			return true;
		},
		&keep);
	// opacity
	{
		obs_source_t *fl = obs_source_get_filter_by_name(dualSrc, "Kennel.gg dual opacity");
		obs_data_t *st = obs_data_create();
		obs_data_set_double(st, "opacity", std::clamp(cfg.dualOpacity, 10, 100) / 100.0);
		if (!fl) {
			fl = obs_source_create_private("color_filter_v2", "Kennel.gg dual opacity", st);
			if (fl)
				obs_source_filter_add(dualSrc, fl);
		} else
			obs_source_update(fl, st);
		obs_data_release(st);
		if (fl)
			obs_source_release(fl);
	}
	if (!item)
		item = obs_scene_add(scene, dualSrc);
	if (item) {
		float w = (float)(cfg.dualW * ovi.base_width), h = w * 9.0f / 16.0f;
		struct vec2 pos = {(float)(cfg.dualX * ovi.base_width), (float)(cfg.dualY * ovi.base_height)},
			    bounds = {w, h};
		obs_sceneitem_set_pos(item, &pos);
		obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
		obs_sceneitem_set_bounds(item, &bounds);
		obs_sceneitem_set_visible(item, err.empty());
		moveToTop(item);
		raiseOnTop(cfg);
	} else
		err = "could not add the dual-POV window to the scene";
	obs_source_release(dualSrc);
	obs_source_release(ss);
	return err;
}

std::vector<std::string> Switcher::apply(const Config &cfg, bool on)
{
	std::vector<std::string> errors;
	const Friend *f = cfg.active();
	if (!f) {
		errors.push_back("no squad mate set");
		return errors;
	}
	obs_source_t *ss = sceneSource(cfg);
	if (!ss) {
		errors.push_back("no scene");
		return errors;
	}
	obs_scene_t *scene = obs_scene_from_source(ss);
	std::string name = cfg.sourceFor(*f);

	// 1. the friend's video and its audio
	{
		if (f->isWeb() && on) {
			std::string e = ensureBrowserSource(scene, name.c_str(), webUrl(*f), true);
			if (!e.empty())
				errors.push_back(e);
		}
		obs_source_t *src = obs_get_source_by_name(name.c_str());
		obs_sceneitem_t *item = obs_scene_find_source(scene, name.c_str());
		if (!src || !item) {
			errors.push_back("friend source '" + name + "' is not in scene '" + obs_source_get_name(ss) +
					 "'");
		} else {
			if (on && cfg.bringToFront)
				moveToTop(item);
			if (cfg.keepWarm) {
				ensureHideFilter(src);
				obs_source_t *hf = obs_source_get_filter_by_name(src, Config::hideFilterName());
				if (on) {
					if (hf)
						obs_source_set_enabled(hf, false);
					obs_source_set_muted(src, !cfg.friendAudio);
					obs_sceneitem_set_visible(item, true);
				} else {
					obs_source_set_muted(src, true);
					if (hf)
						obs_source_set_enabled(hf, true);
					obs_sceneitem_set_visible(item, true);
				}
				if (hf)
					obs_source_release(hf);
			} else {
				obs_source_t *hf = obs_source_get_filter_by_name(src, Config::hideFilterName());
				if (hf) {
					obs_source_set_enabled(hf, false);
					obs_source_release(hf);
				}
				obs_source_set_muted(src, !(on && cfg.friendAudio));
				obs_sceneitem_set_visible(item, on);
				if (!on)
					hideEverywhere(name);
			}
		}
		if (src)
			obs_source_release(src);
	}

	// 1b. a companion audio source (Discord): follows the same show/hide, mute-based in warm mode
	if (!f->audioSource.empty()) {
		obs_source_t *a = obs_get_source_by_name(f->audioSource.c_str());
		obs_sceneitem_t *ai = obs_scene_find_source(scene, f->audioSource.c_str());
		if (a && ai) {
			// muted unless "play the squad mate's own game audio" is ticked, in every mode
			obs_source_set_muted(a, !(on && cfg.friendAudio));
			obs_sceneitem_set_visible(ai, cfg.keepWarm || on);
		}
		if (a)
			obs_source_release(a);
	}

	// 1c. one feed with sound: theirs while their sound is on, and every other squad mate muted
	applyFriendAudio(cfg, on);

	// 2. the look overlay, above the friend
	{
		std::string e = updateLook(cfg, on && cfg.lookEnabled());
		if (!e.empty())
			errors.push_back("look: " + e);
	}

	// 3. game audio: mute on the way down, restore the exact previous state on the way up. Not for a
	// Discord squad mate: their sound is not handled (Discord hands OBS one mix for the whole call),
	// so yours stays up while they are shown.
	for (auto &input : cfg.muteWhileDowned) {
		if (on && f->kind == FriendKind::Discord)
			break;
		obs_source_t *src = obs_get_source_by_name(input.c_str());
		if (!src) {
			if (on)
				errors.push_back("audio '" + input + "' not found");
			continue;
		}
		if (on) {
			if (!prevMute_.count(input))
				prevMute_[input] = obs_source_muted(src);
			obs_source_set_muted(src, true);
		} else if (prevMute_.count(input)) { // only what this plugin muted goes back
			obs_source_set_muted(src, prevMute_[input]);
			prevMute_.erase(input);
		}
		obs_source_release(src);
	}
	obs_source_release(ss);
	raiseOnTop(cfg); // the streamer's camera and alerts stay over whatever we just showed
	return errors;
}
