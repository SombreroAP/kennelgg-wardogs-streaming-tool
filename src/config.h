#pragma once
#include <string>
#include <vector>

enum class FriendKind {
	Twitch = 0,
	VdoNinja = 1,
	ObsSource = 2,
	Discord = 3,
	/* 4 is retired */ Kick = 5,
	YouTube = 6
};

struct Friend {
	std::string name;
	FriendKind kind = FriendKind::Twitch;
	std::string source;      // OBS source name (ObsSource / Discord: the video source)
	std::string audioSource; // a companion audio source, if the kind has one (none since 0.10.3)
	std::string channel;     // Twitch login, VDO.Ninja stream id, or the Discord window
	int vdoHeight = 1080, vdoFps = 60, vdoKbps = 12000; // VDO.Ninja quality (push and view links)
	std::string vdoCodec = "h264";
	std::string gameName; // their name in the game's NEARBY list ("" = the name above)
	bool trim = true;     // Discord: crop the window's flat borders away, leaving the game picture
	bool isWeb() const
	{
		return kind == FriendKind::Twitch || kind == FriendKind::VdoNinja || kind == FriendKind::Kick ||
		       kind == FriendKind::YouTube;
	}
	const std::string &nearName() const { return gameName.empty() ? name : gameName; }
	/// Added by the Discord roster rather than by hand. The plugin owns these: it adds them when
	/// somebody goes live in the call and takes them away again when they stop, so a slot is never
	/// left behind. Slots you made yourself are never touched.
	bool fromRoster = false;
	/// Their Discord username (the handle), when the roster gave it. A popped-out share is titled
	/// with this, not the display name, so it is what the pop-out watcher matches on.
	std::string handle;
	/// "Any Discord window": the share is watched inside Discord's own window. Every squad mate set
	/// up this way is looking at the same window, so they share one capture.
	static const char *anyDiscordWindow() { return "Discord:Chrome_WidgetWin_1:Discord.exe"; }
	bool sharesDiscordCall() const { return kind == FriendKind::Discord && channel == anyDiscordWindow(); }
	/// Their popped-out window, when the plugin has found one: OBS's "title:class:exe" spelling.
	/// While set, `source` is a capture of that window and `baseSource` is the one to go back to.
	std::string popout, baseSource;
	long long popoutMissingMs = 0; // how long the window has been gone, runtime only
	bool onPopout() const { return kind == FriendKind::Discord && !popout.empty(); }
	static const char *discordCallSourceName() { return "Kennel.gg · Discord call"; }
	static const char *discordCallAudioName() { return "Kennel.gg · Discord call audio"; }
	bool ownsSources() const { return kind == FriendKind::Discord; }
};

