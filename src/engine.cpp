#include "engine.h"
#include "datafile.h"
#include "i18n.h"
#include <QWidget>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QSysInfo>
#include "discord-ipc.h"
#include <optional>
#include <obs-frontend-api.h>
#include <QNetworkInterface>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QProcess>
#include <QRegularExpression>
#include <algorithm>
#include <climits>
#include <cmath>
#include <map>
#include <fstream>
#include <thread>
#include <QBuffer>
#include <QDirIterator>
#include <QJsonArray>
#include <QFileInfo>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QUrl>
#include "http.h"
#include "ui/send-logs.h"
#include "picture.h"
#include <QRandomGenerator>
#include <QDateTime>

#ifdef _WIN32
#define NOMINMAX // windows.h defines min and max as macros, which eats every std::min in this file
#include <windows.h>
#include <shellapi.h>
#endif

#include <obs-module.h>
#include <plugin-support.h>

using clock_ = std::chrono::steady_clock;

Engine::Engine(QObject *parent) : QObject(parent)
{
	sw.log = [this](const std::string &s) {
		log(QString::fromStdString(s));
	};
	cfg.load();
	I18n::load(I18n::resolve(cfg.uiLang));
	loadTemplates();
	{
		char *mp = kennel_file("hud/model.bin");
		hudModel_ = mp ? hud::loadModel(mp, &hudModelErr_) : nullptr;
		if (!mp)
			hudModelErr_ = txs("data/hud/model.bin is missing");
		bfree(mp);
	}
	detRevive_.fromX = 0.15f;
	detRevive_.toX = 0.60f;
	detRevive_.fromY = 0.55f;
	detRevive_.toY = 0.95f;
	lastReviveSeen_ = clock_::now() - std::chrono::hours(1);
	downSince_ = lastReviveSeen_;
	lastPick_ = lastNearbyWarn_ = lastReviveSeen_;
	connect(&replayTimer_, &QTimer::timeout, this, &Engine::replayTick);
	connect(&pipTimer_, &QTimer::timeout, this, &Engine::pipTick);
	connect(&dualTimer_, &QTimer::timeout, this, &Engine::dualTick);
	connect(&healthTimer_, &QTimer::timeout, this, &Engine::sendObsHealth);
	connect(&healthTimer_, &QTimer::timeout, this, &Engine::vehicleDualCheck);
	connect(&healthTimer_, &QTimer::timeout, this, [this]() {
		if (++replayRetryTick_ % 12 == 0) { // the health timer runs every 5 s: once a minute
			clips.retryReplayBuffer();
			// it died mid-stream (no crash-right-after-start, no Stop pressed): bring it back, the
			// same way a lost replay buffer is retried
			if (cfg.launchApp && !appUserStopped_ && appState() == "stopped")
				launchApp();
		}
		// a staged live update goes in when nothing is on screen that it would interrupt
		if (!liveStaged_.isEmpty() && !liveBusy_ && !applied_ && !detected_ && !replaying() && !povPending_)
			applyLiveUpdate();
		if (cfg.helpBuild() && streamStart_.isValid() && replayRetryTick_ % 6 == 0) {
			// what was broken during this stream, for the logs sent at its end (every 30 s is enough)
			for (const auto &h : health())
				if (h.level == 2 && problems_.size() < 40) {
					QString p = h.key + ": " + h.why;
					if (!problems_.contains(p))
						problems_ << p;
				}
			if (clips.lostHotkeyClips() > 0 && !problems_.contains("clips: hotkey clips lost"))
				problems_ << "clips: hotkey clips lost";
		}
	});
	sessionStart_ = QDateTime::currentDateTime();
	connect(&roster, &Roster::changed, this, &Engine::checkAccess);
	connect(&roster, &Roster::polled, this, &Engine::checkAccess);
	connect(&roster, &Roster::changed, this, &Engine::syncRoster);
	connect(&roster, &Roster::polled, this, &Engine::syncRoster); // the minute's grace runs out between changes
	popoutTimer_.setInterval(2000);
	connect(&popoutTimer_, &QTimer::timeout, this, &Engine::watchPopouts);
	webLiveTimer_.setInterval(60000);
	connect(&webLiveTimer_, &QTimer::timeout, this, &Engine::webLiveTick);
	connect(&linkPoll_, &QTimer::timeout, this, &Engine::pollLink);
	cashTimer_.setInterval(250);
	connect(&cashTimer_, &QTimer::timeout, this, &Engine::cashTick);
	pictureTimer_.setInterval(1000);
	connect(&pictureTimer_, &QTimer::timeout, this, &Engine::pictureTick);
	connect(&timer_, &QTimer::timeout, this, &Engine::tick);
	connect(&frameTimer_, &QTimer::timeout, this, &Engine::frameTick);
	downDelay_.setSingleShot(true);
	upDelay_.setSingleShot(true);
	connect(&downDelay_, &QTimer::timeout, this, [this]() {
		if (detected_ && !applied_) {
			pickClosest(TX_NOOP("about to switch"), true); // last reading before the feed goes on screen
			if (reelWanted())
				startReel(); // nobody to show: your own replays instead
			else
				applyNow(true, QString("downed for %1 ms").arg(cfg.downDelayMs));
		}
	});
	connect(&upDelay_, &QTimer::timeout, this, [this]() {
		if (!detected_ && applied_)
			applyNow(false, TX_NOOP("damage log gone"));
	});
	connect(&bridge, &Bridge::message, this, &Engine::onBridgeMessage);
	connect(&bridge, &Bridge::clientConnected, this, [this]() {
		appStatus_ = tx("connected");
		// whether it is the copy we started is told by its process id (app_config "pid"): the time it took to
		// connect said "a copy was already running" at every start, because a fresh one connects in under 1.2 s
		log(tx("Companion app connected."));
		QJsonObject o;
		o["type"] = "config";
		o["gameSource"] = QString::fromStdString(cfg.gameSource);
		o["povState"] = povStateName();
		bridge.sendJson(o);
		// the microphone a moment later, once the app has its footing
		QTimer::singleShot(1500, this, [this]() {
			if (!stopping_)
				applyVoice();
		});
		emit stateChanged();
	});
	stateTimer_.setSingleShot(true);
	stateTimer_.setInterval(150);
	hudTimer_.setInterval(150000); // one set every 2.5 minutes while the HUD is on screen
	connect(&hudTimer_, &QTimer::timeout, this, [this]() { hudSample("periodic"); });
	hudTimer_.start();
	QTimer::singleShot(60000, this, &Engine::checkLastCrash);
	replayLenTimer_.setSingleShot(true);
	replayLenTimer_.setInterval(1200);
	connect(&replayLenTimer_, &QTimer::timeout, this, &Engine::applyReplaySecondsNow);
	connect(&stateTimer_, &QTimer::timeout, this, &Engine::broadcastState);
	connect(this, &Engine::stateChanged, this, [this]() {
		if (!stateTimer_.isActive())
			stateTimer_.start();
	});
	connect(&voice, &VoiceTap::pcm, this, [this](const QByteArray &pcm) {
		if (bridge.clients() > 0)
			bridge.sendAudio(pcm);
		// the loudest sample of the piece, for the dock's level bar and the "is it silent" check
		{
			const int16_t *s = (const int16_t *)pcm.constData();
			int n = (int)(pcm.size() / 2), peak = 0;
			for (int i = 0; i < n; i++)
				peak = std::max(peak, std::abs((int)s[i]));
			qint64 now = QDateTime::currentMSecsSinceEpoch();
			voicePcmMs_ = now;
			voiceLevelDb_ = peak > 0 ? 20.0 * std::log10(peak / 32768.0) : -120.0;
			if (voiceLevelDb_ > -60)
				voiceLoudMs_ = now;
			emit voiceLevel(voiceLevelDb_);
		}
		if (!voiceFlowing_) {
			voiceFlowing_ = true;
			log(tx("Voice: microphone audio is flowing to ClipHound."));
		}
	});
	connect(&bridge, &Bridge::clientDisconnected, this, [this]() {
		appStatus_.clear();
		holding_.clear();
		voiceStatus_.clear();
		voice.detach();
		frameTimer_.stop();
		log(tx("Companion app disconnected."));
		emit stateChanged();
	});
	connect(&clips, &Clips::logged, this, &Engine::log);
	connect(&clips, &Clips::saved, this, [this](const Clips::Entry &e) {
		QJsonObject o;
		o["type"] = "clip_saved";
		o["path"] = e.path;
		// when the save was asked for: the file ends there. ClipHound turns each kill's moment into
		// "seconds before the end" with it, for its library's kills.csv
		o["end_epoch"] = (double)e.when.toMSecsSinceEpoch() / 1000.0;
		o["title"] = e.title;
		o["tags"] = QJsonArray::fromStringList(e.tags);
		bridge.sendJson(o);
		noteChapter(e);
		if (replayAfterClip_ && e.tags.contains("manual")) {
			// "hey kennel, clip replay": the clip is on disk; a moment for OBS to close it, then play
			replayAfterClip_ = false;
			// two seconds for the file to be closed properly; with a vertical canvas, up to three
			// more while its own Backtrack file lands and is paired with this clip
			replayAfterClipTries_ = cfg.verticalOn() ? 6 : 0;
			QTimer::singleShot(2000, this, &Engine::replayAfterClipTick);
		}
		// a clip you asked for yourself is named by what you said around the moment you asked
		if (cfg.voiceEnabled && cfg.voiceNames && voice.attached() && e.tags.contains("manual") &&
		    bridge.clients() > 0) {
			QJsonObject v;
			v["type"] = "voice_name";
			v["path"] = e.path;
			v["epoch"] = (double)e.when.toMSecsSinceEpoch() / 1000.0;
			bridge.sendJson(v);
		}
		emit stateChanged();
	});
}

QString Engine::playerName() const
{
	return cfg.playerName.empty() ? QSysInfo::machineHostName() : QString::fromStdString(cfg.playerName);
}

/// Put the clip length into OBS and, if the buffer is already running, restart it so the new length
/// takes - stop first, start a moment later. OBS's stop is not finished when the call returns, and
/// starting immediately leaves the buffer off, which means no clips at all until OBS is restarted.
/// ClipHound reads the bridge port from its own config.yaml, so changing it in the plugin used to
/// orphan the app for good: it went on knocking at the old port for ever while the dock said
/// "starting" and no clips or NEARBY readings arrived. Write the number into its config too.
void Engine::syncAppPort()
{
	if (cfg.appPath.empty())
		return;
	QString yaml = QFileInfo(QString::fromStdString(cfg.appPath)).absolutePath() + "/config.yaml";
	QFile f(yaml);
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
		return;
	QStringList lines = QString::fromUtf8(f.readAll()).split('\n');
	f.close();
	// the port under the "bridge:" block, left exactly as it is written otherwise
	bool inBridge = false, changed = false;
	static const QRegularExpression rxPort("^(\\s+port:\\s*)(\\d+)(.*)$");
	for (QString &l : lines) {
		if (!l.startsWith(' ') && !l.startsWith('\t'))
			inBridge = l.startsWith("bridge:");
		if (!inBridge)
			continue;
		QRegularExpressionMatch m = rxPort.match(l);
		if (m.hasMatch() && m.captured(2).toInt() != cfg.bridgePort) {
			l = m.captured(1) + QString::number(cfg.bridgePort) + m.captured(3);
			changed = true;
		}
	}
	if (!changed)
		return;
	QFile out(yaml);
	if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
		log(tx("Could not write ClipHound's config.yaml, so it still expects the old bridge port - put "
		       "the port back, or edit %1 by hand.")
			    .arg(yaml));
		return;
	}
	out.write(lines.join('\n').toUtf8());
	out.close();
	log(tx("ClipHound's port updated to %1; restarting it so it reconnects.").arg(cfg.bridgePort));
	if (cfg.launchApp) {
		stopApp();
		QTimer::singleShot(2000, this, [this]() {
			if (!stopping_ && cfg.launchApp)
				launchApp();
		});
	}
}

void Engine::applyReplaySeconds()
{
	// OBS's setting is the user's: the plugin follows it. It used to write its own 45 s into OBS
	// at every start, over whatever the user had chosen
	int obsSecs = Clips::readReplaySeconds();
	if (obsSecs > 0 && obsSecs != cfg.replaySeconds) {
		cfg.replaySeconds = obsSecs;
		cfg.save();
		emit stateChanged();
	}
}

void Engine::setReplaySecondsByUser(int seconds)
{
	cfg.replaySeconds = std::clamp(seconds, 5, 300);
	cfg.save();
	// written to OBS once the value settles: holding the spin box used to write every step and restart the
	// replay buffer over and over, and a restart could land while the last one was still stopping (27 Sep
	// 2026 log: 36 writes and 7 restarts in 10 s, three of them left the buffer off)
	replayLenTimer_.start();
}

void Engine::applyReplaySecondsNow()
{
	replayLenTimer_.stop();
	if (replayRestarting_) {
		replayLenTimer_.start(); // a restart is under way: this value goes in once it is done
		return;
	}
	switch (clips.setReplaySeconds(cfg.replaySeconds)) {
	case Clips::ReplayChange::None:
		return;
	case Clips::ReplayChange::Written:
		log(tx("Clip length set to %1 s in OBS.").arg(cfg.replaySeconds));
		return;
	case Clips::ReplayChange::NeedsRestart:
		log(tx("Clip length set to %1 s - restarting OBS's replay buffer so it takes.").arg(cfg.replaySeconds));
		replayRestarting_ = true;
		obs_frontend_replay_buffer_stop();
		QTimer::singleShot(2500, this, [this]() {
			if (stopping_) {
				replayRestarting_ = false;
				return;
			}
			if (!obs_frontend_replay_buffer_active())
				obs_frontend_replay_buffer_start();
			QTimer::singleShot(1500, this, [this]() {
				replayRestarting_ = false;
				if (stopping_)
					return;
				log(obs_frontend_replay_buffer_active()
					    ? tx("Replay buffer is running again.")
					    : tx("The replay buffer did not come back after the length change - start it "
						 "in OBS (Settings -> Output -> Replay Buffer), or clips cannot save."));
				emit stateChanged();
			});
		});
		return;
	}
}

void Engine::launchApp()
{
	appUserStopped_ = false;
	if (bridge.clients() > 0) {
		log(tx("ClipHound is already running."));
		return;
	}
	QString p = QString::fromStdString(cfg.appPath);
	const QString def = defaultAppPath();
	// none chosen, or the one chosen is gone (a portable OBS moved to another folder or drive)
	if ((p.isEmpty() || !QFileInfo::exists(p)) && QFileInfo::exists(def)) {
		p = def;
		cfg.appPath = def.toStdString();
		cfg.save();
	}
	if (p.isEmpty()) {
		log(tx("ClipHound: no app path set and nothing at %1 (Settings, Advanced, ClipHound connection, Browse).")
			    .arg(def));
		return;
	}
	if (!QFileInfo::exists(p)) {
		// it was there and went: antivirus quarantine is the usual reason (LOG-7066: gone mid-stream, then this
		// line every minute for an hour and a half from the relaunch watchdog)
		if (!appMissingLogged_) {
			appMissingLogged_ = true;
			log(tx("ClipHound.exe is missing from %1 - antivirus often removes it: restore it in Windows Security, "
			       "Protection history, or run the installer again.")
				    .arg(p));
		}
		return;
	}
	appMissingLogged_ = false;
	QString dir = QFileInfo(p).absolutePath();
	qint64 pid = 0;
	QProcess proc;
	proc.setProgram(p);
	proc.setWorkingDirectory(dir);
	QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	env.insert("KENNEL_FROM_OBS", "1");
	proc.setProcessEnvironment(env);
	if (proc.startDetached(&pid)) {
		appLaunchedAt_ = QDateTime::currentDateTime();
		appPid_ = pid;
		appStartedAt_ = QDateTime::currentDateTime();
		appCrashReported_ = false;
		QTimer::singleShot(25000, this, [this]() {
			if (stopping_ || bridge.clients() > 0 || !appRunning())
				return;
			// alive but never said hello: its own log is the only thing that knows why
			log(tx("ClipHound has been starting for 25 s without connecting. The last lines of its log:"));
			for (const QString &l : appLogTail(12))
				log("  " + l);
			log(tx("If those lines say nothing useful, check that no older copy of this plugin is "
			       "installed (%1).")
				    .arg("C:\\ProgramData\\obs-studio\\plugins\\kennel-wardogs"));
			emit stateChanged();
		});
		QTimer::singleShot(6000, this, [this]() {
			if (bridge.clients() == 0 && !appRunning() && !appCrashReported_) {
				appCrashReported_ = true;
				log(tx("ClipHound exited right after starting - open Settings → Logs and look at its log (config problem or missing file)."));
				emit stateChanged();
			}
		});
		log(tx("Started ClipHound (pid %1): %2").arg(pid).arg(p));
		return;
	}
	// fall back to the shell (handles .bat/.cmd and anything Windows wants to elevate or associate)
#ifdef _WIN32
	// ShellExecute a file:// URI (what QDesktopServices::openUrl builds) always refuses to launch an
	// .exe with access denied (error 5) - Windows only allows that through a plain native path, the
	// same way Explorer double-clicks it (LOG-BB3F: ClipHound never started, every stream).
	HINSTANCE sh = ShellExecuteW(nullptr, L"open", (const wchar_t *)QDir::toNativeSeparators(p).utf16(), nullptr,
				     (const wchar_t *)QDir::toNativeSeparators(dir).utf16(), SW_SHOWNORMAL);
	if ((INT_PTR)sh > 32)
		log(tx("Started ClipHound via the shell: %1").arg(p));
	else
		log(tx("Could not start ClipHound: %1 (try the Start-menu shortcut and send me the Logs).").arg(p));
#else
	if (QDesktopServices::openUrl(QUrl::fromLocalFile(p)))
		log(tx("Started ClipHound via the shell: %1").arg(p));
	else
		log(tx("Could not start ClipHound: %1 (try the Start-menu shortcut and send me the Logs).").arg(p));
#endif
}

Engine::~Engine()
{
	stop();
}

void Engine::loadTemplates()
{
	std::lock_guard<std::recursive_mutex> lk(detMx_);
	detGame_.threshold = cfg.threshold;
	detRevive_.threshold = cfg.reviveThreshold;
	applySearchWidth();
	bool loaded = false;
	if (cfg.customTemplateWidthFrac > 0) {
		// custom template: raw floats written by captureTemplate()
		std::ifstream in(Config::configFile("template.bin"), std::ios::binary);
		int w = 0, h = 0;
		if (in && in.read((char *)&w, 4) && in.read((char *)&h, 4) && w > 0 && h > 0 && w < 2000 && h < 2000) {
			std::vector<float> g((size_t)w * h);
			if (in.read((char *)g.data(), g.size() * sizeof(float))) {
				detGame_.setTemplate(g, w, h, (float)cfg.customTemplateWidthFrac);
				loaded = true;
			}
		}
	}
	altDets_ = std::make_shared<AltSet>();
	altLangs_.clear();
	if (!loaded) {
		// the wording only: the gap between the "B" key hint and the words is a different
		// fraction of the screen at every resolution, so a template spanning both can only
		// ever be a near miss on somebody else's setup. The wording is the game's language:
		// a fixed choice loads that one; auto loads what it found last time, or English,
		// and keeps the other languages searching alongside until one of them matches
		static const char *kLangs[] = {"en", "es", "fr"};
		std::string want = cfg.gameLang;
		// a language the downed screen has no wording for yet is searched like auto: every wording
		// we have, until one fits (the HUD's weapon names are still read in the chosen language)
		bool fixed = want != "auto" && !want.empty() && hasDownedTemplate(want);
		if (!fixed)
			want = cfg.gameLangFound.empty() ? "en" : cfg.gameLangFound;
		if (!loadLangTemplate(detGame_, want)) {
			loadLangTemplate(detGame_, "en");
			want = "en";
		}
		loadLangTemplate(detMate_, want);
		detMate_.threshold = cfg.threshold;
		if (!fixed && cfg.gameLangFound.empty())
			for (const char *l : kLangs) {
				if (want == l)
					continue;
				auto d = std::make_unique<Detector>();
				d->threshold = cfg.threshold;
				if (loadLangTemplate(*d, l)) {
					altDets_->push_back(std::move(d));
					altLangs_.push_back(l);
				}
			}
		cfg.customTemplateWidthFrac = 0;
		applySearchWidth();
	}
	if (cfg.memScale > 0)
		detGame_.remember((float)cfg.memScale, (float)cfg.memX, (float)cfg.memY);
	char *r = kennel_file("templates/reviving.png");
	if (r)
		detRevive_.loadTemplatePng(r, 98.0f / 1875.0f);
	bfree(r);
}

/// The damage-log wording in one language: the file and how wide it is on the screen it was cut from.
bool Engine::loadLangTemplate(Detector &d, const std::string &lang)
{
	struct L {
		const char *code, *file;
		float widthFrac;
	};
	// widthFrac = template width / width of the screenshot it was cut from
	static const L kTable[] = {
		{"en", "templates/damagelog.png", 198.0f / 1704.0f},    // VIEW DAMAGE LOG
		{"es", "templates/damagelog_es.png", 270.0f / 2559.0f}, // VER REGISTRO DE DAÑOS
		// cut from a 1080p stream frame with the game 1920 wide; the wording is set smaller than
		// the other languages by the game to fit
		{"fr", "templates/damagelog_fr.png", 212.0f / 1920.0f}, // AFFICHER LE JOURNAL DES DÉGÂTS
	};
	for (const L &l : kTable) {
		if (lang != l.code)
			continue;
		char *p = kennel_file(l.file);
		bool ok = p && d.loadTemplatePng(p, l.widthFrac);
		bfree(p);
		return ok;
	}
	return false;
}

QString Engine::langName(const std::string &lang)
{
	static const std::map<std::string, const char *> names = {{"en", TX_NOOP("English")},
								  {"de", TX_NOOP("German")},
								  {"fr", TX_NOOP("French")},
								  {"es", TX_NOOP("Spanish")},
								  {"it", TX_NOOP("Italian")},
								  {"pt", TX_NOOP("Portuguese")},
								  {"pl", TX_NOOP("Polish")},
								  {"tr", TX_NOOP("Turkish")},
								  {"ru", TX_NOOP("Russian")},
								  {"uk", TX_NOOP("Ukrainian")},
								  {"ja", TX_NOOP("Japanese")},
								  {"ko", TX_NOOP("Korean")},
								  {"zh", TX_NOOP("Simplified Chinese")},
								  {"zh-tw", TX_NOOP("Traditional Chinese")}};
	auto it = names.find(lang);
	return it != names.end() ? txv(QString(it->second)) : QString::fromStdString(lang);
}

const std::vector<std::pair<std::string, QString>> &Engine::gameLanguages()
{
	// the Steam store's interface languages for WARDOGS, each in its own name
	static const std::vector<std::pair<std::string, QString>> list = {
		{"en", "English"},
		{"de", "Deutsch"},
		{"fr", QString::fromUtf8("Français")},
		{"es", QString::fromUtf8("Español")},
		{"it", "Italiano"},
		{"pt", QString::fromUtf8("Português (Brasil)")},
		{"pl", "Polski"},
		{"tr", QString::fromUtf8("Türkçe")},
		{"ru", QString::fromUtf8("Русский")},
		{"uk", QString::fromUtf8("Українська")},
		{"ja", QString::fromUtf8("日本語")},
		{"ko", QString::fromUtf8("한국어")},
		{"zh", QString::fromUtf8("简体中文")},
		{"zh-tw", QString::fromUtf8("繁體中文")},
	};
	return list;
}

bool Engine::hasDownedTemplate(const std::string &lang)
{
	return lang == "en" || lang == "es" || lang == "fr";
}

/// How much of the frame, and how many sizes, the damage-log search covers.
void Engine::applySearchWidth()
{
	std::lock_guard<std::recursive_mutex> lk(detMx_);
	auto set = [&](Detector &d) {
		if (cfg.wideSearch) {
			d.fromX = 0.0f;
			d.toX = 1.0f;
			d.fromY = 0.0f;
			d.toY = 1.0f;
			d.minScale = 0.35f;
			d.maxScale = 2.2f;
		} else {
			// the damage-log area on Settings, Detect areas
			d.fromX = (float)std::clamp(cfg.dmgX, 0.0, 0.99);
			d.toX = (float)std::clamp(cfg.dmgX + cfg.dmgW, d.fromX + 0.01, 1.0);
			d.fromY = (float)std::clamp(cfg.dmgY, 0.0, 0.99);
			d.toY = (float)std::clamp(cfg.dmgY + cfg.dmgH, d.fromY + 0.01, 1.0);
			d.minScale = 0.5f;
			d.maxScale = 1.6f;
		}
		d.unlock();
	};
	set(detGame_);
	for (auto &d : *altDets_)
		set(*d);
}

QImage Engine::grabNative()
{
	if (cfg.gameSource.empty())
		return QImage();
	obs_source_t *src = obs_get_source_by_name(cfg.gameSource.c_str());
	if (!src)
		return QImage();
	int native = (int)obs_source_get_width(src);
	std::vector<uint8_t> bgra;
	int w = 0, h = 0, ls = 0;
	bool ok = native > 0 && capRoi_.grab(src, native, bgra, w, h, ls);
	obs_source_release(src);
	if (!ok)
		return QImage();
	return QImage((const uchar *)bgra.data(), w, h, ls, QImage::Format_ARGB32).copy();
}

/// Cut the template out of this frame at `rect` (fractions), so it is this HUD's own pixels: the
/// key hint, the gap and the wording differ between HUDs, and a template from someone else's
/// screen can only ever be a near miss.
QString Engine::learnTemplate(const QImage &img, QRectF rect)
{
	if (img.isNull() || rect.width() <= 0)
		return tx("No frame to learn from.");
	int x = (int)std::lround(rect.x() * img.width()), y = (int)std::lround(rect.y() * img.height());
	int w = (int)std::lround(rect.width() * img.width()), h = (int)std::lround(rect.height() * img.height());
	int mx = std::max(2, w / 20), my = std::max(2, h / 5); // a little margin, the match box is tight
	x = std::clamp(x - mx, 0, img.width() - 8);
	y = std::clamp(y - my, 0, img.height() - 8);
	w = std::min(w + 2 * mx, img.width() - x);
	h = std::min(h + 2 * my, img.height() - y);
	if (w < 16 || h < 6)
		return tx("That is too small to learn from.");
	std::vector<float> g((size_t)w * h);
	for (int yy = 0; yy < h; yy++)
		for (int xx = 0; xx < w; xx++) {
			QRgb p = img.pixel(x + xx, y + yy);
			g[(size_t)yy * w + xx] = 0.299f * qRed(p) + 0.587f * qGreen(p) + 0.114f * qBlue(p);
		}
	cfg.customTemplateWidthFrac = (double)w / img.width();
	{
		std::lock_guard<std::recursive_mutex> lk(detMx_);
		detGame_.setTemplate(g, w, h, (float)cfg.customTemplateWidthFrac);
	}
	std::ofstream out(Config::configFile("template.bin"), std::ios::binary);
	out.write((const char *)&w, 4);
	out.write((const char *)&h, 4);
	out.write((const char *)g.data(), g.size() * sizeof(float));
	cfg.memScale = cfg.memX = cfg.memY = 0;
	cfg.save();
	downRun_ = upRun_ = 0;
	log(tx("Learned this HUD's damage log: %1x%2 px, %3 of the width.")
		    .arg(w)
		    .arg(h)
		    .arg(cfg.customTemplateWidthFrac, 0, 'f', 3));
	emit stateChanged();
	return "";
}

/// A PNG of the game source exactly as the plugin sees it, for working out why a HUD is not matched.
QString Engine::saveFrame()
{
	QImage img = grabNative();
	if (img.isNull())
		return tx("Could not render the game source '%1' (is a game source set, and showing something?).")
			.arg(QString::fromStdString(cfg.gameSource));
	QString dir = QString::fromStdString(Config::configDir());
	QDir().mkpath(dir);
	QString path = dir + "/frame-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".png";
	if (!img.copy().save(path, "PNG"))
		return tx("Could not write %1").arg(path);
	log(tx("Saved a frame for diagnosis: %1").arg(path));
	return path;
}

void Engine::autoPickAudio()
{
	// Once: the sound of YOUR game goes on the mute list, so a squad mate's POV comes with their
	// sound and not yours on top. The game source when it carries audio (a capture card does),
	// otherwise every desktop-audio input. Microphones are never touched. Cleared by hand, it stays
	// cleared: the flag is saved.
	if (cfg.audioAutoPicked || cfg.gameSource.empty())
		return;
	auto listed = [this](const std::string &n) {
		return std::find(cfg.muteWhileDowned.begin(), cfg.muteWhileDowned.end(), n) !=
		       cfg.muteWhileDowned.end();
	};
	QStringList picked;
	obs_source_t *game = obs_get_source_by_name(cfg.gameSource.c_str());
	if (!game)
		return; // not in this collection yet: try again next time
	bool gameAudio = (obs_source_get_output_flags(game) & OBS_SOURCE_AUDIO) != 0;
	obs_source_release(game);
	if (gameAudio) {
		if (!listed(cfg.gameSource)) {
			cfg.muteWhileDowned.push_back(cfg.gameSource);
			picked << QString::fromStdString(cfg.gameSource);
		}
	} else {
		for (const auto &in : Switcher::inputs())
			if (in.second == "wasapi_output_capture" && !listed(in.first)) {
				cfg.muteWhileDowned.push_back(in.first);
				picked << QString::fromStdString(in.first);
			}
	}
	cfg.audioAutoPicked = true;
	cfg.save();
	if (!picked.isEmpty())
		log(tx("While a squad mate is on screen, %1 is muted so their sound plays instead of yours (Settings, "
		       "Squad & POV, to change).")
			    .arg(picked.join(", ")));
}

