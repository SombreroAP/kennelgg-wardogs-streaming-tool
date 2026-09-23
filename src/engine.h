#pragma once
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>
#include <memory>
#include <QObject>
#include <QImage>
#include <QTimer>
#include <QSet>
#include <QHash>
#include <QDateTime>
#include <QElapsedTimer>
#include "bridge.h"
#include "capture.h"
#include "clips.h"
#include "voice.h"
#include "roster.h"
#include "config.h"
#include "detector.h"
#include "switcher.h"

/// The state machine. Lives on the Qt main thread; capture + matching run on a worker per poll.
class Engine : public QObject {
	Q_OBJECT
public:
	explicit Engine(QObject *parent = nullptr);
	~Engine() override;

	Config cfg;
	Switcher sw;
	Bridge bridge;
	Clips clips;
	VoiceTap voice; // the microphone, on its way to ClipHound
	/// Start or stop the microphone tap to match the settings and whether ClipHound is connected.
	void applyVoice();
	void sendVoiceConfig();   // the voice settings to ClipHound, on its own
	QString appVersion_;      // what the connected ClipHound said it is
	QDateTime appLaunchedAt_; // when we last started ClipHound ourselves
	/// A command from a controller on the bridge (the Stream Deck plugin): the same verbs as
	/// voice, plus toggles, with none of the voice gates.
	void onControl(const QJsonObject &o);
	/// What a controller shows on its keys: sent to every bridge client whenever it changes.
	QJsonObject stateJson() const;
	void broadcastState();
	QTimer stateTimer_; // stateChanged fires a lot; the broadcast is coalesced
	QString voiceStatus() const { return voiceStatus_; }

