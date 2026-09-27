// The dock's health strip and its banners: what the engine already knows about each part of the
// setup, said in one line with the button that fixes it. Nothing here opens a window by itself.
#include "engine.h"
#include "i18n.h"
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

using Fix = QPair<QString, QString>;

static QString q(const std::string &s)
{
	return QString::fromStdString(s);
}

bool Engine::streamingOrRecording() const
{
	return obs_frontend_streaming_active() || obs_frontend_recording_active();
}

void Engine::setVoiceHeard(const QString &text, int kind)
{
	voiceHeard_ = text;
	voiceHeardKind_ = kind;
	voiceHeardAt_ = QDateTime::currentDateTime();
	emit stateChanged();
}

QList<Engine::HealthItem> Engine::health() const
{
	QList<HealthItem> out;
	qint64 now = QDateTime::currentMSecsSinceEpoch();

	// ----- the game source: seen, and read
	{
		HealthItem h;
		h.key = "game";
		h.label = tx("Game");
		obs_source_t *src = cfg.gameSource.empty() ? nullptr : obs_get_source_by_name(cfg.gameSource.c_str());
		if (cfg.gameSource.empty()) {
			h.level = 2;
			h.why = tx("No game source chosen, so nothing can tell when you are downed.");
			h.fixes = {Fix("settings:general", tx("Choose it"))};
		} else if (!src) {
			h.level = 2;
			h.why = tx("'%1' is not in OBS any more.").arg(q(cfg.gameSource));
			h.fixes = {Fix("settings:general", tx("Choose another"))};
		} else if (!lastWatchError_.empty()) {
			h.level = 2;
			h.why = tx("'%1' shows nothing: is it in a scene and turned on?").arg(q(cfg.gameSource));
			h.fixes = {Fix("settings:general", tx("Check it"))};
		} else if (!detGame_.hasTemplate()) {
			h.level = 2;
			h.why = tx("No downed-screen template is loaded.");
			h.fixes = {Fix("settings:advanced", tx("Fix it"))};
		} else {
			h.level = 0;
			h.why = (detected_ ? tx("Downed") : tx("Watching '%1'").arg(q(cfg.gameSource)));
			if (!cfg.gameLangFound.empty() && cfg.gameLang == "auto")
				h.why += tx(" · game in %1").arg(langName(cfg.gameLangFound));
			else if (cfg.gameLang != "auto")
				h.why += tx(" · game in %1").arg(langName(cfg.gameLang));
			if (lastNearBest_ > 0 && !detected_)
				h.why += tx(" · closest match last minute %1 (downed at %2)")
						 .arg(lastNearBest_, 0, 'f', 2)
						 .arg(cfg.threshold, 0, 'f', 2);
		}
		if (src)
			obs_source_release(src);
		out << h;
	}

	// ----- the scene the plugin works in
	{
		HealthItem h;
		h.key = "scene";
		h.label = tx("Scene");
		obs_source_t *sc = cfg.sceneName.empty() ? nullptr : obs_get_source_by_name(cfg.sceneName.c_str());
		QString live = sceneMismatch();
		if (cfg.sceneName.empty()) {
			h.level = 2;
			h.why = tx("No scene chosen for the plugin to work in.");
			h.fixes = {Fix("settings:general", tx("Choose it"))};
		} else if (!sc) {
			h.level = 2;
			h.why = tx("The scene '%1' is not in this scene collection.").arg(q(cfg.sceneName));
			h.fixes = {Fix("settings:general", tx("Choose another"))};
		} else if (!live.isEmpty()) {
			h.level = 1;
			h.why = tx("Live scene is '%1', but the plugin works in '%2': nothing it shows is on stream.")
					.arg(live, q(cfg.sceneName));
			h.fixes = {Fix("scene:switch", tx("Switch to %1").arg(q(cfg.sceneName))),
				   Fix("scene:use", tx("Work in %1").arg(live))};
		} else {
			h.level = 0;
			h.why = tx("Working in '%1'").arg(q(cfg.sceneName));
		}
		if (sc)
			obs_source_release(sc);
		out << h;
	}

	// ----- ClipHound
	{
		HealthItem h;
		h.key = "cliphound";
		h.label = "ClipHound";
		QString st = appState();
		if (!cfg.bridgeEnabled) {
			h.level = 3;
			h.why = tx("Off: the connection to ClipHound is switched off.");
			h.fixes = {Fix("settings:advanced", tx("Turn it on"))};
		} else if (st == "connected") {
			h.level = 0;
			h.why = tx("ClipHound: %1").arg(appStatus_.isEmpty() ? tx("connected") : appStatus_);
		} else if (st == "starting") {
			h.level = 1;
			h.why = tx("ClipHound is starting...");
		} else if (st == "crashed") {
			h.level = 2;
			h.why = tx("ClipHound closed right after starting.");
			h.fixes = {Fix("logs", tx("Open logs")), Fix("app:start", tx("Try again"))};
		} else if (cfg.appPath.empty() && !QFileInfo::exists(defaultAppPath())) {
			h.level = 3;
			h.why = tx("ClipHound is not installed: run the installer again and tick it "
				   "for kill-feed clips, Closest and voice (portable OBS: unzip the portable "
				   "download into the OBS folder).");
		} else if (appUserStopped_) {
			h.level = 3;
			h.why = tx("ClipHound is stopped: no kill-feed clips, Closest or voice until it runs.");
			h.fixes = {Fix("app:start", tx("Start ClipHound"))};
		} else {
			h.level = cfg.launchApp ? 2 : 1;
			h.why = tx("ClipHound is not running: no kill-feed clips, Closest or voice.");
			h.fixes = {Fix("app:start", tx("Start ClipHound"))};
		}
		out << h;
	}

	// ----- Discord: squad automation through the Kennel Ops bot
	{
		HealthItem h;
		h.key = "discord";
		h.label = "Discord";
		Access a = rosterAccess();
		if (!cfg.rosterEnabled) {
			h.level = 3;
			h.why = tx("Squad automation is off. Members of the Kennel.gg Discord get live squad mates by "
				   "themselves.");
			h.fixes = {Fix("discord:join", tx("Join Kennel.gg Discord")),
				   Fix("settings:squad", tx("Turn it on"))};
		} else if (cfg.myDiscord.empty() || a == Access::NoUsername) {
			h.level = 1;
			h.why = tx("Your Discord username is not set, so the bot cannot follow your voice channel.");
			h.fixes = {Fix("discord:detect", tx("Detect")), Fix("discord:type", tx("Type it"))};
		} else if (a == Access::NotMember) {
			h.level = 1;
			h.why = tx("'%1' is not in the Kennel.gg Discord: squad automation is for its members.")
					.arg(q(cfg.myDiscord));
			h.fixes = {Fix("discord:join", tx("Join")), Fix("discord:detect", tx("Detect again")),
				   Fix("settings:squad", tx("Turn it off"))};
		} else if (roster.running() && !roster.healthy()) {
			h.level = 1;
			h.why = tx("Discord voice: %1").arg(roster.status());
		} else {
			h.level = 0;
			h.why = tx("Discord voice: %1").arg(rosterStatus());
		}
		out << h;
	}

	// ----- OBS's replay buffer, which every clip comes out of
	if (cfg.clipUseReplay) {
		HealthItem h;
		h.key = "replay";
		h.label = tx("Replay");
		if (!obs_frontend_replay_buffer_active()) {
			h.level = 2;
			h.why = tx("OBS's replay buffer is off, so no clip can be saved.");
			h.fixes = {Fix("replay:start", tx("Start it"))};
		} else {
			h.level = 0;
			h.why = tx("Replay buffer running, %1 s").arg(cfg.replaySeconds);
		}
		out << h;
	}

	// ----- voice (beta): only when it is switched on
	if (cfg.voiceEnabled) {
		HealthItem h;
		h.key = "voice";
		h.label = tx("Voice");
		QString mic = voice.sourceName();
		if (!appConnected()) {
			h.level = 1;
			h.why = tx("Voice listens through ClipHound, which is not running.");
			h.fixes = {Fix("app:start", tx("Start ClipHound"))};
		} else if (!voice.attached()) {
			h.level = 2;
			h.why = voiceStatus_.isEmpty() ? tx("No microphone to listen to.")
						       : tx("Voice: %1").arg(voiceStatus_);
			h.fixes = {Fix("settings:voice", tx("Pick a mic"))};
		} else if (now - voicePcmMs_ > 5000 && now - voiceAttachMs_ > 5000) {
			h.level = 2;
			h.why = tx("No sound is arriving from '%1'.").arg(mic);
			h.fixes = {Fix("settings:voice", tx("Pick another mic"))};
		} else if (now - voiceLoudMs_ > 120000 && now - voiceAttachMs_ > 120000) {
			h.level = 1;
			h.why = tx("'%1' has been silent for two minutes: muted?").arg(mic);
			h.fixes = {Fix("settings:voice", tx("Pick another mic"))};
		} else {
			h.level = 0;
			h.why = voiceStatus_.isEmpty() ? tx("Listening to '%1'").arg(mic)
						       : tx("Listening to '%1' · %2").arg(mic, voiceStatus_);
		}
		out << h;
	}
	return out;
}