void Engine::start()
{
	cfg.startCount++;
	// one scene, always: the plugin's sources go into it and nowhere else. An install from before
	// this took "whatever is live", which put sources into the wrong scene when people switched
	if (cfg.sceneName.empty()) {
		obs_source_t *s = obs_frontend_get_current_scene();
		if (s) {
			cfg.sceneName = obs_source_get_name(s) ? obs_source_get_name(s) : "";
			obs_source_release(s);
			if (!cfg.sceneName.empty())
				log(tx("Scene: '%1' is the scene the plugin works in (it was the one live). Change it under "
				       "Settings, General, if you stream WARDOGS from another.")
					    .arg(QString::fromStdString(cfg.sceneName)));
		}
	}
	cfg.save();
	autoPickAudio();
	// the Discord app knows who you are; a moment after start, so OBS is up first
	QTimer::singleShot(3000, this, [this]() {
		if (!stopping_)
			detectDiscordUser(false);
	});
	if (cfg.appPath.empty()) {
		// the installer (or the portable download) put ClipHound here; adopt it once so the app
		// starts with OBS
		QString def = defaultAppPath();
		if (QFileInfo::exists(def)) {
			cfg.appPath = def.toStdString();
			cfg.launchApp = true;
			cfg.save();
			log(tx("Found ClipHound at %1; it will start with OBS (Settings, General).").arg(def));
		}
	}
	clips.nameTemplate = QString::fromStdString(cfg.clipNameTemplate);
	clips.seriesWindowS = cfg.clipSeriesS;
	clips.folder = QString::fromStdString(cfg.clipFolder);
	clips.watchFolders = cfg.backtrackFolder.empty() ? QStringList()
							 : QStringList{QString::fromStdString(cfg.backtrackFolder)};
	clips.autoStartReplay = cfg.autoStartReplay && cfg.clipUseReplay;
	clips.useReplay = cfg.clipUseReplay;
	clips.hotkeys.clear();
	for (auto &h : cfg.clipHotkeys)
		clips.hotkeys << QString::fromStdString(h);
	if (cfg.bridgeEnabled)
		if (!bridge.listen((quint16)cfg.bridgePort))
			log(tx("ClipHound's bridge could NOT open port %1 - something else is already on it, "
			       "usually an older copy of this plugin still installed. ClipHound will sit at "
			       "\"starting\" and no clips will fire until that is sorted: check for "
			       "C:\\ProgramData\\obs-studio\\plugins\\kennel-wardogs and delete it, then "
			       "restart OBS.")
				    .arg(cfg.bridgePort));
	applyReplaySeconds();
	if (cfg.autoStartReplay && cfg.clipUseReplay)
		clips.ensureReplayBuffer();
	webLiveTimer_.start();
	pictureTimer_.start();
	QTimer::singleShot(8000, this, [this]() {
		if (!stopping_)
			webLiveTick();
	});
	if (cfg.launchApp)
		launchApp();
	sw.migrateNames(cfg); // sources a build before 0.7.0 made, under their old names
	if (!cfg.discordShared1) {
		// 0.7.5 gave every squad mate watching the Discord call their own capture of the same
		// window. Fold them into the one shared capture; a pop-out gets its own again when it appears.
		int folded = 0;
		for (auto &f : cfg.friends) {
			if (!f.sharesDiscordCall() || f.source == Friend::discordCallSourceName())
				continue;
			sw.removeFriendSources(cfg, f);
			f.source.clear();
			f.audioSource.clear();
			std::string e = sw.createFriendSources(cfg, f);
			if (!e.empty())
				log(tx("Could not remake %1's Discord capture: %2")
					    .arg(QString::fromStdString(f.name), QString::fromStdString(e)));
			else
				folded++;
		}
		cfg.discordShared1 = true;
		cfg.save();
		if (folded)
			log(tx("Squad mates watching the Discord call now share one capture (%1 moved over).")
				    .arg(folded));
	}
	if (cfg.discordAudio1) {
		int n = sw.removeDiscordAudio(cfg); // leftovers of slots removed since: gone quietly
		if (n)
			log(tx("Removed %1 old audio capture(s) the plugin no longer uses.").arg(n));
	} else {
		int n = sw.removeDiscordAudio(cfg);
		cfg.discordAudio1 = true;
		cfg.save();
		if (n)
			log((n == 1 ? tx("The plugin no longer captures Discord's sound (1 audio capture removed). Discord "
					 "hands OBS one mix for the whole call, so it comes through whatever already carries "
					 "Discord on your stream.")
				    : tx("The plugin no longer captures Discord's sound (%1 audio captures removed). Discord "
					 "hands OBS one mix for the whole call, so it comes through whatever already carries "
					 "Discord on your stream.")
					      .arg(n)));
	}
	armPopoutWatch();
	// once per slot and Discord user, not at every start: a Discord name that differs from the in-game name is
	// normal (logs: the same three notes in 10 of 10 sessions)
	for (const auto &f : cfg.friends)
		if (f.kind == FriendKind::Discord && !f.handle.empty() &&
		    QString::fromStdString(f.handle).compare(QString::fromStdString(f.name), Qt::CaseInsensitive) !=
			    0) {
			std::string key = f.name + "|" + f.handle;
			if (std::find(cfg.notedHandles.begin(), cfg.notedHandles.end(), key) != cfg.notedHandles.end())
				continue;
			cfg.notedHandles.push_back(key);
			cfg.save();
			log(tx("Squad: the slot named %1 is set to %2's stream. If that is not who it should show, remove "
			       "it and add them again with Add.")
				    .arg(QString::fromStdString(f.name), QString::fromStdString(f.handle)));
		}
#ifdef _WIN32
	// Two copies of this plugin both load, and the second one gets no bridge port: ClipHound then
	// connects to the wrong one and everything looks like it is "starting" for ever.
	for (const char *old :
	     {"C:/ProgramData/obs-studio/plugins/kennel-wardogs", "C:/ProgramData/obs-studio/plugins/povbridge"})
		if (QFileInfo::exists(old) && (oldCopy_ = QString(old).replace('/', '\\'), true))
			log(tx("An older copy of this plugin is still installed at %1. Close OBS, delete that "
			       "folder, and start OBS again - with both installed they fight over ClipHound's "
			       "bridge, clips never fire, and OBS can hang on the way out.")
				    .arg(QString(old).replace('/', '\\')));
#endif
	applyRosterConfig();
	sw.stopMedia(cfg); // the replay source forgets last session's file (it was decoding it at load)
	timer_.start(std::max(100, cfg.pollMs));
	cashTimer_.start();
	if (!cfg.replayFull1) {
		// 0.22.0: instant replays fill the screen, framed by the stinger
		cfg.replayFull1 = true;
		cfg.replayScale = 100;
		cfg.save();
	}
	if (!cfg.povCalm1) {
		// 0.25.0: swaps between squad mates settle for longer, so the SWITCHING POV stinger is not
		// playing all the time
		cfg.nearCooldownS = std::max(cfg.nearCooldownS, 12);
		cfg.povCalm1 = true;
		cfg.save();
	}
	if (!cfg.sessionNet1) {
		// 0.24.1: the bar's Earned and Spent become one Session balance, green up, red down
		QStringList show = QString::fromStdString(cfg.sessionShow).split(',', Qt::SkipEmptyParts);
		int at = std::min(show.indexOf("earned") < 0 ? 1 << 20 : show.indexOf("earned"),
				  show.indexOf("spent") < 0 ? 1 << 20 : show.indexOf("spent"));
		if (at < (1 << 20)) {
			show.removeAll("earned");
			show.removeAll("spent");
			if (!show.contains("net"))
				show.insert(std::min<int>(at, show.size()), "net");
			cfg.sessionShow = show.join(',').toStdString();
		}
		cfg.sessionNet1 = true;
		cfg.save();
	}
	// the linked account's state, then sessions left on this PC by a stream that ended offline
	QTimer::singleShot(15000, this, [this]() {
		if (!stopping_)
			refreshAccount();
	});
	QTimer::singleShot(20000, this, [this]() {
		if (!stopping_)
			flushStatsQueue();
	});
	// the stinger page loaded well before the first replay, so its first wipe is not missed
	QTimer::singleShot(3000, this, [this]() {
		if (stopping_)
			return;
		sw.ensureStinger(cfg, cfg.replayStinger || cfg.povStinger);
		if (!cfg.pipRestore.empty() && !replaying()) {
			// OBS stopped while the game was the small window: put it back where it was
			sw.pipRestore(cfg.pipRestore);
			cfg.pipRestore.clear();
			cfg.save();
			log(tx("Instant replay: the game was left small by the last session; it is back where it was."));
		}
	});
	if (!hudModel_)
		log(tx("Session stats: the cash reader could not start (%1) - kills and deaths are still counted.")
			    .arg(QString::fromStdString(hudModelErr_)));
	healthTimer_.start(5000);
	sw.groupFeeds(cfg);    // feeds made before 0.34.6 were loose in the scene, one row each
	clearSquadLeftovers(); // OBS keeps what was showing when it closed; the stream starts on your own POV
	if (cfg.keepWarm && !applied_ && cfg.active())
		sw.armWarm(cfg);
	QTimer::singleShot(15000, this, [this]() {
		if (!stopping_)
			checkForUpdate(false); // at OBS start, well after everything is up
	});
	// and every 6 hours after: OBS often stays open for days, and a release should not wait for a restart
	updateTimer_.setInterval(6 * 3600 * 1000);
	connect(&updateTimer_, &QTimer::timeout, this, [this]() {
		if (!stopping_ && updStep_ != UpdStep::Downloading)
			checkForUpdate(false);
	});
	updateTimer_.start();
	if (cfg.dualEnabled && cfg.dual())
		QTimer::singleShot(2500, this, [this]() { // after the browser module is fully up
			if (!stopping_ && cfg.dualEnabled && cfg.dual())
				setDual(true, TX_NOOP("on at start-up (Dual POV tab)"));
		});
	emit stateChanged();
}

void Engine::pushAppConfig()
{
	if (bridge.clients() == 0) {
		cfg.appConfigDirty = true;
		cfg.save();
		return;
	}
	QJsonObject set;
	set["player_name"] = QString::fromStdString(cfg.appPlayerName);
	// the game's language as the downed search found it (or as set): the HUD's weapon names are read in it
	set["ui_lang"] = QString::fromStdString(I18n::current()); // the highlights title cards
	set["game_lang"] = QString::fromStdString(cfg.gameLang == "auto" || cfg.gameLang.empty() ? cfg.gameLangFound
												 : cfg.gameLang);
	set["library"] = QString::fromStdString(cfg.appLibrary);
	set["broadcaster"] = QString::fromStdString(cfg.appBroadcaster);
	set["twitch_enabled"] = cfg.appTwitchEnabled;
	set["clip_every_kill"] = cfg.appEveryKill;
	set["roi"] = QJsonArray{cfg.feedX, cfg.feedY, cfg.feedW, cfg.feedH};
	set["multikill_window"] = cfg.appMultikillWindow;
	set["series_window"] = cfg.clipSeriesS; // Twitch titles get "part 2", "part 3" inside this
	set["clip_trim"] = cfg.clipTrim;
	set["clip_trim_lead_s"] = cfg.clipTrimLeadS;
	set["run_merge"] = cfg.runMerge;
	set["run_cut_gaps"] = cfg.runCutGaps;
	set["run_gap_s"] = cfg.runGapS;
	set["replay_chat"] = cfg.replayChat; // "!replay" in chat plays the last highlight
	set["chat_clips"] = std::clamp(cfg.chatClips, 0, 3);
	set["twitch_markers"] = cfg.twitchMarkers;
	set["replay_cooldown_s"] = cfg.replayCooldownS;
	set["replay_word"] = QString::fromStdString(cfg.replayWord.empty() ? "!replay" : cfg.replayWord);
	set["chat_kick"] = QString::fromStdString(cfg.chatKick);
	set["chat_youtube"] = QString::fromStdString(cfg.chatYouTube);
	set["fps"] = cfg.appFps > 0 ? cfg.appFps : 10;
	QJsonObject nb;
	nb["enabled"] = cfg.nearEnabled;
	nb["roi"] = QJsonArray{cfg.nearX, cfg.nearY, cfg.nearW, cfg.nearH};
	QJsonArray names;
	for (const auto &f : cfg.friends)
		if (!f.nearName().empty())
			names.append(QString::fromStdString(f.nearName()));
	// their Discord username as well, when it differs: the in-game name is often a guess made from
	// it, and ClipHound's matcher is fuzzy, so either spelling finds them
	for (const auto &f : cfg.friends)
		if (!f.handle.empty() && QString::fromStdString(f.handle).compare(QString::fromStdString(f.nearName()),
										  Qt::CaseInsensitive) != 0)
			names.append(QString::fromStdString(f.handle));
	nb["names"] = names;
	set["nearby"] = nb;
	QJsonObject vh;
	// read the vehicle corner whenever the window could be turned on by it, or is up and must go
	// when you get out - whichever way it was turned on
	vh["enabled"] = cfg.dual() != nullptr && (cfg.dualAuto || (dualOn_ && !cfg.dualKeep));
	vh["roi"] = QJsonArray{cfg.vehX, cfg.vehY, cfg.vehW, cfg.vehH};
	set["vehicle"] = vh;
	set["ticker"] = QJsonObject{{"enabled", cfg.sessionTrack}}; // the kill ticker under the crosshair
	set["inventory"] = QJsonObject{{"enabled", inventoryWatched()},
				       {"combine", QJsonArray{cfg.invCX, cfg.invCY, cfg.invCW, cfg.invCH}},
				       {"tab", QJsonArray{cfg.invTX, cfg.invTY, cfg.invTW, cfg.invTH}}};
	QJsonObject o;
	o["type"] = "app_config";
	o["set"] = set;
	bridge.sendJson(o);
	cfg.appConfigDirty = false;
	cfg.save();
}

// ----- is there a newer build? -----

/// "0.4.10" is newer than "0.4.9": compare the numbers, not the text.
bool Engine::isNewer(const QString &a, const QString &b)
{
	QStringList x = a.split('.'), y = b.split('.');
	for (int i = 0; i < std::max(x.size(), y.size()); i++) {
		int ax = i < x.size() ? x[i].section(QRegularExpression("[^0-9]"), 0, 0).toInt() : 0;
		int by = i < y.size() ? y[i].section(QRegularExpression("[^0-9]"), 0, 0).toInt() : 0;
		if (ax != by)
			return ax > by;
	}
	return false;
}

bool Engine::updateAvailable() const
{
	return !newVersion_.isEmpty() && isNewer(newVersion_, PLUGIN_VERSION) &&
	       newVersion_.toStdString() != cfg.updateSkip;
}

void Engine::checkForUpdate(bool manual)
{
	if (!manual && !cfg.updateCheck)
		return;
	QString url = QString::fromStdString(cfg.updateUrl);
	if (url.isEmpty()) {
		updateState_ = tx("no update address set");
		emit updateChecked();
		return;
	}
	updateState_ = tx("checking...");
	emit updateChecked();
	Http::getAsync(
		this, url, 8000, QString("KennelggWardogsOBSTool/%1").arg(PLUGIN_VERSION),
		[this, manual](Http::Result r) {
			if (!r.ok) {
				updateState_ = tx("could not check (%1)").arg(r.error);
				if (manual)
					log(tx("Update check: %1").arg(updateState_));
				emit updateChecked();
				return;
			}
			QJsonObject o = QJsonDocument::fromJson(r.body).object();
			newVersion_ = o.value("version").toString();
			newUrl_ = o.value("url").toString();
			newNotes_ = o.value("notes").toString();
			newDownload_ = o.value("download").toString();
			newSha_.clear();
			QString inst = QUrl(newDownload_).fileName();
			if (!inst.isEmpty())
				newSha_ = o.value("sha256").toObject().value(inst).toString().toLower();
			newPortable_ = o.value("portable").toString();
			newPortableSha_ =
				o.value("sha256").toObject().value(QUrl(newPortable_).fileName()).toString().toLower();
			// the same line from changelog/<code>.md, when that version has been translated
			QString local = o.value("notes_i18n")
						.toObject()
						.value(QString::fromStdString(I18n::current()))
						.toString();
			if (!local.isEmpty())
				newNotes_ = local;
			if (newVersion_.isEmpty())
				updateState_ = tx("nothing published to check against yet");
			else if (isNewer(newVersion_, PLUGIN_VERSION)) {
				updateState_ = tx("%1 is out (you have %2)").arg(newVersion_, QString(PLUGIN_VERSION));
				log(tx("A newer build is out: %1").arg(newVersion_) +
				    (newNotes_.isEmpty() ? "" : " - " + newNotes_) +
				    (newUrl_.isEmpty() ? "" : "  " + newUrl_));
				fetchWhatsNew();
				// updating by itself: the installer downloads in the background for when OBS closes,
				// and a patch release's ClipHound and overlays go live at the next quiet moment
				if (cfg.autoUpdate && canSelfUpdate()) {
					startUpdate();
					if (samePatchLine(newVersion_, PLUGIN_VERSION))
						startLiveUpdate();
				}
			} else
				updateState_ = tx("up to date (%1)").arg(QString(PLUGIN_VERSION));
			emit updateChecked();
			emit stateChanged();
		});
}

static const char *kRepoRaw = "https://raw.githubusercontent.com/SombreroAP/kennelgg-wardogs-streaming-tool/main/";

/// The changelog sections newer than this version, in the plugin's language when that language has
/// them (changelog/<code>.md starts at 0.27.0; older sections come from the English CHANGELOG.md).
void Engine::fetchWhatsNew()
{
	auto newer = [](const QString &text) {
		QStringList out;
		const QStringList parts = text.split(QRegularExpression("(?m)^(?=## )"));
		for (const QString &p : parts) {
			QRegularExpressionMatch m = QRegularExpression("^##\\s+v?([0-9][0-9.]*)").match(p);
			if (m.hasMatch() && isNewer(m.captured(1), PLUGIN_VERSION))
				out << p.trimmed();
		}
		return out;
	};
	QString ua = QString("KennelggWardogsOBSTool/%1").arg(PLUGIN_VERSION);
	Http::getAsync(this, QString(kRepoRaw) + "CHANGELOG.md", 10000, ua, [this, newer, ua](Http::Result en) {
		QStringList english = en.ok ? newer(QString::fromUtf8(en.body)) : QStringList();
		auto finish = [this](const QStringList &secs) {
			whatsNew_ = secs.join("\n\n");
			if (updateAvailable())
				emit updateNoticeReady();
			emit stateChanged();
		};
		if (I18n::current() == "en") {
			finish(english);
			return;
		}
		QString url = QString(kRepoRaw) + "changelog/" + QString::fromStdString(I18n::current()) + ".md";
		Http::getAsync(this, url, 10000, ua, [newer, english, finish](Http::Result tr) {
			QStringList local = tr.ok ? newer(QString::fromUtf8(tr.body)) : QStringList();
			// each version in the translation when it has it, else the English one
			QStringList secs;
			for (const QString &e : english) {
				QString ver = e.section('\n', 0, 0);
				QString pick = e;
				for (const QString &l : local)
					if (l.section('\n', 0, 0).trimmed() == ver.trimmed())
						pick = l;
				secs << pick;
			}
			if (english.isEmpty())
				secs = local;
			finish(secs);
		});
	});
}

bool Engine::updateNoticeDue() const
{
	return updateAvailable() && !noticeShownThisRun_ && !streamingOrRecording();
}

void Engine::updateNoticeShown(bool skipVersion)
{
	noticeShownThisRun_ = true;
	if (skipVersion) {
		cfg.updateSkip = newVersion_.toStdString();
		cfg.save();
		emit stateChanged();
	}
}

bool Engine::canSelfUpdate() const
{
#ifdef _WIN32
	if (newDownload_.isEmpty() || newSha_.size() != 64)
		return false;
	// the installer puts the plugin in ProgramData\obs-studio\plugins; a portable OBS keeps it next to OBS
	const char *bin = obs_get_module_binary_path(obs_current_module());
	QString p = QString::fromUtf8(bin ? bin : "").replace('\\', '/').toLower();
	return p.contains("/obs-studio/plugins/kennelgg/");
#else
	return false;
#endif
}

void Engine::startUpdate()
{
	if (!canSelfUpdate()) {
		QDesktopServices::openUrl(QUrl(newUrl_.isEmpty() ? QString("https://kennel.gg/streaming/") : newUrl_));
		return;
	}
	if (updStep_ == UpdStep::Downloading)
		return;
	if (updStep_ == UpdStep::Ready && QFileInfo::exists(updPath_)) {
		emit stateChanged();
		return;
	}
	QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/kennelgg-update";
	QDir().mkpath(dir);
	QString path = dir + "/" + QUrl(newDownload_).fileName();
	updStep_ = UpdStep::Downloading;
	updPct_ = 0;
	updErr_.clear();
	log(tx("Update: downloading %1...").arg(newVersion_));
	emit stateChanged();
	QString want = newSha_, ver = newVersion_;
	Http::downloadAsync(
		this, newDownload_, path, QString("KennelggWardogsOBSTool/%1").arg(PLUGIN_VERSION),
		[this](qint64 got, qint64 total) {
			int pct = total > 0 ? (int)(got * 100 / total) : -1;
			if (pct != updPct_) {
				updPct_ = pct;
				emit stateChanged();
			}
		},
		[this, path, want, ver](Http::Result r) {
			if (!r.ok) {
				updStep_ = UpdStep::Failed;
				updErr_ = r.error;
				log(tx("Update: the download failed (%1).").arg(r.error));
				emit stateChanged();
				return;
			}
			// the file is only run if it is exactly the one the release lists
			QFile f(path);
			QCryptographicHash h(QCryptographicHash::Sha256);
			if (!f.open(QIODevice::ReadOnly) || !h.addData(&f) ||
			    QString::fromLatin1(h.result().toHex()) != want) {
				f.close();
				QFile::remove(path);
				updStep_ = UpdStep::Failed;
				updErr_ = tx("the file did not match the release's checksum");
				log(tx("Update: %1, so it was deleted.").arg(updErr_));
				emit stateChanged();
				return;
			}
			updPath_ = path;
			updVersion_ = ver;
			updStep_ = UpdStep::Ready;
			log(tx("Update: %1 is downloaded and checked, ready to install.").arg(ver));
			emit stateChanged();
		});
}

bool Engine::installUpdate()
{
#ifdef _WIN32
	if (updStep_ != UpdStep::Ready || !QFileInfo::exists(updPath_))
		return false;
	if (obs_frontend_streaming_active() || obs_frontend_recording_active()) {
		log(tx("Update: not while you are streaming or recording - it closes OBS. It is ready for after."));
		return false;
	}
	// update mode: the installer waits for OBS to close, stops ClipHound, installs quietly and opens
	// OBS again from where it is now, as you (not as the administrator the installer runs as)
	QString obs = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
	QString args = QString("/SILENT /SUPPRESSMSGBOXES /NORESTART /UPDATE \"/OBS=%1\"").arg(obs);
	HINSTANCE h = ShellExecuteW(nullptr, L"open", (const wchar_t *)QDir::toNativeSeparators(updPath_).utf16(),
				    (const wchar_t *)args.utf16(), nullptr, SW_SHOWNORMAL);
	if ((INT_PTR)h <= 32) {
		log(tx("Update: the installer did not start (Windows said %1). Install it by hand: %2")
			    .arg(QString::number((INT_PTR)h), updPath_));
		return false;
	}
	log(tx("Update: installing %1 - OBS closes now and opens again when it is done.").arg(newVersion_));
	if (obs_frontend_replay_buffer_active())
		obs_frontend_replay_buffer_stop();
	QTimer::singleShot(1500, this, []() {
		if (QWidget *w = (QWidget *)obs_frontend_get_main_window())
			w->close();
	});
	return true;
#else
	return false;
#endif
}

int Engine::startVodScan(const QString &source)
{
	if (bridge.clients() == 0)
		return 0;
	QJsonObject o;
	o["type"] = "vod_scan";
	o["id"] = ++vodId_;
	o["source"] = source;
	o["player"] = QString::fromStdString(cfg.appPlayerName);
	bridge.sendJson(o);
	log(tx("VOD scan: started on %1.").arg(source));
	return vodId_;
}

void Engine::cancelVodScan()
{
	QJsonObject o;
	o["type"] = "vod_cancel";
	o["id"] = vodId_;
	bridge.sendJson(o);
}

QString Engine::vodClipDir() const
{
	QString dir = QString::fromStdString(cfg.clipFolder);
	if (dir.isEmpty()) {
		char *p = obs_frontend_get_current_record_output_path();
		if (p) {
			dir = QString::fromUtf8(p);
			bfree(p);
		}
	}
	return dir;
}

void Engine::clipVod(const QList<int> &indices)
{
	QJsonObject o;
	o["type"] = "vod_clip";
	o["id"] = vodId_;
	QJsonArray a;
	for (int i : indices)
		a.append(i);
	o["indices"] = a;
	o["out"] = vodClipDir();
	bridge.sendJson(o);
}

void Engine::twitchLogin()
{
	if (bridge.clients() == 0) {
		log(tx("Twitch login needs ClipHound running (the dock's menu, Start ClipHound)."));
		return;
	}
	QJsonObject o;
	o["type"] = "twitch_login";
	bridge.sendJson(o);
}

void Engine::twitchLogout()
{
	QJsonObject o;
	o["type"] = "twitch_logout";
	bridge.sendJson(o);
}

/// The last few lines of ClipHound's own log, for when it starts but never says hello.
QString Engine::defaultAppPath()
{
	// portable OBS runs from <OBS>\bin\64bit and looks for plugins only inside its own folder;
	// the portable download puts ClipHound in <OBS>\ClipHound
	QString portable = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../../ClipHound/ClipHound.exe");
	if (QFileInfo::exists(portable))
		return portable;
	return "C:/ProgramData/Kennel.gg/ClipHound/ClipHound.exe";
}

QStringList Engine::appLogTail(int lines) const
{
	QString dir =
		QFileInfo(cfg.appPath.empty() ? defaultAppPath() : QString::fromStdString(cfg.appPath)).absolutePath();
	QFile f(dir + "/cliphound.log");
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
		return {tx("(no cliphound.log at %1 - it may not have got far enough to write one)").arg(dir)};
	QStringList all = QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts);
	f.close();
	return all.mid(std::max(0, (int)all.size() - lines));
}

bool Engine::appRunning() const
{
#ifdef _WIN32
	if (appPid_ <= 0)
		return false;
	HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)appPid_);
	if (!h)
		return false;
	bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
	CloseHandle(h);
	return alive;
#else
	return appPid_ > 0;
#endif
}

QString Engine::appState() const
{
	if (bridge.clients() > 0)
		return "connected";
	if (appRunning())
		return "starting";
	if (appPid_ > 0 && appStartedAt_.isValid() && appStartedAt_.secsTo(QDateTime::currentDateTime()) < 120)
		return "crashed";
	return "stopped";
}

void Engine::stopApp()
{
	if (bridge.clients() > 0) {
		QJsonObject o;
		o["type"] = "shutdown";
		bridge.sendJson(o);
	}
#ifdef _WIN32
	if (appPid_ > 0) {
		HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, (DWORD)appPid_);
		if (h) {
			if (WaitForSingleObject(h, 1500) == WAIT_TIMEOUT)
				TerminateProcess(h, 0);
			CloseHandle(h);
		}
	}
#endif
	appPid_ = 0;
	appUserStopped_ = true;
	log(tx("ClipHound stopped."));
	emit stateChanged();
}

void Engine::closeApp()
{
	if (!cfg.closeAppWithObs)
		return;
	bool told = false;
	if (bridge.clients() > 0) {
		QJsonObject o;
		o["type"] = "shutdown";
		bridge.sendJson(o);
		// the bytes out before the socket goes. This used to spin the event loop for up to 1.5 s
		// from inside OBS's exit handler, which let timers and other plugins' work run mid-exit
		bridge.flush(300);
		told = true;
	}
#ifdef _WIN32
	if (appPid_ > 0) {
		HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, (DWORD)appPid_);
		if (h) {
			// a moment to exit on its own, then it is ended: OBS must not wait on it
			DWORD r = WaitForSingleObject(h, told ? 1500 : 0);
			if (r == WAIT_TIMEOUT)
				TerminateProcess(h, 0);
			CloseHandle(h);
			log(r == WAIT_TIMEOUT ? tx("ClipHound was ended with OBS.") : tx("ClipHound closed with OBS."));
		}
		appPid_ = 0;
	}
#endif
	(void)told;
}

void Engine::stop()
{
	// called at OBS's exit event, again from the destructor, and possibly from the module unload:
	// everything after the first call is a no-op
	if (stopped_.exchange(true))
		return;
	stopping_ = true;
	stopTimers();
	stateTimer_.stop();
	roster.stop();
	voice.detach();
	closeApp();
	bridge.close();
	Http::shutdown(); // the live checks and the roster fetch: cancelled, and their threads waited for
	stopReplay(TX_NOOP("OBS closing"));
	sw.shutdown(); // the dual-POV scene and its browser page, before obs-browser unloads
	// the poll and frame workers capture `this`: let them finish before the object can go
	waitWorkers(2000);
}

void Engine::stopTimers()
{
	timer_.stop();
	frameTimer_.stop();
	webLiveTimer_.stop();
	pictureTimer_.stop();
	cashTimer_.stop();
	healthTimer_.stop();
	popoutTimer_.stop();
	replayTimer_.stop();
	pipTimer_.stop();
	dualTimer_.stop();
	downDelay_.stop();
	upDelay_.stop();
}

void Engine::waitWorkers(int ms)
{
	// the workers finish on their own; what matters is that none is still inside libobs (a source
	// lookup, a render) when OBS takes the sources or the graphics away. The old wait watched
	// busy_/frameBusy_, which only clear from a queued call - one that cannot run while OBS's exit
	// handler has the thread - so it always ran its full two seconds and proved nothing
	QElapsedTimer t;
	t.start();
	while (workers_ > 0 && t.elapsed() < ms)
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	if (workers_ > 0)
		log(tx("%1 worker thread(s) still running after %2 ms.").arg((int)workers_).arg(ms));
}

void Engine::sceneCleanup()
{
	// OBS is about to release every source: at exit, and when the scene collection changes.
	// Nothing of ours may keep one alive past this point. The dual-POV scene (its browser page and
	// the squad's window captures inside it) and the microphone tap did, so OBS logged "Not all
	// sources were cleared when clearing scene data" at every close, and with a Backtrack output
	// still running the video teardown that followed could hang - OBS never got to unloading
	// its modules and had to be ended from Task Manager.
	if (stopped_ || paused_)
		return;
	paused_ = true;
	stopTimers();
	waitWorkers(2000);
	if (replaying())
		stopReplay(TX_NOOP("scene collection closing"));
	if (voice.attached())
		voice.detach();
	sw.shutdown();
}

void Engine::applyRosterConfig()
{
	if (!cfg.rosterEnabled || cfg.rosterUrl.empty()) {
		roster.stop();
		return;
	}
	roster.configure(QString::fromStdString(cfg.rosterUrl), cfg.rosterPollS,
			 QString::fromStdString(cfg.rosterChannel), QString::fromStdString(cfg.rosterGuild));
}

void Engine::supportNoteShown()
{
	cfg.supportAsked = true;
	cfg.save();
}

void Engine::languageNoteShown()
{
	cfg.langAskShown = true;
	cfg.save();
}

Engine::Access Engine::rosterAccess() const
{
	if (!roster.running() || !roster.healthy() || !roster.membersKnown())
		return Access::Unknown; // nothing polled yet, or the address cannot be read
	if (roster.homeGuild().compare(Config::kennelHomeGuild(), Qt::CaseInsensitive) != 0)
		return Access::NotMember; // not the Kennel.gg bot
	if (cfg.myDiscord.empty())
		return Access::NoUsername;
	return roster.isMember(QString::fromStdString(cfg.myDiscord)) ? Access::Ok : Access::NotMember;
}

bool Engine::rosterLive() const
{
	if (!cfg.rosterEnabled || !roster.running() || !roster.healthy())
		return false;
	Access a = rosterAccess();
	if (a == Access::NotMember || a == Access::NoUsername)
		return false;
	// and it has to see me in a voice channel: on another server, or out of voice, it knows nothing
	// about my squad, and the dock must not hide everyone
	QString me = QString::fromStdString(cfg.myDiscord).toLower();
	for (const auto &m : roster.members())
		if (m.handle.toLower() == me)
			return true;
	return false;
}

bool Engine::rosterOpen() const
{
	Access a = rosterAccess();
	return a == Access::Ok || a == Access::Unknown;
}

QString Engine::rosterStatus() const
{
	switch (rosterAccess()) {
	case Access::NoUsername:
		return tx("locked - enter your Discord username (Setup) so the bot can see you are in the Kennel.gg "
			  "Discord");
	case Access::NotMember:
		return tx("locked - \"%1\" is not in the Kennel.gg Discord. Join it (the Discord button on the dock), and "
			  "check the username is the lower-case one under your display name")
			.arg(QString::fromStdString(cfg.myDiscord));
	default:
		return roster.status();
	}
}

QString Engine::discordUrl() const
{
	return roster.joinUrl().isEmpty() ? QString(Config::kennelDiscordUrl()) : roster.joinUrl();
}

void Engine::setMyDiscord(const QString &user)
{
	cfg.myDiscord = user.trimmed().toLower().toStdString();
	cfg.rosterEnabled = true;
	cfg.save();
	applyRosterConfig();
	roster.poll();
	log(cfg.myDiscord.empty() ? tx("Discord username cleared.")
				  : tx("Discord username set to %1; checking the Kennel.gg Discord for it.")
					    .arg(QString::fromStdString(cfg.myDiscord)));
	emit stateChanged();
}

namespace {
/// Counts a detached worker out when its body ends, whichever way it ends.
struct WorkerGuard {
	std::atomic<int> &n;
	explicit WorkerGuard(std::atomic<int> &c) : n(c) {}
	~WorkerGuard() { n--; }
};
} // namespace

void Engine::detectDiscordUser(bool byHand)
{
	if (stopping_)
		return;
	workers_++;
	std::thread([this, byHand]() {
		WorkerGuard guard(workers_);
		std::optional<DiscordIpc::User> u = DiscordIpc::currentUser(1500);
		QString name = u ? u->username.trimmed().toLower() : QString();
		QMetaObject::invokeMethod(
			this,
			[this, name, byHand]() {
				if (stopping_)
					return;
				QString mine = QString::fromStdString(cfg.myDiscord).toLower();
				if (name.isEmpty()) {
					if (byHand)
						log(tx("Could not ask Discord who you are: is the Discord desktop app running on "
						       "this PC and logged in?"));
				} else if (mine.isEmpty() || byHand) {
					if (mine != name) {
						log(tx("Discord is logged in as %1: taken as your Discord username.")
							    .arg(name));
						setMyDiscord(name);
					} else if (byHand)
						log(tx("Discord confirms your username: %1.").arg(name));
				} else if (mine != name)
					log(tx("Discord on this PC is logged in as %1, but the plugin was given %2. Detect (Setup, or "
					       "the dock) switches to %1.")
						    .arg(name, mine));
				emit discordUserDetected(name, byHand);
			},
			Qt::QueuedConnection);
	}).detach();
}

void Engine::checkAccess()
{
	// the roster's own state, said once per change, so the log shows what the dock is working from
	QString rs = roster.status();
	if (cfg.rosterEnabled && rs != lastRosterStatus_) {
		lastRosterStatus_ = rs;
		log(tx("Discord voice: %1.").arg(rs));
	}
	logOfferedChanges(); // who is live comes from here: say who came onto or left the dock's list
	checkUnpopped();
	Access a = rosterAccess();
	if (a == lastAccess_)
		return;
	Access was = lastAccess_;
	lastAccess_ = a;
	if (a == Access::NotMember || a == Access::NoUsername) {
		log(tx("Squad automation is %1.").arg(rosterStatus()));
		addEvent(tx("squad automation locked - join the Kennel.gg Discord"));
	} else if (a == Access::Ok && was != Access::Unknown) {
		log(tx("Squad automation unlocked: %1 is in the Kennel.gg Discord.")
			    .arg(QString::fromStdString(cfg.myDiscord)));
		syncRoster();
	}
	emit stateChanged();
}