struct Config {
	// what to watch / where to switch
	std::string gameSource;
	std::string sceneName; // "" = the scene that is live
	std::vector<Friend> friends;
	int activeFriend = 0;
	std::vector<std::string> muteWhileDowned;
	/// Sources that stay above everything the plugin adds: the streamer's camera, their alerts.
	/// First in the list is the topmost.
	std::vector<std::string> onTop;
	bool onTopV1 = false; // seeded once from what is in the scene
	// A second, portrait canvas (OBS 31.1+ canvases, as Aitum Stream Suite makes them): the swap,
	// the look overlay and the instant replay happen there too. Beta, off until switched on.
	bool verticalEnabled = false;
	bool verticalV2 = false; // one-time: an install that had a vertical scene keeps it on
	std::string canvasV;     // the canvas the vertical scene lives on ("" = found by name alone)
	std::string sceneV;      // the vertical scene the swap is applied in
	bool verticalOn() const { return verticalEnabled && !sceneV.empty(); }
	std::vector<std::string> onTopV; // the vertical scene's camera and alerts (its own sources)
	bool onTopVSeeded = false;
	bool onTopGameFixV1 =
		false; // one-time: the game source taken out of the on-top lists (0.18.16)       // guessed once from that scene
	int lookTopV = 22;            // portrait: the POV tag's height, percent of the canvas from the top
	std::string lookPos = "ml";   // where the POV tag sits: tl tc ml mc bl br
	bool lookPosV1 = false;       // one-time move off the bottom-left corner
	bool audioAutoPicked = false; // desktop audio was ticked automatically once
	bool bringToFront = true;
	bool keepWarm = true;
	bool preloadFeeds = false;   // every squad mate's feed loaded and playing, hidden and silent
	bool friendAudio = true;     // the squad mate on screen is the one feed with sound on your stream
	bool audioDefaults2 = false; // one-time move to "nothing of yours is muted by default"
	bool audioDefaults3 = false; // ...and once more: nothing muted, and no sound taken from their feed
	bool discordShared1 = false; // one-time move to one shared capture of the Discord window
	bool discordAudio1 = false;  // one-time: the Discord audio captures deleted (0.10.3)
	bool nearMax99 = false;      // one-time: the swap-over range moved to 99 m (0.10.6)
	bool audioDefaults4 = false; // the mute list cleared once more: the auto-pick had been refilling it
	bool audioDefaults5 = false; // one-time: the squad mate on screen has sound, on by default (0.9.1)
	bool audioDefaults6 = false; // one-time: your own game sound muted while they are up (0.10.2)
	// Discord roster: the Kennel.gg bot publishes who is in voice and who is sharing, and squad
	// slots fill themselves in from it. Off until an address is entered.
	bool rosterEnabled = false;
	std::string rosterUrl = kennelRosterUrl(); // the Kennel.gg one unless somebody runs their own bot
	/// Your own Discord username. Says which voice channel is yours in the roster, and keeps your own
	/// stream from being added as a squad mate. Optional.
	std::string myDiscord;
	static const char *kennelRosterUrl()
	{
		return "https://kennel.gg/api/voice-KHc1U8CRGy8Ixq2JAeKj6JNAjdo8mDDu.json";
	}
	std::string rosterChannel; // only this voice channel ("" = whichever one people are in)
	std::string rosterGuild;   // only this Discord server ("" = every one the bot can see)
	bool setupDone = false;    // the setup ran once; an empty squad at start is normal with pop-outs
	int startCount = 0;        // OBS starts with the plugin loaded, for the one-time support note
	bool supportAsked = false; // the support note was shown (once, on the third start)
	static const char *supportUrl() { return "https://streamlabs.com/sombrerogg/tip"; }
	/// The Kennel.gg Discord, through the plugin's own invite (its joins are counted apart).
	static const char *kennelDiscordUrl() { return "https://discord.gg/nDyJ7SSM8q"; }
	static const char *kennelHomeGuild() { return "Kennel.gg"; }
	static const char *botInviteUrl()
	{
		return "https://discord.com/oauth2/authorize?client_id=1542623631111098378&scope=bot&permissions=1024";
	}
	int rosterPollS = 6;          // how often to ask
	bool rosterAddSources = true; // create the Discord capture for whoever goes live
	/// Pin every bound pop-out above other windows, tucked to the screen edge with a sliver showing,
	/// so Discord keeps drawing it while the game covers it. Needs the game in borderless windowed.
	bool popoutTuck = true;
	/// Where bound pop-outs live: -1 = tucked to the edge of their own screen (a sliver showing);
	/// 0, 1, ... = parked on that monitor, stacked, fully visible and on top. Parking on a screen
	/// the game and OBS are not on keeps Discord's own volume control on each one within reach.
	int popoutMonitor = -1;
	int clipSeriesS = 45; // clips this close together are a run: "[1 of 3]" names, "part 2" on Twitch
	// Instant replay: the last highlight played back on the stream, cut down to the action.
	int replayPreS = 3;          // seconds before the first kill
	int replayPostS = 5;         // seconds after the last kill
	int replayScale = 75;        // percent of the canvas it takes, centred, under the always-on-top list
	int replayVolume = 40;       // the clip's own sound on the stream, percent, when replaySound is on
	bool replaySound = false;    // off by default: the clip carries your mic and the game from a minute ago
	bool replayHwDecode = false; // the GPU decodes replay files (off: one PC's decoder refused 1440p60 at load)
	int replayCooldownS = 60;    // chat may trigger it this often at most (30 s to 15 min)
	bool replayChat = true;      // the trigger word from subscribers and moderators in chat plays it
	std::string replayWord = "!replay"; // what they type
	std::string chatKick;               // your Kick channel, for the chat trigger (Twitch comes from the login)
	std::string chatYouTube;            // your YouTube channel or @handle, for the chat trigger
	std::string highlightsFolder;       // where the highlights compilations live ("" = <clip folder>/highlights)
	std::string replayLabel = "Instant replay"; // the tag on the replay's frame
	bool highlightsAuto = false;                // build the session's highlights compilation when the stream stops
	int highlightsMax = 12;                     // at most this many clips in it
	// What ClipHound does to a clip file once it is named (Settings, Clips)
	bool clipTrim = true; // cut the file so it starts clipTrimLeadS before the first kill (stream copy)
	int clipTrimLeadS = 10;
	bool runMerge = false;   // clips within the run window become one file of continuous action
	bool runCutGaps = false; // ...with the dead space between kills cut out
	int runGapS = 12;        // a gap longer than this is dead space
	static const char *replaySourceName() { return "Kennel.gg · Replay"; }
	static const char *chimeSourceName() { return "Kennel.gg · Chime"; }
	static const char *replayFrameName() { return "Kennel.gg · Replay frame"; }
	static const char *replayFrameNameV() { return "Kennel.gg · Replay frame (vertical)"; }
	static const char *replaySourceNameV() { return "Kennel.gg · Replay (vertical)"; }
	int vdoBitrateKbps = 12000; // VDO.Ninja video bitrate asked for on both ends (wired or fibre: 12-20 Mbit/s)

