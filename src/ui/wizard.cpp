#include "ui/wizard.h"
#include <obs-frontend-api.h>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QSysInfo>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>
#include <QInputDialog>
#include <obs-module.h>
#include <algorithm>

static QLabel *note(const QString &t, QWidget *p)
{
	auto *l = new QLabel(t, p);
	l->setWordWrap(true);
	l->setTextFormat(Qt::RichText);
	l->setOpenExternalLinks(true);
	{
		QFont f = l->font();
		if (f.pointSizeF() > 0)
			f.setPointSizeF(f.pointSizeF() - 0.5);
		else if (f.pixelSize() > 2)
			f.setPixelSize(f.pixelSize() - 1);
		l->setFont(f);
	}
	return l;
}

enum { PageYou, PageGame, PageSquad, PageClips, PageCheck };

SetupWizard::SetupWizard(Engine *engine, QWidget *parent) : QWizard(parent), e_(engine)
{
	setWindowTitle("Kennel.gg Wardogs - setup");
	setWizardStyle(QWizard::ModernStyle);
	setOption(QWizard::NoBackButtonOnStartPage, true);
	setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
	setMinimumSize(560, 420);
	resize(720, 560);
	setPage(PageYou, pageYou());
	setPage(PageGame, pageGame());
	setPage(PageSquad, pageSquad());
	setPage(PageClips, pageClips());
	setPage(PageCheck, pageCheck());
	connect(this, &QWizard::currentIdChanged, this, [this](int id) {
		if (id == PageGame)
			fillGame();
		if (id == PageSquad)
			fillSquad();
		if (id == PageCheck) {
			commit(); // the check is of what was just set, not of what was there before
			fillChecks();
			checkTick_.start();
		} else
			checkTick_.stop();
	});
	checkTick_.setInterval(1500);
	connect(&checkTick_, &QTimer::timeout, this, &SetupWizard::fillChecks);
}

QWizardPage *SetupWizard::pageYou()
{
	auto *p = new QWizardPage(this);
	p->setTitle("You");
	p->setSubTitle("Downed in WARDOGS? Your stream shows a squad mate's POV until you are back up. Your mic is "
		       "never touched.");
	auto *f = new QFormLayout(p);
	gameName_ = new QLineEdit(QString::fromStdString(e_->cfg.appPlayerName), p);
	gameName_->setPlaceholderText("exactly as the kill feed shows it");
	f->addRow("Your name in WARDOGS", gameName_);
	f->addRow(note("Kill-feed clips need it: without it ClipHound cannot tell your kills from anyone else's. "
		       "It also goes on your highlights title card.",
		       p));
	lang_ = new QComboBox(p);
	lang_->addItem("Auto (found from the downed screen)", "auto");
	lang_->addItem("English", "en");
	lang_->addItem("Español", "es");
	lang_->addItem("Français", "fr");
	int li = lang_->findData(QString::fromStdString(e_->cfg.gameLang));
	lang_->setCurrentIndex(li < 0 ? 0 : li);
	f->addRow("Game language", lang_);
	f->addRow(note("The plugin reads the words on the downed screen. English, Spanish and French so far; for "
		       "another language, open a ticket in the Kennel.gg Discord.",
		       p));
	lookName_ = new QCheckBox("Show a \"POV · NAME\" tag over the squad mate's feed", p);
	lookName_->setChecked(e_->cfg.lookName);
	f->addRow(lookName_);
	return p;
}