void Engine::syncRoster()
{
	if (!rosterLive())
		return;
	// Only the people actually sharing get a slot. Everyone in the call would mean a window
	// capture each for feeds that do not exist, and Discord puts every share inside the one
	// window anyway - a slot for somebody who is not live could never show anything.
	QList<Roster::Member> live = roster.streamers();
	// "with you": when your Discord username is known, only the channel you are sitting in counts
	if (!cfg.myDiscord.empty()) {
		QString me = QString::fromStdString(cfg.myDiscord).toLower(), myChan;
		for (const auto &m : roster.members())
			if (m.handle.toLower() == me)
				myChan = m.channel;
		QList<Roster::Member> here;
		for (const auto &m : live)
			if (!myChan.isEmpty() && m.channel == myChan && m.handle.toLower() != me)
				here.append(m);
		live = here;
	}
	auto same = [](const Roster::Member &m, const Friend &f) {
		QString h = m.handle.toLower(), n = m.name.toLower();
		QString fh = QString::fromStdString(f.handle).toLower(), fn = QString::fromStdString(f.name).toLower();
		return (!h.isEmpty() && (h == fh || h == fn)) || n == fn;
	};
	QString activeName = (cfg.activeFriend >= 0 && cfg.activeFriend < (int)cfg.friends.size())
				     ? QString::fromStdString(cfg.friends[cfg.activeFriend].name)
				     : QString();
	bool changed = false;

	// gone: they stopped sharing or left the call
	for (size_t i = cfg.friends.size(); i-- > 0;) {
		Friend &f = cfg.friends[i];
		if (!f.fromRoster)
			continue; // yours, not ours
		bool still = false;
		for (const auto &m : live)
			if (same(m, f))
				still = true;
		QString key = QString::fromStdString(f.name).toLower();
		if (still) {
			rosterGone_.remove(key);
			continue;
		}
		// Discord drops the go-live flag for a moment when a stream restarts or changes quality,
		// and a slot deleted then took its pop-out capture with it and came back unbound. Keep it
		// while its pop-out window is open, and otherwise for a minute; it is simply not offered
		// meanwhile (the roster says it is not live)
		if (f.onPopout())
			continue;
		if (!rosterGone_.contains(key)) {
			rosterGone_.insert(key, QDateTime::currentDateTime());
			continue;
		}
		if (rosterGone_.value(key).secsTo(QDateTime::currentDateTime()) < 60)
			continue;
		rosterGone_.remove(key);
		if (applied_ && (int)i == cfg.activeFriend)
			applyNow(false, TX_NOOP("their Discord share ended"));
		sw.removeFriendSources(cfg, f);
		log(tx("Squad: %1 stopped sharing a minute ago - slot removed.").arg(QString::fromStdString(f.name)));
		cfg.friends.erase(cfg.friends.begin() + (long)i);
		changed = true;
	}

	// new: somebody went live in the call
	for (const auto &m : live) {
		bool known = false;
		for (auto &f : cfg.friends)
			if (same(m, f)) {
				known = true;
				if (f.handle.empty() && !m.handle.isEmpty()) {
					f.handle = m.handle.toStdString(); // an older slot learns the username
					changed = true;
				}
			}
		if (known)
			continue; // already there, by hand, from a pop-out, or from an earlier poll
		if (isMe(m.handle))
			continue;
		Friend f;
		// the slot is called what Discord calls them (the username, not "Private Gazreyn"): it is
		// what a pop-out is titled with and what their in-game name is matched against
		f.name = (m.handle.isEmpty() ? m.name : m.handle).toStdString();
		f.handle = m.handle.toStdString();
		f.kind = FriendKind::Discord;
		// matched by executable, so it follows the Go Live window whether or not it is popped out
		f.channel = "Discord:Chrome_WidgetWin_1:Discord.exe";
		f.fromRoster = true;
		if (cfg.rosterAddSources) {
			std::string e = sw.createFriendSources(cfg, f);
			if (!e.empty()) {
				log(tx("Squad: %1 went live in Discord, but the capture could not be made: %2")
					    .arg(m.name, QString::fromStdString(e)));
				continue;
			}
		}
		cfg.friends.push_back(f);
		changed = true;
		log(tx("Squad: %1 is sharing in Discord voice - slot added. Discord's sound is not handled: Discord hands "
		       "OBS one mix for the whole call, so it comes through whatever already carries Discord on your "
		       "stream, and your own game sound stays up while they are shown.")
			    .arg(QString::fromStdString(f.name)));
	}

	if (!changed)
		return;
	// the list moved under it; keep pointing at the same person rather than at whoever slid into
	// that position
	cfg.activeFriend = 0;
	for (size_t i = 0; i < cfg.friends.size(); ++i)
		if (QString::fromStdString(cfg.friends[i].name) == activeName)
			cfg.activeFriend = (int)i;
	cfg.save();
	if (cfg.keepWarm && !applied_)
		sw.armWarm(cfg);
	armPopoutWatch();
	emit stateChanged();
}

/// Is this Discord window somebody's stream, popped out? Discord titles those "<username>'s
/// Stream", and "Discord Popout" for the second before it has drawn. The whole call popped out is
/// titled with the channel name ("General VC"), a camera tile with the bare username: neither is
/// a stream, and neither is ever captured.
/// Discord titles a popped-out stream with its owner, in the client's own language: "sombrero's
/// Stream" in English, "Stream de bouga34" in French (seen on a real PC), "Stream von x" in German,
/// "Stream di x" in Italian, "Stream van x" in Dutch, "Transmissão de x" in Portuguese, "Stream de
/// x" in Spanish, "Стрим x" in Russian, "xのストリーム" in Japanese. Each pattern captures the owner.
/// "Discord Popout" is the title for the second before the window has drawn.
static const QList<QRegularExpression> &popoutPatterns()
{
	static const QList<QRegularExpression> pats = {
		QRegularExpression(QStringLiteral("^(.+?)\\s*(?:['’‘]s?)\\s*stream\\s*$"),
				   QRegularExpression::CaseInsensitiveOption),
		QRegularExpression(
			QStringLiteral(
				"^(?:stream|transmiss[aã]o|transmisi[oó]n|diffusion)\\s+(?:de|von|di|van|af|av|du|d')\\s*(.+?)\\s*$"),
			QRegularExpression::CaseInsensitiveOption),
		QRegularExpression(QStringLiteral("^(?:стрим|трансляция)\\s+(.+?)\\s*$"),
				   QRegularExpression::CaseInsensitiveOption),
		QRegularExpression(QStringLiteral("^(.+?)\\s*(?:のストリーム|의 스트림|的直播)\\s*$"),
				   QRegularExpression::CaseInsensitiveOption),
		QRegularExpression(QStringLiteral("^(.+?)\\s+stream\\s*$"),
				   QRegularExpression::CaseInsensitiveOption), // "<name> Stream", a name ending in s
	};
	return pats;
}

static bool isStreamPopout(const std::string &title)
{
	QString t = QString::fromStdString(title).trimmed();
	if (t.compare("Discord Popout", Qt::CaseInsensitive) == 0)
		return true;
	for (const auto &re : popoutPatterns())
		if (re.match(t).hasMatch())
			return true;
	return false;
}

/// Whose window a Discord pop-out is, lower case: the owner captured from the title in whichever
/// language the client runs. A title that fits no pattern comes back whole, so a slot named by
/// hand after the full title still matches.
static QString popoutOwner(const std::string &title)
{
	QString t = QString::fromStdString(title).trimmed();
	for (const auto &re : popoutPatterns()) {
		QRegularExpressionMatch m = re.match(t);
		if (m.hasMatch() && !m.captured(1).trimmed().isEmpty())
			return m.captured(1).trimmed().toLower();
	}
	return t.toLower();
}

/// Letters and digits only, lower case: "_bgb_" and "BGB" are the same name.
static QString bare(const QString &s)
{
	QString o;
	for (QChar c : s.toLower())
		if (c.isLetterOrNumber())
			o += c;
	return o;
}

/// The title in OBS's "title:class:exe" window spelling (":" in a title is written "#3A").
static std::string windowTitle(const std::string &window)
{
	QString t = QString::fromStdString(window).section(':', 0, 0);
	return t.replace("#3A", ":").replace("#22", "\"").toStdString();
}

bool Engine::isMe(const QString &discordUser) const
{
	QString u = discordUser.toLower();
	if (u.isEmpty())
		return false;
	if (!cfg.myDiscord.empty() && u == QString::fromStdString(cfg.myDiscord).toLower())
		return true;
	if (!cfg.playerName.empty() && u == QString::fromStdString(cfg.playerName).toLower())
		return true;
	QString tw = twitch_.value("login").toString().toLower();
	return !tw.isEmpty() && u == tw;
}

QString Engine::addPopouts(QStringList *addedOut)
{
	std::vector<Switcher::Popout> wins = Switcher::discordPopouts();
	QStringList added, already, failed;
	int unnamed = 0, mine = 0;
	for (const auto &w : wins) {
		if (!isStreamPopout(w.title))
			continue; // the whole call popped out, a camera tile: not a stream, never captured
		QString owner = popoutOwner(w.title);
		if (owner == "discord popout") {
			unnamed++; // Discord has not titled it yet; a second later it will have
			continue;
		}
		if (isMe(owner)) {
			mine++;
			continue;
		}
		bool known = false;
		for (const auto &f : cfg.friends)
			if (owner == QString::fromStdString(f.handle).toLower() ||
			    owner == QString::fromStdString(f.name).toLower())
				known = true;
		if (known) {
			already << owner;
			continue;
		}
		Friend f;
		f.name = owner.toStdString();   // the slot is called what Discord calls them...
		f.handle = owner.toStdString(); // ...and that is also what the game's NEARBY list is matched on
		f.kind = FriendKind::Discord;
		f.channel = Friend::anyDiscordWindow(); // the Discord window is where it goes back to
		std::string err = sw.createFriendSources(cfg, f);
		if (err.empty())
			err = sw.bindPopout(cfg, f, w); // and their own window, by exact title, is what it shows
		if (!err.empty()) {
			failed << QString("%1 (%2)").arg(owner, QString::fromStdString(err));
			continue;
		}
		cfg.friends.push_back(f);
		added << owner;
		if (addedOut)
			*addedOut << owner;
		log(tx("Squad: added %1 from their popped-out Discord stream (\"%2\").")
			    .arg(owner, QString::fromStdString(w.title)));
	}
	if (!added.isEmpty()) {
		if (cfg.friends.size() == added.size())
			cfg.activeFriend = 0;
		cfg.save();
		if (cfg.keepWarm && !applied_)
			sw.armWarm(cfg);
		armPopoutWatch();
		emit stateChanged();
	}
	QStringList out;
	if (!added.isEmpty())
		out << tx("Added %1. Mute each of their streams in Discord (right-click the stream, Mute): Discord hands "
			  "OBS one mix for the whole call, so an unmuted stream's game sound plays in your headphones and "
			  "goes out on your stream through Desktop Audio the whole time. The plugin does not handle it.")
				.arg(added.join(", "));
	if (!already.isEmpty())
		out << (already.size() == 1 ? tx("%1 is already in the squad.") : tx("%1 are already in the squad."))
				.arg(already.join(", "));
	if (!failed.isEmpty())
		out << tx("Could not add %1.").arg(failed.join("; "));
	if (unnamed)
		out << (unnamed == 1 ? tx("1 pop-out is not titled yet - give Discord a second and press Add again.")
				     : tx("%1 pop-outs are not titled yet - give Discord a second and press Add again.")
					       .arg(unnamed));
	if (mine)
		out << tx("Your own stream is popped out; it is not added.");
	if (out.isEmpty())
		out << tx("No popped-out Discord stream found. In Discord, right-click a squad mate's stream and "
			  "choose Pop Out, mute the stream (right-click it again), then press Add.");
	// nothing was added: say exactly which Discord windows were seen, so a title that does not look
	// the way this expects can be read straight off the panel
	if (added.isEmpty()) {
		QStringList seen;
		for (const auto &w : wins)
			seen << "\"" + QString::fromStdString(w.title) + "\"";
		out << (seen.isEmpty() ? tx("No Discord window other than the main one is open.")
				       : tx("Discord windows seen: %1.").arg(seen.join(", ")));
	}
	log(tx("Squad: Add - %1").arg(out.join(" ")));
	return out.join(" ");
}

void Engine::releasePopout(const Friend &f)
{
	if (!f.onPopout())
		return;
	for (const auto &p : Switcher::discordPopouts())
		if (p.window == f.popout)
			Switcher::untuckPopout(p);
}

void Engine::releaseAllPopouts()
{
	for (const auto &f : cfg.friends)
		releasePopout(f);
}

void Engine::showPopouts(bool show)
{
	popoutsShown_ = show;
	if (show) {
		releaseAllPopouts();
		log(tx("Squad: pop-outs brought back on screen. They can go black while covered until you tuck "
		       "them again."));
	} else {
		log(tx("Squad: pop-outs tucked away again."));
		watchPopouts();
	}
	emit stateChanged();
}

void Engine::armPopoutWatch()
{
	int n = 0;
	for (const auto &f : cfg.friends)
		if (f.kind == FriendKind::Discord)
			n++;
	if (n && !popoutTimer_.isActive()) {
		popoutTimer_.start();
		log(n == 1 ? tx("Pop-out watch on for 1 Discord squad mate: pop a share out of Discord and their slot "
				"takes that window by itself.")
			   : tx("Pop-out watch on for %1 Discord squad mates: pop a share out of Discord and their slot "
				"takes that window by itself.")
				     .arg(n));
		watchPopouts();
	} else if (!n && popoutTimer_.isActive()) {
		popoutTimer_.stop();
		log(tx("Pop-out watch off: no Discord squad mates."));
	}
}

static const int kPopoutGraceMs = 20000; // a pop-out has to be gone this long before its slot lets go

void Engine::watchPopouts()
{
	if (stopping_)
		return;
	std::vector<Switcher::Popout> wins;
	const Switcher::Popout *mainWin = nullptr;
	auto all = Switcher::discordPopouts();
	for (const auto &p : all)
		if (isStreamPopout(p.title))
			wins.push_back(p); // the call view or a camera tile popped out is not a stream
		else if (!mainWin && QString::fromStdString(p.title).contains("Discord", Qt::CaseInsensitive))
			mainWin = &p;
	// squad mates without a pop-out watch Discord's own window: keep that capture on it by its exact
	// title, never "any Discord window", which could land on a pop-out or on your own stream
	if (mainWin && sw.pinDiscordCall(mainWin->window))
		blog(LOG_INFO, "[kennelgg] Discord call capture follows the main window: %s", mainWin->title.c_str());
	auto lower = [](const std::string &s) {
		return QString::fromStdString(s).toLower();
	};
	std::vector<bool> taken(wins.size(), false);
	bool changed = false;
	int parked = 0;               // stacking order on the parking monitor, or down the tucked edge
	int bound = (int)wins.size(); // how many pop-outs there are to place, for the spacing
	const Friend *active = cfg.active();
	std::string activeName = active ? active->name : "";

	// 1. everyone: is their window still there, or is there one for them now
	for (auto &f : cfg.friends) {
		if (f.kind != FriendKind::Discord)
			continue;
		QString name = lower(f.name), handle = lower(f.handle);
		// a slot named after the whole window title ("stream de bouga34") means the owner inside it
		QString nameOwner = popoutOwner(f.name);
		// the window picked for the slot by hand: its owner is theirs, whatever the slot is called
		QString picked = f.sharesDiscordCall() || f.channel.empty() ? QString()
									    : popoutOwner(windowTitle(f.channel));
		int hit = -1;
		auto owns = [&](const QString &owner) {
			return (!handle.isEmpty() && owner == handle) || owner == name || owner == nameOwner ||
			       (!picked.isEmpty() && owner == picked) ||
			       (bare(owner).size() >= 2 &&
				(bare(owner) == bare(name) || (!handle.isEmpty() && bare(owner) == bare(handle))));
		};
		// the window the slot is already on, while it is open: a rename, or a name that matches nothing, does not
		// take it away (26 Sep 2026 report: "_bgb_" renamed "BGB" lost its pop-out and fell back to Discord's
		// main window, which showed the streamer's own stream, or the same squad mate under every name). Known by
		// the window itself where we have it: two pop-outs can share a title, and Discord can retitle one
		for (size_t i = 0; i < wins.size() && hit < 0 && f.onPopout(); ++i) {
			if (taken[i] || (f.popoutHwnd ? wins[i].hwnd != f.popoutHwnd : wins[i].window != f.popout))
				continue;
			QString owner = popoutOwner(wins[i].title);
			if (owner != "discord popout" && !owns(owner)) {
				// the same window now shows somebody else (Discord reused it): it is not theirs any more, and
				// OBS would go on showing that other person under their name (LOG-11BC)
				sw.unbindPopout(cfg, f);
				changed = true;
				log(tx("Squad: %1's pop-out now shows %2 - back to the Discord window for them.")
					    .arg(QString::fromStdString(f.name), owner));
				if (applied_ && f.name == activeName)
					applyNow(true, TX_NOOP("their pop-out shows someone else"));
				break;
			}
			if (!f.popoutHwnd)
				f.popoutHwnd = wins[i].hwnd;
			hit = (int)i;
		}
		// exact owner first, so "bryan" can never take "bryanx's Stream"; letters and digits only, so "BGB"
		// is "_bgb_"
		for (size_t i = 0; i < wins.size() && hit < 0; ++i) {
			if (taken[i])
				continue;
			QString owner = popoutOwner(wins[i].title);
			if (owner == "discord popout") // not drawn yet, so not named yet
				continue;
			if (owns(owner))
				hit = (int)i;
		}
		// then a looser look for slots named by hand: the slot name inside the owner's username
		for (size_t i = 0; hit < 0 && i < wins.size(); ++i) {
			if (taken[i])
				continue;
			QString owner = popoutOwner(wins[i].title);
			if (owner == "discord popout")
				continue;
			if (name.size() >= 4 && owner.contains(name))
				hit = (int)i;
		}
		// a pop-out with no name yet is left alone until Discord titles it (about a second): guessing it was the
		// one squad mate still unbound gave that slot a re-opened pop-out of someone else, or your own stream
		// (LOG-11BC)
		if (hit >= 0) {
			taken[hit] = true;
			f.popoutMissingMs = 0;
			if (!f.playing) {
				// their pop-out is open: you are watching them, so they are in this session's squad
				f.playing = true;
				changed = true;
				log(tx("Squad: %1 is playing (their pop-out is open).")
					    .arg(QString::fromStdString(f.name)));
			}
			if (!f.onPopout()) {
				std::string e = sw.bindPopout(cfg, f, wins[hit]);
				if (!e.empty()) {
					log(tx("Squad: found %1's pop-out but could not capture it: %2")
						    .arg(QString::fromStdString(f.name), QString::fromStdString(e)));
					continue;
				}
				log(tx("Squad: %1's share is popped out (\"%2\") - showing that window for them.")
					    .arg(QString::fromStdString(f.name),
						 QString::fromStdString(wins[hit].title)));
				changed = true;
				if (applied_ && f.name == activeName) {
					if (!f.baseSource.empty())
						Switcher::hideEverywhere(f.baseSource);
					applyNow(true, TX_NOOP("their pop-out appeared"));
				}
			}
			if (cfg.popoutTuck && !popoutsShown_ && !wins[hit].minimized) {
				if (cfg.popoutMonitor >= 0) {
					if (Switcher::parkPopout(wins[hit], cfg.popoutMonitor, parked++, bound))
						log(tx("Squad: %1's pop-out parked on monitor %2, on top and fully visible, so "
						       "Discord keeps drawing it and its controls stay in reach.")
							    .arg(QString::fromStdString(f.name),
								 QString::number(cfg.popoutMonitor + 1)));
				} else if (Switcher::tuckPopout(wins[hit], parked++))
					log(tx("Squad: %1's pop-out pinned on top and tucked to the right edge of its screen, so "
					       "Discord keeps drawing it while other windows cover it. Press Show pop-outs "
					       "on the Squad panel to reach its controls.")
						    .arg(QString::fromStdString(f.name)));
			}
			QString minKey = QString::fromStdString(f.name) + "/min";
			if (wins[hit].minimized != minimised_.contains(QString::fromStdString(f.name))) {
				if (wins[hit].minimized)
					minimised_.insert(QString::fromStdString(f.name), (quintptr)wins[hit].hwnd);
				else
					minimised_.remove(QString::fromStdString(f.name));
				emit stateChanged();
			}
			if (wins[hit].minimized && popoutNote_ != minKey) {
				popoutNote_ = minKey;
				log(tx("Squad: %1's pop-out is minimised, so its picture is frozen - restore the window (it can "
				       "sit behind the game, just not minimised).")
					    .arg(QString::fromStdString(f.name)));
			}
		} else if (f.onPopout()) {
			minimised_.remove(QString::fromStdString(f.name));
			// gone this tick. A stream that hiccups, a pop-out Discord redraws, a title that is
			// blank for a second: none of that is "closed". Give it a while before deciding.
			f.popoutMissingMs += popoutTimer_.interval();
			if (f.popoutMissingMs < kPopoutGraceMs)
				continue;
			sw.unbindPopout(cfg, f);
			log(tx("Squad: %1's pop-out has been gone for a while - back to the Discord window for them. Pop "
			       "it out again and the slot takes it straight back.")
				    .arg(QString::fromStdString(f.name)));
			changed = true;
			if (applied_ && f.name == activeName)
				applyNow(true, TX_NOOP("their pop-out closed"));
		}
	}

	// 2. a named pop-out nobody matched: say whose it is, once. Your own stream lands here too.
	for (size_t i = 0; i < wins.size(); ++i) {
		if (taken[i])
			continue;
		QString t = QString::fromStdString(wins[i].title);
		if (t.compare("Discord Popout", Qt::CaseInsensitive) == 0)
			continue;
		if (popoutNote_ != t) {
			popoutNote_ = t;
			QString owner = popoutOwner(wins[i].title);
			bool me = !cfg.playerName.empty() && owner == lower(cfg.playerName);
			log((me ? tx("Squad: a pop-out of Discord user '%1' is open (\"%2\"), which is you, so no slot "
				     "takes it.")
				: tx("Squad: a pop-out of Discord user '%1' is open (\"%2\"), but no slot is named that. "
				     "Name their slot with their Discord username, or turn on Squad from Discord and it "
				     "fills itself in."))
				    .arg(owner, t));
		}
	}
	if (changed) {
		cfg.save();
		if (cfg.keepWarm && !applied_)
			sw.armWarm(cfg);
		emit stateChanged();
	}
}

void Engine::setUiLanguage(const std::string &code)
{
	cfg.uiLang = code.empty() ? "auto" : code;
	cfg.save();
	std::string was = I18n::current();
	I18n::load(I18n::resolve(cfg.uiLang));
	if (I18n::current() == was)
		return;
	// the session bar's page carries the language on its address: point it at the new one
	if (obs_source_t *src = obs_get_source_by_name(Config::sessionOverlayName())) {
		obs_data_t *st = obs_source_get_settings(src);
		QString u = QString::fromUtf8(obs_data_get_string(st, "url"));
		u.remove(QRegularExpression("&lang=[^&]*"));
		u += "&lang=" + QString::fromStdString(I18n::current());
		obs_data_set_string(st, "url", u.toUtf8().constData());
		obs_source_update(src, st);
		obs_data_release(st);
		obs_source_release(src);
	}
	reloadConfig();  // the stinger's page, on its new address
	pushAppConfig(); // ClipHound's highlights title cards
	log(tx("Language: %1").arg(QString::fromStdString(I18n::current())));
	emit languageChanged();
	emit stateChanged();
}

void Engine::reloadConfig()
{
	if (!stopping_)
		// on or off as Settings say; back after a scene-collection change
		sw.ensureStinger(cfg, cfg.replayStinger || cfg.povStinger);
	if (paused_ && !stopping_) {
		// after a scene-collection change: sceneCleanup() let go of everything, so start again
		paused_ = false;
		timer_.start(std::max(100, cfg.pollMs));
		healthTimer_.start(5000);
		webLiveTimer_.start();
		pictureTimer_.start();
		cashTimer_.start();
		if (bridge.clients() > 0 && frameTimer_.interval() > 0)
			frameTimer_.start();
	}
	applyVoice();
	if (cfg.verticalOn()) {
		// the squad's sources into the vertical scene now, not only at the first swap
		std::string ev = sw.applyVertical(cfg, applied_);
		if (!ev.empty())
			log(tx("Vertical: %1").arg(QString::fromStdString(ev)));
	}
	autoPickAudio(); // the game source may have just been chosen
	clips.nameTemplate = QString::fromStdString(cfg.clipNameTemplate);
	clips.seriesWindowS = cfg.clipSeriesS;
	clips.folder = QString::fromStdString(cfg.clipFolder);
	clips.watchFolders = cfg.backtrackFolder.empty() ? QStringList()
							 : QStringList{QString::fromStdString(cfg.backtrackFolder)};
	clips.autoStartReplay = cfg.autoStartReplay && cfg.clipUseReplay;
	clips.useReplay = cfg.clipUseReplay;
	clips.hotkeys.clear();
	for (auto &h : cfg.clipHotkeys)
		clips.hotkeys << QString::fromStdString(h);
	if (cfg.bridgeEnabled && (!bridge.listening() || bridge.port() != cfg.bridgePort)) {
		syncAppPort(); // ClipHound has to be told, or it knocks at the old port for ever
		if (!bridge.listen((quint16)cfg.bridgePort))
			log(tx("ClipHound's bridge could not open port %1 (something else has it).")
				    .arg(cfg.bridgePort));
	} else if (!cfg.bridgeEnabled && bridge.listening())
		bridge.close();
	applyRosterConfig();
	armPopoutWatch();
	applyReplaySeconds();
	{
		std::lock_guard<std::recursive_mutex> lk(detMx_);
		detGame_.threshold = cfg.threshold;
		detRevive_.threshold = cfg.reviveThreshold;
		detGame_.unlock();
		for (auto &d : *altDets_) {
			d->threshold = cfg.threshold;
			d->unlock();
		}
	}
	timer_.setInterval(std::max(100, cfg.pollMs));
	if (cfg.groupFeeds)
		sw.groupFeeds(cfg); // loose feeds into "Kennel.gg · Squad POVs" (and the ones there before 0.34.6)
	else
		sw.ungroupFeeds(cfg); // Settings turned the group off: the feeds back in the scene as they are
	if (cfg.keepWarm && !applied_ && cfg.active())
		sw.armWarm(cfg);
	sw.raiseOnTop(cfg);                 // the camera and alerts list may have just changed
	sw.applyFriendAudio(cfg, applied_); // the Switch tab's tick box takes effect on the spot
	pushAppConfig();                    // areas, names and rules the app reads
	emit stateChanged();
}

bool Engine::revivingRecent() const
{
	return clock_::now() - lastReviveSeen_ < std::chrono::seconds(3);
}

QImage Engine::lastFrame() const
{
	std::lock_guard<std::mutex> lk(frameMx_);
	return lastFrame_;
}

std::string Engine::stateText() const
{
	const Friend *f = cfg.active();
	QString name = f ? QString::fromStdString(f->name) : tx("squad mate");
	if (!cfg.enabled)
		return txs("Auto switch off - your own POV");
	if (applied_)
		return (revivingRecent() ? tx("Showing %1 - being revived") : tx("Showing %1's POV"))
			.arg(name)
			.toStdString();
	if (cfg.gameSource.empty())
		return txs("No game source set");
	if (!cfg.autoDetect || !detGame_.hasTemplate())
		return txs("Manual only");
	return txs("Watching your POV");
}

void Engine::addEvent(const QString &text)
{
	events_ << QDateTime::currentDateTime().toString("HH:mm:ss") + "  " + text;
	while (events_.size() > 30)
		events_.removeFirst();
	log(tx("Event: %1").arg(text));
	emit stateChanged();
}

void Engine::log(const QString &msg)
{
	obs_log(LOG_INFO, "%s", msg.toUtf8().constData());
	logLines_ << QDateTime::currentDateTime().toString("HH:mm:ss.zzz") + "  " + msg;
	while (logLines_.size() > 500)
		logLines_.removeFirst();
	emit logged(msg);
}

// ----- the companion app -----

void Engine::onBridgeMessage(const QJsonObject &o)
{
	QString type = o.value("type").toString();
	if (type == "tally") {
		onTally(o);
		return;
	}
	if (type == "subscribe") {
		if (!o.value("frames").toBool(true))
			QTimer::singleShot(200, this,
					   [this]() { broadcastState(); }); // a controller or the stats overlay
		double fps = bridge.wantedFps();
		if (fps > 0)
			frameTimer_.start((int)(1000.0 / fps));
		else
			frameTimer_.stop();
	} else if (type == "clip") {
		QStringList tags;
		for (auto v : o.value("tags").toArray())
			tags << v.toString();
		QList<double> moments;
		for (auto v : o.value("moments").toArray())
			moments << v.toDouble();
		QJsonObject info = o.value("info").toObject();
		QString err = clips.request(o.value("title").toString(), tags, o.value("source").toString("app"),
					    moments, info);
		QJsonObject r;
		r["type"] = "clip_result";
		r["ok"] = err.isEmpty();
		r["error"] = err;
		r["id"] = o.value("id");
		bridge.sendJson(r);
		if (!err.isEmpty())
			log(tx("Clip request: %1").arg(err));
	} else if (type == "vod_progress") {
		emit vodProgress(o);
	} else if (type == "vod_done") {
		if (o.value("error").toString().isEmpty())
			log(tx("VOD scan: %1 highlights found.").arg(o.value("moments").toArray().size()));
		else
			log(tx("VOD scan: %1").arg(o.value("error").toString()));
		emit vodDone(o);
	} else if (type == "vod_clip_done") {
		emit vodClipDone(o);
	} else if (type == "highlights_status") {
		appStatus_ = o.value("text").toString();
		emit stateChanged();
	} else if (type == "highlights_ready") {
		highlightsBuilding_ = false;
		if (o.value("ok").toBool()) {
			QString path = o.value("path").toString();
			log(tx("Highlights ready: %1 (%2 clips).")
				    .arg(QFileInfo(path).fileName())
				    .arg(o.value("clips").toInt()));
			addEvent(QDateTime::currentDateTime().toString("HH:mm:ss") + "  " +
				 tx("HIGHLIGHTS READY %1").arg(QFileInfo(path).fileName()));
			if (highlightsThenPlay_) {
				highlightsThenPlay_ = false;
				playCompilation(TX_NOOP("built"));
			}
		} else
			log(tx("Highlights: could not build - %1.").arg(o.value("error").toString()));
		emit stateChanged();
	} else if (type == "replay") {
		QString who = o.value("who").toString("chat");
		QString err = chatReplay(who);
		QJsonObject r;
		r["type"] = "replay_result";
		r["ok"] = err.isEmpty();
		r["error"] = err;
		r["who"] = who;
		bridge.sendJson(r);
		if (!err.isEmpty())
			log(tx("Chat replay from %1 not played: %2.").arg(who, err));
	} else if (type == "app_config" && o.contains("values")) {
		qint64 pid = (qint64)o.value("pid").toDouble();
		if (pid > 0 && appPid_ > 0 && pid != appPid_ && appRunning() && pid != otherAppPid_) {
			otherAppPid_ = pid;
			log(tx("Another ClipHound (pid %1) is connected besides the one the plugin started (pid %2). If "
			       "voice or clips misbehave, close every ClipHound.exe in Task Manager and press Start "
			       "ClipHound on the dock.")
				    .arg(pid)
				    .arg(appPid_));
		}
		QJsonObject v = o.value("values").toObject();
		QString av = v.value("app_version").toString();
		if (av != appVersion_) {
			appVersion_ = av;
			if (av.isEmpty())
				log(tx("Companion app: ClipHound with no version file (a copy older than 0.18.11, or run from "
				       "source)."));
			else if (av != PLUGIN_VERSION && av != newVersion_ && av != liveDataVersion())
				log(tx("Companion app: ClipHound %1 but this plugin is %2. Another copy of ClipHound is running "
				       "from somewhere else: close it (Task Manager, ClipHound.exe) and press Start ClipHound "
				       "on the dock.")
					    .arg(av, QString(PLUGIN_VERSION)));
			else
				log(tx("Companion app: ClipHound %1.").arg(av));
		}
		if (cfg.appConfigDirty) {
			pushAppConfig(); // ours wins: the user edited while the app was away
		} else {
			if (v.value("player_name").toString().isEmpty() && !cfg.appPlayerName.empty()) {
				pushAppConfig(); // the app came back with a fresh config: give it ours
				return;
			}
			cfg.appPlayerName = v.value("player_name").toString().toStdString();
			cfg.appLibrary = v.value("library").toString().toStdString();
			cfg.appBroadcaster = v.value("broadcaster").toString().toStdString();
			cfg.appTwitchEnabled = v.value("twitch_enabled").toBool();
			cfg.save();
			emit appConfigReceived();
			// the kill-feed and NEARBY areas are picked in this window, so ours win
			QJsonArray r = v.value("roi").toArray();
			QJsonObject nb = v.value("nearby").toObject();
			QJsonArray nr = nb.value("roi").toArray();
			auto same = [](const QJsonArray &a, double x, double y, double w, double h) {
				return a.size() == 4 && std::abs(a[0].toDouble() - x) < 1e-4 &&
				       std::abs(a[1].toDouble() - y) < 1e-4 && std::abs(a[2].toDouble() - w) < 1e-4 &&
				       std::abs(a[3].toDouble() - h) < 1e-4;
			};
			// clip_every_kill and the multikill window only ever come from our Settings dialog; ClipHound's
			// own copy can be stale (a fresh install, an old config.yaml) and must never overwrite ours -
			// a stream could run for hours with the checkbox checked but the app silently still off (LOG-201D)
			if (!same(r, cfg.feedX, cfg.feedY, cfg.feedW, cfg.feedH) ||
			    !same(nr, cfg.nearX, cfg.nearY, cfg.nearW, cfg.nearH) ||
			    nb.value("enabled").toBool() != cfg.nearEnabled ||
			    (int)v.value("fps").toDouble() != cfg.appFps ||
			    v.value("clip_every_kill").toBool() != cfg.appEveryKill ||
			    std::abs(v.value("multikill_window").toDouble(30) - cfg.appMultikillWindow) > 1e-6)
				pushAppConfig();
		}
	} else if (type == "name_guess") {
		// ClipHound read your in-game name off the kill feed (the rows with a distance are yours)
		QString g = o.value("name").toString().trimmed();
		if (g.isEmpty())
			return;
		if (cfg.appPlayerName.empty()) {
			setInGameName(g);
			log(tx("ClipHound read your in-game name from the kill feed: %1. Your kills, deaths and kill clips "
			       "count from now; change it in Settings, General (Your name in WARDOGS) if it is not right.")
				    .arg(g));
		} else {
			nameGuess_ = g; // a different name is set and never matches: the dock offers this one
			log(tx("ClipHound reads your in-game name as %1 in the kill feed, but %2 is set, so your kills and "
			       "deaths are not being counted.")
				    .arg(g, QString::fromStdString(cfg.appPlayerName)));
		}
		emit stateChanged();
	} else if (type == "twitch_status") {
		// ClipHound answers every settings push with its status: say it only when it changes
		bool same = twitch_.value("state") == o.value("state") && twitch_.value("login") == o.value("login") &&
			    twitch_.value("error") == o.value("error");
		twitch_ = o;
		QString st = o.value("state").toString();
		if (!same && st == "ok" && !o.value("login").toString().isEmpty())
			log(tx("Twitch: logged in as %1.").arg(o.value("login").toString()));
		else if (!same && st == "error")
			log(tx("Twitch login: %1").arg(o.value("error").toString()));
		emit twitchStatusChanged();
	} else if (type == "nearby") {
		onNearby(o);
	} else if (type == "vehicle") {
		onVehicle(o.value("seat").toString());
	} else if (type == "inventory") {
		onInventory(o.value("open").toBool());
	} else if (type == "kill_reward") {
		// ClipHound read the kill ticker under the crosshair: a run's box total (the latest wins)
		int id = o.value("id").toInt();
		int64_t amt = (int64_t)o.value("amount").toDouble();
		qint64 t = (qint64)o.value("t").toDouble();
		if (amt > 0) {
			bool found = false;
			for (auto &k : killRuns_)
				if (k.id == id) {
					k.amount = amt;
					k.last = QDateTime::currentMSecsSinceEpoch();
					found = true;
				}
			if (!found)
				killRuns_.push_back({id, amt, t > 0 ? t : QDateTime::currentMSecsSinceEpoch(),
						     QDateTime::currentMSecsSinceEpoch(), 0});
			while (killRuns_.size() > 30)
				killRuns_.pop_front();
		}
	} else if (type == "holding") {
		holding_ = o.value("name").toString();
		emit stateChanged();
	} else if (type == "nearby_test_result") {
		QStringList texts;
		for (auto v : o.value("texts").toArray())
			texts << v.toString();
		QStringList dists;
		for (auto v : o.value("dists").toArray())
			dists << v.toString();
		log(tx("Test read of the NEARBY area: %1 row(s), %2 chip(s); names read [%3]; metres read [%4].")
			    .arg(o.value("rows").toInt())
			    .arg(o.value("chips").toInt())
			    .arg(texts.join(" | "), dists.join(" | ")));
		emit nearbyTested(o);
	} else if (type == "event") {
		addEvent(o.value("text").toString());
	} else if (type == "status") {
		appStatus_ = o.value("text").toString();
		emit stateChanged();
	} else if (type == "pov") {
		QString force = o.value("force").toString();
		if (force == "downed")
			applyNow(true, TX_NOOP("companion app"));
		else if (force == "up")
			applyNow(false, TX_NOOP("companion app"));
	} else if (type == "control") {
		onControl(o);
	} else if (type == "state_please") {
		bridge.sendJson(stateJson());
	} else if (type == "voice") {
		onVoiceCommand(o.value("cmd").toString(), o.value("name").toString(), o.value("heard").toString());
	} else if (type == "voice_chime") {
		// the same sound into the stream: the wake chime, or the taken / not-understood tones
		QString kind = o.value("kind").toString("wake");
		bool want = kind == "wake" ? cfg.voiceChime : cfg.voiceTones;
		if (cfg.voiceEnabled && want && cfg.voiceChimeWhere != "pc") {
			const char *file = kind == "ok"     ? "sounds/ok.wav"
					   : kind == "fail" ? "sounds/fail.wav"
							    : "sounds/chime.wav";
			char *p = kennel_file(file);
			if (p) {
				std::string e = sw.playSound(cfg, p, cfg.voiceChimeVol);
				if (!e.empty())
					log(tx("Chime: %1").arg(QString::fromStdString(e)));
			}
			bfree(p);
		}
	} else if (type == "voice_miss") {
		setVoiceHeard(tx("\u201c%1\u201d \u2192 not a command").arg(o.value("heard").toString().simplified()),
			      2);
	} else if (type == "voice_wake") {
		setVoiceHeard(tx("wake phrase heard, listening for a command"), 3);
	} else if (type == "voice_ready") {
		// the app's voice module is up: the settings sent at connect may have come too early
		if (cfg.voiceEnabled)
			applyVoice();
	} else if (type == "voice_mic") {
		// ClipHound hears the microphone the plugin sends it: silent for two minutes, or back
		if (o.value("silent").toBool())
			log(tx("Voice: the microphone has been silent for the last two minutes (%1 dBFS). Is the right "
			       "source picked in Settings, Voice, and is it unmuted in Windows?")
				    .arg(o.value("db").toInt()));
		else
			log(tx("Voice: the microphone is heard again."));
	} else if (type == "voice_status") {
		QString s = o.value("text").toString();
		if (s != voiceStatus_) {
			voiceStatus_ = s;
			log(tx("Voice: %1").arg(s));
			emit stateChanged();
		}
	} else if (type == "clip_name") {
		QString path = o.value("path").toString(), title = o.value("title").toString();
		if (title.trimmed().isEmpty())
			log(tx("Voice: nothing usable was said around the clip, name kept."));
		else {
			QString to = clips.retitle(path, title, o.value("text").toString());
			if (to.isEmpty())
				log(tx("Voice: could not rename the clip to \"%1\".").arg(title));
			else
				addEvent(tx("Named: %1").arg(title));
		}
		emit stateChanged();
	}
}