	/// One area the dock's health strip shows: fine, needs a look, broken, or switched off. Each
	/// knows what would fix it, as actions the dock offers as buttons.
	struct HealthItem {
		QString key;   // game | scene | cliphound | discord | replay | voice
		QString label; // what the dot says
		int level = 0; // 0 fine, 1 needs a look, 2 broken, 3 off; the item is left out when not in use
		QString why;   // one plain line
		QList<QPair<QString, QString>> fixes; // action id, button text
	};
	QList<HealthItem> health() const;
	/// A message in the dock instead of a window over OBS: nothing modal ever opens by itself while
	/// someone streams. Problems from the health strip first, then one-off notes.
	struct Banner {
		QString id;
		int level = 0; // 0 note, 1 needs a look, 2 broken
		QString text;  // rich text
		QList<QPair<QString, QString>> actions;
		bool dismissable = true;
	};
	QList<Banner> banners() const;
	void dismissBanner(const QString &id);
	/// Carry out a health or banner action. False when it is not the engine's to do (it opens a
	/// window: Settings, Setup, the Squad panel, the logs): the dock does those.
	bool runAction(const QString &id);
	/// The dock added these pop-outs: remind to mute them in Discord, and to check in-game names.
	void noteAddedPopouts(const QStringList &names);
	/// Closest was asked for without ClipHound running: say so in the dock, with a way to start it.
	void noteClosestNeedsApp();
	/// Pressed Stop: ClipHound being off is then a choice, not a problem.
	bool appStoppedByUser() const { return appUserStopped_; }
	/// The microphone's level as it goes to ClipHound, dBFS (-120 when silent or not listening).
	double voiceLevelDb() const { return voiceLevelDb_; }
	/// The last thing the listener did with what you said, for the dock: "heard ... → clip saved".
	QString voiceHeard() const { return voiceHeard_; }
	int voiceHeardKind() const { return voiceHeardKind_; } // 0 nothing yet, 1 command, 2 not understood, 3 wake
	QDateTime voiceHeardAt() const { return voiceHeardAt_; }
	bool streamingOrRecording() const;
	/// The scene live in OBS right now, when it is not the scene the plugin works in (else "").
	QString sceneMismatch() const;
	QString playerName() const;
	Roster roster; // who is in Discord voice and who is sharing (published by the Kennel.gg bot)
	void applyRosterConfig();
	/// Bring the squad slots into line with the roster. Adds a slot when somebody goes live in the
	/// call, takes it away when they stop. Slots you added by hand are never touched.
	void syncRoster();
	/// Discord titles a popped-out share with its owner's username. Find those windows, give each
	/// squad mate their own capture of theirs, and take it back when the window goes.
	void watchPopouts();
	void armPopoutWatch();
	/// Every popped-out Discord stream on this PC becomes a squad mate named by its Discord
	/// username, bound to that window. Returns what happened, for the Squad panel.
	QString addPopouts(QStringList *addedOut = nullptr);
	/// Their pop-out back off the top and on screen (when a slot is removed, or the tuck turned off).
	void releasePopout(const Friend &f);
	void releaseAllPopouts();
	/// Bring every bound pop-out back on screen so its own controls can be used (Discord's volume,
	/// for one), and stop tucking until this is turned off again.
	void showPopouts(bool show);
	bool popoutsShown() const { return popoutsShown_; }
	/// Put this squad mate in the dual window and keep it there until it is turned off by hand.
	void showInDual(int idx, const QString &why = "squad panel");
	/// Instant replay: the last highlight, cut to the action (replayPreS before the first kill to
	/// replayPostS after the last), on the stream at replayScale of the canvas. `why` for the log.
	void playReplay(const QString &why = "dock");
	/// The newest highlights compilation in the highlights folder, full screen.
	void playCompilation(const QString &why = "dock");
	/// Ask ClipHound to build this session's compilation from the clips saved since the stream (or
	/// OBS) started. `thenPlay`: play it on the stream when it is ready.
	void requestHighlights(const QString &why, bool thenPlay = false);
	QString highlightsDir() const;
	bool highlightsBuilding() const { return highlightsBuilding_; }
	/// OBS started or stopped streaming: the session boundary for the compilation.
	void onStreaming(bool live);
	void stopReplay(const QString &why = "dock");
	/// A "!replay" from chat: plays if the cooldown has passed. Returns "" or why not.
	QString chatReplay(const QString &who);
	bool replaying() const { return replayLengthMs_ > 0; }
	QString replayWhat() const { return replayWhat_; } // what is playing, for the dock
	/// Is this Discord username you (your own stream is never a squad mate).
	bool isMe(const QString &discordUser) const;
	/// Squad automation (the roster) is for members of the Kennel.gg Discord: the bot publishes
	/// its member list and the username from Setup is checked against it.
	enum class Access { Ok, NoUsername, NotMember, Unknown };
	Access rosterAccess() const;
	bool rosterOpen() const; // Ok, or nothing to check against yet
	/// The roster is on, readable, and not locked: what the dock's live-only lists and the liveness
	/// checks go by. When the address cannot be read the pop-outs alone decide, as without a roster.
	bool rosterLive() const;
	QString rosterStatus() const;
	/// The Kennel.gg Discord, through the plugin's own invite.
	QString discordUrl() const;
	/// Save the streamer's Discord username and turn the roster on: the dock's unlock path.
	void setMyDiscord(const QString &user);
	/// Ask the Discord app on this PC who it is logged in as, off the UI thread, and take that as
	/// the username when none was given (or always, byHand). Fires discordUserDetected either way.
	void detectDiscordUser(bool byHand = false);
	/// OBS's replay-buffer length is the user's: read it, never write it unasked.
	void applyReplaySeconds();
	/// The user changed the length in our settings: write it into OBS, restart the buffer if it runs.
	void setReplaySecondsByUser(int seconds);
	void syncAppPort();
	QString appStatus() const { return appStatus_; }
	bool appConnected() const { return bridge.clients() > 0; }
	void onReplaySaved() { clips.onReplaySaved(); }
	void launchApp();
	void closeApp();         // on OBS exit
	void stopApp();          // user pressed Stop
	bool appRunning() const; // process alive (even if not connected yet)
	/// The last lines of ClipHound's log.
	QStringList appLogTail(int lines = 12) const;
	QString appState() const; // "connected" | "starting" | "crashed" | "stopped"
	void pushAppConfig();     // send the ClipHound settings to the app