QWizardPage *SetupWizard::pageGame()
{
	auto *p = new QWizardPage(this);
	p->setTitle("Your game");
	p->setSubTitle(
		"Which OBS source shows WARDOGS? It is watched for the damage log that appears while you are downed.");
	auto *v = new QVBoxLayout(p);
	auto *row = new QHBoxLayout();
	game_ = new QComboBox(p);
	auto *mk = new QPushButton("Create a Game Capture for me", p);
	row->addWidget(game_, 1);
	row->addWidget(mk);
	v->addLayout(row);
	gameHint_ = note("", p);
	v->addWidget(gameHint_);
	// the one scene the plugin works in: its POV sources, overlay and label go into this scene
	// and nowhere else. Sources added to some other scene are the usual reason "nothing shows"
	auto *sl = new QLabel("<b>Scene the plugin works in</b> - the scene you stream WARDOGS from. The squad "
			      "mate's POV, the label and the overlay are added to this scene only.",
			      p);
	sl->setWordWrap(true);
	v->addWidget(sl);
	scene_ = new QComboBox(p);
	v->addWidget(scene_);
	v->addWidget(note("Pick the scene that is live while you play. If you use several, pick the main gameplay "
			  "one; it can be changed later under Settings, General.",
			  p));
	v->addStretch(1);
	connect(mk, &QPushButton::clicked, this, [this]() {
		std::string e = e_->sw.createGameCapture(e_->cfg);
		gameHint_->setText(e.empty() ? "Added a Game Capture (any fullscreen game) at the bottom of your scene."
					     : QString::fromStdString(e));
		fillGame();
	});
	return p;
}

void SetupWizard::fillGame()
{
	game_->clear();
	QString chosen = QString::fromStdString(e_->cfg.gameSource);
	int pick = -1;
	for (auto &i : Switcher::inputs()) {
		if (i.first == Config::webSourceName() || i.first == Config::overlaySourceName() ||
		    i.first.rfind("Kennel · ", 0) == 0)
			continue;
		if (i.second.find("audio") != std::string::npos || i.second.find("wasapi") != std::string::npos ||
		    i.second.find("text") != std::string::npos || i.second == "browser_source" ||
		    i.second == "image_source" || i.second == "color_source")
			continue;
		game_->addItem(QString::fromStdString(i.first), QString::fromStdString(i.second));
		int idx = game_->count() - 1;
		if (i.first == chosen.toStdString())
			pick = idx;
		else if (pick < 0 && (i.second == "game_capture" || i.second == "dshow_input"))
			pick = idx;
	}
	if (pick >= 0)
		game_->setCurrentIndex(pick);
	// scenes: the saved one, else the one that is live now
	scene_->clear();
	QString cur = QString::fromStdString(e_->cfg.sceneName);
	if (cur.isEmpty()) {
		obs_source_t *s = obs_frontend_get_current_scene();
		if (s) {
			cur = obs_source_get_name(s);
			obs_source_release(s);
		}
	}
	for (auto &s : Switcher::sceneNames())
		scene_->addItem(QString::fromStdString(s));
	int si = scene_->findText(cur);
	scene_->setCurrentIndex(si < 0 ? 0 : si);
	if (game_->count() == 0)
		gameHint_->setText("No video source in OBS yet. Press the button and one is added for you.");
	else if (gameHint_->text().isEmpty())
		gameHint_->setText(
			"Game Capture or a capture card is the usual answer. On a two-PC setup pick the input that carries the gameplay.");
}

