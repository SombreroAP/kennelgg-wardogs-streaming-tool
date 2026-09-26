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
#include <QPixmap>
#include "ui/quick-add.h"

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
		e_->wantPreview(id == PageGame);
		if (id == PageGame)
			previewTick_.start();
		else
			previewTick_.stop();
		if (id == PageSquad)
			fillSquad();
		if (id == PageCheck) {
			commit(); // the check is of what was just set, not of what was there before
			fillChecks();
			checkTick_.start();
		} else
			checkTick_.stop();
	});
	previewTick_.setInterval(1000);
	connect(&previewTick_, &QTimer::timeout, this, &SetupWizard::showPreview);
	connect(this, &QDialog::finished, this, [this](int) { e_->wantPreview(false); });
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
	for (const auto &l : Engine::gameLanguages())
		lang_->addItem(l.second, QString::fromStdString(l.first));
	int li = lang_->findData(QString::fromStdString(e_->cfg.gameLang));
	lang_->setCurrentIndex(li < 0 ? 0 : li);
	f->addRow("Game language", lang_);
	f->addRow(note("Weapon names on your HUD are read in all of the game's languages. The downed screen is "
		       "recognised in English, Spanish and French so far; in another language, pick it here and "
		       "open a ticket in the Kennel.gg Discord to have its downed screen added.",
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
	auto *ph = new QLabel("<b>What the plugin sees</b> - you should see WARDOGS here:", p);
	v->addWidget(ph);
	preview_ = new QLabel(p);
	preview_->setFixedSize(320, 180);
	preview_->setAlignment(Qt::AlignCenter);
	preview_->setStyleSheet("background:#111; color:#888; border:1px solid #333;");
	v->addWidget(preview_);
	previewNote_ = note("", p);
	v->addWidget(previewNote_);
	v->addStretch(1);
	// the preview follows the source picked here at once, before Next saves it
	connect(game_, &QComboBox::currentTextChanged, this, [this](const QString &t) {
		if (!t.isEmpty())
			e_->cfg.gameSource = t.toStdString();
	});
	connect(mk, &QPushButton::clicked, this, [this]() {
		std::string e = e_->sw.createGameCapture(e_->cfg);
		gameHint_->setText(e.empty() ? "Added a Game Capture (any fullscreen game) at the bottom of your scene."
					     : QString::fromStdString(e));
		fillGame();
	});
	return p;
}

void SetupWizard::showPreview()
{
	if (!preview_)
		return;
	QImage img = e_->lastFrame();
	if (img.isNull()) {
		preview_->setText("waiting for a frame...");
		previewNote_->setText(game_->count() ? "" : "No source to look at yet.");
		return;
	}
	preview_->setPixmap(
		QPixmap::fromImage(img.scaled(preview_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
	// mostly black: the game is not running, or the source is not capturing it
	QImage small = img.scaled(64, 36).convertToFormat(QImage::Format_Grayscale8);
	long sum = 0;
	for (int y = 0; y < small.height(); ++y) {
		const uchar *row = small.constScanLine(y);
		for (int x = 0; x < small.width(); ++x)
			sum += row[x];
	}
	double mean = (double)sum / (small.width() * small.height());
	previewNote_->setText(
		mean < 6 ? "<span style=\"color:#ce6050\">Black.</span> Start WARDOGS, or pick another source. A Game "
			   "Capture set to \"any fullscreen application\" picks the game up once you click into it."
			 : "If that is WARDOGS, press Next. If it is something else, pick another source above.");
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
	p->setSubTitle("Whose POV goes on your stream while you are down. Add them whichever way they stream to you.");
	auto *v = new QVBoxLayout(p);

	// A. Discord
	v->addWidget(new QLabel("<b>A. Squad mates sharing their screen in Discord</b>", p));
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
	rosterOn_ = new QCheckBox("In a Kennel.gg voice channel: add whoever goes live there by themselves", p);
	rosterOn_->setChecked(true);
	rosterOn_->setToolTip("The Kennel Ops bot in the Kennel.gg Discord publishes who is in voice and who is "
			      "streaming. With this on the plugin follows your channel and offers only people who "
			      "are live.");
	v->addWidget(rosterOn_);
	v->addWidget(note("Not a member of the Kennel.gg Discord yet? <a href=\"" + e_->discordUrl() +
				  "\">Join here</a>. In any other server: in Discord, right-click their stream, "
				  "<b>Pop Out</b>, right-click it again and <b>Mute</b> it, then press:",
			  p));
	auto *popRow = new QHBoxLayout();
	auto *pop = new QPushButton("Add the pop-outs open now", p);
	popRow->addWidget(pop);
	popResult_ = note("", p);
	popRow->addWidget(popResult_, 1);
	v->addLayout(popRow);
	connect(pop, &QPushButton::clicked, this, [this]() {
		QStringList added;
		QString what = e_->addPopouts(&added);
		popResult_->setText(added.isEmpty() ? (what.isEmpty() ? "No popped-out stream found. Pop one out in "
									"Discord first (not minimised)."
								      : what.toHtmlEscaped())
						    : "Added " + added.join(", ").toHtmlEscaped() + ".");
		fillSquad();
	});

	// B. everything else
	v->addSpacing(8);
	v->addWidget(new QLabel("<b>B. Squad mates streaming on Twitch, Kick or YouTube, or sending a VDO.Ninja "
				"link</b>",
				p));
	v->addWidget(note("Paste their channel link, or type their channel name (several at once is fine). "
			  "Nothing for them to set up; VDO.Ninja is quickest (under half a second) and gives you a "
			  "link to send them.",
			  p));
	auto *quick = new QuickAdd(e_, p);
	v->addWidget(quick);
	connect(quick, &QuickAdd::added, this, [this]() { fillSquad(); });

	v->addSpacing(8);
	v->addWidget(new QLabel("<b>Your squad now</b>", p));
	squad_ = new QListWidget(p);
	squad_->setMaximumHeight(110);
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
		QString st = e_->feedStateText(f);
		squad_->addItem(QString::fromStdString(f.name) + "  ·  " + kind + (st.isEmpty() ? "" : "  ·  " + st));
	}
	if (squad_->count() == 0)
		squad_->addItem("(nobody yet - fine if they share in a Kennel.gg voice channel: they are added as they "
				"go live)");
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
	bool appInstalled = QFileInfo::exists(Engine::defaultAppPath()) || !e_->cfg.appPath.empty();
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
	clipTest_ = new QPushButton("Save a test clip now", p);
	clipResult_ = note("", p);
	auto *tr = new QHBoxLayout();
	tr->addWidget(clipTest_);
	tr->addWidget(clipResult_, 1);
	f->addRow(tr);
	connect(clipTest_, &QPushButton::clicked, this, [this]() {
		if (replaySecs_->value() != e_->cfg.replaySeconds)
			e_->setReplaySecondsByUser(replaySecs_->value());
		if (!obs_frontend_replay_buffer_active()) {
			obs_frontend_replay_buffer_start();
			clipResult_->setText("Starting OBS's replay buffer... press again in five seconds.");
			return;
		}
		QString err = e_->clips.request("setup test", {"test"}, "manual");
		clipResult_->setText(err.isEmpty() ? "Saving..." : err.toHtmlEscaped());
	});
	connect(&e_->clips, &Clips::saved, this, [this](const Clips::Entry &en) {
		if (clipResult_ && en.title == "setup test")
			clipResult_->setText("<span style=\"color:#8f9c5a\">Saved</span> " +
					     QFileInfo(en.path).fileName().toHtmlEscaped() + "<br>in " +
					     QFileInfo(en.path).absolutePath().toHtmlEscaped());
	});
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

	statsShare_ = new QCheckBox("Share my session stats with kennel.gg for the public leaderboards", p);
	statsShare_->setChecked(e_->cfg.statsConsent == 1);
	v->addSpacing(6);
	v->addWidget(statsShare_);
	v->addWidget(note(
		"Ticked, Finish opens kennel.gg to link this PC to your kennel.gg account (the same one as for "
		"wagers and the Cash Cup). At the end of each stream your account then gets the session's kills, "
		"deaths, assists, revives, headshots, vehicles, downs, money earned and spent, time in game, longest "
		"kill and top weapon. Never your clips, video, voice or chat. Off unless you tick it; stop and "
		"delete any time in Settings.",
		p));
	auto *extras = new QLabel("<b>Optional extras</b>", p);
	v->addSpacing(8);
	v->addWidget(extras);
	auto *eg = new QGridLayout();
	eg->setColumnStretch(0, 1);
	auto extra = [&](int row, const QString &text, QPushButton *btn) {
		eg->addWidget(note(text, p), row, 0);
		eg->addWidget(btn, row, 1, Qt::AlignTop);
	};
	swapTest_ = new QPushButton("Show them for 5 s", p);
	extra(0,
	      "<b>Try the swap</b>: puts your squad mate's POV on your stream for five seconds, exactly as when "
	      "you are downed, then your own comes back. Viewers see it if you are live.",
	      swapTest_);
	swapResult_ = note("", p);
	eg->addWidget(swapResult_, 1, 0, 1, 2);
	connect(swapTest_, &QPushButton::clicked, this, [this]() {
		if (!e_->cfg.active()) {
			swapResult_->setText("Add a squad mate on the Squad page first.");
			return;
		}
		if (e_->applied())
			return;
		QString who = QString::fromStdString(e_->cfg.active()->name);
		e_->applyNow(true, "setup test");
		swapResult_->setText("Showing " + who.toHtmlEscaped() +
				     " now. Look at OBS's preview: their picture should fill your scene.");
		swapTest_->setEnabled(false);
		QTimer::singleShot(5000, this, [this]() {
			if (e_->applied())
				e_->applyNow(false, "setup test over");
			swapTest_->setEnabled(true);
			swapResult_->setText("Back to your POV. Did their picture show? If it was black, their stream "
					     "has not started or their pop-out is minimised.");
		});
	});
	voiceBtn_ = new QPushButton(p);
	extra(2,
	      "<b>Voice control</b> (beta, English): say \"hey kennel, clip that\", \"instant replay\", \"show "
	      "gazreyn\". Listens to your OBS mic through ClipHound; nothing is recorded.",
	      voiceBtn_);
	auto *vert = new QPushButton("Set it up...", p);
	extra(3,
	      "<b>Vertical canvas</b> (beta): the swap and the instant replay on a portrait canvas as well (Aitum "
	      "Stream Suite).",
	      vert);
	auto *deck = new QPushButton("Get it...", p);
	extra(4, "<b>Stream Deck plugin</b>: keys for each squad mate, cycle, clip, replay, voice and Dual POV.", deck);
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
		[]() { QDesktopServices::openUrl(QUrl("https://kennel.gg/streaming/#streamdeck")); });
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
		c.appPath = Engine::defaultAppPath().toStdString();
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
	// finishing Setup is the answer: ticked is yes, left unticked is no (Settings can change it)
	if (statsShare_ && (statsShare_->isChecked() ? 1 : 2) != e_->cfg.statsConsent)
		e_->setStatsConsent(statsShare_->isChecked());
	e_->cfg.setupDone = true;
	e_->cfg.save();
	e_->log("Setup done. Get downed once to see it work.");
	emit e_->stateChanged();
	QWizard::accept();
}