	// dual POV: a squad mate's feed in a small window over your own POV (tank / chopper crews)
	bool dualEnabled = false; // the window up from start-up, forced
	bool dualStartV2 = false;
	bool cpuDefaultsV1 =
		false; // one-time: the compilation and run merging go off unless chosen (0.18.0)               // one-time: clears a dualEnabled that ticked itself (0.17.3)
	int dualFriend = -1;                    // index into friends, -1 = none
	std::string dualPreset = "tank-driver"; // tank-driver | tank-gunner | havoc-pilot | havoc-gunner | custom
	double dualX = 0.012, dualY = 0.19, dualW = 0.26; // fractions of the canvas; height keeps 16:9
	int dualOpacity = 100;
	bool dualKeep = false;   // leave the window up when you get out of the vehicle
	bool dualLook = true;    // a small frame and the person's name on the dual window (with the look effects)
	int dualNameScale = 100; // size of the name on the dual window, percent of the default
	static const char *dualLookName() { return "Kennel.gg dual look"; }
	bool dualAuto = false; // ClipHound reads the vehicle keybind list and turns the window on / off
	double vehX = 0.86, vehY = 0.60, vehW = 0.14, vehH = 0.25; // where that list is (fractions)
	static const char *dualSceneName() { return "Kennel.gg dual"; }
	static const char *dualFeedName() { return "Kennel.gg dual feed"; }
	const Friend *dual() const
	{
		return dualFriend >= 0 && dualFriend < (int)friends.size() ? &friends[dualFriend] : nullptr;
	}

	// update check: a small JSON on kennel.gg, no account and no telemetry
	bool updateCheck = true;
	std::string updateUrl = "https://kennel.gg/obs-tools/latest.json";
	std::string updateSkip; // a version the user asked not to be told about again

	// look overlay
	bool lookName = true, lookPlate = true, lookCam = false, lookGrain = false, lookVignette = false;
	bool lookMark = true; // a small Kennel.gg mark in the corner while a squad mate is on screen
	std::string lookLabel = "POV";
	int grainAmount = 40;

	std::string playerName; // your name, for the look overlay and to keep your own stream out of the squad

	// companion app / bridge / clips
	int bridgePort = 47820;
	bool bridgeEnabled = true;
	std::string appPath; // ClipHound (or any companion) to launch when OBS starts
	bool launchApp = false;
	bool closeAppWithObs = true;
	std::string clipFolder; // move renamed replay clips here ("" = leave in OBS's recording folder)
	// ClipHound settings edited in the plugin and pushed to the app over the bridge
	std::string appPlayerName, appLibrary, appBroadcaster;
	bool appTwitchEnabled = false;
	bool appEveryKill = false;
	double feedX = 0.0, feedY = 0.42, feedW = 0.24,
	       feedH = 0.16; // kill-feed area (fractions of the game source) sent to ClipHound
	double appMultikillWindow = 30;
	int appFps = 10; // frames per second ClipHound reads the kill feed at
	// the game's NEARBY list (bottom right), read by ClipHound: show whoever is closest
	bool nearEnabled = false; // pick the squad mate the game says is nearest when you go down
	bool nearFollow = true;   // keep following the nearest one while you are down
	double nearX = 0.80, nearY = 0.79, nearW = 0.19,
	       nearH = 0.14;   // where the NEARBY list is (fractions of the game source)
	int nearMarginM = 15;  // someone must be this many metres closer to take over mid-swap
	int nearMaxM = 99;     // once on screen, only swap over to someone this close or closer
	int nearCooldownS = 4; // shortest gap between two swaps of the feed while down, 1-10 s
	int nearTtlS = 20;     // a reading older than this is stale and ignored
	bool appConfigDirty =
		false; // edited while the app was not connected; push on connect // tell ClipHound to quit when OBS closes (and end it if we started it)
	std::string clipNameTemplate = "{title} - {date} {time}"; // the same words as the Twitch clip, then when
	bool autoStartReplay = true;
	int replaySeconds = 45;               // how far back a clip reaches; written into OBS's replay-buffer setting
	bool invSwitch = true;                // magazine packing: a squad mate's POV while the inventory screen is open
	bool clipOnDowned = false;            // also clip when you get downed (the moment before is in the buffer)
	bool clipUseReplay = true;            // save OBS's own replay buffer on a clip
	std::vector<std::string> clipHotkeys; // OBS hotkey names fired on every clip (e.g. Aitum Backtrack "save")
	std::string backtrackFolder; // where Aitum Backtrack writes; new files there after a trigger get our name