QWizardPage *SetupWizard::pageSquad()
{
	auto *p = new QWizardPage(this);
	p->setTitle("Your squad");
	p->setSubTitle("Whose POV viewers see while you are down: the squad mates streaming in your Discord call.");
	auto *v = new QVBoxLayout(p);
	v->addWidget(
		note("<b>Each session, for each squad mate:</b>"
		     "<ol style=\"margin-top:2px\">"
		     "<li>In Discord, right-click their stream and choose <b>Pop Out</b>.</li>"
		     "<li>Right-click it again and <b>Mute</b> it, or their game sound goes out on your stream.</li>"
		     "<li>Press <b>Add pop-outs</b> on the dock.</li>"
		     "</ol>"
		     "Keep pop-outs open but never minimised: a minimised window stops drawing and their "
		     "picture freezes. The dock warns you if one is.",
		     p));
	auto *form = new QFormLayout();
	me_ = new QLineEdit(QString::fromStdString(e_->cfg.myDiscord), p);
	me_->setPlaceholderText("the lower-case one under your display name");
	auto *meRow = new QHBoxLayout();
	meRow->addWidget(me_, 1);
	auto *detect = new QPushButton("Detect", p);
	detect->setToolTip("Ask the Discord app on this PC who it is logged in as.");
	meRow->addWidget(detect);
	form->addRow("Your Discord username", meRow);
	v->addLayout(form);
	connect(detect, &QPushButton::clicked, this, [this]() { e_->detectDiscordUser(true); });
	connect(e_, &Engine::discordUserDetected, this, [this](const QString &u, bool byHand) {
		if (!u.isEmpty() && (byHand || me_->text().trimmed().isEmpty()))
			me_->setText(u);
	});
	if (me_->text().trimmed().isEmpty())
		e_->detectDiscordUser(false);
	rosterOn_ = new QCheckBox("Add squad mates by themselves when they go live in my voice channel", p);
	rosterOn_->setChecked(true);
	rosterOn_->setToolTip(
		"The Kennel Ops bot in the Kennel.gg Discord publishes who is in voice and who is "
		"streaming. With this on the plugin follows your channel, shows only people who are live, "
		"and drops anyone who leaves.");
	v->addWidget(rosterOn_);
	v->addWidget(note("Squad automation is for members of the <b>Kennel.gg Discord</b>: the Kennel Ops bot "
			  "checks your username there, then follows whichever of its voice channels you are in. "
			  "Not a member yet? <a href=\"" +
				  e_->discordUrl() +
				  "\">Join here</a>. Without it, Add pop-outs still works in any Discord server.",
			  p));
	v->addWidget(note("Squad mates on Twitch, Kick, YouTube or VDO.Ninja: Settings, Squad &amp; POV, Add.", p));
	squad_ = new QListWidget(p);
	squad_->setMaximumHeight(90);
	v->addWidget(squad_);
	v->addStretch(1);
	connect(e_, &Engine::stateChanged, this, [this]() { fillSquad(); });
	return p;
}

void SetupWizard::fillSquad()
{
	squad_->clear();
	for (size_t i = 0; i < e_->cfg.friends.size(); i++) {
		auto &f = e_->cfg.friends[i];
		QString kind = f.kind == FriendKind::Twitch     ? "Twitch"
			       : f.kind == FriendKind::Kick     ? "Kick"
			       : f.kind == FriendKind::YouTube  ? "YouTube"
			       : f.kind == FriendKind::VdoNinja ? "VDO.Ninja"
			       : f.kind == FriendKind::Discord  ? "Discord"
								: "OBS source";
		squad_->addItem(QString::fromStdString(f.name) + "  ·  " + kind);
	}
	if (squad_->count() == 0)
		squad_->addItem("(nobody yet: that is normal, they come from pop-outs as you play)");
}

QWizardPage *SetupWizard::pageClips()
{
	auto *p = new QWizardPage(this);
	p->setTitle("Clips");
	p->setSubTitle("Clips come out of OBS's replay buffer, which is started for you.");
	auto *f = new QFormLayout(p);
	replaySecs_ = new QSpinBox(p);
	replaySecs_->setRange(5, 300);
	replaySecs_->setSuffix(" s");
	replaySecs_->setValue(e_->cfg.replaySeconds);
	f->addRow("Each clip reaches back", replaySecs_);
	f->addRow(note("This is OBS's own replay-buffer length. 45 s is plenty for a fight.", p));
	clipDowned_ = new QCheckBox("Save a clip whenever I get downed", p);
	clipDowned_->setChecked(e_->cfg.clipOnDowned);
	f->addRow(clipDowned_);
	bool appInstalled = QFileInfo::exists("C:/ProgramData/Kennel.gg/ClipHound/ClipHound.exe") ||
			    !e_->cfg.appPath.empty();
	launchApp_ = new QCheckBox("Start ClipHound with OBS", p);
	launchApp_->setChecked(appInstalled && (e_->cfg.launchApp || e_->cfg.appPath.empty()));
	launchApp_->setEnabled(appInstalled);
	f->addRow(launchApp_);
	f->addRow(note(
		appInstalled ? "ClipHound runs in the background: it clips your notable kills from the kill feed, "
			       "reads the NEARBY list for Closest, and listens for voice commands if you turn them on."
			     : "ClipHound was not installed. Run the installer again and tick it for kill-feed "
			       "clips, Closest and voice.",
		p));
	f->addRow(note("The dock's <b>Save clip</b> button and the hotkey \"Kennel.gg Wardogs: save a clip now\" "
		       "save one by hand.",
		       p));
	return p;
}

