#pragma once
#include <functional>
#include <map>
#include <string>
#include <cstdint>
#include <vector>
#include <obs.h>
#include "capture.h"
#include "config.h"

/// Everything POVBridge does to OBS: show/hide the friend, the look overlay, mute game audio.
/// Call on the UI thread.
class Switcher {
public:
	std::function<void(const std::string &)> log;
	std::string lastVerticalWhere_; // the vertical scene as last logged

	/// Show the active squad mate (on) or go back to the streamer's own POV. Returns problems, if any.
	std::vector<std::string> apply(const Config &cfg, bool on);
	/// Warm mode: friend source present in the scene, transparent and muted.
	void armWarm(const Config &cfg);
	void armOne(const Config &cfg, const Friend &f);
	/// The dual-POV window: a squad mate's feed, small, over your own POV. Creates what it needs.
	/// The small window. `rearm`: when taking it down while you are alive, put the squad mate's
	/// capture back into its warm state; false while the full-screen swap is showing them instead.
	std::string applyDual(const Config &cfg, bool on, bool rearm = true);
	void shutdown(); // on OBS exit, before modules unload
	/// Look overlay on/off (also used for preview).
	std::string updateLook(const Config &cfg, bool on);

	static std::vector<std::string> sceneNames();
	/// Scenes of another canvas (Aitum Vertical's), which the scene list does not show.
	/// Scenes on the other canvases (canvas name, scene name); canvas "" for scenes an older
	/// vertical plugin keeps outside the scene list.
	static std::vector<std::pair<std::string, std::string>> otherCanvasScenes();
	/// The vertical scene as a source (a reference: release it), or null.
	static obs_source_t *verticalSceneSource(const Config &cfg);
	/// The instant replay on the vertical scene as well: full width, centred (the clip is 16:9).
	/// pathV: the vertical canvas's own clip of the moment; "" = show the horizontal one there.
	std::string playMediaVertical(const Config &cfg, int scalePct, bool frame, const std::string &pathV = "");
	/// A short sound into the stream's mix: a media source with the file, in the plugin's scene.
	std::string playSound(const Config &cfg, const std::string &path, int volumePct);
	/// The swap and the look overlay in the vertical scene, if one is set. "" or a problem.
	std::string applyVertical(const Config &cfg, bool on);
	/// Scene items in the plugin's scene, topmost first: name and source type.
	std::vector<std::pair<std::string, std::string>> sceneItems(const Config &cfg);
	/// Fingerprint of what a source shows right now; sample it fast to measure its real frame rate.
	uint64_t feedHash(const std::string &sourceName);
	/// A Discord window that is not the main one: a popped-out share or call. Discord titles a
	/// popped-out tile with the person's username once it has drawn, so the title says whose it is.
	struct Popout {
		std::string title, cls, window; // window: OBS's "title:class:exe" spelling, ready to set
		bool minimized = false;
		uintptr_t hwnd = 0;
	};
	/// Discord stops drawing a window that is completely covered, and the capture goes black. Pin
	/// the pop-out above other windows and tuck it to the edge of its screen with a few pixels
	/// showing: Discord then keeps drawing all of it, and the capture takes all of it. Returns
	/// true if the window was moved this call (false: already tucked, or no window).
	/// `slot` staggers them down the edge: two pop-outs on the same spot would cover each other's
	/// sliver and Discord would stop drawing the one underneath.
	static bool tuckPopout(const Popout &p, int slot);
	/// Park the pop-out on monitor `mon` (0-based), fully visible, on top, `slot` of `total` places
	/// from the top; a window wider than the monitor is shrunk to fit, and more windows than fit are
	/// staggered rather than stacked on one spot. Returns true if it moved.
	static bool parkPopout(const Popout &p, int mon, int slot, int total);
	/// The opposite: off the top, back fully on screen.
	static void untuckPopout(const Popout &p);
	/// The monitors, as "2560x1600 at 0,0" strings in the order parkPopout counts them.
	static std::vector<std::string> monitors();
	static std::vector<Popout> discordPopouts();
	/// Give a squad mate their own capture of this pop-out, bound by exact title. "" or a problem.
	std::string bindPopout(const Config &cfg, Friend &f, const Popout &p);
	/// Back onto the shared Discord capture; their own one is deleted.
	void unbindPopout(const Config &cfg, Friend &f);
	/// Crop a Discord window capture down to the picture inside it. "" or a problem.
	std::string trimToContent(const Config &cfg, const Friend &f);
	/// Put the streamer's own camera and alerts back over the top of everything we add.
	void raiseOnTop(const Config &cfg);
	/// The same for the vertical scene: its own list, or the main list's names when it has none.
	void raiseOnTopV(const Config &cfg);
	std::vector<std::pair<std::string, std::string>> sceneItemsV(const Config &cfg);
	std::vector<std::string> guessOnTopV(const Config &cfg);
	/// A first guess at what belongs on top: cameras, and anything that looks like alerts.
	std::vector<std::string> guessOnTop(const Config &cfg);
	/// Hide every scene item of this source in every scene (belt and braces for the way back).
	static int hideEverywhere(const std::string &sourceName);
	int hideAllFriends(const Config &cfg);
	/// One feed with sound: while `showing` and cfg.friendAudio, the active squad mate's video and
	/// audio sources are unmuted and every other squad mate's are muted; otherwise all of them are
	/// muted. Only sources that belong to a squad slot are touched, never the streamer's own.
	void applyFriendAudio(const Config &cfg, bool showing);
	/// Play a video file on the stream: a media source, `scalePct` of the canvas, centred, kept
	/// under the always-on-top list. Loads and starts it; seek and stop are the caller's, on
	/// timers, since the length is only known once it has loaded. "" or a problem.
	std::string playMedia(const Config &cfg, const std::string &path, int scalePct, int volumePct,
			      bool frame = false, const std::string &pathV = "");
	/// The playing file's length in ms (0 until it has loaded), and a seek into it.
	int64_t mediaDurationMs() const;
	void seekMedia(int64_t ms);
	bool mediaEnded() const;
	int mediaState() const;      // obs_media_state, OBS_MEDIA_STATE_NONE when there is no source
	int64_t mediaTimeMs() const; // where playback is in the file
	void stopMedia(const Config &cfg);
	/// Choices a source kind offers for one of its list properties (e.g. window_capture "window").
	static std::vector<std::pair<std::string, std::string>> listProperty(const char *kind,
									     const char *prop); // name, value
	static bool kindAvailable(const char *kind);
	/// Create an input of this kind (or reuse one with the name), put it in the scene, optionally full-canvas. Returns "" or an error.
	std::string createInScene(const Config &cfg, const char *kind, const std::string &name, obs_data_t *settings,
				  bool fullCanvas, bool visible, bool toBottom = false);
	/// The sources the plugin made for this squad mate (never one of theirs, never the shared one).
	static std::vector<std::string> friendSourceNames(const Config &cfg, const Friend &f);
	/// Take those out of every scene and delete them. Returns how many went.
	int removeFriendSources(const Config &cfg, const Friend &f);
	/// The Discord audio captures earlier builds made, deleted once. Returns how many went.
	int removeDiscordAudio(Config &cfg);
	/// Rename what builds before 0.7.0 created, once. Returns how many sources moved.
	int migrateNames(Config &cfg);
	/// Sources a squad mate needs, created and placed. Fills f.source / f.audioSource.
	std::string createFriendSources(const Config &cfg, Friend &f);
	std::string createGameCapture(Config &cfg);

private:
	Capture trimCap_, hashCap_;        // renders a frame of a feed to find its borders
	obs_scene_t *dualScene_ = nullptr; // the private nested scene behind the dual-POV window

public:
	static std::vector<std::pair<std::string, std::string>> inputs(); // name, id
	static std::string webUrl(const Friend &f);
	static std::string vdoPushUrl(const Friend &f);
	static std::string overlayUrl(const Config &cfg, const std::string &friendName);

private:
	std::map<std::string, bool> prevMute_;
	std::string dualInner_; // the capture inside the dual window while it is up: never re-armed
	obs_source_t *sceneSource(const Config &cfg); // +ref
	std::string ensureBrowserSource(obs_scene_t *scene, const char *name, const std::string &url, bool rerouteAudio,
					int width = 0, int height = 0);
	std::string ensureHideFilter(obs_source_t *src);
	static void moveToTop(obs_sceneitem_t *item) { obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP); }
};