QString Engine::sceneMismatch() const
{
	if (cfg.sceneName.empty())
		return QString();
	obs_source_t *s = obs_frontend_get_current_scene();
	if (!s)
		return QString();
	QString live = obs_source_get_name(s) ? obs_source_get_name(s) : "";
	obs_source_release(s);
	if (live.isEmpty() || live == QString::fromStdString(cfg.sceneName))
		return QString();
	return live;
}

QJsonObject Engine::stateJson() const
{
	QJsonObject o;
	o["type"] = "state";
	o["applied"] = applied_;
	o["enabled"] = cfg.enabled;
	o["active"] = cfg.active() ? QString::fromStdString(cfg.active()->name) : "";
	o["activeIndex"] = cfg.activeFriend;
	o["dual"] = dualOn_;
	o["voice"] = cfg.voiceEnabled;
	o["voiceStatus"] = voiceStatus_;
	o["replayPlaying"] = replayTimer_.isActive();
	o["inVoice"] = rosterLive();
	// for the Stream Deck's keys and dials (0.31.0)
	o["lang"] = QString::fromStdString(I18n::current());
	o["role"] = sessionRole();
	o["closest"] = closestName();
	o["invSwitch"] = cfg.invSwitch;
	o["dualAuto"] = cfg.dualAuto;
	o["stingers"] = cfg.replayStinger || cfg.povStinger;
	o["nameTag"] = cfg.lookName;
	o["popoutsShown"] = popoutsShown_;
	o["highlightsBuilding"] = highlightsBuilding_;
	o["whooshOn"] = cfg.replayStingerSound && cfg.stingerVolume > 0;
	o["whooshVolume"] = cfg.stingerVolume;
	{
		QJsonObject sj = session_.json();
		QJsonArray show;
		for (const QString &id : QString::fromStdString(cfg.sessionShow).split(',', Qt::SkipEmptyParts))
			show.append(id.trimmed());
		sj["show"] = show; // what the on-stream bar draws, in order: Settings, Clips & replays
		QJsonObject ov;
		ov["on"] = cfg.sessionOverlayOn;
		ov["pos"] = QString::fromStdString(cfg.sessionOverlayPos);
		ov["mode"] = QString::fromStdString(cfg.sessionOverlayMode);
		sj["overlay"] = ov; // the page animates every change of these
		o["session"] = sj;
	}
	QJsonArray fr;
	for (size_t i = 0; i < cfg.friends.size(); i++) {
		const Friend &f = cfg.friends[i];
		QJsonObject j;
		j["name"] = QString::fromStdString(f.name);
		j["index"] = (int)i;
		Feed st = feedState(f);
		j["live"] = st == Feed::Live;
		j["off"] = st == Feed::Off;
		fr.append(j);
	}
	o["friends"] = fr;
	return o;
}

void Engine::broadcastState()
{
	if (bridge.allClients() > 0)
		bridge.sendJson(stateJson());
	logOfferedChanges();
}

// ----- session stats

void Engine::resetSession(const QString &why)
{
	session_.reset();
	booked_.clear();
	cashStab_ = tourney::Stabilizer();
	tallied_.clear();
	dropPending_ = false;
	gapSeen_ = 0;
	gapSince_ = 0;
	killRuns_.clear();
	lineLog_.clear();
	lastCashAt_ = 0;
	startCand_ = -1;
	startCandAt_ = 0;
	startCandN_ = 0;
	lastBalEventAt_ = 0;
	balHiddenBefore_ = 0;
	startGapSeen_ = false;
	log(tx("Session stats: counting from now (%1).").arg(txv(why)));
	emit stateChanged();
}

QString Engine::cashStatus() const
{
	if (!cfg.sessionTrack)
		return "off";
	if (!hudModel_)
		return tx("the cash reader could not start");
	if (cfg.gameSource.empty())
		return tx("no game source");
	qint64 now = QDateTime::currentMSecsSinceEpoch();
	if (cashOkAt_ && now - cashOkAt_ < 5000)
		return "";
	return cashWhy_.isEmpty() ? tx("your balance is not on screen") : cashWhy_;
}

/// Where the cash reader looks, as fractions of a W x H game source: the area set on Settings, Detect
/// areas, or the corner it was measured on (0.42 x 0.22 of the height, from the top right).
QRectF Engine::cashArea(double W, double H) const
{
	if (cfg.cashCustom && cfg.cashW > 0.01 && cfg.cashH > 0.01)
		return QRectF(cfg.cashX, cfg.cashY, cfg.cashW, cfg.cashH);
	double rw = std::min(W, hud::kRegionW * H) / std::max(1.0, W);
	return QRectF(1.0 - rw, 0.0, rw, hud::kRegionH);
}

/// The top-right corner of the game (the balance and the reward lines under it), drawn at the size
/// a 4K frame has it, so every resolution reaches the reader alike. A worker per read, one at a time.
void Engine::cashTick()
{
	if (stopping_ || cashBusy_ || !hudModel_ || !cfg.sessionTrack || cfg.gameSource.empty())
		return;
	cashBusy_ = true;
	std::string name = cfg.gameSource;
	auto model = hudModel_;
	workers_++;
	std::thread([this, name, model]() {
		WorkerGuard guard(workers_);
		hud::Reading r;
		bool ok = false;
		obs_source_t *src = obs_get_source_by_name(name.c_str());
		if (src) {
			double W = obs_source_get_width(src), H = obs_source_get_height(src);
			if (W >= 320 && H >= 240) {
				QRectF a = cashArea(W, H);
				std::vector<uint8_t> bgra;
				int w = 0, h = 0, ls = 0;
				if (capCash_.grabRegion(src, a.x(), a.y(), a.width(), a.height(), hud::kRefW, bgra, w,
							h, ls)) {
					r = hud::read(*model, bgra.data(), w, h, ls);
					ok = true;
				}
			}
			obs_source_release(src);
		}
		qint64 t = QDateTime::currentMSecsSinceEpoch();
		QMetaObject::invokeMethod(
			this,
			[this, r, ok, t]() {
				cashBusy_ = false;
				if (!stopping_ && ok)
					onCashReading(r, t);
			},
			Qt::QueuedConnection);
	}).detach();
}

void Engine::onCashReading(const hud::Reading &r, qint64 t)
{
	std::vector<tourney::Event> ev;
	cashStab_.push(t, r, ev);
	if (r.balance.ok)
		cashOkAt_ = t;
	else
		cashWhy_ = QString::fromStdString(r.why);
	bool changed = false;
	for (const auto &e : ev) {
		if (e.kind == tourney::Event::Bal) {
			// how long the balance had been out of sight when this value came: a big change across a
			// menu, a death or a loading screen can be real money (a payout); one while it was on
			// screen all along is far more likely a misread
			if (e.v != session_.balanceNow)
				balHiddenBefore_ = lastBalEventAt_ ? t - lastBalEventAt_ : 0;
			lastBalEventAt_ = t;
			if (!session_.haveBalance) {
				// the start only once it has held: seen again 5 s or more later (the stabilizer repeats a
				// steady balance every 10 s). A menu screen read in a font
				// the reader does not know (the vendor's header, $573,198 read as $537,158) once became
				// the start, and the difference was later "earned" in one go (0.26.6 report)
				if (e.v != startCand_) {
					startCand_ = e.v;
					startCandAt_ = t;
					startCandN_ = 0;
				}
				if (++startCandN_ >= 2 && t - startCandAt_ >= 5000) {
					session_.haveBalance = true;
					// minus what the reward lines counted while it was being confirmed, so those are
					// not taken back again as a gap
					session_.balanceStart = e.v - session_.net();
				}
			} else if (dropPending_) {
				if (e.v >= dropFrom_)
					dropPending_ = false; // it came back: a misread, not a purchase
				else if (e.v < dropTo_)
					dropTo_ = e.v; // bought more before the first drop settled
			} else if (e.v < session_.balanceNow) {
				dropPending_ = true;
				dropFrom_ = session_.balanceNow;
				dropTo_ = e.v;
				dropAt_ = t;
			}
			lastCashOkMs_ = QDateTime::currentMSecsSinceEpoch();
			if (session_.balanceNow != e.v || e.v == session_.balanceStart) {
				session_.balanceNow = e.v;
				changed = true;
			}
		} else if (e.kind == tourney::Event::Feed && !e.hasXp) {
			// A reward shows a money line and an XP line ("KILL +$1,000", "KILL 250XP"): XP lines never
			// count. A line whose amount did not read ("ROTORS DESTROYED" with the figure lost) still
			// counts, unless the same reward already did within 2.5 s with one of the two lacking an
			// amount: that is the same reward's other line. Two real "KILL +$1,000" a second apart
			// both carry amounts, so both count.
			QString reason = QString::fromStdString(e.txt).toUpper().simplified();
			if (e.hasAmt && e.amt > 0) {
				qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
				lineLog_.push_back({nowMs, e.amt});
				while (lineLog_.size() > 200)
					lineLog_.pop_front();
				// a line read after the wallet match already booked its money (as REWARD/KILL): it names that
				// money, it is not more of it. Counted again, the next match took it back as SPENT (logs: +$720
				// then -$720 five seconds later, Earned and Spent both inflated)
				int64_t fresh = e.amt;
				while (fresh > 0 && !booked_.isEmpty()) {
					Booked &b = booked_.first();
					if (nowMs - b.t > 60000) {
						booked_.removeFirst();
						continue;
					}
					int64_t take = std::min(fresh, b.amt);
					session_.roleEarned[b.role] -= take; // moved to the role the line names
					session_.roleEarned[Session::role(reason)] += take;
					if (reason.contains("ZONE"))
						session_.zoneEarned += take;
					b.amt -= take;
					fresh -= take;
					if (b.amt <= 0)
						booked_.removeFirst();
				}
				if (fresh <= 0) {
					changed = true;
					continue; // all of it was already counted
				}
				const int64_t amt = fresh;
				session_.earned += amt;
				if (reason.contains("ZONE"))
					session_.zoneEarned += amt;
				session_.roleEarned[Session::role(reason)] += amt;
				// what it was for, in the words the bar has room for: a code from the reason's words
				// (KILL, REVIVE, HEAL, SPOT, SUPPLY...), translated on the page. A game in another language,
				// or letters that could not settle, is just REWARD - never letter salad on stream
				QString why = Session::why(reason);
				if (why == "REWARD" && !reason.isEmpty() && !unknownReasons_.contains(reason)) {
					// a reward line the plugin has no role for: in the log, so its wording can be added
					unknownReasons_.insert(reason);
					blog(LOG_INFO, "[kennelgg] reward line not recognised: \"%s\" +$%lld",
					     reason.toUtf8().constData(), (long long)amt);
				}
				session_.noteMoney(amt, why);
				changed = true;
			}
			if (reason.isEmpty())
				continue;
			while (!tallied_.isEmpty() && t - tallied_.first().t > 5000)
				tallied_.removeFirst();
			bool same = false;
			for (const auto &p : tallied_)
				if (p.reason == reason && t - p.t < 2500 && (!p.amt || !e.hasAmt))
					same = true;
			if (same)
				continue;
			tallied_.append({reason, t, e.hasAmt});
			if (reason == "KILL" || reason == "REVENGE KILL")
				session_.hudKills++;
			else if (reason == "ASSIST")
				session_.assists++;
			else if (reason.contains("REVIV") || reason.contains("RESUSC"))
				session_.revives++; // REVIVE, TEAMMATE REVIVED, HOT ZONE REVIVE (real matches, Sep 2026)
			else if (reason == "HEADSHOT")
				session_.headshots++;
			else if (reason == "VEHICLE DESTROYED" || reason == "ROTORS DESTROYED")
				session_.vehicles++;
			else
				switch (Session::role(reason)) {
				case Session::Medical:
					session_.heals++;
					break;
				case Session::Recon:
					session_.spots++;
					break;
				case Session::Logistics:
					session_.supplies++;
					break;
				case Session::Building:
					session_.builds++;
					break;
				case Session::Transport:
					session_.transports++;
					break;
				default:
					break;
				}
			changed = true;
		}
	}
	// time in game: only while the balance is on screen. A gap longer than two seconds between reads
	// (OBS busy, the source gone) is not counted
	if (cashStab_.hudVisible(t) && lastCashAt_ && t - lastCashAt_ < 2000)
		session_.activeMs += t - lastCashAt_;
	lastCashAt_ = t;
	if (t - lastPerMinEmit_ >= 5000 && session_.perMinute() >= 0) {
		lastPerMinEmit_ = t; // $/min moves with the clock: the dock and the bar follow every 5 s
		changed = true;
	}
	if (dropPending_ && t - dropAt_ >= 5000) {
		session_.spent += dropFrom_ - dropTo_;
		session_.noteMoney(-(dropFrom_ - dropTo_), "PURCHASE"); // a loadout, a vehicle, a buy
		dropPending_ = false;
		changed = true;
	}
	// The wallet is the truth: the session balance (earned - spent) always comes back to how far the
	// balance has moved since the session began. A reward line lost against a white sky, one read
	// wrong, or money that moved while the HUD was hidden (a payout between matches) is made good
	// once the wallet and the lines have held still for 4 s, so one missed line never leaves the
	// running total wrong.
	// Only against a wallet read just now. While it goes unread for minutes (a white sky, a menu), every reward
	// line read in that time was taken back as SPENT against the old balance, and the lot came back as one
	// REWARD when the wallet reappeared: $160 zone ticks corrected down and up 72 times (LOG-11BC). The 4 s
	// settle starts again once it is read.
	int64_t walletV = 0, walletSeen = 0;
	bool walletFresh = cashStab_.balance(&walletV, &walletSeen) && t - walletSeen <= 1500;
	if (session_.haveBalance && !dropPending_ && !walletFresh) {
		if (gapSeen_ != 0)
			gapSince_ = t;
	} else if (session_.haveBalance && !dropPending_) {
		int64_t gap = (session_.balanceNow - session_.balanceStart) - session_.net();
		if (gap == 0) {
			gapSeen_ = 0;
			gapSince_ = 0;
		} else if (gap != gapSeen_) {
			gapSeen_ = gap; // still moving (a line may be read a moment after the balance changed)
			gapSince_ = t;
		} else if (t - gapSince_ >= 4000) {
			// A change of $10,000 or more that no reward line and no kill run accounts for, while
			// the balance was on screen: the start (or the last reading) was a misread, not money.
			// The session balance keeps its tally and the wallet it is measured against moves.
			int64_t explained = 0;
			{
				qint64 now = QDateTime::currentMSecsSinceEpoch();
				for (const auto &k : killRuns_)
					if (now - k.last <= 120000)
						explained += std::max<int64_t>(0, k.amount - k.credited);
			}
			// The very first gap since calibration is never a payout picked up while the wallet was
			// hidden - nothing has had the chance to pay out yet - so it skips the 45 s allowance a
			// later gap gets: a start read locked onto a wrong number (a menu's font the reader does
			// not know) and corrected once the real HUD showed up after loading into the match, still
			// reads as a huge one-off "earned" without this (adventurebear's 1.4M report, LOG-0777).
			bool misread = std::llabs(gap) >= 10000 && (balHiddenBefore_ < 45000 || !startGapSeen_) &&
				       (gap < 0 || explained * 2 < gap) && !dropPending_;
			startGapSeen_ = true;
			if (misread) {
				session_.balanceStart += gap;
				log(tx("Session balance: your wallet read %1 against what the session had counted, with "
				       "nothing on screen to earn or spend it - taken as a misread of the balance, not money.")
					    .arg(Session::money(gap, true)));
				gapSeen_ = 0;
				gapSince_ = 0;
				changed = true;
			} else if (gap > 0) {
				// the kill ticker first: money a kill run showed that the corner lines around it did
				// not, is kill money; whatever is left is REWARD
				qint64 now = QDateTime::currentMSecsSinceEpoch();
				int64_t left = gap, asKill = 0;
				for (auto &k : killRuns_) {
					if (now - k.last > 120000 || left <= 0)
						continue;
					int64_t lines = 0;
					for (const auto &l : lineLog_)
						if (l.t >= k.start - 1500 && l.t <= k.last + 3000)
							lines += l.amt;
					int64_t missing = k.amount - lines - k.credited;
					if (missing > 0) {
						int64_t take = std::min(missing, left);
						k.credited += take;
						asKill += take;
						left -= take;
					}
				}
				session_.earned += gap;
				if (asKill > 0) {
					session_.noteMoney(asKill, "KILL");
					session_.roleEarned[Session::Combat] += asKill;
					booked_.append({now, asKill, Session::Combat});
				}
				if (left > 0) {
					session_.noteMoney(left, "REWARD");
					session_.roleEarned[Session::Other] += left;
					booked_.append({now, left, Session::Other});
				}
				if (asKill > 0)
					log(tx("Session balance: %1 of it the kill ticker shows as kill money.")
						    .arg(Session::money(asKill, true)));
			} else {
				session_.spent += -gap;
				session_.noteMoney(gap, "SPENT");
			}
			if (!misread) {
				log(tx("Session balance matched to your wallet: %1 the reward lines had not accounted for.")
					    .arg(Session::money(gap, true)));
				gapSeen_ = 0;
				gapSince_ = 0;
				changed = true;
			}
		}
	}
	if (changed)
		emit stateChanged();
}

/// ClipHound read a kill-feed row with you in it: {"type":"tally","kill":bool,"death":bool,
/// "headshot":bool,"distance":m,"weapon":name}.
void Engine::onTally(const QJsonObject &o)
{
	if (!cfg.sessionTrack)
		return;
	if (o.value("death").toBool())
		session_.deaths++;
	else if (o.value("kill").toBool()) {
		session_.kills++;
		if (o.value("headshot").toBool())
			session_.feedHeadshots++;
		session_.longestKillM = std::max(session_.longestKillM, o.value("distance").toInt());
		QString w = o.value("weapon").toString();
		if (!w.isEmpty())
			session_.weapons[w]++;
	}
	emit stateChanged();
}

void Engine::writeSessionSummary()
{
	if (!cfg.sessionTrack)
		return;
	QString text = session_.summary();
	if (session_.killCount() || session_.earned || session_.deaths)
		sessionEndedAt_ = QDateTime::currentDateTime();
	queueSessionUpload();
	for (const auto &l : text.split('\n', Qt::SkipEmptyParts))
		log(tx("Session stats: %1").arg(l));
	QString dir;
	for (auto it = clips.history().rbegin(); it != clips.history().rend() && dir.isEmpty(); ++it)
		if (!it->path.isEmpty())
			dir = QFileInfo(it->path).absolutePath();
	if (dir.isEmpty())
		return;
	QFile f(dir + "/Session stats " + session_.start.toString("yyyy-MM-dd HH-mm") + ".txt");
	if (f.open(QIODevice::WriteOnly | QIODevice::Text))
		f.write(text.toUtf8());
}

// ----- the leaderboards (opt-in)

static QString statsQueuePath()
{
	return QString::fromStdString(Config::configFile("stats-queue.jsonl"));
}

void Engine::setStatsConsent(bool yes)
{
	cfg.statsConsent = yes ? 1 : 2;
	cfg.save();
	if (yes && !accountLinked()) {
		log(tx("Leaderboards: sharing is on; link this PC to your kennel.gg account to take part."));
		linkAccount(); // taking part means a kennel.gg account, as for wagers and the Cash Cup
	} else
		log(yes ? tx("Leaderboards: your session stats go to your kennel.gg account at the end of each stream "
			     "(Settings, Clips & replays, to stop).")
			: tx("Leaderboards: nothing is shared."));
	if (yes)
		flushStatsQueue();
	emit stateChanged();
}

// ----- the kennel.gg account link (the wager/tournament system's device link, reused)

QStringList Engine::accountMissing() const
{
	QStringList out;
	for (const auto &v : accountInfo_.value("missing").toArray())
		out << v.toString();
	if (out.isEmpty() && !accountInfo_.isEmpty() && !accountComplete()) {
		// an older /me without "missing": work it out from the connections themselves
		if (accountInfo_.value("discord").toVariant().isNull())
			out << "discord";
		if (accountInfo_.value("steam").toVariant().isNull())
			out << "steam";
		if (!accountInfo_.value("twitch_verified").toBool())
			out << "twitch";
	}
	return out;
}

void Engine::linkAccount()
{
	if (linkPoll_.isActive()) {
		QDesktopServices::openUrl(QUrl(QString(accountApi()) + "/link/start?code=" + linkCode_));
		return;
	}
	QJsonObject o;
	// a Streaming Tool install: it can read the account, never post game readings (kennel-tourney §8j.1)
	o["kind"] = "streaming";
	o["plugin"] = QString("kennelgg-streaming %1").arg(PLUGIN_VERSION);
	o["obs"] = QString(obs_get_version_string());
	o["os"] = QSysInfo::prettyProductName();
	o["name"] = QSysInfo::machineHostName();
	const QString ua = QString("KennelggWardogsOBSTool/%1").arg(PLUGIN_VERSION);
	Http::requestAsync(
		this, "POST", QString(accountApi()) + "/device/start", QJsonDocument(o).toJson(QJsonDocument::Compact),
		"Content-Type: application/json\r\n", 10000, ua, [this](Http::Result r) {
			QJsonObject a = QJsonDocument::fromJson(r.body).object();
			if (!r.ok || a.value("device_code").toString().isEmpty()) {
				log(tx("Account: kennel.gg did not give a link code (%1).")
					    .arg(r.error.isEmpty() ? QString("HTTP %1").arg(r.status) : r.error));
				return;
			}
			deviceCode_ = a.value("device_code").toString();
			linkCode_ = a.value("user_code").toString();
			linkUntil_ = QDateTime::currentMSecsSinceEpoch() + a.value("expires_in").toInt(900) * 1000;
			linkPoll_.start(std::max(2, a.value("interval").toInt(3)) * 1000);
			log(tx("Account: sign in on kennel.gg to link this PC (code %1).").arg(linkCode_));
			QDesktopServices::openUrl(QUrl(QString(accountApi()) + "/link/start?code=" + linkCode_));
			emit stateChanged();
		});
}

void Engine::pollLink()
{
	if (deviceCode_.isEmpty() || QDateTime::currentMSecsSinceEpoch() > linkUntil_) {
		linkPoll_.stop();
		if (!deviceCode_.isEmpty())
			log(tx("Account: the link code %1 expired. Press Link again.").arg(linkCode_));
		deviceCode_.clear();
		linkCode_.clear();
		emit stateChanged();
		return;
	}
	QJsonObject o;
	o["device_code"] = deviceCode_;
	const QString ua = QString("KennelggWardogsOBSTool/%1").arg(PLUGIN_VERSION);
	Http::requestAsync(this, "POST", QString(accountApi()) + "/device/poll",
			   QJsonDocument(o).toJson(QJsonDocument::Compact), "Content-Type: application/json\r\n", 10000,
			   ua, [this](Http::Result r) {
				   QJsonObject a = QJsonDocument::fromJson(r.body).object();
				   QString st = a.value("status").toString();
				   if (st == "linked") {
					   linkPoll_.stop();
					   deviceCode_.clear();
					   linkCode_.clear();
					   QJsonObject p = a.value("player").toObject();
					   cfg.accountToken = a.value("token").toString().toStdString();
					   cfg.accountId = p.value("id").toString().toStdString();
					   cfg.accountName = p.value("name").toString().toStdString();
					   cfg.save();
					   log(tx("Account: linked to kennel.gg as %1.")
						       .arg(QString::fromStdString(cfg.accountName)));
					   refreshAccount();
					   flushStatsQueue();
					   emit stateChanged();
				   } else if (st == "expired") {
					   linkUntil_ = 0; // the next tick tidies up
				   }
			   });
}

void Engine::refreshAccount()
{
	if (!accountLinked())
		return;
	const QString ua = QString("KennelggWardogsOBSTool/%1").arg(PLUGIN_VERSION);
	Http::requestAsync(
		this, "GET", QString(accountApi()) + "/me", QByteArray(),
		"Authorization: Bearer " + QString::fromStdString(cfg.accountToken) + "\r\n", 10000, ua,
		[this](Http::Result r) {
			if (r.status == 401 || r.status == 403) {
				log(tx("Account: kennel.gg no longer knows this PC's link (unlinked on the site?). Link "
				       "it again to keep sharing."));
				cfg.accountToken.clear();
				cfg.save();
				accountInfo_ = QJsonObject();
				emit stateChanged();
				return;
			}
			QJsonObject me = QJsonDocument::fromJson(r.body).object();
			if (!r.ok || me.isEmpty())
				return;
			accountInfo_ = me.value("account").toObject();
			QString name = me.value("player").toObject().value("name").toString();
			if (!name.isEmpty() && name.toStdString() != cfg.accountName) {
				cfg.accountName = name.toStdString();
				cfg.save();
			}
			emit stateChanged();
		});
}

void Engine::unlinkAccount()
{
	if (!accountLinked())
		return;
	const QString ua = QString("KennelggWardogsOBSTool/%1").arg(PLUGIN_VERSION);
	Http::requestAsync(this, "POST", QString(accountApi()) + "/device/unlink", QByteArray("{}"),
			   "Content-Type: application/json\r\nAuthorization: Bearer " +
				   QString::fromStdString(cfg.accountToken) + "\r\n",
			   10000, ua, [](Http::Result) {});
	cfg.accountToken.clear();
	cfg.accountId.clear();
	cfg.accountName.clear();
	cfg.save();
	accountInfo_ = QJsonObject();
	log(tx("Account: this PC is unlinked from kennel.gg. Nothing is shared until it is linked again."));
	emit stateChanged();
}

void Engine::queueSessionUpload()
{
	if (cfg.statsConsent != 1 || !cfg.sessionTrack || session_.activeMs < 60000)
		return; // no consent, or under a minute in game: nothing worth a leaderboard row
	QJsonObject s;
	s["started"] = session_.start.toString(Qt::ISODate);
	s["active_s"] = (double)(session_.activeMs / 1000);
	s["kills"] = session_.killCount();
	s["deaths"] = session_.deaths;
	s["assists"] = session_.assists;
	s["revives"] = session_.revives;
	s["headshots"] = session_.headshotCount();
	s["vehicles"] = session_.vehicles;
	s["downs"] = session_.downs;
	s["earned"] = (double)session_.earned;
	s["spent"] = (double)session_.spent;
	s["longest_m"] = session_.longestKillM;
	s["top_weapon"] = session_.topWeapon();
	QJsonObject o;
	o["session"] = s;
	o["version"] = PLUGIN_VERSION;
	QFile f(statsQueuePath());
	if (f.open(QIODevice::Append | QIODevice::Text))
		f.write(QJsonDocument(o).toJson(QJsonDocument::Compact) + "\n");
	flushStatsQueue();
}