QWizardPage *SetupWizard::pageCheck()
{
	auto *p = new QWizardPage(this);
	p->setTitle("Check");
	p->setSubTitle("What the plugin sees right now. Anything red has a button that fixes it.");
	auto *v = new QVBoxLayout(p);
	auto *box = new QWidget(p);
	checks_ = new QGridLayout(box);
	checks_->setContentsMargins(0, 0, 0, 0);
	checks_->setHorizontalSpacing(10);
	checks_->setColumnStretch(1, 1);
	v->addWidget(box);
	checkSummary_ = note("", p);
	v->addWidget(checkSummary_);

	auto *extras = new QLabel("<b>Optional extras</b>", p);
	v->addSpacing(8);
	v->addWidget(extras);
	auto *eg = new QGridLayout();
	eg->setColumnStretch(0, 1);
	auto extra = [&](int row, const QString &text, QPushButton *btn) {
		eg->addWidget(note(text, p), row, 0);
		eg->addWidget(btn, row, 1, Qt::AlignTop);
	};
	voiceBtn_ = new QPushButton(p);
	extra(0,
	      "<b>Voice control</b> (beta, English): say \"hey kennel, clip that\", \"instant replay\", \"show "
	      "gazreyn\". Listens to your OBS mic through ClipHound; nothing is recorded.",
	      voiceBtn_);
	auto *vert = new QPushButton("Set it up...", p);
	extra(1,
	      "<b>Vertical canvas</b> (beta): the swap and the instant replay on a portrait canvas as well (Aitum "
	      "Stream Suite).",
	      vert);
	auto *deck = new QPushButton("Get it...", p);
	extra(2, "<b>Stream Deck plugin</b>: keys for each squad mate, cycle, clip, replay, voice and Dual POV.", deck);
	v->addLayout(eg);
	v->addStretch(1);
	auto showVoice = [this]() {
		voiceBtn_->setText(e_->cfg.voiceEnabled ? "On - turn off" : "Turn on");
	};
	showVoice();
	connect(voiceBtn_, &QPushButton::clicked, this, [this, showVoice]() {
		e_->cfg.voiceEnabled = !e_->cfg.voiceEnabled;
		e_->cfg.save();
		e_->applyVoice();
		e_->log(e_->cfg.voiceEnabled ? "Voice control on (from Setup)." : "Voice control off (from Setup).");
		showVoice();
		fillChecks();
	});
	connect(vert, &QPushButton::clicked, this, [this]() { emit openSettingsPage("vertical"); });
	connect(deck, &QPushButton::clicked, this,
		[]() { QDesktopServices::openUrl(QUrl("https://kennel.gg/obs/#streamdeck")); });
	return p;
}