	/// One line of the game's NEARBY list, as ClipHound read it.
	struct NearbyEntry {
		QString name;         // what the OCR read
		QString match;        // the squad mate's in-game name it matched, "" = nobody we know
		int dist = 0;         // metres
		bool unknown = false; // nearby, but the metres could not be read
	};
	QList<NearbyEntry> nearby() const { return nearby_; }
	bool nearbyFresh() const;
	QString nearbyText() const;   // "MasterBaiter 9 m  ·  ChusanDesu 76 m"
	QString nearbyStatus() const; // the same, or why there is no reading
	/// Index of the configured squad mate the game says is nearest, or -1. Fills metres if given.
	int closestFriend(int *metres = nullptr, QString *problem = nullptr) const;
	void nearbyTest(); // ask ClipHound to read the NEARBY area once and say what it saw
	/// Ask kennel.gg whether there is a newer build. Nothing is sent but the request itself.
	void checkForUpdate(bool manual);
	QString updateState() const { return updateState_; }
	QString newVersion() const { return newVersion_; }
	QString newVersionUrl() const { return newUrl_; }
	QString newVersionNotes() const { return newNotes_; }
	bool updateAvailable() const;
	static bool isNewer(const QString &a, const QString &b); // is a newer than b
	void twitchLogin();
	void twitchLogout();
	QJsonObject twitchStatus() const { return twitch_; }
signals:
	void twitchStatusChanged();
	void appConfigReceived();
	void updateChecked();
	void nearbyTested(const QJsonObject &result);
	/// Discord on this PC answered who it is logged in as ("" when it did not). byHand: from a button.
	void discordUserDetected(const QString &user, bool byHand);
	/// The damage log keeps scoring close to, never over, the line for every wording we have:
	/// the game is probably in a language the plugin does not know. Once per install.
	void languageUnknown();
	/// The microphone's level, about ten times a second while voice listens.
	void voiceLevel(double db);

public:
	bool applied() const { return applied_; }
	bool dualOn() const { return dualOn_; }
	/// Turned on by hand (the Force button, a hotkey, the Squad panel): the vehicle detector may
	/// not close it. Off means whatever is up is the detector's.
	bool dualForced() const { return dualOn_ && !dualAutoOn_; }
	QString vehicleSeat() const { return vehicleSeat_; }
	bool detected() const { return detected_; }
	bool revivingRecent() const;
	double reviveProgress() const { return reviveProgress_; }
	Match lastGame() const { return lastGame_; }
	Match lastRevive() const { return lastRevive_; }
	bool hasTemplate() const { return detGame_.hasTemplate(); }
	bool customTemplate() const { return cfg.customTemplateWidthFrac > 0; }
	QImage lastFrame() const;
	void wantPreview(bool on) { previewWanted_ = on; }
	std::string stateText() const;
	QStringList recentLog() const { return logLines_; }
	QStringList recentEvents() const { return events_; }
	void addEvent(const QString &text);

	void start();
	void stop();
	/// OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP: OBS is about to release every source (exit, or a
	/// scene-collection change). Lets go of everything of ours first; reloadConfig() starts it again.
	void sceneCleanup();
	void reloadConfig(); // after the settings dialog saved, and after a scene-collection change
	void loadTemplates();
	void applySearchWidth();
	static QString langName(const std::string &lang); // "es" -> "Spanish"
	/// PNG of the game source as the plugin sees it. Returns the path, or a message starting with a capital.
	QString saveFrame();
	/// The game source at its own resolution, for saving or for learning the HUD.
	QImage grabNative();
	/// Cut a template from where the damage log is right now, so it matches this HUD exactly.
	QString learnTemplate(const QImage &native, QRectF rect);
	/// Squads fill themselves in from pop-outs now, so an empty squad at start is normal: the setup
	/// runs until it has been finished once, or while there is no game source.
	bool needsSetup() const { return cfg.gameSource.empty() || (!cfg.setupDone && cfg.friends.empty()); }
	/// True once: on the third start with the plugin, the dock shows the support note.
	bool wantsSupportNote() const { return !cfg.supportAsked && cfg.startCount >= 3; }
	void supportNoteShown();
	void languageNoteShown();
	/// A command ClipHound heard after the wake word. cmd: replay | dual | dual_on | dual_off | clip |
	/// show | me | highlights. `name`: for show, the squad mate as heard. `heard`: the words.
	void onVoiceCommand(const QString &cmd, const QString &name, const QString &heard);
	/// Tick every desktop-audio input once, when nothing was chosen yet.
	void autoPickAudio();

public slots:
	void applyNow(bool on, const QString &why);
	void toggle();
	void toggleDual();
	void setDual(bool on, const QString &why);