QList<Engine::Banner> Engine::banners() const
{
	QList<Banner> out;
	auto add = [&](Banner b) {
		if (!dismissed_.contains(b.id))
			out << b;
	};
	// problems first: whatever on the strip is broken or needs a look, with its fixes
	for (int lvl : {2, 1})
		for (const HealthItem &h : health())
			if (h.level == lvl && !(lvl == 1 && h.fixes.isEmpty())) {
				// (a passing state with nothing to press, like ClipHound starting, stays on the dot)
				Banner b;
				b.id = "h:" + h.key + ":" + h.why;
				b.level = h.level;
				b.text = h.why.toHtmlEscaped();
				b.actions = h.fixes;
				b.dismissable = h.level < 2;
				add(b);
			}
	if (!oldCopy_.isEmpty()) {
		Banner b;
		b.id = "oldcopy";
		b.level = 2;
		b.text = tx("An older copy of this plugin is still installed at <b>%1</b>. Close OBS and delete that "
			    "folder: two copies fight over ClipHound and OBS can hang on the way out.")
				 .arg(oldCopy_.toHtmlEscaped());
		b.actions = {Fix("folder:" + QFileInfo(oldCopy_).absolutePath(), tx("Open its folder"))};
		b.dismissable = false;
		add(b);
	}
	if (cfg.statsConsent == 0 && cfg.setupDone) {
		// asked once, plainly, never as a pop-up: nothing is sent until the answer is yes
		Banner b;
		b.id = "statsconsent";
		b.level = 0;
		b.text = tx("<b>Leaderboards:</b> share your session stats on the kennel.gg leaderboards? "
			    "They go to your kennel.gg account (the same as for wagers and the Cash Cup; linking "
			    "it is the next step) at the end of each stream: the session's kills, deaths, "
			    "assists, revives, headshots, vehicles, downs, money earned and spent, time in game, "
			    "longest kill and top weapon. Never your clips, video, voice or chat. Stop and delete "
			    "any time in Settings.");
		b.actions = {Fix("stats:yes", tx("Yes, share")), Fix("stats:no", tx("No thanks")),
			     Fix("stats:about", tx("More"))};
		add(b);
	}
	if (cfg.statsConsent == 1 && !accountLinked()) {
		Banner b;
		b.id = "account:" + linkCode_;
		b.level = 1;
		b.text =
			linkCode_.isEmpty()
				? tx("<b>Leaderboards:</b> taking part needs a kennel.gg account, the same one as for wagers "
				     "and the Cash Cup. Your stats wait on this PC until it is linked.")
				: tx("<b>Linking to kennel.gg:</b> sign in on the page that opened (code <b>%1</b>). This PC "
				     "picks the link up by itself.")
					  .arg(linkCode_.toHtmlEscaped());
		b.actions = {Fix("account:link",
				 linkCode_.isEmpty() ? tx("Link kennel.gg account") : tx("Open the page again"))};
		add(b);
	}
	if (cfg.statsConsent == 1 && accountLinked() && !accountMissing().isEmpty()) {
		Banner b;
		b.id = "accountmissing:" + accountMissing().join(",");
		b.level = 0;
		b.text = tx("Linked to kennel.gg as <b>%1</b>. Still to connect: <b>%2</b> - the same connections as "
			    "for wagers and the Cash Cup.")
				 .arg(QString::fromStdString(cfg.accountName).toHtmlEscaped(),
				      accountMissing().join(", ").toHtmlEscaped());
		b.actions = {Fix("account:page", tx("Finish on kennel.gg"))};
		add(b);
	}
	if (sessionEndedAt_.isValid() && sessionEndedAt_.secsTo(QDateTime::currentDateTime()) < 3 * 3600) {
		Banner b;
		b.id = "statsimage:" + sessionEndedAt_.toString(Qt::ISODate);
		b.level = 0;
		b.text = tx("Stream over: <b>%1</b>. Make a picture of it for X, Instagram or Discord?")
				 .arg(session_.line().toHtmlEscaped());
		b.actions = {Fix("statsimage", tx("Get stats image"))};
		add(b);
	}
	if (!unpopped_.isEmpty()) {
		Banner b;
		b.id = "pop:" + unpopped_.join(","); // dismissed until somebody else joins the list
		b.level = 1;
		b.text =
			(unpopped_.size() == 1
				 ? tx("<b>%1</b> is live in your voice channel but not popped out, so they can only be shown "
				      "through your main Discord window. In Discord, right-click their stream, <b>Pop "
				      "Out</b>, then <b>Mute</b> it: the plugin picks the window up by itself.")
				 : tx("<b>%1</b> are live in your voice channel but not popped out, so they can only be shown "
				      "through your main Discord window. In Discord, right-click each stream, <b>Pop "
				      "Out</b>, then <b>Mute</b> it: the plugin picks the window up by itself."))
				.arg(unpopped_.join(", ").toHtmlEscaped());
		b.actions = {Fix("squad", tx("Open Squad"))};
		add(b);
	}
	for (auto it = minimised_.begin(); it != minimised_.end(); ++it) {
		Banner b;
		b.id = "min:" + it.key();
		b.level = 1;
		b.text = tx("<b>%1</b>'s pop-out is minimised, so their picture is frozen. It can sit behind the game, "
			    "just not minimised.")
				 .arg(it.key().toHtmlEscaped());
		b.actions = {Fix("popout:restore:" + it.key(), tx("Restore it"))};
		add(b);
	}
	if (langBanner_) {
		Banner b;
		b.id = "lang";
		b.level = 1;
		b.text =
			tx("The downed screen never matched the English, Spanish or French wording. If your game is in "
			   "another language, save a frame while downed and send it in a ticket; it goes into the next "
			   "build. In one of those three? Then cut your own header under Advanced.");
		b.actions = {Fix("lang:frame", tx("Save a frame")), Fix("lang:discord", tx("Open Discord")),
			     Fix("settings:general", tx("Set the language"))};
		add(b);
	}
	if (!frameSaved_.isEmpty()) {
		Banner b;
		b.id = "frame:" + frameSaved_;
		b.level = 0;
		b.text = tx("Saved <b>%1</b>. Attach it to a ticket in the Kennel.gg Discord.")
				 .arg(QFileInfo(frameSaved_).fileName().toHtmlEscaped());
		b.actions = {Fix("folder:" + QFileInfo(frameSaved_).absolutePath(), tx("Open the folder")),
			     Fix("lang:discord", tx("Open Discord"))};
		add(b);
	}
	if (!muteNames_.isEmpty()) {
		Banner b;
		b.id = "mute:" + muteNames_.join(",");
		b.level = 0;
		b.text = tx("Added <b>%1</b>. Mute each of them in Discord (right-click their stream, Mute): otherwise "
			    "their game sound plays in your headphones and goes out on your stream.")
				 .arg(muteNames_.join(", ").toHtmlEscaped());
		if (cfg.popoutTuck && cfg.popoutMonitor < 0 && !popoutsShown_)
			b.actions = {Fix("popouts:show", tx("Show pop-outs"))};
		add(b);
	}
	if (!nameCheck_.isEmpty()) {
		Banner b;
		b.id = "names:" + nameCheck_.join(",");
		b.level = 0;
		b.text =
			tx("Closest matches squad mates by their in-game name, taken from Discord for now. Check it for "
			   "<b>%1</b>.")
				.arg(nameCheck_.join(", ").toHtmlEscaped());
		b.actions = {Fix("squad", tx("Open Squad"))};
		add(b);
	}
	if (closestAsk_ && !appConnected()) {
		Banner b;
		b.id = "closest";
		b.level = 1;
		b.text = tx("Closest needs ClipHound: it reads the NEARBY list in the corner of your game.");
		b.actions = {Fix("app:start", tx("Start ClipHound"))};
		add(b);
	}
	if (needsSetup()) {
		Banner b;
		b.id = "setup";
		b.level = 1;
		b.text = tx("Setup is not finished yet.");
		b.actions = {Fix("wizard", tx("Open Setup"))};
		b.dismissable = false;
		add(b);
	}
	if (updateAvailable()) {
		Banner b;
		b.id = "update:" + newVersion_;
		b.level = 0;
		b.text = tx("Version <b>%1</b> is out (you have %2).")
				 .arg(newVersion_.toHtmlEscaped(), QString(PLUGIN_VERSION));
		if (!newUrl_.isEmpty())
			b.actions = {Fix("update:open", tx("Download"))};
		add(b);
	}
	if (wantsSupportNote()) {
		Banner b;
		b.id = "support";
		b.level = 0;
		b.text = tx("Enjoying the plugin? It is free and always will be; support keeps it moving.");
		b.actions = {Fix("support:open", tx("Support development"))};
		add(b);
	}
	return out;
}