/// Sends the queue one line at a time; a line leaves the file only once kennel.gg took it. The
/// name, Discord and Twitch go with each send, as they are now.
void Engine::flushStatsQueue()
{
	if (statsFlushing_ || stopping_ || cfg.statsConsent != 1 || !accountLinked())
		return; // no consent, or no kennel.gg account linked: the sessions wait on this PC
	QFile f(statsQueuePath());
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
		return;
	QList<QByteArray> lines = f.readAll().split('\n');
	f.close();
	lines.removeAll(QByteArray());
	if (lines.isEmpty())
		return;
	QJsonObject o = QJsonDocument::fromJson(lines.first()).object(); // who it is comes from the link
	statsFlushing_ = true;
	const QString ua = QString("KennelggWardogsOBSTool/%1").arg(PLUGIN_VERSION);
	Http::requestAsync(
		this, "POST", QString(statsUrl()) + "/session", QJsonDocument(o).toJson(QJsonDocument::Compact),
		"Content-Type: application/json\r\nAuthorization: Bearer " + QString::fromStdString(cfg.accountToken) +
			"\r\n",
		10000, ua, [this](Http::Result r) {
			statsFlushing_ = false;
			if (r.status == 401) {
				refreshAccount(); // the link was revoked: say so, and keep the sessions for the next link
				return;
			}
			bool taken = r.ok && r.status == 200;
			bool refused = r.status == 400 || r.status == 403; // never going to be taken
			if (taken || refused) {
				QFile q(statsQueuePath());
				if (q.open(QIODevice::ReadOnly | QIODevice::Text)) {
					QList<QByteArray> rest = q.readAll().split('\n');
					q.close();
					rest.removeAll(QByteArray());
					if (!rest.isEmpty())
						rest.removeFirst();
					if (q.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
						for (const auto &l : rest)
							q.write(l + "\n");
				}
				log(taken ? tx("Leaderboards: this stream's stats were shared with kennel.gg.")
					  : tx("Leaderboards: kennel.gg refused a session (HTTP %1); it was dropped.")
						    .arg(r.status));
				if (taken)
					QTimer::singleShot(1500, this, [this]() { flushStatsQueue(); });
			} else
				log(tx("Leaderboards: could not reach kennel.gg (%1); the stats wait on this PC and go next time.")
					    .arg(r.error.isEmpty() ? QString("HTTP %1").arg(r.status) : r.error));
		});
}

void Engine::deleteSharedStats()
{
	QFile::remove(statsQueuePath()); // nothing waiting on this PC will be sent either
	if (!accountLinked()) {
		log(tx("Leaderboards: link this PC to your kennel.gg account to delete what it shared."));
		return;
	}
	QJsonObject o;
	const QString ua = QString("KennelggWardogsOBSTool/%1").arg(PLUGIN_VERSION);
	Http::requestAsync(
		this, "POST", QString(statsUrl()) + "/delete", QJsonDocument(o).toJson(QJsonDocument::Compact),
		"Content-Type: application/json\r\nAuthorization: Bearer " + QString::fromStdString(cfg.accountToken) +
			"\r\n",
		10000, ua, [this](Http::Result r) {
			QJsonObject a = QJsonDocument::fromJson(r.body).object();
			log(r.ok && a.value("ok").toBool()
				    ? tx("Leaderboards: deleted from kennel.gg (%1 sessions).")
					      .arg(a.value("deleted").toInt())
				    : tx("Leaderboards: the delete did not go through (%1); try again in a minute.")
					      .arg(r.error.isEmpty() ? QString("HTTP %1").arg(r.status) : r.error));
		});
}

bool Engine::hasSessionOverlay() const
{
	obs_source_t *s = obs_get_source_by_name(Config::sessionOverlayName());
	if (s)
		obs_source_release(s);
	return s != nullptr;
}

QString Engine::addSessionOverlay()
{
	char *p = kennel_file("overlay/session.html");
	std::string path = p ? p : "";
	bfree(p);
	if (path.empty())
		return tx("the overlay page is missing from the plugin's data folder");
	std::replace(path.begin(), path.end(), '\\', '/');
	std::string url = "file:///" + path + "?port=" + std::to_string(cfg.bridgePort) + "&lang=" + I18n::current();
	obs_source_t *ss = cfg.sceneName.empty() ? nullptr : obs_get_source_by_name(cfg.sceneName.c_str());
	if (!ss || !obs_scene_from_source(ss)) {
		if (ss)
			obs_source_release(ss);
		return tx("no scene: pick the plugin's scene under Settings, General");
	}
	obs_scene_t *scene = obs_scene_from_source(ss);
	const char *name = Config::sessionOverlayName();
	// the whole canvas: the page places the bar itself (top left or bottom centre) and animates
	// its moves, so the source never needs moving by hand
	struct obs_video_info ovi;
	obs_get_video_info(&ovi);
	obs_source_t *src = obs_get_source_by_name(name);
	obs_data_t *st = obs_data_create();
	obs_data_set_string(st, "url", url.c_str());
	obs_data_set_int(st, "width", ovi.base_width);
	obs_data_set_int(st, "height", ovi.base_height);
	obs_data_set_bool(st, "shutdown", false);
	if (!src)
		src = obs_source_create("browser_source", name, st, nullptr);
	else
		obs_source_update(src, st); // 0.21.0 made a 1920 x 110 strip: now the full canvas
	obs_data_release(st);
	if (!src) {
		obs_source_release(ss);
		return tx("could not create a browser source (is the Browser Source available in this OBS?)");
	}
	obs_sceneitem_t *item = obs_scene_find_source(scene, name);
	bool added = !item;
	if (!item)
		item = obs_scene_add(scene, src);
	if (item) {
		// placed only when it is first added: turning it back on used to undo where you had moved and sized
		// it (27 Sep log: on again 8 times, 5-7 s apart)
		if (added) {
			struct vec2 pos = {0, 0}, bounds = {(float)ovi.base_width, (float)ovi.base_height};
			obs_sceneitem_set_pos(item, &pos);
			obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
			obs_sceneitem_set_bounds(item, &bounds);
		}
		obs_sceneitem_set_visible(item, true);
	}
	cfg.sessionOverlayOn = true;
	cfg.save();
	obs_source_release(src);
	obs_source_release(ss);
	if (added)
		log(tx("Session stats: the overlay is in your scene (%1); move and size it like any source.")
			    .arg(QString(name)));
	QTimer::singleShot(1500, this, [this]() { broadcastState(); });
	return "";
}

/// Who is sharing in my Kennel.gg voice channel without a popped-out window on this PC. Without
/// one they can be shown only through the main Discord window, one at a time and only while it is
/// on their stream, so the dock says who to pop out.
void Engine::checkUnpopped()
{
	QStringList now;
	if (rosterLive()) {
		QString me = QString::fromStdString(cfg.myDiscord).toLower(), myChan;
		for (const auto &m : roster.members())
			if (m.handle.toLower() == me)
				myChan = m.channel;
		for (const auto &m : roster.members()) {
			if (!m.streaming || m.channel != myChan || isMe(m.handle))
				continue;
			QString h = m.handle.toLower(), n = m.name.toLower();
			bool popped = false;
			for (const auto &f : cfg.friends) {
				if (f.kind != FriendKind::Discord || !f.onPopout())
					continue;
				QString fh = QString::fromStdString(f.handle).toLower(),
					fn = QString::fromStdString(f.name).toLower();
				if ((!h.isEmpty() && (fh == h || fn == h)) || fn == n)
					popped = true;
			}
			if (!popped)
				now << (m.handle.isEmpty() ? m.name : m.handle);
		}
	}
	QDateTime t = QDateTime::currentDateTime();
	for (auto it = unpoppedSince_.begin(); it != unpoppedSince_.end();)
		it = now.contains(it.key()) ? std::next(it) : unpoppedSince_.erase(it);
	QStringList due;
	for (const auto &n : now) {
		if (!unpoppedSince_.contains(n))
			unpoppedSince_.insert(n, t);
		else if (unpoppedSince_.value(n).secsTo(t) >= 20)
			due << n;
	}
	due.sort(Qt::CaseInsensitive);
	if (due != unpopped_) {
		QStringList added;
		for (const auto &n : due)
			if (!unpopped_.contains(n))
				added << n;
		unpopped_ = due;
		if (!added.isEmpty())
			log((added.size() == 1
				     ? tx("Squad: %1 is live in your voice channel but not popped out on this PC.")
				     : tx("Squad: %1 are live in your voice channel but not popped out on this PC."))
				    .arg(added.join(", ")));
		emit stateChanged();
	}
}

/// One line each time a squad mate comes onto or leaves the dock's list, with the reason, so a
/// log says why somebody who was streaming was not there.
void Engine::logOfferedChanges()
{
	QSet<QString> now;
	for (const auto &f : cfg.friends)
		if (inSquadNow(f))
			now.insert(QString::fromStdString(f.name));
	if (!offeredKnown_) {
		offeredKnown_ = true;
		offered_ = now;
		return;
	}
	QStringList in, out;
	for (const auto &n : now)
		if (!offered_.contains(n))
			in << n;
	for (const auto &f : cfg.friends) {
		QString n = QString::fromStdString(f.name);
		if (!offered_.contains(n) || now.contains(n))
			continue;
		QString why = noGamePicture(f) ? tx("no game picture in their capture")
			      : feedState(f) == Feed::Off && rosterLive()
				      ? tx("the Discord voice list has them not live in your channel")
			      : feedState(f) == Feed::Off ? tx("not streaming")
			      : rosterLive()              ? tx("the Discord voice list cannot vouch for them")
							  : tx("not ticked as Playing");
		out << QString("%1 (%2)").arg(n, why);
	}
	for (const auto &n : offered_)
		if (std::none_of(cfg.friends.begin(), cfg.friends.end(),
				 [&](const Friend &f) { return QString::fromStdString(f.name) == n; }))
			out << tx("%1 (slot removed)").arg(n);
	offered_ = now;
	if (!in.isEmpty())
		log(tx("Squad list: now offering %1.").arg(in.join(", ")));
	if (!out.isEmpty())
		log(tx("Squad list: no longer offering %1.").arg(out.join(", ")));
}

void Engine::onControl(const QJsonObject &o)
{
	QString cmd = o.value("cmd").toString();
	QString name = o.value("name").toString();
	log(tx("Controller: %1").arg(cmd + (name.isEmpty() ? "" : " " + name)));
	if (cmd == "replay") {
		playReplay(TX_NOOP("controller"));
	} else if (cmd == "clip") {
		clipNow("manual", {"manual", "controller"}, "controller");
	} else if (cmd == "clip_replay") {
		clipAndReplay(TX_NOOP("controller"));
	} else if (cmd == "dual_toggle") {
		toggleDual();
	} else if (cmd == "dual_on") {
		setDual(true, TX_NOOP("controller"));
	} else if (cmd == "dual_off") {
		setDual(false, TX_NOOP("controller"));
	} else if (cmd == "voice_toggle" || cmd == "voice_on" || cmd == "voice_off") {
		bool on = cmd == "voice_on" ? true : cmd == "voice_off" ? false : !cfg.voiceEnabled;
		cfg.voiceEnabled = on;
		cfg.save();
		applyVoice();
		if (!on)
			voiceStatus_.clear();
		log(on ? tx("Voice control on (controller).") : tx("Voice control off (controller)."));
		emit stateChanged();
	} else if (cmd == "auto_toggle") {
		setEnabled(!cfg.enabled);
	} else if (cmd == "me") {
		if (applied_)
			applyNow(false, TX_NOOP("controller"));
		else
			clearSquadLeftovers();
	} else if (cmd == "highlights") {
		requestHighlights(TX_NOOP("controller"), true);
	} else if (cmd == "show") {
		// by name, by index, or "auto": the live squad mate (the first one live, else the chosen one)
		int idx = -1;
		if (o.contains("index"))
			idx = o.value("index").toInt(-1);
		else if (name == "auto" || name.isEmpty()) {
			// "auto" while a squad mate is up is the same key again: back to you. Asking again who is live could
			// hand over the other squad mate instead, when the one on screen had just flickered off the offer
			// list (LOG-11BC)
			if (applied_ && o.value("toggle").toBool(true)) {
				applyNow(false, TX_NOOP("controller"));
				return;
			}
			idx = anyLiveFriend();
			if (idx < 0)
				idx = cfg.activeFriend;
		} else
			for (size_t i = 0; i < cfg.friends.size(); i++)
				if (QString::fromStdString(cfg.friends[i].name).compare(name, Qt::CaseInsensitive) == 0)
					idx = (int)i;
		if (idx < 0 || idx >= (int)cfg.friends.size()) {
			log(tx("Controller: no squad mate to show."));
			return;
		}
		// the same key again while they are up: back to your own POV
		if (applied_ && cfg.activeFriend == idx && o.value("toggle").toBool(true)) {
			applyNow(false, TX_NOOP("controller"));
			return;
		}
		setActive(idx);
		applyNow(true, "controller: " + QString::fromStdString(cfg.friends[idx].name));
	} else if (cmd == "cycle") {
		int n = (int)cfg.friends.size();
		if (n == 0) {
			log(tx("Controller: no squad mates."));
			return;
		}
		int next = cfg.activeFriend;
		for (int k = 1; k <= n; k++) {
			int i = (cfg.activeFriend + k) % n;
			if (inSquadNow(cfg.friends[i])) {
				next = i;
				break;
			}
		}
		setActive(next);
		applyNow(true, "controller: " + QString::fromStdString(cfg.friends[next].name));
	} else if (cmd == "cycle_pick" || cmd == "cycle_pick_prev") {
		// the squad dial: choose who is next without putting them up (already up: the swap follows)
		int n = (int)cfg.friends.size();
		if (n == 0)
			return;
		int step = cmd == "cycle_pick" ? 1 : n - 1;
		int next = cfg.activeFriend;
		for (int k = 1; k <= n; k++) {
			int i = (cfg.activeFriend + k * step) % n;
			if (feedState(cfg.friends[i]) != Feed::Off) {
				next = i;
				break;
			}
		}
		if (next != cfg.activeFriend)
			setActive(next);
	} else if (cmd == "closest") {
		askNearbyNow();
		pickClosest(TX_NOOP("controller"), true);
		if (cfg.active())
			applyNow(true, TX_NOOP("controller: closest squad mate"));
	} else if (cmd == "clip_tag") {
		// a tag of its own on the clip (letters, digits and dashes; it is also in the file name)
		QString tag;
		for (QChar c : o.value("tag").toString().toLower())
			if (c.isLetterOrNumber() || c == '-')
				tag += c;
		tag = tag.left(20);
		if (tag.isEmpty())
			tag = "highlight";
		clipNow(tag, {"manual", "controller", tag}, "controller");
	} else if (cmd == "session_overlay_toggle") {
		if (cfg.sessionOverlayOn && hasSessionOverlay()) {
			cfg.sessionOverlayOn = false; // it animates out; the source stays for next time
			cfg.save();
		} else {
			QString err = addSessionOverlay(); // adds it if missing, and turns it on
			if (!err.isEmpty())
				log(tx("Session stats overlay: %1").arg(err));
		}
	} else if (cmd == "session_reset") {
		resetSession(tx("reset from the Stream Deck"));
	} else if (cmd == "role") {
		QString want = o.value("role").toString();
		for (const Session::Preset &p : Session::presets())
			if (want == p.id) {
				cfg.sessionShow = p.show;
				cfg.save();
				log(tx("Session bar: %1.").arg(txv(p.label)));
			}
	} else if (cmd == "stats_image") {
		emit controllerWants("stats_image");
	} else if (cmd == "send_logs") {
		emit controllerWants("send_logs");
	} else if (cmd == "invswitch_toggle") {
		setInvSwitch(!cfg.invSwitch);
	} else if (cmd == "dualauto_toggle") {
		cfg.dualAuto = !cfg.dualAuto;
		cfg.save();
		pushAppConfig();
		log(cfg.dualAuto ? tx("Dual POV: opens and closes by itself in vehicles.")
				 : tx("Dual POV: by hand only."));
	} else if (cmd == "stingers_toggle") {
		bool on = !(cfg.replayStinger || cfg.povStinger);
		cfg.replayStinger = cfg.povStinger = on;
		cfg.save();
		sw.ensureStinger(cfg, on);
		log(on ? tx("Stingers on.") : tx("Stingers off."));
	} else if (cmd == "nametag_toggle") {
		cfg.lookName = !cfg.lookName;
		cfg.save();
		sw.updateLook(cfg, lookPreview_ || applied_);
		log(cfg.lookName ? tx("Name tag on.") : tx("Name tag off."));
	} else if (cmd == "popouts_toggle") {
		showPopouts(!popoutsShown_);
	} else if (cmd == "whoosh_toggle") {
		cfg.replayStingerSound = !cfg.replayStingerSound;
		if (cfg.replayStingerSound && cfg.stingerVolume <= 0)
			cfg.stingerVolume = 50;
		cfg.save();
		if (cfg.replayStinger || cfg.povStinger)
			sw.ensureStinger(cfg, true);
		log(cfg.replayStingerSound ? tx("Whoosh on.") : tx("Whoosh off."));
	} else if (cmd == "whoosh_volume") {
		cfg.stingerVolume = std::clamp(cfg.stingerVolume + o.value("delta").toInt(), 0, 100);
		cfg.replayStingerSound = cfg.stingerVolume > 0;
		cfg.save();
		// the page takes its volume from its address: reload it once, when the dial stops
		int gen = ++whooshGen_;
		QTimer::singleShot(600, this, [this, gen]() {
			if (gen == whooshGen_ && (cfg.replayStinger || cfg.povStinger))
				sw.ensureStinger(cfg, true);
		});
	}
	emit stateChanged();
}

void Engine::setInGameName(const QString &name)
{
	// in-game handles never have internal spaces; the kill-feed OCR sometimes splits a short one
	// letter by letter ("B G B"), and a name that keeps it never fuzzy-matches the feed again on
	// ClipHound's side, so no kills are counted (only trimming the ends let that through, LOG-11E6)
	QString clean = name;
	clean.remove(QRegularExpression("\\s+"));
	cfg.appPlayerName = clean.toStdString();
	cfg.save();
	nameGuess_.clear();
	pushAppConfig();
	emit stateChanged();
}

QString Engine::sessionRole() const
{
	for (const Session::Preset &p : Session::presets())
		if (cfg.sessionShow == p.show)
			return p.id;
	return "custom";
}

QString Engine::closestName() const
{
	if (!nearbyFresh())
		return QString();
	QString best;
	int bestDist = INT_MAX;
	for (const NearbyEntry &n : nearby_)
		if (!n.match.isEmpty() && !n.unknown && n.dist < bestDist) {
			best = n.match;
			bestDist = n.dist;
		}
	return best;
}

void Engine::sendVoiceConfig()
{
	if (bridge.clients() == 0)
		return;
	QJsonObject o;
	o["type"] = "voice_config";
	o["enabled"] = true;
	o["wake"] = QString::fromStdString(cfg.voiceWake);
	o["commands"] = cfg.voiceCommands;
	o["names"] = cfg.voiceNames;
	o["chime"] = cfg.voiceChime;
	o["tones"] = cfg.voiceTones;
	o["chime_volume"] = cfg.voiceChimeVol;
	o["chime_local"] = cfg.voiceChimeWhere != "obs"; // ClipHound plays it on the PC's speakers
	QJsonArray allow;
	if (cfg.voiceCmdReplay)
		allow.append("replay");
	if (cfg.voiceCmdClip)
		allow.append("clip");
	if (cfg.voiceCmdDual)
		allow.append("dual");
	if (cfg.voiceCmdForce)
		allow.append("force");
	if (cfg.voiceCmdChange)
		allow.append("change");
	if (cfg.voiceCmdClosest)
		allow.append("closest");
	allow.append("me");
	o["allow"] = allow;
	QJsonArray names;
	for (const Friend &f : cfg.friends)
		names.append(QString::fromStdString(f.name));
	o["squad"] = names;
	bridge.sendJson(o);
}

void Engine::replayAfterClipTick()
{
	if (stopping_)
		return;
	if (replayAfterClipTries_ > 0) {
		const auto &h = clips.history();
		bool haveV = !h.empty() && !h.back().pathV.isEmpty();
		if (!haveV) {
			replayAfterClipTries_--;
			QTimer::singleShot(500, this, &Engine::replayAfterClipTick);
			return;
		}
	}
	replayAfterClipTries_ = 0;
	playReplay(TX_NOOP("clip replay"));
}

void Engine::applyVoice()
{
	if (!cfg.voiceEnabled || bridge.clients() == 0) {
		if (voice.attached()) {
			voice.detach();
			log(tx("Voice: microphone released."));
		}
		return;
	}
	QString mic = QString::fromStdString(cfg.voiceMic);
	if (mic.isEmpty())
		mic = VoiceTap::pickMic();
	if (mic.isEmpty()) {
		if (voiceStatus_ != tx("no microphone source in OBS")) {
			voiceStatus_ = tx("no microphone source in OBS");
			log(tx("Voice: no microphone source found in OBS; add a Mic/Aux input or pick one in Settings."));
		}
		return;
	}
	if (voice.attached() && voice.sourceName() == mic) {
		sendVoiceConfig(); // asked again (the app just came up): the same settings, again
		return;
	}
	voiceFlowing_ = false;
	voiceAttachMs_ = voiceLoudMs_ = QDateTime::currentMSecsSinceEpoch();
	voicePcmMs_ = 0;
	QString err = voice.attach(mic);
	if (!err.isEmpty()) {
		log(tx("Voice: %1").arg(err));
		return;
	}
	log(tx("Voice: listening to '%1' (16 kHz mono goes to ClipHound, nothing is recorded).").arg(mic));
	sendVoiceConfig();
}

void Engine::onVoiceCommand(const QString &cmd, const QString &name, const QString &heard)
{
	if (!cfg.voiceEnabled || !cfg.voiceCommands)
		return;
	log(tx("Voice command: %1  (\"%2\")").arg(cmd + (name.isEmpty() ? "" : " " + name), heard));
	{
		// what the command did, in the dock's words (translated here, when it is shown)
		static const QHash<QString, QString> did = {{"replay", TX_NOOP("instant replay")},
							    {"clip", TX_NOOP("clip saved")},
							    {"clip_replay", TX_NOOP("clip saved, replaying")},
							    {"dual", TX_NOOP("Dual POV")},
							    {"dual_on", TX_NOOP("Dual POV on")},
							    {"dual_off", TX_NOOP("Dual POV off")},
							    {"highlights", TX_NOOP("highlights")},
							    {"me", TX_NOOP("back to you")},
							    {"force", TX_NOOP("squad mate's POV")},
							    {"closest", TX_NOOP("closest squad mate")},
							    {"change", TX_NOOP("next squad mate")}};
		QString what = cmd == "show" ? tx("showing %1").arg(name) : txv(did.value(cmd, cmd));
		setVoiceHeard(tx("\u201c%1\u201d \u2192 %2").arg(heard.simplified(), what), 1);
	}
	if (cmd == "replay" && cfg.voiceCmdReplay) {
		playReplay(TX_NOOP("voice"));
	} else if (cmd == "dual" && cfg.voiceCmdDual) {
		toggleDual();
	} else if (cmd == "dual_on" && cfg.voiceCmdDual) {
		setDual(true, TX_NOOP("voice"));
	} else if (cmd == "dual_off" && cfg.voiceCmdDual) {
		setDual(false, TX_NOOP("voice"));
	} else if (cmd == "clip" && cfg.voiceCmdClip) {
		clipNow("clip", {"manual", "voice"}, "voice");
	} else if (cmd == "clip_replay" && cfg.voiceCmdClip && cfg.voiceCmdReplay) {
		// save now, play it back the moment the file lands
		replayAfterClip_ = true;
		clipNow("clip", {"manual", "voice"}, "voice");
	} else if (cmd == "highlights") {
		requestHighlights(TX_NOOP("voice"), true);
	} else if (cmd == "me") {
		if (applied_)
			applyNow(false, TX_NOOP("voice"));
		else
			clearSquadLeftovers();
	} else if (cmd == "force" && cfg.voiceCmdForce) {
		// the squad mate's POV now, downed or not
		if (!cfg.active()) {
			log(tx("Voice: no squad mate to show."));
			return;
		}
		if (feedState(*cfg.active()) == Feed::Off) {
			int alt = anyLiveFriend();
			if (alt >= 0)
				setActive(alt);
		}
		applyNow(true, TX_NOOP("voice: squad mate POV"));
	} else if (cmd == "closest" && cfg.voiceCmdClosest) {
		askNearbyNow();
		pickClosest(TX_NOOP("voice"), true);
		if (cfg.active())
			applyNow(true, TX_NOOP("voice: closest squad mate"));
	} else if (cmd == "change" && cfg.voiceCmdChange && name.isEmpty()) {
		// no name said: the next squad mate on offer
		int n = (int)cfg.friends.size();
		if (n < 2) {
			log(tx("Voice: only one squad mate to choose from."));
			return;
		}
		int next = cfg.activeFriend;
		for (int k = 1; k <= n; k++) {
			int i = (cfg.activeFriend + k) % n;
			if (inSquadNow(cfg.friends[i])) {
				next = i;
				break;
			}
		}
		setActive(next);
		applyNow(true, "voice: change to " + QString::fromStdString(cfg.friends[next].name));
	} else if ((cmd == "show" || cmd == "change") && cfg.voiceCmdChange) {
		// the squad mate as heard, matched loosely against the slots
		QString want = name.toLower().simplified();
		QString first = want.section(' ', 0, 0); // "bouga34 please" -> "bouga34"
		int best = -1;
		double bestScore = 0;
		for (int i = 0; i < (int)cfg.friends.size(); i++) {
			QString n = QString::fromStdString(cfg.friends[i].name).toLower();
			QString plain = n;
			plain.remove(QRegularExpression("[^a-z0-9 ]"));
			double score = 0;
			if (n == want || plain == want)
				score = 1.0;
			else if (!want.isEmpty() && (plain.startsWith(want) || plain.contains(" " + want)))
				score = 0.8;
			else if (!want.isEmpty() && want.size() >= 3 && plain.contains(want))
				score = 0.6;
			else if (first.size() >= 3 && (plain == first || plain.startsWith(first)))
				score = 0.5;
			else if (first.size() >= 4 && plain.contains(first))
				score = 0.4;
			if (score > bestScore) {
				bestScore = score;
				best = i;
			}
		}
		if (best < 0) {
			log(tx("Voice: no squad mate sounds like \"%1\".").arg(name));
			return;
		}
		cfg.activeFriend = best;
		cfg.save();
		applyNow(true, "voice: show " + QString::fromStdString(cfg.friends[best].name));
		emit stateChanged();
	}
}

// ----- the game's NEARBY list (bottom right): who is closest -----

void Engine::clearNearby()
{
	if (nearby_.isEmpty() && !nearbyAt_.isValid())
		return;
	nearby_.clear();
	nearbyAt_ = QDateTime();
	nearbyEmptySince_ = QDateTime();
	nearbyLine_.clear();
	nearbyWho_.clear();
	emit stateChanged();
}

void Engine::onNearby(const QJsonObject &o)
{
	if (!detected_ && !applied_) {
		clearNearby(); // you are up: who was near you a moment ago is not relevant
		return;
	}
	QList<NearbyEntry> list;
	for (auto v : o.value("list").toArray()) {
		QJsonObject e = v.toObject();
		NearbyEntry n;
		n.name = e.value("name").toString();
		n.match = e.value("match").toString();
		n.dist = e.value("dist").toInt(-1);
		n.unknown = e.value("unknown").toBool() || n.dist >= 998;
		if (n.dist >= 0)
			list << n;
	}
	// an empty reading is usually one bad frame (ClipHound only reports empty after three in a
	// row), so it never throws away a good list: freshness decides when that list stops counting
	if (!list.isEmpty()) {
		nearby_ = list;
		nearbyAt_ = QDateTime::currentDateTime();
		nearbyEmptySince_ = QDateTime();
	} else if (!nearbyEmptySince_.isValid())
		nearbyEmptySince_ = QDateTime::currentDateTime();
	QString line = nearbyText();
	if (line != nearbyLine_) {
		nearbyLine_ = line;
		emit stateChanged(); // the dock shows the metres; they change constantly
	}
	QStringList who;
	for (const auto &e : nearby_)
		who << (e.match.isEmpty() ? e.name : e.match);
	if (who.join(',') != nearbyWho_) { // only who is there is worth a log line
		nearbyWho_ = who.join(',');
		log(tx("Nearby: %1").arg(line.isEmpty() ? tx("nobody") : line));
	}
	// follow the closest all the time, so the dock always shows who would be used and that feed
	// is the one kept warm; the margin and the cooldown inside pickClosest stop it flapping
	if (cfg.nearEnabled)
		pickClosest(applied_ ? TX_NOOP("still down") : detected_ ? TX_NOOP("going down") : TX_NOOP("nearest"));
}

bool Engine::nearbyFresh() const
{
	return nearbyAt_.isValid() && nearbyAt_.secsTo(QDateTime::currentDateTime()) <= std::max(2, cfg.nearTtlS);
}

QString Engine::nearbyText() const
{
	QStringList parts;
	for (const auto &e : nearby_)
		parts << (e.match.isEmpty() ? e.name : e.match) + " " +
				 (e.unknown ? tx("? m") : tx("%1 m").arg(e.dist));
	return parts.join("  ·  ");
}

/// One line for the dock: the reading, or why there is not one.
QString Engine::nearbyStatus() const
{
	if (!cfg.nearEnabled)
		return tx("off");
	if (bridge.clients() == 0)
		return tx("ClipHound is NOT running - Closest cannot work until it is (dock → Start ClipHound)");
	if (!detected_ && !applied_)
		return tx("N/A while you are up");
	if (!nearbyAt_.isValid())
		return nearbyEmptySince_.isValid()
			       ? tx("nobody matched yet - use Test read under Settings, Detect areas")
			       : (bridge.clients() > 0 ? tx("read when you go down (nothing read yet)")
						       : tx("ClipHound is not running"));
	QString t = nearbyText();
	if (nearbyFresh())
		return nearbyEmptySince_.isValid() ? tx("%1  (last seen)").arg(t) : t;
	return tx("%1  (%2 s old)").arg(t, QString::number(nearbyAt_.secsTo(QDateTime::currentDateTime())));
}

int Engine::friendIndexFor(const QString &gameName) const
{
	if (gameName.isEmpty())
		return -1;
	for (size_t i = 0; i < cfg.friends.size(); i++)
		if (QString::fromStdString(cfg.friends[i].nearName()).compare(gameName, Qt::CaseInsensitive) == 0)
			return (int)i;
	for (size_t i = 0; i < cfg.friends.size(); i++)
		if (QString::fromStdString(cfg.friends[i].name).compare(gameName, Qt::CaseInsensitive) == 0)
			return (int)i;
	for (size_t i = 0; i < cfg.friends.size(); i++)
		if (QString::fromStdString(cfg.friends[i].handle).compare(gameName, Qt::CaseInsensitive) == 0)
			return (int)i;
	return -1;
}

bool Engine::feedUsable(const Friend &f) const
{
	if (f.isWeb())
		return !f.channel.empty();
	std::string n = cfg.sourceFor(f);
	if (n.empty())
		return false;
	obs_source_t *src = obs_get_source_by_name(n.c_str());
	if (!src)
		return false;
	obs_source_release(src);
	return true;
}

Engine::Feed Engine::feedState(const Friend &f) const
{
	if (f.kind == FriendKind::Twitch || f.kind == FriendKind::Kick || f.kind == FriendKind::YouTube) {
		if (rosterAccess() != Access::Ok)
			return Feed::Unknown;
		return webLive_.value(webLiveKey(f), Feed::Unknown);
	}
	if (f.kind != FriendKind::Discord)
		return Feed::Unknown;
	// The voice roster is the authority when it knows my channel: a squad mate it does not list
	// there is not streaming to me, whatever windows are still open on this PC - Discord leaves a
	// pop-out up after a stream ends, and a bound window is not proof of a picture.
	if (rosterLive()) {
		QString h = QString::fromStdString(f.handle).toLower(), n = QString::fromStdString(f.name).toLower();
		QString me = QString::fromStdString(cfg.myDiscord).toLower(), myChan;
		if (!me.isEmpty())
			for (const auto &m : roster.members())
				if (m.handle.toLower() == me)
					myChan = m.channel;
		auto isThem = [&](const Roster::Member &m) {
			QString mh = m.handle.toLower(), mn = m.name.toLower();
			if (!h.isEmpty() && (mh == h || mn == h))
				return true;
			if (mh == n || mn == n)
				return true;
			// display names carry a rank ("Recruit Moriar") in front of the name a slot was given
			return n.size() >= 3 &&
			       (mn.endsWith(" " + n) || mn.startsWith(n + " ") || mn.contains(" " + n + " "));
		};
		for (const auto &m : roster.members())
			if (isThem(m)) {
				if (!myChan.isEmpty() && m.channel != myChan)
					return Feed::Off;
				// the roster saying they are live is proof enough to offer them: the picture check
				// only takes them off screen if what their capture holds turns out to be the call
				// grid once it is shown (0.19.7 hid live squad mates whenever the main Discord
				// window was on the call while their stream was watched in a pop-out)
				return m.streaming ? Feed::Live : Feed::Off;
			}
		if (!myChan.isEmpty())
			return Feed::Off; // the roster knows my channel and they are not in it
		// the roster does not see me at all: I am playing on a server the bot is not in, or not in
		// voice. It cannot vouch for anyone, so the windows on this PC decide, as without a roster
	}
	// nobody can vouch for them: what their capture holds decides, never the call grid
	if (noGamePicture(f))
		return Feed::Off;
	if (f.onPopout())
		return Feed::Live; // no roster to ask: a bound window is the best sign there is
	return Feed::Unknown;
}

QString Engine::feedStateText(const Friend &f) const
{
	switch (feedState(f)) {
	case Feed::Live:
		return tx("live");
	case Feed::Off:
		if (webOtherGame_.contains(webLiveKey(f)))
			return tx("playing %1").arg(webOtherGame_.value(webLiveKey(f)));
		return noGamePicture(f) ? tx("no game picture") : tx("not streaming");
	default:
		return "";
	}
}

bool Engine::inSquadNow(const Friend &f) const
{
	Feed st = feedState(f);
	if (rosterLive())
		return st == Feed::Live; // in a Kennel.gg voice channel: whoever is live in it
	return f.playing && st != Feed::Off;
}

/// Someone worth putting up for a swap nobody asked for (the inventory, a vehicle): confirmed live,
/// or a feed nothing can check (a VDO.Ninja link, an OBS source) - never a slot whose stream is off
/// or cannot be vouched for.
bool Engine::confirmedLive(const Friend &f) const
{
	Feed st = feedState(f);
	if (st == Feed::Live)
		return true;
	return st == Feed::Unknown && (f.kind == FriendKind::VdoNinja || f.kind == FriendKind::ObsSource);
}

int Engine::confirmedLiveFriend() const
{
	if (cfg.active() && confirmedLive(*cfg.active()))
		return cfg.activeFriend;
	for (size_t i = 0; i < cfg.friends.size(); ++i)
		if (confirmedLive(cfg.friends[i]))
			return (int)i;
	return -1;
}

int Engine::anyLiveFriend() const
{
	if (cfg.active() && feedState(*cfg.active()) != Feed::Off)
		return cfg.activeFriend;
	for (size_t i = 0; i < cfg.friends.size(); ++i)
		if (feedState(cfg.friends[i]) == Feed::Live)
			return (int)i;
	for (size_t i = 0; i < cfg.friends.size(); ++i)
		if (feedState(cfg.friends[i]) == Feed::Unknown)
			return (int)i;
	return -1;
}

bool Engine::noGamePicture(const Friend &f) const
{
	if (!cfg.pictureCheck || f.kind != FriendKind::Discord)
		return false;
	auto it = picture_.constFind(QString::fromStdString(f.source));
	if (it == picture_.constEnd() || !it->off)
		return false;
	// a capture nobody has looked at for a minute (hidden since) is not known to be empty any more
	return QDateTime::currentMSecsSinceEpoch() - it->atMs < 60000;
}

QStringList Engine::namesOn(const QString &source) const
{
	QStringList out;
	for (const auto &f : cfg.friends)
		if (f.kind == FriendKind::Discord && QString::fromStdString(f.source) == source)
			out << QString::fromStdString(f.name);
	return out;
}

/// Look at every Discord capture that is running: the warm one under its hide filter, the one on
/// screen, the vertical canvas's. A capture that is not showing anywhere is not drawn by OBS, so
/// what it holds is old and is not looked at. On screen it is looked at every second, so the call
/// grid comes off within two; otherwise every other second.
void Engine::pictureTick()
{
	if (stopping_ || pictureBusy_ || !cfg.pictureCheck)
		return;
	pictureTickN_++;
	if (!applied_ && (pictureTickN_ % 2) != 0)
		return;
	std::vector<std::string> names;
	for (const auto &f : cfg.friends)
		if (f.kind == FriendKind::Discord && !f.source.empty() &&
		    std::find(names.begin(), names.end(), f.source) == names.end())
			names.push_back(f.source);
	if (names.empty())
		return;
	pictureBusy_ = true;
	workers_++;
	std::thread([this, names]() {
		WorkerGuard guard(workers_);
		struct Seen {
			QString source;
			Picture::Look look;
		};
		std::vector<Seen> seen;
		for (const auto &n : names) {
			if (stopping_)
				break;
			obs_source_t *src = obs_get_source_by_name(n.c_str());
			if (!src)
				continue;
			if (obs_source_showing(src)) {
				obs_source_t *hf = obs_source_get_filter_by_name(src, Config::hideFilterName());
				std::vector<uint8_t> bgra;
				int w = 0, h = 0, ls = 0;
				if (capPicture_.grabRegion(src, 0, 0, 1, 1, Picture::kWidth, bgra, w, h, ls, hf))
					seen.push_back(
						{QString::fromStdString(n), Picture::look(bgra.data(), w, h, ls)});
				if (hf)
					obs_source_release(hf);
			}
			obs_source_release(src);
		}
		QMetaObject::invokeMethod(
			this,
			[this, seen]() {
				pictureBusy_ = false;
				if (stopping_)
					return;
				for (const auto &s : seen)
					if (!s.look.empty)
						pictureSeen(s.source, s.look.picture, s.look.area, s.look.density);
			},
			Qt::QueuedConnection);
	}).detach();
}

void Engine::pictureSeen(const QString &source, bool picture, double area, double density)
{
	PictureSeen &p = picture_[source];
	p.atMs = QDateTime::currentMSecsSinceEpoch();
	p.area = area;
	p.density = density;
	const Friend *act = cfg.active();
	bool onScreen = applied_ && act && QString::fromStdString(act->source) == source;
	QStringList who = namesOn(source);
	QString names = who.size() > 3 ? tx("%1 and %2 others").arg(who.mid(0, 2).join(", ")).arg(who.size() - 2)
				       : who.join(", ");
	if (picture) {
		p.good++;
		p.bad = 0;
		// three good looks to come back on offer (two to go off): a feed on the edge flipped on and off the list
		// every few seconds, and every flip changed the squad the Stream Deck showed (LOG-11BC: 8 flips)
		if (!p.off || p.good < 3)
			return;
		p.off = false;
		log(tx("Picture check: %1 - a game picture again, back on offer.").arg(names));
		// still downed, and the swap came off because this feed had nothing: put it back
		if (detected_ && !applied_ && cfg.enabled && !downDelay_.isActive() && act &&
		    QString::fromStdString(act->source) == source)
			applyNow(true, tx("game picture back in %1's feed").arg(QString::fromStdString(act->name)));
		emit stateChanged();
		return;
	}
	p.bad++;
	p.good = 0;
	// put up by hand: a dark scene or Discord's window around the stream can fail the check (26 Sep log: shows from
	// the Stream Deck taken down, pressed again 7 s later). Yours to take down, and not taken off offer under you
	// either (LOG-11BC)
	if (onScreen && manualShow_)
		return;
	// two looks on screen (about two seconds), three when it is only warm: a stream loading or a
	// dark moment never takes a squad mate off
	if (p.off || p.bad < (onScreen ? 2 : 3))
		return;
	p.off = true;
	bool shared = who.size() > 1 || source == Friend::discordCallSourceName();
	log(shared ? tx("Picture check: no game picture in the Discord window (largest picture %1% of it, %2% "
			"filled; a stream is at least %3% and %4%) - %5 not shown until there is. Discord is showing "
			"the call or a channel, or is minimised: click Watch Stream on a squad mate in Discord, or pop "
			"their stream out.")
			     .arg((int)std::lround(area * 100))
			     .arg((int)std::lround(density * 100))
			     .arg((int)std::lround(Picture::kMinArea * 100))
			     .arg((int)std::lround(Picture::kMinDensity * 100))
			     .arg(names)
		   : tx("Picture check: no game picture in %1's feed (largest picture %2% of it, %3% filled; a "
			"stream is at least %4% and %5%) - they are not shown until there is. Their stream ended or "
			"has not started.")
			     .arg(names)
			     .arg((int)std::lround(area * 100))
			     .arg((int)std::lround(density * 100))
			     .arg((int)std::lround(Picture::kMinArea * 100))
			     .arg((int)std::lround(Picture::kMinDensity * 100)));
	if (onScreen) {
		// somebody else whose picture comes from another capture: live first, then unknown. The
		// roster may still call this one live, so anyLiveFriend() could hand them straight back
		int alt = -1;
		for (Feed want : {Feed::Live, Feed::Unknown}) {
			for (size_t i = 0; i < cfg.friends.size() && alt < 0; ++i)
				if ((int)i != cfg.activeFriend && cfg.friends[i].source != act->source &&
				    feedState(cfg.friends[i]) == want && inSquadNow(cfg.friends[i]))
					alt = (int)i;
			if (alt >= 0)
				break;
		}
		if (alt >= 0) {
			log(tx("Picture check: %1's feed has no game picture - showing %2 instead.")
				    .arg(QString::fromStdString(act->name),
					 QString::fromStdString(cfg.friends[alt].name)));
			setActive(alt);
		} else
			applyNow(false, tx("no game picture in %1's feed").arg(QString::fromStdString(act->name)));
	}
	emit stateChanged();
}

int Engine::nearbyDistanceOf(int friendIdx) const
{
	if (friendIdx < 0 || friendIdx >= (int)cfg.friends.size())
		return -1;
	for (const auto &e : nearby_)
		if (!e.match.isEmpty() && friendIndexFor(e.match) == friendIdx)
			return e.dist;
	return -1;
}

int Engine::closestFriend(int *metres, QString *problem) const
{
	if (!nearbyFresh())
		return -1;
	int best = -1, bestD = 0, bestRank = 9;
	for (const auto &e : nearby_) {
		if (e.match.isEmpty() || e.dist < 0)
			continue;
		int i = friendIndexFor(e.match);
		if (i < 0) {
			if (problem)
				*problem = tx("%1 is nearby but is not one of your squad mates here").arg(e.match);
			continue;
		}
		if (!feedUsable(cfg.friends[i])) {
			if (problem)
				*problem = tx("%1 is nearby but their feed is not usable (source missing in OBS?)")
						   .arg(QString::fromStdString(cfg.friends[i].name));
			continue;
		}
		// a squad mate with nothing to show is never the one to show, however close: the nearest
		// one who is actually streaming wins, and a slot nobody can vouch for comes after those
		Feed state = feedState(cfg.friends[i]);
		if (state == Feed::Off) {
			if (problem)
				*problem = tx("%1 is closest at %2 m but is not streaming")
						   .arg(QString::fromStdString(cfg.friends[i].name))
						   .arg(e.dist);
			continue;
		}
		int rank = state == Feed::Live ? 0 : 1;
		if (best < 0 || rank < bestRank || (rank == bestRank && e.dist < bestD)) {
			best = i;
			bestD = e.dist;
			bestRank = rank;
		}
	}
	if (best >= 0 && metres)
		*metres = bestD;
	return best;
}

void Engine::nearbyTest()
{
	if (bridge.clients() == 0) {
		log(tx("Test read: ClipHound is not running (dock → Start ClipHound)."));
		QJsonObject o;
		o["error"] = tx("ClipHound is not running");
		emit nearbyTested(o);
		return;
	}
	QJsonObject o;
	o["type"] = "nearby_test";
	bridge.sendJson(o);
}

void Engine::askNearbyNow()
{
	if (!cfg.nearEnabled || bridge.clients() == 0)
		return;
	QJsonObject o;
	o["type"] = "nearby_now";
	bridge.sendJson(o);
	nearbyAskedAt_ = clock_::now();
}

/// Make the squad mate the game says is nearest the active one. Cheap and safe to call often.
void Engine::pickClosest(const QString &why, bool decisive)
{
	if (!cfg.nearEnabled || cfg.friends.size() < 2)
		return;
	int d = 0;
	QString problem;
	int idx = closestFriend(&d, &problem);
	if (idx < 0) {
		// nobody near you has a picture: any live squad mate beats staying on one who does not
		// on screen, this waits like any swap: one missed NEARBY read swapped twice in 300 ms (logs)
		bool calm = !applied_ ||
			    clock_::now() - lastPick_ >= std::chrono::seconds(std::clamp(cfg.nearCooldownS, 3, 30));
		if (calm && cfg.active() && feedState(*cfg.active()) != Feed::Live) {
			int alt = anyLiveFriend();
			if (alt >= 0 && alt != cfg.activeFriend && feedState(cfg.friends[alt]) == Feed::Live) {
				log((problem.isEmpty()
					     ? tx("Closest squad mate: nobody near you is streaming - showing %1, who is live.")
						       .arg(QString::fromStdString(cfg.friends[alt].name))
					     : tx("Closest squad mate: nobody near you is streaming (%1) - showing %2, who is "
						  "live.")
						       .arg(problem, QString::fromStdString(cfg.friends[alt].name))));
				setActive(alt);
				lastPick_ = clock_::now();
				return;
			}
		}
		// a reading asked for a moment ago is still on its way: not "no reading yet" (20 of 91 of those came
		// within 10 ms of asking)
		bool asking = !nearbyFresh() && clock_::now() - nearbyAskedAt_ < std::chrono::seconds(3);
		if (!asking && clock_::now() - lastNearbyWarn_ > std::chrono::seconds(60)) {
			lastNearbyWarn_ = clock_::now();
			if (!problem.isEmpty())
				log(tx("Closest squad mate: %1.").arg(problem));
			else
				log(bridge.clients() == 0
					    ? tx("Closest squad mate: ClipHound is not running, so the NEARBY list cannot be read - "
						 "keeping the squad mate you picked.")
					    : (nearbyFresh()
						       ? tx("Closest squad mate: nobody in the NEARBY list is one of your squad mates "
							    "(check their in-game names in the Squad window).")
						       : tx("Closest squad mate: no reading from the NEARBY list yet (check the NEARBY "
							    "box under Settings, Detect areas).")));
		}
		return;
	}
	if (idx == cfg.activeFriend)
		return;
	if (applied_ && !cfg.nearFollow)
		return; // showing someone already and the user asked not to change mid-swap
	// Going down (not on screen yet): every reading is decisive, the nearest one wins outright.
	// On screen: the "wait between swaps" slider is the only thing holding a swap back.
	if (applied_ && !decisive) {
		// The range rule only protects a squad mate who is still in the list. If the one on
		// screen has left it (dead, far away, not near you), any streaming squad mate in the list
		// is better than them, however far - the ones closest to you may not be streaming at all.
		int cur = nearbyDistanceOf(cfg.activeFriend);
		if (cur >= 0 && cfg.nearMaxM > 0 && d > cfg.nearMaxM)
			return; // too far to be the one coming for you: stay on who is on screen
		if (cur >= 0 && d > cur - std::max(0, cfg.nearMarginM))
			return; // not enough closer than who is on screen to be worth a swap (the margin, in metres)
		auto left = std::chrono::seconds(std::clamp(cfg.nearCooldownS, 3, 30)) - (clock_::now() - lastPick_);
		if (left.count() > 0) {
			if (clock_::now() - lastNearbyWarn_ > std::chrono::seconds(5)) {
				lastNearbyWarn_ = clock_::now();
				log(tx("Closest is %1 at %2 m; keeping %3 for another %4 s (wait between swaps).")
					    .arg(QString::fromStdString(cfg.friends[idx].name))
					    .arg(d)
					    .arg(QString::fromStdString(cfg.active() ? cfg.active()->name : ""))
					    .arg((int)std::chrono::duration_cast<std::chrono::seconds>(left).count() +
						 1));
			}
			return;
		}
	}
	QString nm = QString::fromStdString(cfg.friends[idx].name);
	QString ign = QString::fromStdString(cfg.friends[idx].nearName());
	if (ign.compare(nm, Qt::CaseInsensitive) != 0)
		nm += " " + tx("(in game %1)").arg(ign);
	switchTo(idx, tx("%1 is closest at %2 m - %3 - read [%4]").arg(nm).arg(d).arg(txv(why), nearbyText()));
}

/// setActive() without the "you chose this" wording: used by the closest-squad-mate picker.
void Engine::switchTo(int idx, const QString &why)
{
	if (idx < 0 || idx >= (int)cfg.friends.size() || idx == cfg.activeFriend)
		return;
	bool wasOn = applied_;
	if (wasOn)
		applyNow(false, TX_NOOP("switching squad mate"));
	cfg.activeFriend = idx;
	cfg.save();
	lastPick_ = clock_::now();
	if (wasOn)
		applyNow(true, why);
	else if (cfg.keepWarm)
		sw.armWarm(cfg);
	log(tx("Squad mate: %1.").arg(txv(why)));
	addEvent(tx("Closest: %1").arg(QString::fromStdString(cfg.friends[idx].name)));
	emit stateChanged();
}

QString Engine::webLiveKey(const Friend &f)
{
	QString ch = QString::fromStdString(f.channel).trimmed();
	if (ch.startsWith('@') && f.kind != FriendKind::YouTube)
		ch.remove(0, 1);
	if (f.kind != FriendKind::YouTube)
		ch = ch.toLower();
	return QString::number((int)f.kind) + ":" + ch;
}

/// Ask each streaming service whether a squad mate's channel is live. Members of the Kennel.gg
/// Discord only, like the rest of the squad automation; everyone else sees no change.
/// A Twitch or Kick category that is WARDOGS ("WARDOGS", "War Dogs", "WARDOGS: Early Access"...).
bool Engine::isWardogs(const QString &game)
{
	QString g = game.toLower();
	g.remove(QRegularExpression("[^a-z]"));
	return g.contains("wardogs");
}

void Engine::webLiveTick()
{
	if (stopping_ || rosterAccess() != Access::Ok)
		return;
	const QString ua = QString("KennelggWardogsOBSTool/%1").arg(PLUGIN_VERSION);
	const QString browserUa = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
				  "Chrome/128.0 Safari/537.36";
	QSet<QString> seen;
	for (const Friend &f : cfg.friends) {
		if (f.kind != FriendKind::Twitch && f.kind != FriendKind::Kick && f.kind != FriendKind::YouTube)
			continue;
		QString key = webLiveKey(f);
		QString ch = key.mid(key.indexOf(':') + 1);
		if (ch.isEmpty() || seen.contains(key) || webLiveBusy_.contains(key))
			continue;
		seen.insert(key);
		webLiveBusy_.insert(key);
		auto settle = [this, key, ch](Feed state, QString note, const QString &game = QString()) {
			webLiveBusy_.remove(key);
			// live on another game (or Just Chatting between matches): not a WARDOGS POV to swap to. They are
			// offered again by themselves the moment their category is WARDOGS again (29 Sep 2026 request)
			if (state == Feed::Live && !game.isEmpty() && !isWardogs(game)) {
				webOtherGame_[key] = game;
				state = Feed::Off;
				note = tx("playing %1").arg(game);
			} else
				webOtherGame_.remove(key);
			Feed was = webLive_.value(key, Feed::Unknown);
			if (state == Feed::Unknown)
				webLive_.remove(key);
			else
				webLive_[key] = state;
			if (was != state) {
				log(QString("%1: %2%3")
					    .arg(ch)
					    .arg(state == Feed::Live  ? tx("live")
						 : state == Feed::Off ? tx("offline")
								      : tx("unknown"))
					    .arg(note.isEmpty() ? "" : " (" + note + ")"));
				emit stateChanged();
			}
		};
		if (f.kind == FriendKind::Twitch) {
			// the same query Twitch's own site sends; no account needed
			QJsonObject q;
			q["query"] = "query($l:String!){user(login:$l){stream{id game{name}}}}";
			QJsonObject v;
			v["l"] = ch;
			q["variables"] = v;
			Http::requestAsync(
				this, "POST", "https://gql.twitch.tv/gql",
				QJsonDocument(q).toJson(QJsonDocument::Compact),
				"Client-Id: kimne78kx3ncx6brgo4mv6wki5h1ko\r\nContent-Type: application/json\r\n", 8000,
				ua, [settle](Http::Result r) {
					if (!r.ok) {
						settle(Feed::Unknown, r.error);
						return;
					}
					QJsonObject d = QJsonDocument::fromJson(r.body).object()["data"].toObject();
					QJsonValue user = d["user"];
					if (!user.isObject()) {
						settle(Feed::Unknown, tx("no such channel"));
						return;
					}
					QJsonObject st = user.toObject()["stream"].toObject();
					QString game = st["game"].toObject()["name"].toString();
					settle(user.toObject()["stream"].isObject() ? Feed::Live : Feed::Off,
					       game.isEmpty() ? "" : game, game);
				});
		} else if (f.kind == FriendKind::Kick) {
			Http::requestAsync(this, "GET",
					   "https://kick.com/api/v2/channels/" +
						   QString::fromUtf8(QUrl::toPercentEncoding(ch)),
					   QByteArray(), "", 8000, browserUa, [settle](Http::Result r) {
						   if (!r.ok) {
							   settle(Feed::Unknown, r.error);
							   return;
						   }
						   QJsonObject o = QJsonDocument::fromJson(r.body).object();
						   if (o.isEmpty()) {
							   settle(Feed::Unknown, tx("no answer"));
							   return;
						   }
						   QString game = o["livestream"]
									  .toObject()["categories"]
									  .toArray()
									  .first()
									  .toObject()["name"]
									  .toString();
						   settle(o["livestream"].isObject() ? Feed::Live : Feed::Off,
							  game.isEmpty() ? "" : game, game);
					   });
		} else {
			// a channel id gets its /live page, a video id its watch page; a live one carries videoDetails with isLive
			QString url = (ch.startsWith("UC") && ch.size() >= 20)
					      ? "https://www.youtube.com/channel/" + ch + "/live"
					      : "https://www.youtube.com/watch?v=" + ch;
			Http::requestAsync(
				this, "GET", url, QByteArray(), "Cookie: SOCS=CAI\r\nAccept-Language: en\r\n", 12000,
				browserUa, [settle](Http::Result r) {
					if (!r.ok) {
						settle(Feed::Unknown, r.error);
						return;
					}
					// a real YouTube page carries ytInitialData; the consent page does not. A channel
					// that is live serves its stream's watch page here, whose videoDetails say
					// isLive; one that is not serves its home page, with no videoDetails at all
					if (!r.body.contains("ytInitialData")) {
						settle(Feed::Unknown, tx("page not readable"));
						return;
					}
					bool live = r.body.contains("\"videoDetails\"") &&
						    r.body.contains("\"isLive\":true");
					settle(live ? Feed::Live : Feed::Off, "");
				});
		}
	}
	// slots that are gone take their answers with them
	for (auto it = webLive_.begin(); it != webLive_.end();) {
		if (seen.contains(it.key()))
			++it;
		else
			it = webLive_.erase(it);
	}
}

/// What is on screen, for ClipHound: "up" (your POV), "downed", "inventory" (magazine packing) or "shown"
/// (put up by hand). Only "downed" (and "reviving") makes it read NEARBY and tag clips "downed": an inventory
/// swap sent "downed" and it read NEARBY through the inventory screen (logs: 26 of 35 "no reading" lines)
QString Engine::povStateName() const
{
	if (!applied_)
		return "up";
	if (inventoryShow_)
		return "inventory";
	if (manualShow_ && !detected_)
		return "shown";
	return "downed";
}

void Engine::sendPov(const QString &state)
{
	if (bridge.clients() == 0)
		return;
	QJsonObject o;
	o["type"] = "pov";
	o["state"] = state;
	const Friend *f = cfg.active();
	o["friend"] = f ? QString::fromStdString(f->name) : "";
	bridge.sendJson(o);
}

/// Native-resolution crop of the game source for the companion app (the kill feed is ~9 px text at 1080p).
void Engine::frameTick()
{
	if (frameBusy_ || stopping_ || cfg.gameSource.empty() || bridge.clients() == 0)
		return;
	// which streams are due now; none = the legacy single region
	qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
	std::vector<Bridge::Stream> due;
	for (auto &s : bridge.streams())
		if (nowMs >= s.nextMs) {
			s.nextMs = nowMs + (qint64)(1000.0 / s.fps);
			due.push_back(s);
		}
	if (!bridge.streams().empty() && due.empty())
		return;
	if (due.empty()) {
		Bridge::Stream s;
		s.id = -1;
		s.roi = bridge.wantedRoi();
		s.width = bridge.wantedWidth();
		due.push_back(s);
	}
	frameBusy_ = true;
	std::string name = cfg.gameSource;
	workers_++;
	std::thread([this, due, name]() {
		WorkerGuard guard(workers_);
		struct Out {
			int id;
			QByteArray jpeg;
			int w = 0, h = 0;
		};
		std::vector<Out> outs;
		obs_source_t *src = obs_get_source_by_name(name.c_str());
		if (src) {
			for (const auto &s : due) {
				// only that region is rendered and read back: a kill-feed crop is a few hundred
				// kilobytes, where the whole frame used to be fourteen megabytes ten times a second
				std::vector<uint8_t> bgra;
				int w, h, ls;
				if (!capRoi_.grabRegion(src, s.roi.x(), s.roi.y(), s.roi.width(), s.roi.height(),
							s.width, bgra, w, h, ls))
					continue;
				QImage img(bgra.data(), w, h, ls, QImage::Format_ARGB32);
				Out o;
				o.id = s.id;
				o.w = w;
				o.h = h;
				QBuffer buf(&o.jpeg);
				buf.open(QIODevice::WriteOnly);
				img.save(&buf, "JPG", 88);
				outs.push_back(std::move(o));
			}
			obs_source_release(src);
		}
		qint64 ts = QDateTime::currentMSecsSinceEpoch();
		QMetaObject::invokeMethod(
			this,
			[this, outs, ts]() {
				frameBusy_ = false;
				for (const auto &o : outs) {
					if (o.jpeg.isEmpty())
						continue;
					if (o.id < 0)
						bridge.sendFrame(o.jpeg, o.w, o.h, ts);
					else
						bridge.sendFrame2(o.id, o.jpeg, o.w, o.h, ts);
				}
			},
			Qt::QueuedConnection);
	}).detach();
}

void Engine::clipNow(const QString &title, const QStringList &tags, const QString &source)
{
	QString err = clips.request(title, tags, source);
	if (!err.isEmpty())
		log(tx("Clip: %1").arg(err));
	emit stateChanged();
}

// ----- the poll -----

void Engine::tick()
{
	if (busy_ || stopping_ || cfg.gameSource.empty())
		return;
	// The full-frame search is the expensive path and it runs exactly while you are alive (nothing to lock
	// on to). Doing it on every third poll keeps it near 1-2 % of a core; once locked, every poll is cheap.
	tickN_++;
	bool quick = !lastGame_.locked && !revivingRecent() && (tickN_ % 6) != 0;
	// the friend-feed "REVIVING" search is the expensive one: full search every 5th poll, cheap remembered-spot check otherwise,
	// so the damage-log poll (what switches you back) keeps its 100 ms cadence while the friend is on screen
	bool reviveFull = (tickN_ % 5) == 0;
	busy_ = true;
	bool wantRevive = applied_ && cfg.watchRevive && cfg.active();
	std::string gameName = cfg.gameSource, friendName = wantRevive ? cfg.sourceFor(*cfg.active()) : "";
	bool preview = previewWanted_;
	// clutch: one squad mate's feed read for the downed screen every half second, in turn (they stay loaded
	// and hidden while nobody shows them, so a downed one is known before they would be put on screen)
	std::string mateSrc;
	QString mateName;
	if (cfg.clutch && !cfg.friends.empty() && tickN_ % 5 == 0) {
		int n = (int)cfg.friends.size();
		for (int k = 0; k < n && mateSrc.empty(); ++k) {
			const Friend &f = cfg.friends[(mateTurn_ + k) % n];
			if (!feedUsable(f) || feedState(f) == Feed::Off)
				continue;
			mateSrc = cfg.sourceFor(f);
			mateName = QString::fromStdString(f.name);
			mateTurn_ = (mateTurn_ + k + 1) % n;
		}
	}

	std::shared_ptr<AltSet> alts = altDets_;
	workers_++;
	std::thread([this, gameName, friendName, wantRevive, preview, quick, reviveFull, alts, mateSrc, mateName]() {
		WorkerGuard guard(workers_);
		Result r;
		obs_source_t *src = obs_get_source_by_name(gameName.c_str());
		if (src) {
			std::vector<uint8_t> bgra;
			int w, h, ls;
			if (capGame_.grab(src, Detector::FrameWidth, bgra, w, h, ls)) {
				Frame f = Detector::fromBGRA(bgra.data(), w, h, ls);
				// taken after the grab, never with OBS's graphics lock held
				std::lock_guard<std::recursive_mutex> lk(detMx_);
				r.game = detGame_.compare(f, quick);
				// game language on auto: the other wordings get the same look; the best of them
				// is kept apart and onResult decides whether it is really the one on screen
				for (size_t i = 0; i < alts->size(); i++) {
					Match a = (*alts)[i]->compare(f, quick);
					if (a.score > r.alt.score) {
						r.alt = a;
						r.altLang = (int)i;
						r.alts = alts;
					}
				}
				r.ok = true;
				if (preview) {
					r.bgra = std::move(bgra);
					r.w = w;
					r.h = h;
					r.ls = ls;
				}
			}
			obs_source_release(src);
		}
		if (wantRevive && (reviveFull || detRevive_.remembers())) {
			obs_source_t *fs = obs_get_source_by_name(friendName.c_str());
			if (fs) {
				std::vector<uint8_t> bgra;
				int w, h, ls;
				if (capFriend_.grab(fs, Detector::FrameWidth, bgra, w, h, ls)) {
					Frame f = Detector::fromBGRA(bgra.data(), w, h, ls);
					std::lock_guard<std::recursive_mutex> lk(detMx_);
					r.revive = detRevive_.compare(f, !reviveFull);
					if (r.revive.score >= detRevive_.threshold) {
						// the progress ring sits at a fixed offset below the word (measured on a real frame)
						float tw = r.revive.w * w;
						float cx = r.revive.x * w + 0.47f * tw,
						      cy = r.revive.y * h + 1.31f * tw, rad = 0.39f * tw;
						int lit = 0, n = 72;
						for (int i = 0; i < n; i++) {
							float a = (float)i / n * 6.2831853f;
							bool on = false;
							for (int dr = -1; dr <= 1 && !on; dr++) {
								int px = (int)std::lround(cx +
											  (rad + dr) * std::cos(a)),
								    py = (int)std::lround(cy +
											  (rad + dr) * std::sin(a));
								if (px >= 0 && py >= 0 && px < w && py < h &&
								    f.gray[(size_t)py * w + px] > 170)
									on = true;
							}
							lit += on;
						}
						r.progress = (double)lit / n;
					}
				}
				obs_source_release(fs);
			}
		}
		if (!mateSrc.empty()) {
			r.mateName = mateName;
			if (obs_source_t *ms = obs_get_source_by_name(mateSrc.c_str())) {
				std::vector<uint8_t> bgra;
				int w, h, ls;
				if (capMate_.grab(ms, Detector::FrameWidth, bgra, w, h, ls)) {
					Frame f = Detector::fromBGRA(bgra.data(), w, h, ls);
					std::lock_guard<std::recursive_mutex> lk(detMx_);
					r.mate = detMate_.compare(f, false);
					r.mateOk = true;
				}
				obs_source_release(ms);
			}
		}
		QMetaObject::invokeMethod(
			this, [this, r = std::move(r)]() mutable { onResult(std::move(r)); }, Qt::QueuedConnection);
	}).detach();
}

void Engine::onResult(Result r)
{
	if (!r.mateName.isEmpty() && r.mateOk) {
		MateSeen &m = mates_[r.mateName];
		bool down = r.mate.score >= detMate_.threshold;
		m.at = QDateTime::currentMSecsSinceEpoch();
		m.downRun = down ? m.downRun + 1 : 0;
		m.upRun = down ? 0 : m.upRun + 1;
		// two reads in a row either way (a second apart at most) before it counts
		bool was = m.downed;
		if (m.downRun >= 2)
			m.downed = true;
		else if (m.upRun >= 2)
			m.downed = false;
		if (m.downed != was) {
			log(m.downed ? tx("Clutch: %1 is down.").arg(r.mateName)
				     : tx("Clutch: %1 is up.").arg(r.mateName));
			clutchStep();
		}
	}
	busy_ = false;
	if (stopping_ || paused_) // a result that lands while OBS takes the scenes down switches nothing in them
		return;
	if (!r.ok) {
		std::string e = tx("Watch: cannot render game source '%1'.")
					.arg(QString::fromStdString(cfg.gameSource))
					.toStdString();
		if (lastWatchError_ != e) {
			lastWatchError_ = e;
			log(QString::fromStdString(e));
		}
		return;
	}
	lastWatchError_.clear();
	gameSeen_ = true;
	if (r.altLang >= 0 && r.alts == altDets_ && r.altLang < (int)altDets_->size() && r.alt.score > r.game.score &&
	    r.alt.score >= std::max(cfg.threshold, kLangSure)) {
		// another language's wording, clearly on screen: it counts as the downed screen, and once
		// it has held for a moment it becomes the wording we search, remembered for next time.
		// One frame used to be enough, and the French wording scores 0.80-0.88 on ordinary play
		// (a lobby, fog, a browser window): English games were locked to French for good and
		// "downed" in every fight (LOG-9428, 28 Sep 2026)
		r.game = r.alt;
		auto now = std::chrono::steady_clock::now();
		if (langRun_ != r.altLang) {
			langRun_ = r.altLang;
			langSince_ = now;
		}
		if (now - langSince_ >= std::chrono::milliseconds(kLangHoldMs)) {
			std::string lang = altLangs_[(size_t)r.altLang];
			{
				std::lock_guard<std::recursive_mutex> lk(detMx_);
				detGame_ = std::move(*(*altDets_)[(size_t)r.altLang]);
			}
			altDets_ = std::make_shared<AltSet>();
			altLangs_.clear();
			langRun_ = -1;
			cfg.gameLangFound = lang;
			cfg.save();
			pushAppConfig(); // ClipHound reads the HUD's weapon names in this language
			log(tx("Game language: %1 (the damage log matched the %1 wording).").arg(langName(lang)));
			emit stateChanged();
		}
	} else
		langRun_ = -1;
	if (r.game.locked && !lastGame_.locked && detGame_.remembers()) {
		cfg.memScale = detGame_.memScale();
		cfg.memX = detGame_.memX();
		cfg.memY = detGame_.memY();
		cfg.save();
	}
	lastGame_ = r.game;
	lastRevive_ = r.revive;
	// "REVIVING" is on screen when you revive someone too: it only means you are being revived while you are
	// down (26 Sep log: 93 of 114 "reviving you" came with nobody down, and left ClipHound tagging every clip
	// "downed" and reading NEARBY non-stop)
	if (r.revive.score >= cfg.reviveThreshold && detected_) {
		bool was = revivingRecent();
		lastReviveSeen_ = clock_::now();
		reviveProgress_ = r.progress;
		if (!was) {
			log(tx("A squad mate is reviving you - switching back the instant the damage log goes."));
			sendPov("reviving");
		}
	} else if (!revivingRecent())
		reviveProgress_ = -1;
	timer_.setInterval(revivingRecent() ? 100 : std::max(100, cfg.pollMs));
	if (!r.bgra.empty()) {
		QImage img(r.bgra.data(), r.w, r.h, r.ls, QImage::Format_ARGB32);
		std::lock_guard<std::mutex> lk(frameMx_);
		lastFrame_ = img.copy();
	}
	detect(r.game);
	emit frameUpdated();
}

void Engine::detect(const Match &m)
{
	if (!cfg.enabled || !cfg.autoDetect || m.score < 0)
		return;
	bool match = m.score >= cfg.threshold;
	// a near miss is a downed screen the template does not quite fit (the game in another
	// language, an odd resolution): say so once a minute, so a log tells which
	if (!detected_) {
		if (m.score > nearBest_)
			nearBest_ = m.score;
		auto now = clock_::now();
		if (now - nearSince_ >= std::chrono::seconds(60)) {
			if (nearBest_ >= 0.60 && nearBest_ < cfg.threshold) {
				log(tx("Downed search: best match %1 in the last minute, below the threshold of %2. "
				       "If you were downed in that time, the damage-log header on your screen does "
				       "not match the built-in one (game language or resolution): cut your own on "
				       "Settings, Advanced.")
					    .arg(nearBest_, 0, 'f', 3)
					    .arg(cfg.threshold, 0, 'f', 2));
				// three such minutes with every wording in play and none ever matching: the
				// game is most likely in a language we do not have. Say so, once
				lastNearBest_ = nearBest_;
				if (nearBest_ >= 0.62 && ++nearMinutes_ >= 3 && !cfg.langAskShown &&
				    (cfg.gameLang == "auto" || !hasDownedTemplate(cfg.gameLang)) &&
				    cfg.gameLangFound.empty() && cfg.customTemplateWidthFrac <= 0) {
					cfg.langAskShown = true;
					cfg.save();
					log(tx("The damage log never matched the English, Spanish or French wording: is the game "
					       "in another language? Save a frame while downed and open a ticket in the Kennel.gg "
					       "Discord."));
					langBanner_ = true;
					emit languageUnknown();
					emit stateChanged();
				}
			}
			nearBest_ = 0;
			nearSince_ = now;
		}
	}
	if (detected_) {
		if (m.score > peakScore_)
			peakScore_ = m.score;
		// Hold on. A bright sky behind the translucent panel, smoke or a muzzle flash washes the
		// header out for a poll or two; the log itself has not moved. So while you are down the
		// score only has to stay above the hold level, and the match has to still be in the place
		// the log was found - noise elsewhere in the frame cannot keep you down.
		double hold = std::max(0.50, cfg.threshold - std::max(0.0, cfg.holdDrop));
		bool sameSpot = std::fabs(m.x - downX_) < 0.03f && std::fabs(m.y - downY_) < 0.03f;
		if (m.score >= cfg.threshold && sameSpot)
			fullSince_ = clock_::now();
		// ...but only as a bridge: a washout lasts a moment. If the log has not scored a clean
		// match for kHoldMs the hold lapses, so nothing on screen can pin you down for good.
		bool bridging = clock_::now() - fullSince_ < std::chrono::milliseconds(kHoldMs);
		match = sameSpot && m.score >= (bridging ? hold : cfg.threshold);
		// the fading-away rule stays for the revive case, where switching back a poll sooner shows
		if (match && revivingRecent() && m.score < peakScore_ - cfg.releaseDrop)
			match = false;
	}
	if (match) {
		downRun_++;
		upRun_ = 0;
	} else {
		upRun_++;
		downRun_ = 0;
	}
	bool fast = revivingRecent();
	int needUp = fast ? 1 : cfg.upFrames;
	int minDown = fast ? 0 : cfg.minDownMs;
	if (!detected_ && downRun_ >= cfg.downFrames) {
		detected_ = true;
		// one down, however often the detection drops and comes back during it (a revive ring, the killcam
		// moving behind the log): uploaded sessions had 411 downs against 4 deaths, one every 2.7 s in another
		downAt_ = QDateTime::currentDateTime();
		bool newDown = downEndedAt_.time_since_epoch().count() == 0 ||
			       clock_::now() - downEndedAt_ > std::chrono::seconds(20);
		if (newDown) {
			session_.downs++;
			if (cfg.helpBuild()) // the damage log, as the downed reader sees it
				QTimer::singleShot(700, this, [this]() {
					if (detected_)
						hudSample("downed");
				});
		}
		peakScore_ = m.score;
		downX_ = m.x;
		downY_ = m.y;
		fullSince_ = clock_::now();
		detGame_.holdThreshold = std::max(0.50, cfg.threshold - std::max(0.0, cfg.holdDrop));
		upDelay_.stop();
		// the clip is of you going down: it does not wait for (or need) a squad mate to swap to. It used to be
		// made inside the swap, so with no squad mate added, or a revive inside the delay, there was none
		// (27 Sep 2026 report)
		// a flicker inside one down is not a second clip (it would also eat the clip gap a kill clip needs). The
		// reel that starts with the down needs that clip too
		if (newDown && (cfg.clipOnDowned || (cfg.reelFromDown && !applied_ && reelWanted())))
			clips.request("downed", {"downed"}, "pov");
		askNearbyNow(); // fresh NEARBY reading while the delay runs
		pickClosest(TX_NOOP("downed"), true);
		if (cfg.clutch && !applied_ && cfg.active() && mateDowned(*cfg.active())) {
			int up = clutchPick();
			if (up >= 0) {
				cfg.activeFriend = up;
				cfg.save();
				emit stateChanged();
			}
		}
		// whoever is about to be shown must have a picture: a squad mate the roster has in voice
		// but not streaming shows nothing, so a live one takes their place, or you stay on your own
		if (!applied_ && cfg.active() && feedState(*cfg.active()) == Feed::Off) {
			int alt = anyLiveFriend();
			if (alt < 0 && !cfg.downedReplays && !cfg.downedReplaysAlways) {
				log(tx("Downed, but none of the squad is streaming a game picture right now - staying on your own "
				       "POV."));
				return;
			}
			// with the replay reel on, the delay runs as usual and your replays play instead
			if (alt >= 0 && alt != cfg.activeFriend) {
				log(tx("Downed: %1 is not streaming, showing %2 instead.")
					    .arg(QString::fromStdString(cfg.active()->name),
						 QString::fromStdString(cfg.friends[alt].name)));
				cfg.activeFriend = alt;
				cfg.save();
				emit stateChanged();
			}
		}
		if (!applied_) {
			if (cfg.downDelayMs <= 0) {
				if (reelWanted())
					startReel();
				else
					applyNow(true, QString("downed screen detected (%1)").arg(m.score, 0, 'f', 3));
			} else {
				// cancelled if the log goes away first (a blip, or a quick revive); the stinger's
				// run-up comes out of the wait, so the squad mate is on screen when they were before
				downDelay_.start(std::max(0, cfg.downDelayMs - (cfg.povStinger ? kPovCoverMs : 0)));
				log(reelWanted()
					    ? tx("Downed - playing your replays in %1 ms unless you are revived first.")
						      .arg(cfg.downDelayMs)
					    : tx("Downed - showing the squad mate in %1 ms unless you are revived first.")
						      .arg(cfg.downDelayMs));
			}
		}
	} else if (detected_ && upRun_ >= needUp && clock_::now() - downSince_ >= std::chrono::milliseconds(minDown)) {
		detected_ = false;
		downEndedAt_ = clock_::now();
		detGame_.holdThreshold = 0;
		bool delayRunning = downDelay_.isActive();
		downDelay_.stop();
		if (reelOn_) {
			reelOn_ = false; // revived: the reel ends with the usual way back to live
			if (replaying())
				endReplay(TX_NOOP("revived"));
		}
		if (povPending_ && povTarget_ && !applied_)
			povTarget_ = false; // revived while SWITCHING POV was on its way: it plays, nothing swaps
		clearNearby();
		if (applied_) {
			// a POV stays up for povMinS at least, so a flicker of the damage log does not swap
			// twice in a second (and play the stinger twice)
			qint64 shownMs =
				std::chrono::duration_cast<std::chrono::milliseconds>(clock_::now() - downSince_)
					.count();
			int hold = (int)std::max<qint64>(0, (qint64)cfg.povMinS * 1000 - shownMs);
			int wait = std::max(fast ? 0 : cfg.upDelayMs, hold);
			if (wait <= 0)
				applyNow(false, fast ? TX_NOOP("revived (squad mate's revive seen, damage log gone)")
						     : tx("damage log gone (%1)").arg(m.score, 0, 'f', 3));
			else
				upDelay_.start(wait);
		} else {
			sendPov("up");    // ClipHound heard "reviving" or "downed" hints for this down: it is over
			if (delayRunning) // not after a manual back to your POV during a long down (26 Sep log)
				log(tx("Damage log gone before the delay ended - no switch."));
		}
	}
}

// ----- actions -----

void Engine::applyNow(bool on, const QString &why)
{
	// who asked: a view put up by hand (Stream Deck, voice, the dock) is not taken down by the picture check
	if (!on)
		manualShow_ = false;
	else if (why.startsWith("controller") || why.startsWith("voice") || why == "button" ||
		 why.startsWith("setup test"))
		manualShow_ = true;
	else if (why.startsWith("downed") || why.startsWith("inventory") || why.startsWith("companion"))
		manualShow_ = false;
	if (on)
		inventoryShow_ = why.startsWith("inventory");
	// cut straight away: no stinger wanted, or not a swap anyone should watch
	bool instant = !cfg.povStinger || stopping_ || !cfg.active() || why == "paused" ||
		       why.startsWith("squad mate removed") || why.startsWith("setup test");
	if (instant && !povPending_) {
		applySwitch(on, why);
		return;
	}
	if (povPending_) { // the stinger is on its way: the swap under its cover is the last one asked for
		povTarget_ = on;
		povWhy_ = why;
		return;
	}
	if (on == applied_ && (!on || povShown_ == cfg.activeFriend)) {
		applySwitch(on, why); // nothing on screen would change
		return;
	}
	povPending_ = true;
	povTarget_ = on;
	povWhy_ = why;
	// a moment later, so "off then on" (switching squad mate) is one stinger naming who comes next
	QTimer::singleShot(0, this, [this]() {
		if (stopping_) {
			povPending_ = false;
			return;
		}
		sw.ensureStinger(cfg, true);
		QJsonObject o;
		o["type"] = "stinger";
		o["dir"] = "pov";
		const Friend *a = cfg.active();
		o["sub"] = povTarget_ && a ? QString::fromStdString(a->name) : tx("your POV"); // on stream
		bridge.sendJson(o);
		QTimer::singleShot(kPovCoverMs, this, [this]() {
			povPending_ = false;
			if (stopping_)
				return;
			bool on = povTarget_;
			if (on && applied_ && povShown_ != cfg.activeFriend)
				applySwitch(false, TX_NOOP("switching squad mate"));
			applySwitch(on, povWhy_);
			// the swap raised the squad mate's feed, their look and the camera: the stinger goes
			// back over all of them for the rest of its wipe
			sw.ensureStinger(cfg, true);
		});
	});
}

void Engine::applySwitch(bool on, const QString &why)
{
	if (applying_)
		return;
	// Going back to your own POV never needs a squad mate: the one on screen may have just been removed
	// (their Discord share ended) while the stinger covered the switch. Refusing left the stream on a
	// feed that no longer existed, and every press of Me said "Add a squad mate first" (0.26.6 report).
	bool noMate = !cfg.active();
	if (noMate && on) {
		log(tx("Add a squad mate first."));
		return;
	}
	if (noMate && !applied_ && !lookPreview_) {
		// already on your own POV, but a squad mate's page or name plate can still be up from an earlier
		// session or a squad mate removed since (29 Sep 2026 report: nobody in the squad, the old POV on
		// stream and Me did nothing)
		clearSquadLeftovers();
		return;
	}
	applying_ = true;
	std::vector<std::string> errors;
	if (noMate)
		sw.backToOwn(cfg);
	else
		errors = sw.apply(cfg, on);
	if (!noMate) {
		std::string ev = sw.applyVertical(cfg, on); // the same swap on the portrait canvas, if set
		if (!ev.empty()) {
			errors.push_back(tx("vertical: %1").arg(QString::fromStdString(ev)).toStdString());
			log(tx("Vertical: %1").arg(QString::fromStdString(ev)));
		}
	}
	if (!on) {
		// your own POV takes priority when you are up: every squad mate and the look overlay go, in every scene
		int n = sw.hideAllFriends(cfg);
		if (n > 0)
			log(n == 1 ? tx("Squad mate feeds hidden (1 item).")
				   : tx("Squad mate feeds hidden (%1 items).").arg(n));
	}
	applied_ = on;
	povShown_ = on ? cfg.activeFriend : -1;
	lookPreview_ = false;
	if (on)
		downSince_ = clock_::now();
	else {
		detRevive_.unlock();
		if (!detected_)
			clearNearby();
	}
	if (on) {
		// their window has had a moment to draw by now; crop Discord's chrome off what we show
		QTimer::singleShot(500, this, [this]() {
			const Friend *a = cfg.active();
			if (!stopping_ && applied_ && a && a->kind == FriendKind::Discord && a->trim)
				sw.trimToContent(cfg, *a);
		});
	}
	if (dualOn_) {
		// the small window makes way for the full-screen swap, and returns. Not re-armed on the way
		// down: the swap has just shown that capture, and warm would make it transparent again.
		dualTimer_.stop();
		dualDir_ = 0;
		if (!on)
			sw.dualK = 0.02; // back up: it grows in again as the screen uncovers
		sw.applyDual(cfg, !on, false);
		sw.dualK = 1.0;
		if (!on)
			dualAnimate(1, cfg.povStinger ? 500 : 0); // as SWITCHING POV uncovers
	}
	sendPov(povStateName());
	// the events list names why: downed, the inventory, a voice or Stream Deck command, a button
	{
		QString w = why.toLower();
		QString label = w.startsWith("inventory")    ? tx("INVENTORY")
				: w.startsWith("downed")     ? tx("DOWNED")
				: w.startsWith("voice")      ? tx("VOICE")
				: w.startsWith("controller") ? tx("STREAM DECK")
				: w.startsWith("closest")    ? tx("CLOSEST")
							     : tx("SHOWING");
		events_ << QDateTime::currentDateTime().toString("HH:mm:ss") + "  " +
				   (on ? tx("%1 - showing %2").arg(label, QString::fromStdString(cfg.active()->name))
				       : tx("back up - %1").arg(txv(why)));
	}
	while (events_.size() > 30)
		events_.removeFirst();
	QString msg = on ? tx("Showing %1's POV - %2.").arg(QString::fromStdString(cfg.active()->name), txv(why))
			 : tx("Back to your POV - %1.").arg(txv(why));
	if (!errors.empty()) {
		QString list;
		for (size_t i = 0; i < errors.size(); i++)
			list += QString::fromStdString(errors[i]) + (i + 1 < errors.size() ? "; " : "");
		msg += "  " + tx("Problems: %1").arg(list);
	}
	log(msg);
	applying_ = false;
	emit stateChanged();
}

/// Where the action is in a clip, in ms from the start of the file: from the sidecar offsets
/// when the plugin wrote them, else the usual "about 7 s before the end".
static void replayWindow(const Clips::Entry &e, qint64 durMs, int preS, int postS, qint64 *startMs, qint64 *endMs)
{
	qint64 first = e.firstS >= 0 ? (qint64)(e.firstS * 1000) : (e.momentS >= 0 ? (qint64)(e.momentS * 1000) : 7000);
	qint64 last = e.momentS >= 0 ? (qint64)(e.momentS * 1000) : first;
	*startMs = std::max<qint64>(0, durMs - first - (qint64)preS * 1000);
	*endMs = std::min<qint64>(durMs, durMs - last + (qint64)postS * 1000);
	if (*endMs <= *startMs + 1000)
		*endMs = std::min<qint64>(durMs, *startMs + 8000);
}

/// Save a clip now and play it back as the instant replay the moment it has landed.
void Engine::clipAndReplay(const QString &why)
{
	replayAfterClip_ = true;
	clipNow("manual", {"manual", why}, why);
}

void Engine::playReplay(const QString &why)
{
	const Clips::Entry *last = nullptr;
	for (auto it = clips.history().rbegin(); it != clips.history().rend(); ++it)
		if (!it->path.isEmpty() && QFileInfo::exists(it->path)) {
			last = &*it;
			break;
		}
	if (!last) {
		log(tx("Instant replay: no highlight saved yet this session."));
		return;
	}
	reelOn_ = false; // asked for by hand: the downed reel (if any) gives way
	playReplayEntry(*last, why);
}

void Engine::playReplayEntry(const Clips::Entry &entry, const QString &why)
{
	const Clips::Entry *last = &entry;
	if (replaying())
		stopReplay(TX_NOOP("replaced"));
	std::string e = sw.playMedia(cfg, last->path.toStdString(), cfg.replayScale,
				     cfg.replaySound ? cfg.replayVolume : 0, true,
				     cfg.verticalOn() ? last->pathV.toStdString() : std::string());
	if (!e.empty()) {
		log(tx("Instant replay: %1").arg(QString::fromStdString(e)));
		return;
	}
	replayWhat_ = last->title.isEmpty() ? QFileInfo(last->path).fileName() : last->title;
	// the length is known only once the file has loaded: the tick seeks and arms the stop
	pendingReplay_ = *last;
	replayLengthMs_ = 1; // "playing, not yet sought"
	replaySought_ = false;
	replayShown_ = false;
	replayOutSent_ = false;
	if (cfg.replayStinger)
		sw.ensureStinger(cfg, true); // loaded long before; this puts it back on top of everything
	replayClock_.start();
	replayTimer_.start(150);
	addEvent(QDateTime::currentDateTime().toString("HH:mm:ss") + "  " + tx("REPLAY %1").arg(replayWhat_));
	log(tx("Instant replay: %1 - %2.").arg(replayWhat_, txv(why)));
	emit stateChanged();
}

QString Engine::highlightsDir() const
{
	QString folder = QString::fromStdString(cfg.highlightsFolder);
	if (!folder.isEmpty())
		return folder;
	QString base = QString::fromStdString(cfg.clipFolder);
	if (base.isEmpty())
		base = QString::fromStdString(cfg.backtrackFolder);
	if (base.isEmpty()) {
		QStringList bt = Clips::discoverBacktrackFolders();
		if (!bt.isEmpty())
			base = bt.first();
	}
	if (base.isEmpty() && !clips.history().empty())
		base = QFileInfo(clips.history().back().path).absolutePath();
	return base.isEmpty() ? QString() : base + "/highlights";
}

void Engine::onStreaming(bool live)
{
	QJsonObject o;
	o["type"] = "stream";
	o["state"] = live ? "started" : "stopped";
	bridge.sendJson(o);
	if (!live)
		// a newer version that turned up while you were live: offer it now the stream is over
		QTimer::singleShot(8000, this, [this]() {
			if (!stopping_ && updateNoticeDue() && !whatsNew_.isEmpty())
				emit updateNoticeReady();
		});
	if (live) {
		sessionStart_ = QDateTime::currentDateTime();
		streamStart_ = sessionStart_;
		gameSeen_ = false;
		chapters_.clear();
		resetSession(TX_NOOP("the stream started"));
		log(tx("Streaming: the highlights session starts here."));
		return;
	}
	writeChapters();
	writeSessionSummary();
	// helping build the plugin: a stream where something was broken sends its logs by itself, with what it was
	// a stream the game never showed in (Just Chatting, another game: LOG-B72B) is not a broken setup to report
	if (cfg.helpBuild() && gameSeen_ && !problems_.isEmpty() && streamStart_.isValid() &&
	    streamStart_.secsTo(QDateTime::currentDateTime()) >= 180 && autoLogsSent_ < 3) {
		autoLogsSent_++;
		SendLogs::sendAuto(this,
				   "Automatic report at the end of a stream. Problems seen:\n" + problems_.join("\n"),
				   false);
	}
	problems_.clear();
	streamStart_ = QDateTime();
	if (cfg.highlightsAuto)
		requestHighlights(TX_NOOP("stream ended"), false);
}

void Engine::noteChapter(const Clips::Entry &e)
{
	if (!cfg.ytChapters || !streamStart_.isValid() || e.title == "setup test")
		return;
	// the moment itself, not the save: the last kill's seconds-before-the-end when known, else
	// a little before the save (a hotkey or a voice command comes just after the action)
	double back = e.momentS > 0 ? e.momentS : 8.0;
	qint64 at = e.when.toMSecsSinceEpoch() - (qint64)(back * 1000) - streamStart_.toMSecsSinceEpoch();
	QString title = e.title == "manual" ? tx("Clip") : e.title == "downed" ? tx("Downed") : e.title;
	title = title.simplified();
	if (title.size() > 70)
		title = title.left(67) + "...";
	chapters_.append({std::max<qint64>(0, at), title});
}

/// YouTube makes chapters from timestamps in a video's description: the first at 0:00, at least
/// three, each at least 10 s long. A live stream's VOD starts when the stream did, so the times
/// into this stream are the times into its VOD (a Twitch VOD downloaded to YouTube too).
void Engine::writeChapters()
{
	if (!cfg.ytChapters || !streamStart_.isValid())
		return;
	auto stamp = [](qint64 ms) {
		qint64 s = ms / 1000;
		return s >= 3600 ? QString("%1:%2:%3")
					   .arg(s / 3600)
					   .arg(s / 60 % 60, 2, 10, QChar('0'))
					   .arg(s % 60, 2, 10, QChar('0'))
				 : QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
	};
	auto list = chapters_;
	std::sort(list.begin(), list.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
	QStringList lines{"0:00 " + tx("Start")};
	qint64 prev = 0;
	for (const auto &c : list) {
		if (c.first - prev < 10000)
			continue; // closer than 10 s to the one before: YouTube would drop every chapter
		lines << stamp(c.first) + " " + c.second;
		prev = c.first;
	}
	if (lines.size() < 3) {
		log(lines.size() == 2
			    ? tx("YouTube chapters: 1 moment this stream - YouTube needs at least two after 0:00, "
				 "so no list was written.")
			    : tx("YouTube chapters: %1 moments this stream - YouTube needs at least two after "
				 "0:00, so no list was written.")
				      .arg(lines.size() - 1));
		return;
	}
	lastChapters_ = lines.join("\n") + "\n";
	// next to the clips, where the VOD's other material is
	QString dir;
	for (auto it = clips.history().rbegin(); it != clips.history().rend() && dir.isEmpty(); ++it)
		if (!it->path.isEmpty())
			dir = QFileInfo(it->path).absolutePath();
	if (dir.isEmpty())
		dir = QDir::homePath() + "/Videos";
	QString path = dir + "/YouTube chapters " + streamStart_.toString("yyyy-MM-dd HH-mm") + ".txt";
	QFile f(path);
	if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
		f.write(lastChapters_.toUtf8());
		f.close();
		lastChaptersPath_ = path;
		log(tx("YouTube chapters: %1 for this stream saved to %2 - paste them into the VOD's description "
		       "(the dock's menu has Copy YouTube chapters).")
			    .arg(lines.size())
			    .arg(QDir::toNativeSeparators(path)));
	} else
		log(tx("YouTube chapters: could not write %1 - the dock's menu still has Copy YouTube chapters.")
			    .arg(QDir::toNativeSeparators(path)));
	emit stateChanged();
}

void Engine::requestHighlights(const QString &why, bool thenPlay)
{
	if (!appConnected()) {
		log(tx("Highlights: ClipHound is not running, so nothing can be built."));
		return;
	}
	QJsonArray arr;
	for (const auto &e : clips.history()) {
		if (e.when < sessionStart_.addSecs(-5) || e.path.isEmpty() || !QFileInfo::exists(e.path))
			continue;
		QJsonObject c;
		c["path"] = e.path;
		if (!e.pathV.isEmpty() && QFileInfo::exists(e.pathV))
			c["pathV"] = e.pathV; // the vertical canvas's clip of the same moment
		c["title"] = e.title;
		c["tags"] = QJsonArray::fromStringList(e.tags);
		c["when"] = e.when.toString(Qt::ISODate);
		arr.append(c);
	}
	if (arr.isEmpty()) {
		log(tx("Highlights: no clips saved this session yet, nothing to build."));
		return;
	}
	QJsonObject o;
	o["type"] = "highlights_build";
	o["clips"] = arr;
	o["out"] = highlightsDir();
	o["player"] = playerName();
	o["lang"] = QString::fromStdString(I18n::current()); // the title cards' words
	o["max"] = cfg.highlightsMax;
	o["vertical"] = cfg.verticalOn(); // a portrait compilation as well, from the twins
	bridge.sendJson(o);
	highlightsBuilding_ = true;
	highlightsThenPlay_ = thenPlay;
	log((arr.size() == 1 ? tx("Highlights: building from 1 clip - %1.").arg(txv(why))
			     : tx("Highlights: building from %1 clips - %2.").arg(arr.size()).arg(txv(why))));
	emit stateChanged();
}

void Engine::sendObsHealth()
{
#ifdef _WIN32
	// The 0.13.0 start-up freeze ended in "used all of its system allowance of handles for Window
	// Manager objects": something in the OBS process was making USER objects fast. Count them, and
	// say so in the log when the count climbs, so the next one names its culprit by time.
	{
		DWORD user = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
		DWORD gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
		if (lastUserObjects_ == 0 || user > lastUserObjects_ + 300 || user > 6000)
			log(tx("Windows objects held by OBS: %1 user, %2 GDI (the limit is 10000 each).")
				    .arg(user)
				    .arg(gdi));
		lastUserObjects_ = user;
	}
#endif
	if (!appConnected())
		return;
	QJsonObject o;
	o["type"] = "obs_health";
	o["lagged"] = (double)obs_get_lagged_frames();
	o["total"] = (double)obs_get_total_frames();
	double dropped = 0;
	if (obs_output_t *out = obs_frontend_get_streaming_output()) {
		dropped = (double)obs_output_get_frames_dropped(out);
		obs_output_release(out);
	}
	o["dropped"] = dropped;
	bridge.sendJson(o);
}

void Engine::playCompilation(const QString &why)
{
	QString folder = highlightsDir();
	QFileInfoList files =
		folder.isEmpty() || !QDir(folder).exists()
			? QFileInfoList()
			: QDir(folder).entryInfoList({"*.mp4", "*.mkv", "*.mov"}, QDir::Files, QDir::Time);
	// the portrait compilation sits next to its landscape one, named "... [vertical]"
	files.erase(std::remove_if(files.begin(), files.end(),
				   [](const QFileInfo &f) { return f.completeBaseName().endsWith(" [vertical]"); }),
		    files.end());
	// nothing built yet, or clips saved since the last one: build first, then play
	QDateTime lastClip;
	for (const auto &e : clips.history())
		if (e.when >= sessionStart_.addSecs(-5))
			lastClip = e.when;
	if (files.isEmpty() || (lastClip.isValid() && files.first().lastModified() < lastClip)) {
		if (highlightsBuilding_) {
			highlightsThenPlay_ = true;
			log(tx("Highlights: still building; it will play when it is ready."));
			return;
		}
		requestHighlights(why, true);
		return;
	}
	if (replaying())
		stopReplay(TX_NOOP("replaced"));
	QString pathV;
	if (cfg.verticalOn()) {
		QFileInfo v(files.first().absolutePath() + "/" + files.first().completeBaseName() + " [vertical]." +
			    files.first().suffix());
		if (v.exists())
			pathV = v.absoluteFilePath();
		else
			log(tx("Play highlights: no vertical compilation next to this one, so the vertical scene gets the "
			       "landscape one (the twin is built when vertical clips are there)."));
	}
	std::string e = sw.playMedia(cfg, files.first().absoluteFilePath().toStdString(), 100,
				     cfg.replaySound ? cfg.replayVolume : 0, false, pathV.toStdString());
	if (!e.empty()) {
		log(tx("Play highlights: %1").arg(QString::fromStdString(e)));
		return;
	}
	replayWhat_ = files.first().fileName();
	pendingReplay_ = Clips::Entry{};
	pendingReplay_.path.clear();
	replayLengthMs_ = 1;
	replaySought_ = true; // from the start, to the end
	replayShown_ = false;
	replayStartMs_ = 0;
	replayEndMs_ = 0; // = the whole file, once its length is known
	replayClock_.start();
	replayTimer_.start(250);
	addEvent(QDateTime::currentDateTime().toString("HH:mm:ss") + "  " + tx("HIGHLIGHTS %1").arg(replayWhat_));
	log(tx("Play highlights: %1 - %2.").arg(replayWhat_, txv(why)));
	emit stateChanged();
}

void Engine::replayTick()
{
	if (!replaying())
		return;
	qint64 dur = sw.mediaDurationMs();
	int st = sw.mediaState();
	if (!replaySought_) {
		// wait for the file to be open and playing before asking for the seek: a seek sent while
		// it is still opening is dropped, and the whole clip plays from the start
		if (dur <= 0 || st != OBS_MEDIA_STATE_PLAYING) {
			if (replayClock_.elapsed() > 5000) {
				log(tx("Instant replay: the file did not start playing."));
				stopReplay(TX_NOOP("failed"));
			}
			return;
		}
		replayWindow(pendingReplay_, dur, cfg.replayPreS, cfg.replayPostS, &replayStartMs_, &replayEndMs_);
		// the first moments play under the stinger's cover: start that much earlier, so the viewer
		// still sees the whole lead-in
		if (cfg.replayStinger && replayStartMs_ > 0)
			replayStartMs_ = std::max<qint64>(0, replayStartMs_ - kStingerInRevealMs);
		if (replayStartMs_ > 0)
			sw.seekMedia(replayStartMs_);
		replaySought_ = true;
		replaySeekChecks_ = 0;
		replayLengthMs_ = std::max<qint64>(1, replayEndMs_ - replayStartMs_);
		replayClock_.restart();
		return;
	}
	if (replayEndMs_ == 0 && dur > 0) {
		replayEndMs_ = dur; // the whole file (the compilation)
		replayLengthMs_ = dur;
	}
	qint64 t = sw.mediaTimeMs();
	// did the seek take? If playback is still well before the start after a moment, ask again
	if (replayStartMs_ > 0 && replaySeekChecks_ < 3 && replayClock_.elapsed() > 500 && t < replayStartMs_ - 1500) {
		sw.seekMedia(replayStartMs_);
		replaySeekChecks_++;
		replayClock_.restart();
		return;
	}
	if (!replayShown_) {
		// on screen once the seek has taken (at once when playing from the start); if it has not
		// taken after a moment, show anyway rather than leave the viewer with nothing
		bool there = replayStartMs_ == 0 || t >= replayStartMs_ - 300 || replayClock_.elapsed() > 1500;
		if (!there || st != OBS_MEDIA_STATE_PLAYING)
			return;
		replayShown_ = true;
		replayClock_.restart();
		if (cfg.replayStinger) {
			// "INSTANT REPLAY" wipes across; the replay goes on full screen while it covers everything,
			// the game over it at full size, and the game shrinks away to its corner as the wipe leaves
			sendStinger("in");
			QTimer::singleShot(kStingerInCoverMs, this, [this]() {
				if (replaying() && !replayOutSent_) {
					sw.showMedia(cfg);
					startPip(kStingerInRevealMs - kStingerInCoverMs -
						 200);               // shrinking as it uncovers
					sw.ensureStinger(cfg, true); // over the camera and alerts showMedia raised
				}
			});
		} else {
			sw.showMedia(cfg);
			startPip(0);
		}
		return;
	}
	if (replayOutSent_)
		return; // "back to live" is playing: the replay stops under its cover
	// the way back starts just before the end, so its cover lands on the last frame
	bool pastEnd = replayEndMs_ > 0 && t >= replayEndMs_ - outLeadMs();
	bool ended = replayClock_.elapsed() > 800 && (st == OBS_MEDIA_STATE_ENDED || st == OBS_MEDIA_STATE_STOPPED);
	bool safety = replayClock_.elapsed() > replayLengthMs_ + 4000; // the clock never lies, the state might
	if (reelOn_ && detected_ && (pastEnd || ended || safety)) {
		reelNext(); // still down: the one before it, straight away
		return;
	}
	if (pastEnd || ended || safety)
		endReplay(TX_NOOP("finished"));
}

void Engine::sendStinger(const char *dir)
{
	QJsonObject o;
	o["type"] = "stinger";
	o["dir"] = dir;
	bridge.sendJson(o);
}

int Engine::outLeadMs() const
{
	bool pip = sw.pipOn();
	if (cfg.replayStinger)
		return kStingerOutCoverMs + (pip ? kPipGrowLeadMs : 0);
	return pip ? kPipGrowMs : 0;
}

void Engine::endReplay(const QString &why)
{
	if (!replaying())
		return;
	if (replayOutSent_)
		return; // already on its way out
	bool pip = sw.pipOn();
	if (!replayShown_ || (!cfg.replayStinger && !pip)) {
		stopReplay(why);
		return;
	}
	replayOutSent_ = true;
	int lead = 0;
	if (pip) {
		// the game grows back to full size first; the wordless "out" wipe follows it over
		growPip();
		lead = cfg.replayStinger ? kPipGrowLeadMs : kPipGrowMs;
	}
	if (cfg.replayStinger)
		QTimer::singleShot(lead, this, [this]() {
			if (!replaying())
				return;
			sw.ensureStinger(cfg, true);
			sendStinger("out");
		});
	QTimer::singleShot(lead + (cfg.replayStinger ? kStingerOutCoverMs : 0), this, [this, why]() {
		if (replaying())
			stopReplay(why);
	});
}

// --- the picture-in-picture ----------------------------------------------------------------

void Engine::startPip(int delayMs)
{
	// instant replays only (not the highlights reel), the main canvas only
	// clutch's replays always keep you live in the corner: a revive is what everyone is waiting for
	if ((!cfg.replayPip && !(cfg.clutch && reelOn_)) || pendingReplay_.path.isEmpty())
		return;
	std::string saved = sw.pipBegin(cfg);
	if (saved.empty())
		return; // no game capture in the plugin's scene: the replay stays as it is
	cfg.pipRestore = saved;
	cfg.save();
	pipDir_ = 1;
	QTimer::singleShot(delayMs, this, [this]() {
		if (!sw.pipOn() || pipDir_ != 1)
			return;
		pipClock_.start();
		pipTimer_.start(16);
	});
}

void Engine::growPip()
{
	if (!sw.pipOn())
		return;
	sendPipFrame(false);
	pipDir_ = -1;
	pipClock_.start();
	pipTimer_.start(16);
}

void Engine::pipTick()
{
	if (!sw.pipOn() || pipDir_ == 0) {
		pipTimer_.stop();
		return;
	}
	int ms = pipDir_ > 0 ? kPipShrinkMs : kPipGrowMs;
	double p = std::min(1.0, pipClock_.elapsed() / (double)ms);
	// ease in and out (cubic): it gathers speed, then settles into place
	double e = p < 0.5 ? 4 * p * p * p : 1 - std::pow(-2 * p + 2, 3) / 2;
	sw.pipStep(pipDir_ > 0 ? e : 1 - e);
	if (p >= 1.0) {
		pipTimer_.stop();
		if (pipDir_ > 0)
			sendPipFrame(true); // the LIVE frame round the small window, once it has landed
		pipDir_ = 0;
	}
}

void Engine::endPip()
{
	pipTimer_.stop();
	pipDir_ = 0;
	if (cfg.pipRestore.empty() && !sw.pipOn())
		return;
	sendPipFrame(false);
	sw.pipRestore(cfg.pipRestore);
	cfg.pipRestore.clear();
	cfg.save();
}

void Engine::sendPipFrame(bool on)
{
	QJsonObject o;
	o["type"] = "pip";
	o["on"] = on;
	if (on) {
		double r[4];
		sw.pipRect(r);
		o["rect"] = QJsonArray{r[0], r[1], r[2], r[3]};
	}
	bridge.sendJson(o);
}

void Engine::stopReplay(const QString &why)
{
	replayTimer_.stop();
	endPip(); // the game back exactly where it was, whatever stopped the replay
	bool was = replaying();
	replayLengthMs_ = 0;
	replayStartMs_ = replayEndMs_ = 0;
	sw.stopMedia(cfg);
	if (was)
		log(tx("Replay off - %1.").arg(txv(why)));
	replayWhat_.clear();
	emit stateChanged();
}

QString Engine::chatReplay(const QString &who)
{
	if (!cfg.replayChat)
		return tx("chat replays are off (Settings, Clips & replays)");
	QDateTime now = QDateTime::currentDateTime();
	if (lastChatReplay_.isValid()) {
		qint64 left = cfg.replayCooldownS - lastChatReplay_.secsTo(now);
		if (left > 0)
			return tx("cooldown: %1 s to go").arg(left);
	}
	bool have = false;
	for (const auto &e : clips.history())
		if (!e.path.isEmpty() && QFileInfo::exists(e.path))
			have = true;
	if (!have)
		return tx("no highlight saved yet");
	lastChatReplay_ = now;
	playReplay("chat: " + who);
	return "";
}

void Engine::dualAnimate(int dir, int delayMs)
{
	dualDir_ = dir;
	dualClock_.start();
	dualDelayMs_ = delayMs;
	dualTimer_.start(16);
}

void Engine::dualTick()
{
	qint64 t = dualClock_.elapsed() - dualDelayMs_;
	if (t < 0)
		return;
	int ms = dualDir_ > 0 ? kDualInMs : kDualOutMs;
	double p = std::min(1.0, t / (double)ms), c = 1.5, k;
	if (dualDir_ > 0) // ease out with a small overshoot: it lands, a touch past full size, and settles
		k = 1 + (c + 1) * std::pow(p - 1, 3) + c * std::pow(p - 1, 2);
	else // a small lift first, then away into its centre
		k = 1 - ((c + 1) * p * p * p - c * p * p);
	sw.dualScale(cfg, std::max(0.001, k));
	if (p < 1.0)
		return;
	dualTimer_.stop();
	int was = dualDir_;
	dualDir_ = 0;
	if (was < 0 && !dualOn_) {
		sw.applyDual(cfg, false); // gone: taken down (and re-armed warm)
		sw.dualScale(cfg, 1.0);
	}
}

void Engine::showInDual(int idx, const QString &why)
{
	if (idx < 0 || idx >= (int)cfg.friends.size())
		return;
	if (cfg.dualFriend != idx) {
		cfg.dualFriend = idx;
		cfg.save();
	}
	if (dualOn_)
		sw.applyDual(cfg, false, false); // swap the person inside the window, not just the label
	setDual(true, why);
}

void Engine::setDual(bool on, const QString &why, bool byDetector)
{
	if (on && !cfg.dual()) {
		// nobody picked for the small window yet: the first squad mate, which is what the Dual POV
		// drop-down on the dock shows
		if (!cfg.friends.empty()) {
			cfg.dualFriend = 0;
			cfg.save();
		} else {
			log(tx("Dual POV: add a squad mate first."));
			return;
		}
	}
	// turned on by hand it stays on; only a window the vehicle detector opened is its to close
	if (on)
		dualAutoOn_ = byDetector; // set before the log line below, which says whether it is forced
	bool show = on && !applied_;
	if (!on && dualOn_ && !applied_) {
		// shrinks away first; the window is taken down when it has gone
		dualOn_ = false;
		dualAnimate(-1);
	} else {
		dualTimer_.stop();
		dualDir_ = 0;
		if (show)
			sw.dualK = 0.02; // placed tiny, then grown in
		std::string e = sw.applyDual(cfg, show);
		sw.dualK = 1.0;
		if (!e.empty()) {
			log(tx("Dual POV: %1").arg(QString::fromStdString(e)));
			if (on)
				return;
		}
		if (show)
			dualAnimate(1);
	}
	dualOn_ = on;
	pushAppConfig(); // ClipHound watches the vehicle corner while the window is up
	log(!on           ? tx("Dual POV off - %1.").arg(txv(why))
	    : dualAutoOn_ ? tx("Dual POV on: %1 in the small window - %2.")
				    .arg(QString::fromStdString(cfg.dual()->name), txv(why))
			  : tx("Dual POV on: %1 in the small window - %2 (forced: stays until you turn it off).")
				    .arg(QString::fromStdString(cfg.dual()->name), txv(why)));
	emit stateChanged();
}

/// ClipHound read the vehicle keybind list: a seat name, "vehicle" (in one, seat unclear) or "none".
void Engine::onInventory(bool open)
{
	qint64 now = QDateTime::currentMSecsSinceEpoch();
	if (!open) {
		if (!invApplied_)
			return;
		invApplied_ = false;
		// a screen that comes and goes (a menu the reader half-recognises) must not flip the stream
		// back and forth: after one swap back, the inventory is not swapped for again for 15 s
		invQuietUntil_ = now + 15000;
		// back to your own POV, unless you went down meanwhile: then the downed swap owns it. Not before the
		// POV has been up a moment: a peek at the inventory made 0.7 s swaps, each with a stinger
		if (applied_ && !detected_) {
			qint64 shown = std::chrono::duration_cast<std::chrono::milliseconds>(clock_::now() - downSince_)
					       .count();
			int hold = std::min(cfg.povMinS, 2) * 1000; // your inventory, not a down: 2 s at most
			int wait = (int)std::max<qint64>(0, hold - shown);
			QTimer::singleShot(wait, this, [this]() {
				if (applied_ && !detected_ && !invApplied_ && inventoryShow_)
					applyNow(false, TX_NOOP("inventory closed"));
			});
		}
		return;
	}
	if (!cfg.invSwitch || !cfg.enabled || applied_ || detected_ || !cfg.active() || now < invQuietUntil_)
		return;
	// only onto a squad mate who is actually streaming (0.26.4: a stream that could not be vouched
	// for used to be enough)
	int alt = confirmedLiveFriend();
	if (alt < 0) {
		log(tx("Inventory open, but no squad mate is confirmed live - staying on your own POV."));
		return;
	}
	if (alt != cfg.activeFriend)
		setActive(alt);
	invApplied_ = true;
	applyNow(true, TX_NOOP("inventory open (magazine packing)"));
}

void Engine::onVehicle(const QString &seat)
{
	vehicleSeat_ = seat;
	if (!cfg.dual())
		return;
	if (seat == "none") {
		vehicleNotLiveLogged_ = false;
		// out of the vehicle: a window the detector opened goes - unless asked to stay. One you
		// turned on yourself is yours to turn off.
		if (dualOn_ && dualAutoOn_ && !cfg.dualKeep) {
			dualAutoOn_ = false;
			setDual(false, TX_NOOP("out of the vehicle"));
		}
		return;
	}
	if (!cfg.dualAuto)
		return; // turning it on by itself is the tick box's job
	if (seat != "vehicle" && seat.toStdString() != cfg.dualPreset) {
		// the seat the game shows wins over the preset chosen by hand
		struct P {
			const char *id;
			double x, y, w;
		};
		static const P presets[] = {{"tank-driver", 0.012, 0.19, 0.26},
					    {"tank-gunner", 0.012, 0.19, 0.26},
					    {"havoc-pilot", 0.012, 0.19, 0.26},
					    {"havoc-gunner", 0.19, 0.075, 0.20}};
		for (const auto &p : presets)
			if (seat == p.id) {
				cfg.dualPreset = p.id;
				cfg.dualX = p.x;
				cfg.dualY = p.y;
				cfg.dualW = p.w;
				cfg.save();
			}
	}
	vehicleDualCheck();
	emit stateChanged();
}

/// In a vehicle with "Auto in vehicles": the dual window opens only for a squad mate who is actually
/// live - the roster (Discord) or the live check (Twitch, Kick, YouTube) says so - and a window it
/// opened goes if they stop. Called when the seat changes and every 5 s, so a squad mate who goes
/// live while you are still in the vehicle comes up then.
void Engine::vehicleDualCheck()
{
	const Friend *f = cfg.dual();
	if (!f || !cfg.dualAuto || vehicleSeat_.isEmpty() || vehicleSeat_ == "none" || applied_)
		return;
	Feed st = feedState(*f);
	QString who = QString::fromStdString(f->name);
	if (!dualOn_) {
		if (!confirmedLive(*f)) {
			if (!vehicleNotLiveLogged_) {
				vehicleNotLiveLogged_ = true;
				log(tx("In a vehicle, but %1 is not confirmed live (Discord or Twitch), so the dual window stays "
				       "closed until they are.")
					    .arg(who));
			}
			return;
		}
		vehicleNotLiveLogged_ = false;
		setDual(true, tx("in a vehicle: %1").arg(vehicleSeat_), true);
		dualAutoOn_ = dualOn_; // the detector's, if it opened
	} else if (dualAutoOn_ && st == Feed::Off) {
		dualAutoOn_ = false;
		setDual(false, tx("%1 is not live any more").arg(who));
	}
}

void Engine::toggleDual()
{
	setDual(!dualOn_, TX_NOOP("hotkey")); // the person is the Dual POV drop-down's pick
}

void Engine::toggle()
{
	applyNow(!applied_, TX_NOOP("hotkey"));
}

void Engine::setActive(int idx)
{
	if (idx < 0 || idx >= (int)cfg.friends.size())
		return;
	bool wasOn = applied_;
	if (wasOn)
		applyNow(false, TX_NOOP("switching squad mate"));
	cfg.activeFriend = idx;
	cfg.save();
	if (wasOn)
		applyNow(true, tx("squad mate is now %1").arg(QString::fromStdString(cfg.friends[idx].name)));
	else if (cfg.keepWarm)
		sw.armWarm(cfg);
	log(tx("Active squad mate: %1.").arg(QString::fromStdString(cfg.friends[idx].name)));
	emit stateChanged();
}

void Engine::setEnabled(bool on)
{
	cfg.enabled = on;
	cfg.save();
	if (!on && applied_)
		applyNow(false, TX_NOOP("paused"));
	downRun_ = upRun_ = 0;
	detected_ = false;
	detGame_.holdThreshold = 0;
	log(on ? tx("Auto switch on: a squad mate takes over when you are downed.")
	       : tx("Auto switch off: your own POV stays up. The squad mate buttons on the dock still work, and clips "
		    "keep coming."));
	// ClipHound watches for the inventory screen only while Auto switch is on: tell it now. It was
	// told only at the next settings change before, so turning Auto switch back on left magazine
	// packing off until OBS restarted
	pushAppConfig();
	emit stateChanged();
}

void Engine::setInvSwitch(bool on)
{
	if (cfg.invSwitch == on)
		return;
	cfg.invSwitch = on;
	cfg.save();
	pushAppConfig();
	log(on ? tx("Magazine packing / inventory POV switching on.")
	       : tx("Magazine packing / inventory POV switching off."));
	emit stateChanged();
}

void Engine::setFriendAudio(bool on)
{
	if (cfg.friendAudio != on) {
		cfg.friendAudio = on;
		cfg.save();
	}
	sw.applyFriendAudio(cfg, applied_);
	const Friend *a = cfg.active();
	QString who = applied_ && a ? QString::fromStdString(a->name) : QString();
	if (on)
		log(who.isEmpty()
			    ? tx("Squad mate's sound on: whoever is on screen is the one feed with sound.")
			    : tx("Squad mate's sound on: %1's feed has sound now; every other squad mate stays muted.")
				      .arg(who));
	else
		log(who.isEmpty() ? tx("Squad mate's sound off: their feeds are silent on your stream.")
				  : tx("Squad mate's sound off: %1's feed is muted on your stream.").arg(who));
	emit stateChanged();
}

void Engine::captureTemplate()
{
	QImage img = lastFrame();
	if (img.isNull()) {
		log(tx("No frame from the game source yet (open the settings window so frames are kept)."));
		return;
	}
	int x = (int)std::lround(cfg.boxX * img.width()), y = (int)std::lround(cfg.boxY * img.height());
	int w = std::max(8, (int)std::lround(cfg.boxW * img.width())),
	    h = std::max(8, (int)std::lround(cfg.boxH * img.height()));
	x = std::clamp(x, 0, img.width() - 8);
	y = std::clamp(y, 0, img.height() - 8);
	w = std::min(w, img.width() - x);
	h = std::min(h, img.height() - y);
	std::vector<float> g((size_t)w * h);
	for (int yy = 0; yy < h; yy++)
		for (int xx = 0; xx < w; xx++) {
			QRgb p = img.pixel(x + xx, y + yy);
			g[(size_t)yy * w + xx] = 0.299f * qRed(p) + 0.587f * qGreen(p) + 0.114f * qBlue(p);
		}
	cfg.customTemplateWidthFrac = (double)w / img.width();
	{
		std::lock_guard<std::recursive_mutex> lk(detMx_);
		detGame_.setTemplate(g, w, h, (float)cfg.customTemplateWidthFrac);
	}
	std::string dir = Config::configDir();
	std::ofstream out(Config::configFile("template.bin"), std::ios::binary);
	out.write((const char *)&w, 4);
	out.write((const char *)&h, 4);
	out.write((const char *)g.data(), g.size() * sizeof(float));
	cfg.save();
	downRun_ = upRun_ = 0;
	log(tx("Custom damage-log template captured from the box."));
	emit stateChanged();
}

void Engine::useBuiltInTemplate()
{
	cfg.customTemplateWidthFrac = 0;
	cfg.save();
	loadTemplates();
	log(tx("Back to the built-in damage-log template."));
	emit stateChanged();
}

void Engine::previewLook(bool on)
{
	lookPreview_ = on && !applied_;
	std::string e = sw.updateLook(cfg, lookPreview_ || applied_);
	if (cfg.verticalOn()) {
		// the portrait overlay previews too, on its own, without moving a squad mate's feed about
		bool show = lookPreview_ || applied_;
		std::string ev = sw.applyVertical(cfg, show);
		if (!ev.empty() && e.empty())
			e = ev;
	}
	log(!e.empty() ? tx("Look: %1").arg(QString::fromStdString(e))
		       : (lookPreview_ ? tx("Look overlay showing in OBS.") : tx("Look overlay hidden.")));
}

/// OBS says at start when the last session did not close cleanly: with "help build the plugin" on, that session's
/// logs go to kennel.gg (a crash nobody reports is a crash nobody fixes).
void Engine::checkLastCrash()
{
	if (!cfg.helpBuild() || stopping_)
		return;
	QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).section('/', 0, -2) +
		 "/obs-studio/logs");
	QFileInfoList l = dir.entryInfoList({"*.txt"}, QDir::Files, QDir::Name | QDir::Reversed); // named by start time
	if (l.size() < 2)
		return;
	QFile f(l.first().absoluteFilePath());
	if (!f.open(QIODevice::ReadOnly))
		return;
	QByteArray head = f.read(64 * 1024);
	if (!head.contains("Crash or unclean shutdown detected"))
		return;
	log(tx("OBS did not close cleanly last time: sending that session's logs to Kennel.gg (you help build the "
	       "plugin; Settings, Clips & replays to stop)."));
	SendLogs::sendAuto(this,
			   "Automatic report: OBS did not close cleanly in the session before " + l.first().fileName(),
			   true);
}