	void setActive(int idx);
	void setEnabled(bool on);
	/// The sound of whoever is on screen, on your stream. On: the squad mate being shown is the one
	/// feed with sound and the unmute moves with the picture; off: every squad mate is silent. The
	/// Switch tab's tick box. Not for Discord squad mates: their sound is not handled.
	void setFriendAudio(bool on);
	void captureTemplate();
	void useBuiltInTemplate();
	void previewLook(bool on);
	void clipNow(const QString &title = "manual", const QStringList &tags = {"manual"},
		     const QString &source = "hotkey");
	void log(const QString &msg);

signals:
	void stateChanged();
	void logged(const QString &msg);
	void frameUpdated();

private:
	using AltSet = std::vector<std::unique_ptr<Detector>>;
	struct Result {
		bool ok = false;
		Match game;
		Match revive;
		int altLang = -1;             // index into altLangs_ when another language's wording scored best
		std::shared_ptr<AltSet> alts; // the set that index belongs to
		double progress = -1;
		std::vector<uint8_t> bgra;
		int w = 0, h = 0, ls = 0;
	};
	void tick();
	void onResult(Result r);
	void frameTick();
	void onBridgeMessage(const QJsonObject &o);
	void onNearby(const QJsonObject &o);
	void onVehicle(const QString &seat);
	/// ClipHound saw the inventory screen open (for two seconds) or close.
	void onInventory(bool open);
	bool invApplied_ = false; // the swap on screen is the inventory's to undo
	void clearNearby();
	void pickClosest(const QString &why, bool decisive = false);
	void switchTo(int idx, const QString &why);
	int friendIndexFor(const QString &gameName) const;
	int nearbyDistanceOf(int friendIdx) const;

public:
	bool feedUsable(const Friend &f) const;
	/// Whether a squad mate has a picture to show right now. Live: their pop-out is bound, or the
	/// Discord roster says they are streaming. Off: the roster has them in voice and not streaming,
	/// so there is nothing to show. Unknown: no way to tell (Twitch, an OBS source, a Discord slot
	/// the roster does not know) - treated as live, as before.
	enum class Feed { Live, Unknown, Off };
	Feed feedState(const Friend &f) const;
	/// Offered on the dock (and cycled through) right now: in a Kennel.gg voice channel, whoever is live
	/// in it; anywhere else, the squad mates ticked as playing, unless known not to be streaming.
	bool inSquadNow(const Friend &f) const;
	QString feedStateText(const Friend &f) const; // "live" / "not streaming" / ""
	/// The best squad mate to show when nothing nearer is known: the active one if not Off, else
	/// any Live one, else -1 when everyone is known to be off.
	int anyLiveFriend() const;

private:
	void askNearbyNow();
	void sendPov(const QString &state);
	void detect(const Match &m);