void Engine::dismissBanner(const QString &id)
{
	dismissed_.insert(id);
	if (id == "support")
		supportNoteShown();
	else if (id == "lang") {
		langBanner_ = false;
		languageNoteShown();
	} else if (id.startsWith("mute:"))
		muteNames_.clear();
	else if (id.startsWith("names:"))
		nameCheck_.clear();
	else if (id == "closest")
		closestAsk_ = false;
	else if (id.startsWith("frame:"))
		frameSaved_.clear();
	emit stateChanged();
}

void Engine::noteAddedPopouts(const QStringList &names)
{
	if (names.isEmpty())
		return;
	muteNames_ = names;
	nameCheck_.clear();
	if (cfg.nearEnabled)
		for (const QString &n : names)
			for (const auto &f : cfg.friends)
				if (q(f.name) == n && f.gameName.empty())
					nameCheck_ << n;
	emit stateChanged();
}

void Engine::noteClosestNeedsApp()
{
	closestAsk_ = true;
	dismissed_.remove("closest");
	emit stateChanged();
}

bool Engine::runAction(const QString &id)
{
	if (id == "stats:yes" || id == "stats:no") {
		setStatsConsent(id == "stats:yes");
		return true;
	}
	if (id == "account:link") {
		linkAccount();
		return true;
	}
	if (id == "account:page") {
		QDesktopServices::openUrl(QUrl("https://kennel.gg/account/"));
		return true;
	}
	if (id == "stats:about") {
		QDesktopServices::openUrl(QUrl("https://kennel.gg/streaming/#leaderboards"));
		return true;
	}
	if (id == "scene:switch") {
		obs_source_t *sc = obs_get_source_by_name(cfg.sceneName.c_str());
		if (sc) {
			obs_frontend_set_current_scene(sc);
			obs_source_release(sc);
		}
	} else if (id == "scene:use") {
		QString live = sceneMismatch();
		if (!live.isEmpty()) {
			cfg.sceneName = live.toStdString();
			cfg.save();
			log(tx("Scene: the plugin now works in '%1'.").arg(live));
			reloadConfig();
		}
	} else if (id == "app:start") {
		launchApp();
	} else if (id == "app:stop") {
		stopApp();
	} else if (id == "replay:start") {
		obs_frontend_replay_buffer_start();
	} else if (id == "discord:detect") {
		detectDiscordUser(true);
	} else if (id == "discord:join" || id == "lang:discord") {
		QDesktopServices::openUrl(
			QUrl(id == "lang:discord" ? QString(Config::kennelDiscordUrl()) : discordUrl()));
	} else if (id == "lang:frame") {
		QString r = saveFrame();
		if (!QFileInfo::exists(r)) // an error, not the path of the saved frame
			log(r);
		else
			frameSaved_ = r;
	} else if (id.startsWith("popout:restore:")) {
		QString name = id.mid(15);
		if (minimised_.contains(name))
			Switcher::restorePopout((uintptr_t)minimised_.value(name));
	} else if (id.startsWith("folder:")) {
		QDesktopServices::openUrl(QUrl::fromLocalFile(id.mid(7)));
	} else if (id == "support:open") {
		QDesktopServices::openUrl(QUrl(Config::supportUrl()));
		supportNoteShown();
	} else if (id == "update:open") {
		QDesktopServices::openUrl(QUrl(newUrl_));
	} else if (id == "popouts:show") {
		showPopouts(true);
	} else
		return false;
	emit stateChanged();
	return true;
}