void Engine::setHudShare(bool on)
{
	cfg.hudShare = on ? 1 : 2;
	cfg.save();
	log(on ? tx("Helping build the plugin: on. Small pictures of your HUD go to kennel.gg every few minutes in a "
		    "match, and the logs after a stream with problems or an OBS crash (Settings, Clips & replays to "
		    "stop).")
	       : tx("Helping build the plugin: off. No HUD pictures or logs are sent unless you press Send logs."));
	emit stateChanged();
	if (on)
		QTimer::singleShot(20000, this, [this]() { hudSample("first"); });
}

/// One set of HUD pictures: the corners the readers look at, cut at the game's own resolution (PNG, nothing
/// scaled), with what the plugin read there, POSTed to kennel.gg/api/stats/hud. Only with cfg.hudShare == 1,
/// only while the balance was read in the last 10 s (the HUD is on screen, you are in a match), 60 sets at most
/// per OBS run.
void Engine::hudSample(const QString &why)
{
	if (!cfg.helpBuild() || stopping_ || hudBusy_ || cfg.gameSource.empty() || hudSent_ >= 60)
		return;
	qint64 now = QDateTime::currentMSecsSinceEpoch();
	if (why != "downed" && now - lastCashOkMs_ > 10000)
		return;
	obs_source_t *src = obs_get_source_by_name(cfg.gameSource.c_str());
	if (!src)
		return;
	int W = (int)obs_source_get_width(src), H = (int)obs_source_get_height(src);
	obs_source_release(src);
	if (W < 320 || H < 200)
		return;
	std::vector<std::pair<QString, QRectF>> parts;
	parts.push_back({"cash", cashArea(W, H)});
	for (const auto &s : bridge.streams()) {
		if (s.id == 0)
			parts.push_back({"feed", s.roi});
		else if (s.id == 4)
			parts.push_back({"plate", s.roi});
		else if (s.id == 5)
			parts.push_back({"ticker", s.roi});
	}
	if (cfg.nearEnabled)
		parts.push_back({"nearby", QRectF(cfg.nearX, cfg.nearY, cfg.nearW, cfg.nearH)});
	if (why == "downed")
		parts.push_back({"damage", QRectF(cfg.dmgX, cfg.dmgY, cfg.dmgW, cfg.dmgH)});
	// what the readers made of it, so each picture comes with its answer to check
	QJsonObject read;
	if (session_.haveBalance)
		read["balance"] = (double)session_.balanceNow;
	read["downed"] = detected_;
	if (detected_)
		read["downed_score"] = peakScore_;
	read["holding"] = holding_;
	read["nearby"] = nearbyText();
	QJsonArray ev;
	for (int i = std::max(0, (int)events_.size() - 4); i < events_.size(); ++i)
		ev.append(events_[i]);
	read["events"] = ev;
	QJsonObject head;
	head["version"] = PLUGIN_VERSION;
	head["game_lang"] = QString::fromStdString(cfg.gameLangFound.empty() ? cfg.gameLang : cfg.gameLangFound);
	head["ui_lang"] = QString::fromStdString(I18n::current());
	head["width"] = W;
	head["height"] = H;
	head["why"] = why;
	head["read"] = read;
	hudBusy_ = true;
	hudSent_++;
	std::string name = cfg.gameSource;
	workers_++;
	std::thread([this, parts, name, head]() {
		WorkerGuard guard(workers_);
		QJsonArray crops;
		obs_source_t *s = obs_get_source_by_name(name.c_str());
		if (s) {
			for (const auto &p : parts) {
				std::vector<uint8_t> bgra;
				int w = 0, h = 0, ls = 0;
				const QRectF &r = p.second;
				if (!capHud_.grabRegion(s, r.x(), r.y(), r.width(), r.height(), 0, bgra, w, h, ls))
					continue;
				QImage img = QImage(bgra.data(), w, h, ls, QImage::Format_ARGB32)
						     .convertToFormat(QImage::Format_RGB888);
				QByteArray png;
				QBuffer buf(&png);
				buf.open(QIODevice::WriteOnly);
				img.save(&buf, "PNG");
				QJsonObject c;
				c["kind"] = p.first;
				c["roi"] = QJsonArray{r.x(), r.y(), r.width(), r.height()};
				c["png"] = QString::fromLatin1(png.toBase64());
				crops.append(c);
			}
			obs_source_release(s);
		}
		QJsonObject body = head;
		body["crops"] = crops;
		QByteArray json = QJsonDocument(body).toJson(QJsonDocument::Compact);
		QMetaObject::invokeMethod(
			this,
			[this, json, n = crops.size()]() {
				if (stopping_ || n == 0) {
					hudBusy_ = false;
					return;
				}
				Http::requestAsync(
					this, "POST", "https://kennel.gg/api/stats/hud", json,
					"Content-Type: application/json\r\n", 30000,
					QString("KennelggWardogsOBSTool/%1").arg(PLUGIN_VERSION),
					[this](Http::Result r) {
						hudBusy_ = false;
						if (!r.ok && hudSent_ <= 1)
							log(tx("HUD pictures: kennel.gg did not take them (%1).")
								    .arg(r.error.isEmpty()
										 ? QString("HTTP %1").arg(r.status)
										 : r.error));
					});
			},
			Qt::QueuedConnection);
	}).detach();
}