	QTimer timer_, frameTimer_, downDelay_, upDelay_;
	QTimer popoutTimer_;
	// Twitch / Kick / YouTube squad mates: is their channel live right now? Asked every minute
	// for members of the Kennel.gg Discord; a slot whose channel is offline is not shown
	QTimer webLiveTimer_;
	QHash<QString, Feed> webLive_; // "kind:channel" -> Live / Off (Unknown = not asked or no answer)
	QSet<QString> webLiveBusy_;    // keys with a request in flight
	void webLiveTick();
	static QString webLiveKey(const Friend &f);
	QTimer replayTimer_; // polls the playing replay: seek once loaded, stop at its end
	qint64 replayStartMs_ = 0, replayEndMs_ = 0, replayLengthMs_ = 0;
	QElapsedTimer replayClock_;
	bool replaySought_ = false, replayShown_ = false;
	int replaySeekChecks_ = 0;
	QString replayWhat_;
	Clips::Entry pendingReplay_;
	QDateTime sessionStart_;
	bool highlightsBuilding_ = false, highlightsThenPlay_ = false;
	QTimer healthTimer_; // OBS's dropped-frame counters to ClipHound, so segment work backs off
	void sendObsHealth();
	unsigned long lastUserObjects_ = 0;
	QDateTime lastChatReplay_;
	void replayTick();
	bool popoutsShown_ = false;
	QString popoutNote_; // the unnamed pop-out we last mentioned, so the log says it once
	Access lastAccess_ = Access::Unknown;
	QString lastRosterStatus_;
	void checkAccess(); // say it once when the roster locks or unlocks
	std::atomic<bool> busy_{false}, stopping_{false}, frameBusy_{false}, stopped_{false};
	std::atomic<int> workers_{0}; // detached worker threads in flight (they count themselves out)
	bool paused_ = false;         // sceneCleanup() ran; reloadConfig() starts the timers again
	void stopTimers();
	void waitWorkers(int ms);
	Capture capGame_, capFriend_, capRoi_;
	QString appStatus_;
	QJsonObject twitch_;
	qint64 appPid_ = 0;
	QDateTime appStartedAt_;
	bool appCrashReported_ = false;
	Detector detGame_, detRevive_;
	// game language on auto: the other wordings, searched alongside until one of them matches
	// a shared set, so a worker in flight keeps the set it started with when settings replace it
	std::shared_ptr<AltSet> altDets_ = std::make_shared<AltSet>();
	std::vector<std::string> altLangs_;
	static bool loadLangTemplate(Detector &d, const std::string &lang);
	bool dualOn_ = false, dualAutoOn_ = false;
	QString vehicleSeat_;
	QString updateState_, newVersion_, newUrl_, newNotes_;
	bool applied_ = false, detected_ = false, applying_ = false, lookPreview_ = false, previewWanted_ = false;
	int downRun_ = 0, upRun_ = 0, tickN_ = 0;
	double peakScore_ = 0;
	double nearBest_ = 0; // best below-threshold score since nearSince_
	int nearMinutes_ = 0; // minutes in which the best score came close without a match
	double voiceLevelDb_ = -120;
	qint64 voicePcmMs_ = 0, voiceLoudMs_ = 0, voiceAttachMs_ = 0;
	QString voiceHeard_;
	int voiceHeardKind_ = 0;
	QDateTime voiceHeardAt_;
	void setVoiceHeard(const QString &text, int kind);
	QSet<QString> dismissed_;
	bool langBanner_ = false;            // the damage log never matched a wording we have
	QString oldCopy_;                    // an older copy of the plugin found installed
	QHash<QString, quintptr> minimised_; // squad mates whose pop-out is minimised, and its window
	QStringList muteNames_;              // pop-outs just added: mute them in Discord
	QStringList nameCheck_;              // just added while Closest is on: check their in-game names
	bool closestAsk_ = false;            // Closest pressed without ClipHound
	bool appUserStopped_ = false;
	QString frameSaved_;           // a frame saved from a banner, to attach to a ticket
	double lastNearBest_ = -1;     // the best below-threshold score of the last whole minute
	QString voiceStatus_;          // what ClipHound says the listener is doing
	QString voicePending_;         // the manual clip waiting for its spoken name
	bool replayAfterClip_ = false; // "clip replay": play the clip back once it is saved
	int replayAfterClipTries_ = 0; // waits of 500 ms left for the vertical file to land
	void replayAfterClipTick();
	bool voiceFlowing_ = false; // the first audio piece has been sent since the tap was attached
	std::chrono::steady_clock::time_point nearSince_ = std::chrono::steady_clock::now();
	float downX_ = 0, downY_ = 0; // where the log was found when we went down (it does not move)
	std::chrono::steady_clock::time_point fullSince_; // last poll the log scored a clean match in that spot
	static constexpr int kHoldMs = 3000;              // how long a washed-out log is held as still there
	std::chrono::steady_clock::time_point downSince_, lastReviveSeen_;
	Match lastGame_, lastRevive_;
	double reviveProgress_ = -1;
	mutable std::mutex frameMx_;
	QImage lastFrame_;
	std::string lastWatchError_;
	QStringList logLines_;
	QStringList events_;
	QList<NearbyEntry> nearby_;
	QDateTime nearbyAt_, nearbyEmptySince_;
	QString nearbyLine_, nearbyWho_;
	std::chrono::steady_clock::time_point lastPick_, lastNearbyWarn_;
};