	// detection
	bool autoDetect = true;
	bool enabled = true;
	double threshold = 0.80;
	bool thresholdV2 = false; // one-time move to 0.80 with the wording-only template
	int pollMs = 100;
	int downFrames = 2, upFrames = 2, minDownMs = 0;
	int downDelayMs =
		2000; // wait this long after the damage log appears before showing the squad mate (cancelled if it goes away)
	int upDelayMs = 0;      // wait this long after it disappears before coming back (0 = instant)
	double holdDrop = 0.15; // while downed the log counts as still there down to (threshold - this)
	bool holdV2 = false;    // one-time move to holding through a washed-out poll
	double releaseDrop =
		0.08; // while downed the score is steady; a drop this big below its peak = the log is fading = revived
	double memScale = 0, memX = 0, memY = 0; // where the damage log was last found (fast re-detect)
	bool watchRevive =
		true; // look for "REVIVING" on the friend's feed and switch back instantly when the damage log goes
	double reviveThreshold = 0.80;
	std::string gameLang =
		"auto";            // damage-log wording: auto | en | es (auto tries every language until one matches)
	std::string gameLangFound; // what auto settled on, so later sessions search one wording only
	bool langAskShown = false; // the once-only "language not supported" note has been shown
	// voice: the microphone goes to ClipHound, which names manual clips from what was said and
	// listens for commands after a wake word. Off until switched on.
	bool voiceEnabled = false;
	std::string voiceMic;                 // OBS source name; "" = the first microphone found
	std::string voiceWake = "hey kennel"; // what starts a command ("hey kennel replay")
	bool voiceNames = true;               // manual clips take their title from what was said around them
	bool voiceChime = true;
	bool voiceTones =
		true; // a rising tone when a command is taken, a falling one when the words were not understood
	int voiceChimeVol = 60;             // 0..100
	std::string voiceChimeWhere = "pc"; // pc: this PC's speakers | obs: into the stream's mix | both
	bool voiceCommands = true;          // "<wake> replay", "<wake> show <name>", ...
	// each command on its own switch (all on when commands are on)
	bool voiceCmdReplay = true, voiceCmdClip = true, voiceCmdDual = true, voiceCmdForce = true,
	     voiceCmdChange = true, voiceCmdClosest = true;
	bool wideSearch = false;            // look over the whole frame at more sizes: slower, for unusual HUDs
	double customTemplateWidthFrac = 0; // 0 = built-in damage-log template
	double boxX = 0.84, boxY = 0.62, boxW = 0.13, boxH = 0.035; // capture box for a custom template

	static const char *webSourceName() { return "Kennel.gg web"; }
	/// Browser source a web feed lives in. With preloading each squad mate gets their own, so
	/// every stream is already playing when the swap happens; otherwise they share one.
	std::string webSourceFor(const Friend &f) const
	{
		return preloadFeeds ? std::string(webSourceName()) + " - " + f.name : webSourceName();
	}
	static const char *overlaySourceName() { return "Kennel.gg look"; }
	static const char *overlaySourceNameV() { return "Kennel.gg look (vertical)"; }
	static const char *hideFilterName() { return "Kennel.gg hide"; }
	/// Source names as builds before 0.7.0 made them; renamed once on start.
	static const char *oldPrefix() { return "Kennel "; }
	static const char *newPrefix() { return "Kennel.gg "; }
	std::string sourceFor(const Friend &f) const { return f.isWeb() ? webSourceFor(f) : f.source; }
	const Friend *active() const
	{
		return activeFriend >= 0 && activeFriend < (int)friends.size() ? &friends[activeFriend] : nullptr;
	}
	bool lookEnabled() const { return lookName || lookCam || lookGrain || lookVignette || lookMark; }

	void load();
	void save() const;
	static std::string configDir();
	static std::string configFile(const char *name);
};