/// A squad mate the downed swap could show now: one chosen, with a capture, and not known to be off.
bool Engine::reelWanted() const
{
	return cfg.downedReplaysAlways || ((cfg.downedReplays || cfg.clutch) && !squadToShow());
}

bool Engine::squadToShow() const
{
	if (cfg.clutch)
		return clutchPick() >= 0;
	const Friend *a = cfg.active();
	if (a && feedUsable(*a) && feedState(*a) != Feed::Off)
		return true;
	return anyLiveFriend() >= 0;
}

/// Downed with nobody to show: this session's replays, newest first. The clip of this very down (saved moments
/// ago) is left out, and so is Setup's test clip.
void Engine::startReel()
{
	if (reelOn_ || !detected_)
		return;
	reel_.clear();
	QDateTime now = QDateTime::currentDateTime();
	const Clips::Entry *own = nullptr; // the clip of this very down
	for (auto it = clips.history().rbegin(); it != clips.history().rend(); ++it) {
		if (it->path.isEmpty() || !QFileInfo::exists(it->path) || it->title == "setup test")
			continue;
		if (downAt_.isValid() && it->when >= downAt_.addSecs(-1)) {
			if (!own && it->title == "downed")
				own = &*it;
			continue;
		}
		reel_.push_back(*it);
	}
	if (cfg.reelFromDown) {
		if (own)
			reel_.insert(reel_.begin(), *own); // first: the moment you went down
		else if (reelWaits_ < 16 && downAt_.isValid() && downAt_.secsTo(now) < 8) {
			// the replay buffer is still writing it: a moment more (it lands a second or two after the down)
			reelWaits_++;
			QTimer::singleShot(250, this, [this]() {
				if (detected_ && !applied_ && !reelOn_)
					startReel();
			});
			return;
		}
	}
	reelWaits_ = 0;
	if (reel_.empty()) {
		if (squadToShow()) { // replays chosen over the squad, but none yet: the squad mate after all
			applyNow(true, TX_NOOP("downed, no replays yet"));
			return;
		}
		log(tx("Downed: no replays saved yet this session to play, so your own POV stays."));
		return;
	}
	reelOn_ = true;
	reelPos_ = 0;
	log(reel_.size() == 1
		    ? tx("Downed with nobody to show: playing your last replay until you are revived.")
		    : tx("Downed with nobody to show: playing your last %1 replays, newest first, until you are "
			 "revived.")
			      .arg(reel_.size()));
	playReplayEntry(reel_[0], TX_NOOP("downed, nobody to show"));
}