void SetupWizard::fillChecks()
{
	if (!checks_)
		return;
	// rebuilt only when something changed, so a button is never pulled from under the mouse
	QString key;
	for (const auto &h : e_->health())
		key += h.key + QString::number(h.level) + h.why + "\n";
	QWidget *holder = checks_->parentWidget();
	if (holder->property("key").toString() == key)
		return;
	holder->setProperty("key", key);
	while (QLayoutItem *it = checks_->takeAt(0)) {
		if (it->widget())
			it->widget()->deleteLater();
		delete it;
	}
	static const char *colour[] = {"#8f9c5a", "#c99a3b", "#ce6050", "#7c8076"};
	int row = 0, bad = 0;
	for (const auto &h : e_->health()) {
		int lvl = std::clamp(h.level, 0, 3);
		auto *dot =
			new QLabel(QString("<span style=\"color:%1; font-size:14pt\">\u25cf</span>").arg(colour[lvl]));
		auto *txt = new QLabel("<b>" + h.label.toHtmlEscaped() + "</b>  " + h.why.toHtmlEscaped());
		txt->setWordWrap(true);
		checks_->addWidget(dot, row, 0, Qt::AlignTop);
		checks_->addWidget(txt, row, 1);
		if (!h.fixes.isEmpty() && lvl != 0) {
			auto *b = new QPushButton(h.fixes.first().second);
			QString id = h.fixes.first().first;
			connect(b, &QPushButton::clicked, this, [this, id]() { runFix(id); });
			checks_->addWidget(b, row, 2, Qt::AlignTop);
		}
		if (lvl == 2)
			bad++;
		row++;
	}
	checkSummary_->setText(
		bad ? "Fix what is red, then press Finish. You can also finish now and use the dock: the same dots "
		      "and buttons are there."
		    : "All set. Press Finish, then <b>get downed once</b> with WARDOGS on screen: the dock's pill "
		      "turns red and your stream shows the squad mate until you are revived.");
}

void SetupWizard::runFix(const QString &id)
{
	if (e_->runAction(id)) {
		fillChecks();
		return;
	}
	if (id.startsWith("settings:"))
		emit openSettingsPage(id.mid(9));
	else if (id == "logs")
		emit openSettingsPage("logs");
	else if (id == "discord:type" || id == "squad")
		setCurrentId(PageSquad);
	else if (id == "wizard")
		restart();
}

void SetupWizard::commit()
{
	Config &c = e_->cfg;
	c.appPlayerName = gameName_->text().trimmed().toStdString();
	if (c.playerName.empty())
		c.playerName = c.appPlayerName; // the name shown to others, unless set otherwise in Settings
	std::string lang = lang_->currentData().toString().toStdString();
	bool langChanged = lang != c.gameLang;
	c.gameLang = lang;
	if (langChanged)
		c.gameLangFound.clear();
	c.lookName = lookName_->isChecked();
	if (game_->currentIndex() >= 0)
		c.gameSource = game_->currentText().toStdString();
	if (scene_ && scene_->currentIndex() >= 0)
		c.sceneName = scene_->currentText().toStdString();
	c.myDiscord = me_->text().trimmed().toLower().remove('@').toStdString();
	c.rosterEnabled = rosterOn_->isChecked();
	c.clipOnDowned = clipDowned_->isChecked();
	c.launchApp = launchApp_->isChecked();
	if (c.launchApp && c.appPath.empty())
		c.appPath = "C:/ProgramData/Kennel.gg/ClipHound/ClipHound.exe";
	c.save();
	if (langChanged)
		e_->loadTemplates();
	if (replaySecs_->value() != c.replaySeconds)
		e_->setReplaySecondsByUser(replaySecs_->value());
	e_->reloadConfig();
	e_->pushAppConfig();
	if (c.launchApp && e_->appState() == "stopped")
		e_->launchApp();
	if (c.clipUseReplay && c.autoStartReplay && !obs_frontend_replay_buffer_active())
		obs_frontend_replay_buffer_start();
}

void SetupWizard::accept()
{
	commit();
	e_->cfg.setupDone = true;
	e_->cfg.save();
	e_->log("Setup done. Get downed once to see it work.");
	emit e_->stateChanged();
	QWizard::accept();
}