void Engine::reelNext()
{
	if (!reelOn_ || reel_.empty() || !detected_) {
		reelOn_ = false;
		endReplay(TX_NOOP("finished"));
		return;
	}
	reelPos_ = (reelPos_ + 1) % (int)reel_.size(); // the one before; after the oldest, the newest again
	playReplayEntry(reel_[reelPos_], TX_NOOP("downed, nobody to show"));
}

/// A patch release's ClipHound and data: its portable zip (checked against the release's checksum) unpacked
/// into %TEMP%, then applied by applyLiveUpdate at a quiet moment.
void Engine::startLiveUpdate()
{
#ifdef _WIN32
	if (liveBusy_ || newPortable_.isEmpty() || newPortableSha_.size() != 64 || liveStagedVersion_ == newVersion_)
		return;
	QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/kennelgg-live";
	QDir(dir).removeRecursively();
	QDir().mkpath(dir);
	QString zip = dir + "/" + QUrl(newPortable_).fileName();
	QString want = newPortableSha_, ver = newVersion_;
	liveBusy_ = true;
	log(tx("Live update: downloading %1's ClipHound and overlays in the background...").arg(ver));
	Http::downloadAsync(
		this, newPortable_, zip, QString("KennelggWardogsOBSTool/%1").arg(PLUGIN_VERSION),
		[](qint64, qint64) {},
		[this, zip, dir, want, ver](Http::Result r) {
			if (!r.ok) {
				liveBusy_ = false;
				log(tx("Live update: the download failed (%1).").arg(r.error));
				return;
			}
			workers_++;
			std::thread([this, zip, dir, want, ver]() {
				WorkerGuard guard(workers_);
				QString err;
				QFile f(zip);
				QCryptographicHash h(QCryptographicHash::Sha256);
				if (!f.open(QIODevice::ReadOnly) || !h.addData(&f) ||
				    QString::fromLatin1(h.result().toHex()) != want)
					err = tx("the file did not match the release's checksum");
				f.close();
				QString out = dir + "/x";
				if (err.isEmpty()) {
					// Windows 10 and 11 have tar, which reads zip
					QDir().mkpath(out);
					int rc = QProcess::execute("tar", {"-xf", zip, "-C", out});
					if (rc != 0 || !QFileInfo::exists(out + "/ClipHound/ClipHound.exe"))
						err = tx("the zip could not be unpacked");
				}
				QFile::remove(zip);
				QMetaObject::invokeMethod(
					this,
					[this, err, out, ver]() {
						liveBusy_ = false;
						if (!err.isEmpty()) {
							log(tx("Live update: %1.").arg(err));
							return;
						}
						liveStaged_ = out;
						liveStagedVersion_ = ver;
						log(tx("Live update: %1 is ready and goes in at the next quiet moment (nothing on "
						       "screen for it to interrupt).")
							    .arg(ver));
					},
					Qt::QueuedConnection);
			}).detach();
		});
#endif
}

#ifdef _WIN32
/// Copy a folder over another, skipping the files that are the user's (ClipHound's config and logs).
static QString copyTree(const QString &from, const QString &to)
{
	QDirIterator it(from, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		QString src = it.next();
		QString rel = QDir(from).relativeFilePath(src);
		QString name = QFileInfo(rel).fileName().toLower();
		if (name == "config.yaml" || name.endsWith(".log") || name.endsWith(".log.old"))
			continue;
		QString dst = to + "/" + rel;
		QDir().mkpath(QFileInfo(dst).absolutePath());
		// the new file goes in next to the old one first: removing the old one before a copy that then failed left
		// ClipHound.exe deleted with nothing in its place
		QString tmp = dst + ".new";
		QFile::remove(tmp);
		if (!QFile::copy(src, tmp))
			return rel;
		bool ok = false;
		for (int i = 0; i < 20 && !ok; ++i) { // a file ClipHound was still closing: a moment more
			if (QFileInfo::exists(dst) && !QFile::remove(dst)) {
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
				continue;
			}
			ok = QFile::rename(tmp, dst);
		}
		if (!ok) {
			QFile::remove(tmp);
			return rel;
		}
	}
	return QString();
}
#endif

/// The staged patch release goes in: ClipHound is stopped, replaced and started again (a few seconds without
/// kill-feed reading), the overlays and translations are copied to plugin_config/kennelgg/live (kennel_file()
/// prefers them) and the pages are pointed at them. The DLL waits for OBS to close.
void Engine::applyLiveUpdate()
{
#ifdef _WIN32
	QString staged = liveStaged_, ver = liveStagedVersion_;
	liveStaged_.clear();
	liveBusy_ = true;
	QString appDir =
		QFileInfo(cfg.appPath.empty() ? defaultAppPath() : QString::fromStdString(cfg.appPath)).absolutePath();
	bool appWas = bridge.clients() > 0 || appRunning();
	log(tx("Live update: putting %1 in - ClipHound restarts, the stream carries on.").arg(ver));
	if (appWas)
		stopApp();
	workers_++;
	std::thread([this, staged, ver, appDir, appWas]() {
		WorkerGuard guard(workers_);
		QString live = liveDataDir();
		QDir(live).removeRecursively();
		QString bad = copyTree(staged + "/data/obs-plugins/kennelgg", live + "/data");
		if (bad.isEmpty()) {
			QFile v(live + "/VERSION");
			if (v.open(QIODevice::WriteOnly))
				v.write(ver.toUtf8());
		}
		QString badApp = copyTree(staged + "/ClipHound", appDir);
		QDir(QFileInfo(staged).absolutePath()).removeRecursively();
		QMetaObject::invokeMethod(
			this,
			[this, ver, bad, badApp, appWas]() {
				liveBusy_ = false;
				if (appWas)
					launchApp();
				if (!bad.isEmpty() || !badApp.isEmpty()) {
					log(tx("Live update: could not replace %1; the rest is in. It installs fully when OBS "
					       "closes.")
						    .arg(bad.isEmpty() ? badApp : bad));
				}
				// the pages and the translations from the new data
				I18n::load(I18n::resolve(cfg.uiLang));
				if (obs_source_t *src = obs_get_source_by_name(Config::sessionOverlayName())) {
					// the session bar's page: the same address after the file, on the new file
					char *pg = kennel_file("overlay/session.html");
					obs_data_t *st = obs_source_get_settings(src);
					QString u = QString::fromUtf8(obs_data_get_string(st, "url"));
					if (pg && u.contains('?')) {
						QString file = QString::fromUtf8(pg).replace('\\', '/');
						u = "file:///" + file + u.mid(u.indexOf('?'));
						obs_data_set_string(st, "url", u.toUtf8().constData());
						obs_source_update(src, st);
					}
					bfree(pg);
					obs_data_release(st);
					obs_source_release(src);
				}
				sw.ensureStinger(cfg, cfg.replayStinger || cfg.povStinger);
				sw.updateLook(cfg, lookPreview_ || applied_);
				log(tx("Live update: %1's ClipHound and overlays are running. The plugin itself updates "
				       "when you close OBS.")
					    .arg(ver));
				emit stateChanged();
			},
			Qt::QueuedConnection);
	}).detach();
#endif
}

void Engine::installOnExit()
{
#ifdef _WIN32
	if (!cfg.autoUpdate || updStep_ != UpdStep::Ready || !QFileInfo::exists(updPath_) ||
	    !isNewer(updVersion_, PLUGIN_VERSION))
		return;
	// no /OBS=: the installer waits for OBS to finish closing, installs quietly and does not open it again
	QString args = "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /UPDATE";
	HINSTANCE h = ShellExecuteW(nullptr, L"open", (const wchar_t *)QDir::toNativeSeparators(updPath_).utf16(),
				    (const wchar_t *)args.utf16(), nullptr, SW_HIDE);
	blog(LOG_INFO, "[kennelgg] update %s installs as OBS closes (%s)", updVersion_.toUtf8().constData(),
	     (INT_PTR)h > 32 ? "started" : "the installer did not start");
#endif
}

/// Your own POV with nothing of the squad's left over: every squad mate's source hidden and the name plate
/// down. Does nothing while a squad mate is on screen on purpose.
void Engine::clearSquadLeftovers()
{
	if (applied_ || lookPreview_ || povPending_ || applying_ || dualOn_ || replaying())
		return;
	int n = sw.hideAllFriends(cfg);
	sw.updateLook(cfg, false);
	if (n > 0)
		log(n == 1 ? tx("Squad mate feeds hidden (1 item).")
			   : tx("Squad mate feeds hidden (%1 items).").arg(n));
	if (cfg.keepWarm && cfg.active())
		sw.armWarm(cfg);
}

/// Clutch: a squad mate read as down in the last 20 s. Nobody read yet (or not for a while) counts as up.
bool Engine::mateDowned(const Friend &f) const
{
	auto it = mates_.find(QString::fromStdString(f.name));
	return it != mates_.end() && it->downed && QDateTime::currentMSecsSinceEpoch() - it->at < 20000;
}

int Engine::clutchPick() const
{
	auto ok = [this](const Friend &f) {
		return feedUsable(f) && feedState(f) != Feed::Off && !mateDowned(f);
	};
	if (cfg.active() && ok(*cfg.active()))
		return cfg.activeFriend;
	for (size_t i = 0; i < cfg.friends.size(); ++i)
		if (feedState(cfg.friends[i]) == Feed::Live && ok(cfg.friends[i]))
			return (int)i;
	for (size_t i = 0; i < cfg.friends.size(); ++i)
		if (ok(cfg.friends[i]))
			return (int)i;
	return -1;
}

void Engine::clutchStep()
{
	if (!cfg.clutch || !detected_)
		return;
	if (applied_ && cfg.active() && mateDowned(*cfg.active())) {
		// the squad mate on screen went down too: the next one up, or your replays
		int up = clutchPick();
		if (up >= 0) {
			log(tx("Clutch: %1 is down too, showing %2.")
				    .arg(QString::fromStdString(cfg.active()->name),
					 QString::fromStdString(cfg.friends[up].name)));
			setActive(up);
		} else
			clutchReel();
		return;
	}
	if (reelOn_) {
		// someone is back up while your replays play: their POV
		int up = clutchPick();
		if (up < 0)
			return;
		log(tx("Clutch: %1 is up, showing their POV.").arg(QString::fromStdString(cfg.friends[up].name)));
		reelOn_ = false;
		if (replaying())
			endReplay(TX_NOOP("squad mate up"));
		cfg.activeFriend = up;
		cfg.save();
		QTimer::singleShot(700, this, [this]() {
			if (detected_ && !applied_ && !reelOn_)
				applyNow(true, TX_NOOP("downed, squad mate up"));
		});
	}
}

void Engine::clutchReel()
{
	log(tx("Clutch: the whole squad is down - playing your replays until someone is revived."));
	applyNow(false, TX_NOOP("squad down"));
	QTimer::singleShot(900, this, [this]() {
		if (detected_ && !applied_ && !reelOn_ && !replaying())
			startReel();
	});
}
