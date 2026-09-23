#include "ui/dock.h"
#include "ui/clips-dialog.h"
#include "ui/settings-dialog.h"
#include "ui/wizard.h"
#include "ui/squad.h"
#include "ui/flow-layout.h"
#include <QVBoxLayout>
#include <functional>
#include <QHBoxLayout>
#include <QDialog>
#include <QMenu>
#include <QTimer>
#include <QMessageBox>
#include <QStyle>
#include <QPixmap>
#include <QInputDialog>
#include <QScreen>
#include <QGuiApplication>
#include <QPlainTextEdit>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <obs-module.h>
#include <plugin-support.h>
#include <QFileInfo>
#include <obs-frontend-api.h>

/// Show a window fully inside the screen OBS is on: centred on OBS but never with its title bar off the top or
/// its edges past the screen (a tall OBS window pushed dialogs above the screen, making them impossible to grab).
static void showOnScreen(QWidget *w)
{
	QWidget *main = (QWidget *)obs_frontend_get_main_window();
	QScreen *scr = main && main->screen() ? main->screen() : QGuiApplication::primaryScreen();
	QRect avail = scr ? scr->availableGeometry() : QRect(0, 0, 1920, 1080);
	// a never-shown window reports a default 640x480, not what its layout needs: start from the
	// layout's own size, at least a comfortable size, and only then clamp to the screen
	QSize sz = w->sizeHint().expandedTo(QSize(1000, 820)).expandedTo(w->size());
	sz.setWidth(std::min(sz.width(), avail.width() - 40));
	sz.setHeight(std::min(sz.height(), avail.height() - 40));
	QPoint c = main ? main->frameGeometry().center() : avail.center();
	int x = std::clamp(c.x() - sz.width() / 2, avail.left() + 20,
			   std::max(avail.left() + 20, avail.right() - sz.width() - 20));
	int y = std::clamp(c.y() - sz.height() / 2, avail.top() + 20,
			   std::max(avail.top() + 20, avail.bottom() - sz.height() - 20));
	w->resize(sz);
	w->move(x, y);
	w->show();
	w->raise();
	w->activateWindow();
	// the first paint used geometry from before the resize (everything squeezed to a few pixels
	// until the user resized the window by hand): run the layout again once the window is up
	QTimer::singleShot(0, w, [w]() {
		if (w->layout())
			w->layout()->activate();
		w->updateGeometry();
		QSize s = w->size();
		w->resize(s + QSize(1, 1));
		w->resize(s);
	});
}

/// The dock's look: the brand's graphite, olive, amber and bone, scoped to this widget so OBS's own
/// theme is left alone. Buttons share one height and one edge; the status line is a pill whose
/// colour is the state; section labels are the condensed face in small caps.
/// The dock's look: the brand's graphite, olive, amber and bone, scoped to this widget so OBS's own
/// theme is left alone. Buttons share one height and one edge; the status line is a pill whose
/// colour is the state; section labels are the condensed face in small caps; the health strip's
/// dots and the banners take their colour from how bad things are.
static const char *kDockStyle = R"(
#kennelDock { background: #1c1f1d; }
#kennelDock QLabel { color: #e6e2d6; }
#kennelDock QLabel#eyebrow { color: #c99a3b; font-family: "Saira Condensed"; font-size: 12pt; font-weight: 700;
	letter-spacing: 2px; padding: 8px 0 2px 0; }
#kennelDock QWidget#headRow { border-bottom: 1px solid #343835; margin-bottom: 2px; }
#kennelDock QLabel#wordmark { color: #ece7db; font-family: "Saira Condensed"; font-size: 17pt; font-weight: 700;
	letter-spacing: 1px; }
#kennelDock QLabel#small { color: #9a9e93; font-size: 8pt; }
#kennelDock QLabel#statePill { padding: 5px 10px; border-radius: 3px; border: 1px solid #3a3e3b; background: #242725;
	font-weight: 600; }
#kennelDock QLabel#statePill[mode="watching"] { border-color: #6f7c45; color: #cbd3a4; }
#kennelDock QLabel#statePill[mode="showing"] { border-color: #ce6050; background: #3a2521; color: #f2c9c1; }
#kennelDock QLabel#statePill[mode="off"] { border-color: #3a3e3b; color: #9a9e93; }
#kennelDock QPushButton { min-height: 24px; padding: 2px 10px; border: 1px solid #3a3e3b; border-radius: 3px;
	background: #262927; color: #e6e2d6; }
#kennelDock QPushButton:hover { background: #2f3330; border-color: #4a4f4b; }
#kennelDock QPushButton:pressed { background: #202321; }
#kennelDock QPushButton:checked { border-color: #c99a3b; background: #2e2a1f; color: #f0d9a8; font-weight: 600; }
#kennelDock QPushButton:disabled { color: #6c7068; border-color: #2e3230; }
#kennelDock QPushButton#person[armed="true"] { border: 1px dashed #c99a3b; }
#kennelDock QPushButton#person[live="true"] { color: #d9e0b6; }
#kennelDock QPushButton#person:checked { border-color: #ce6050; background: #3a2521; color: #f2c9c1; }
#kennelDock QPushButton#person[faded="true"] { color: #7c8076; }
#kennelDock QToolButton { border: 1px solid #3a3e3b; border-radius: 3px; background: #262927; color: #e6e2d6;
	padding: 2px 6px; min-height: 22px; }
#kennelDock QToolButton#dot { border: none; background: transparent; color: #c9c5b8; padding: 0px 2px; min-height: 0px;
	font-family: "IBM Plex Mono"; font-size: 8pt; }
#kennelDock QToolButton#dot:hover { color: #ece7db; text-decoration: underline; }
#kennelDock QToolButton#dot[level="0"] { color: #b9c48a; }
#kennelDock QToolButton#dot[level="1"] { color: #e0b45c; }
#kennelDock QToolButton#dot[level="2"] { color: #ef8a78; font-weight: 700; }
#kennelDock QToolButton#dot[level="3"] { color: #6c7068; }
#kennelDock QFrame#banner { border-radius: 3px; border: 1px solid #3a3e3b; background: #232624; }
#kennelDock QFrame#banner[level="1"] { border-color: #7a6130; background: #2c2719; }
#kennelDock QFrame#banner[level="2"] { border-color: #6b3a30; background: #3a2622; }
#kennelDock QFrame#banner QLabel { color: #e6e2d6; font-size: 8.5pt; }
#kennelDock QFrame#banner QPushButton { min-height: 20px; padding: 1px 8px; font-size: 8.5pt; }
#kennelDock QFrame#banner QToolButton { border: none; background: transparent; color: #9a9e93; }
#kennelDock QCheckBox { color: #c9c5b8; spacing: 4px; font-size: 8.5pt; }
#kennelDock QListWidget { border: 1px solid #2e3230; border-radius: 3px; background: #202321; color: #c9c5b8;
	font-family: "IBM Plex Mono"; font-size: 8pt; }
#kennelDock QToolButton#menuBtn { border: none; background: transparent; font-size: 14pt; padding: 0 6px; }
#kennelDock QProgressBar#voiceBar { border: none; background: #2a2e2b; max-height: 4px; border-radius: 2px; }
#kennelDock QProgressBar#voiceBar::chunk { background: #8f9c5a; border-radius: 2px; }
)";

static QLabel *eyebrow(const QString &text, QWidget *parent)
{
	auto *l = new QLabel(text.toUpper(), parent);
	l->setObjectName("eyebrow");
	return l;
}

static void repolish(QWidget *w, const char *prop, const QVariant &v)
{
	if (w->property(prop) == v)
		return;
	w->setProperty(prop, v);
	w->style()->unpolish(w);
	w->style()->polish(w);
}

/// An eyebrow with a tick box on the right of it: the section heading carries its automatic mode.
static QWidget *headRow(QLabel *label, QCheckBox *box, QWidget *parent)
{
	auto *w = new QWidget(parent);
	w->setObjectName("headRow");
	auto *h = new QHBoxLayout(w);
	h->setContentsMargins(0, 0, 0, 0);
	h->addWidget(label);
	h->addStretch(1);
	if (box)
		h->addWidget(box, 0, Qt::AlignBottom);
	return w;
}

Dock::Dock(Engine *engine, QWidget *parent) : QWidget(parent), e_(engine)
{
	setObjectName("kennelDock");
	setStyleSheet(kDockStyle);
	auto *v = new QVBoxLayout(this);
	v->setContentsMargins(10, 8, 10, 8);
	v->setSpacing(5);

	// ----- header: the hound, the wordmark, and the menu everything occasional lives in
	{
		auto *head = new QHBoxLayout();
		head->setSpacing(8);
		auto *mark = new QLabel(this);
		char *p = obs_module_file("brand/hound_mark.png");
		if (p) {
			QPixmap px(QString::fromUtf8(p));
			bfree(p);
			if (!px.isNull())
				mark->setPixmap(px.scaledToHeight(26, Qt::SmoothTransformation));
		}
		head->addWidget(mark);
		auto *wm = new QLabel("KENNEL.GG WARDOGS", this);
		wm->setObjectName("wordmark");
		head->addWidget(wm);
		head->addStretch(1);
		menuBtn_ = new QToolButton(this);
		menuBtn_->setObjectName("menuBtn");
		menuBtn_->setText(QString::fromUtf8("⋯"));
		menuBtn_->setToolTip(QString("Squad, Setup, Settings, logs and more  ·  v%1").arg(PLUGIN_VERSION));
		menuBtn_->setPopupMode(QToolButton::InstantPopup);
		auto *m = new QMenu(menuBtn_);
		m->addAction("Squad...", this, [this]() { openSquad(); });
		m->addAction("Settings...", this, [this]() { openSettings(); });
		m->addAction("Setup...", this, [this]() { openWizard(); });
		m->addAction("Logs...", this, [this]() { openLogs(); });
		m->addAction("Clips: titles and tags...", this, [this]() { openClips(); });
		m->addSeparator();
		appAct_ = m->addAction("Start ClipHound", this, [this]() {
			QString st = e_->appState();
			if (st == "connected" || st == "starting")
				e_->stopApp();
			else
				e_->launchApp();
			refresh();
		});
		m->addSeparator();
		compactAct_ = m->addAction("Compact dock");
		compactAct_->setCheckable(true);
		compactAct_->setToolTip(
			"Only what is used mid-match: the people, the clip buttons and anything wrong.");
		connect(compactAct_, &QAction::toggled, this, [this](bool on) {
			e_->cfg.dockCompact = on;
			e_->cfg.save();
			refresh();
		});
		compactLiveAct_ = m->addAction("Compact while streaming or recording");
		compactLiveAct_->setCheckable(true);
		connect(compactLiveAct_, &QAction::toggled, this, [this](bool on) {
			e_->cfg.dockCompactLive = on;
			e_->cfg.save();
			refresh();
		});
		m->addSeparator();
		m->addAction("Kennel.gg Discord", this,
			     [this]() { QDesktopServices::openUrl(QUrl(e_->discordUrl())); });
		auto *ver = m->addAction(QString("Kennel.gg Wardogs Streaming Tool %1").arg(PLUGIN_VERSION));
		ver->setEnabled(false);
		menuBtn_->setMenu(m);
		head->addWidget(menuBtn_);
		v->addLayout(head);
	}

	state_ = new QLabel(this);
	state_->setObjectName("statePill");
	state_->setAlignment(Qt::AlignCenter);
	v->addWidget(state_);

	// ----- the health strip: one dot per part of the setup, green to red, a click fixes it
	{
		auto *strip = new QWidget(this);
		auto *fl = new FlowLayout(strip, 6);
		for (const char *k : {"game", "scene", "cliphound", "discord", "replay", "voice"}) {
			auto *b = new QToolButton(strip);
			b->setObjectName("dot");
			b->setCursor(Qt::PointingHandCursor);
			b->hide();
			fl->addWidget(b);
			dots_.insert(k, b);
			QString key = k;
			connect(b, &QToolButton::clicked, this, [this, key]() {
				for (const auto &h : e_->health())
					if (h.key == key) {
						if (!h.fixes.isEmpty())
							runFix(h.fixes.first().first);
						else if (key == "voice")
							openSettings("voice");
						else if (key == "discord")
							openSquad();
						else if (key == "cliphound" || key == "replay")
							openSettings("clips");
						else
							openSettings("general");
					}
			});
		}
		v->addWidget(strip);
	}
	banners_ = new QVBoxLayout();
	banners_->setSpacing(4);
	v->addLayout(banners_);

	// ----- voice, when it is on: how loud the mic is and what came of the last thing said
	{
		voiceRow_ = new QWidget(this);
		auto *vl = new QVBoxLayout(voiceRow_);
		vl->setContentsMargins(0, 2, 0, 0);
		vl->setSpacing(2);
		voiceBar_ = new QProgressBar(voiceRow_);
		voiceBar_->setObjectName("voiceBar");
		voiceBar_->setRange(0, 100);
		voiceBar_->setTextVisible(false);
		voiceBar_->setToolTip("Your microphone as voice control hears it.");
		voiceLbl_ = new QLabel(voiceRow_);
		voiceLbl_->setObjectName("small");
		voiceLbl_->setWordWrap(true);
		vl->addWidget(voiceBar_);
		vl->addWidget(voiceLbl_);
		v->addWidget(voiceRow_);
		connect(e_, &Engine::voiceLevel, this, [this](double db) {
			voiceBar_->setValue(std::clamp((int)((db + 60.0) * 100.0 / 60.0), 0, 100));
		});
	}

	// ----- who is on screen: me, a squad mate, or whoever is closest; auto switch on the heading
	autoSwitch_ = new QCheckBox("Auto switch", this);
	autoSwitch_->setToolTip("Switch to the squad mate by itself when you get downed, and back when you are "
				"revived. Untick to keep your own POV up no matter what; the buttons still work.");
	connect(autoSwitch_, &QCheckBox::toggled, this, [this](bool on) {
		if (!filling_ && on != e_->cfg.enabled)
			e_->setEnabled(on);
	});
	v->addWidget(headRow(eyebrow("On screen", this), autoSwitch_, this));
	povBox_ = new QWidget(this);
	povFlow_ = new FlowLayout(povBox_, 4);
	v->addWidget(povBox_);
	povEmpty_ = new QLabel(this);
	povEmpty_->setObjectName("small");
	povEmpty_->setWordWrap(true);
	v->addWidget(povEmpty_);
	near_ = new QLabel(this);
	near_->setObjectName("small");
	near_->setWordWrap(true);
	v->addWidget(near_);

	// ----- Dual POV: off or a squad mate in the small window; the vehicle mode on the heading
	dualAuto_ = new QCheckBox("Auto in vehicles", this);
	dualAuto_->setToolTip("Open the Dual POV window by itself when you get in a tank or chopper and close it "
			      "when you get out (needs ClipHound). Untick before a match to keep it from happening.");
	connect(dualAuto_, &QCheckBox::toggled, this, [this](bool on) {
		if (filling_ || on == e_->cfg.dualAuto)
			return;
		e_->cfg.dualAuto = on;
		e_->cfg.save();
		e_->pushAppConfig(); // ClipHound only watches the vehicle corner while this is on
		e_->log(on ? "Dual POV: auto on - the window opens and closes with the vehicle."
			   : "Dual POV: auto off - only the buttons open the window.");
	});
	dualHead_ = eyebrow("Dual POV", this);
	v->addWidget(headRow(dualHead_, dualAuto_, this));
	dualBox_ = new QWidget(this);
	dualFlow_ = new FlowLayout(dualBox_, 4);
	v->addWidget(dualBox_);

	// ----- clips
	v->addWidget(headRow(eyebrow("Clips", this), nullptr, this));
	{
		auto *row = new QHBoxLayout();
		row->setSpacing(4);
		saveBtn_ = new QToolButton(this);
		saveBtn_->setText("Save clip");
		saveBtn_->setToolTip("Save a clip now. The arrow tags it, or adds a note.");
		saveBtn_->setPopupMode(QToolButton::MenuButtonPopup);
		saveBtn_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		auto *saveMenu = new QMenu(saveBtn_);
		saveMenu->addAction("Save clip now", this, [this]() { e_->clipNow("manual", {"manual"}, "dock"); });
		saveMenu->addAction("Save clip tagged 'highlight'", this,
				    [this]() { e_->clipNow("highlight", {"highlight", "manual"}, "dock"); });
		saveMenu->addAction("Save clip tagged 'funny'", this,
				    [this]() { e_->clipNow("funny", {"funny", "manual"}, "dock"); });
		saveMenu->addAction("Save clip tagged 'fail'", this,
				    [this]() { e_->clipNow("fail", {"fail", "manual"}, "dock"); });
		saveMenu->addSeparator();
		saveMenu->addAction("Save clip and add a note...", this, [this]() {
			noteNext_ = true; // the clip is saved now; the note comes when the file has landed
			e_->clipNow("manual", {"manual"}, "dock");
		});
		saveMenu->addAction("Clips: titles and tags...", this, [this]() { openClips(); });
		saveBtn_->setMenu(saveMenu);
		connect(saveBtn_, &QToolButton::clicked, this, [this]() { e_->clipNow("manual", {"manual"}, "dock"); });
		connect(&e_->clips, &Clips::saved, this, [this](const Clips::Entry &e) {
			refresh();
			if (!noteNext_ || !e.tags.contains("manual"))
				return;
			noteNext_ = false;
			auto *d = new ClipNoteDialog(e_, e.path, (QWidget *)obs_frontend_get_main_window());
			d->show();
			d->raise();
			d->activateWindow();
		});
		replay_ = new QPushButton("Instant replay", this);
		replay_->setToolTip("Play the last highlight on the stream, cut to the action. Press again to stop.");
		highlights_ = new QPushButton("Highlights", this);
		highlights_->setToolTip(
			"Play this session's highlights compilation, full screen, under your camera "
			"and alerts. Built first when there is nothing newer than your last clip. Press "
			"again to stop.");
		connect(replay_, &QPushButton::clicked, this, [this]() {
			if (e_->replaying())
				e_->stopReplay("dock");
			else
				e_->playReplay("dock");
		});
		connect(highlights_, &QPushButton::clicked, this, [this]() {
			if (e_->replaying())
				e_->stopReplay("dock");
			else
				e_->playCompilation("dock");
		});
		row->addWidget(saveBtn_, 1);
		row->addWidget(replay_, 1);
		row->addWidget(highlights_, 1);
		v->addLayout(row);
	}
	clip_ = new QLabel(this);
	clip_->setObjectName("small");
	clip_->setWordWrap(true);
	clip_->setTextFormat(Qt::RichText);
	connect(clip_, &QLabel::linkActivated, this, [this](const QString &href) {
		QString path = e_->clips.lastPath();
		if (href == "kennel:rename")
			openClips(path);
		else if (href == "kennel:replay")
			e_->playReplay("dock");
	});
	v->addWidget(clip_);

	// ----- squad: the thing done most often mid-session, then the panel
	{
		squadRow_ = new QWidget(this);
		auto *row = new QHBoxLayout(squadRow_);
		row->setContentsMargins(0, 4, 0, 0);
		row->setSpacing(4);
		addPop_ = new QPushButton("Add pop-outs", squadRow_);
		addPop_->setToolTip("Every popped-out Discord stream becomes a squad mate, named by its Discord "
				    "username. Mute each stream in Discord too: the dock reminds you.");
		showPop_ = new QPushButton("Show pop-outs", squadRow_);
		showPop_->setCheckable(true);
		showPop_->setToolTip("Bring the tucked pop-outs back on screen to mute or adjust them; press again "
				     "to tuck them away.");
		auto *sq = new QPushButton("Squad...", squadRow_);
		sq->setToolTip("Squad mates, their in-game names, and how pop-outs are kept drawing.");
		row->addWidget(addPop_, 1);
		row->addWidget(showPop_);
		row->addWidget(sq);
		v->addWidget(squadRow_);
		connect(addPop_, &QPushButton::clicked, this, &Dock::addPopouts);
		connect(showPop_, &QPushButton::clicked, this, [this](bool on) { e_->showPopouts(on); });
		connect(sq, &QPushButton::clicked, this, &Dock::openSquad);
	}

	// ----- what happened
	eventsHead_ = headRow(eyebrow("Events", this), nullptr, this);
	v->addWidget(eventsHead_);
	events_ = new QListWidget(this);
	events_->setMaximumHeight(120);
	events_->setSelectionMode(QAbstractItemView::NoSelection);
	events_->setFocusPolicy(Qt::NoFocus);
	v->addWidget(events_);
	last_ = new QLabel(this);
	last_->setObjectName("small");
	last_->setWordWrap(true);
	v->addWidget(last_);
	v->addStretch(1);

	connect(e_, &Engine::stateChanged, this, &Dock::refresh);
	connect(&e_->roster, &Roster::changed, this, &Dock::refresh);
	connect(&e_->roster, &Roster::polled, this, &Dock::refresh);
	connect(e_, &Engine::updateChecked, this, &Dock::refresh);
	connect(e_, &Engine::discordUserDetected, this, [this](const QString &u, bool byHand) {
		if (byHand && u.isEmpty())
			runFix("discord:type"); // Discord is not running here: type it instead
	});
	connect(e_, &Engine::frameUpdated, this, [this]() {
		if (e_->revivingRecent())
			state_->setText(QString::fromStdString(e_->stateText()) +
					QString(" (%1%)").arg((int)(std::max(0.0, e_->reviveProgress()) * 100)));
	});
	connect(e_, &Engine::logged, this, [this](const QString &s) { last_->setText(s); });
	// the health strip has time in it (a mic silent for two minutes, a scene switched in OBS)
	tick_.setInterval(2000);
	connect(&tick_, &QTimer::timeout, this, [this]() {
		refreshHealth();
		rebuildBanners();
	});
	tick_.start();
	refresh();
}

bool Dock::compact() const
{
	return e_->cfg.dockCompact || (e_->cfg.dockCompactLive && e_->streamingOrRecording());
}

void Dock::runFix(const QString &id)
{
	if (e_->runAction(id))
		return;
	if (id.startsWith("settings:"))
		openSettings(id.mid(9));
	else if (id == "wizard")
		openWizard();
	else if (id == "logs")
		openLogs();
	else if (id == "squad")
		openSquad();
	else if (id == "discord:type") {
		bool ok = false;
		QString v = QInputDialog::getText(
			this, "Your Discord username",
			"Your Discord username - the lower-case one under your display name. The Kennel.gg bot checks it "
			"is in the server, and then follows whichever voice channel you are in.",
			QLineEdit::Normal, QString::fromStdString(e_->cfg.myDiscord), &ok);
		if (ok)
			e_->setMyDiscord(v);
	}
}

void Dock::addPopouts()
{
	QStringList added;
	QString what = e_->addPopouts(&added);
	last_->setText(what);
	// no questions now: the dock says to mute them, and to check in-game names when Closest needs them
	e_->noteAddedPopouts(added);
}

void Dock::refreshHealth()
{
	bool small = compact();
	QSet<QString> seen;
	for (const auto &h : e_->health()) {
		QToolButton *b = dots_.value(h.key);
		if (!b)
			continue;
		seen.insert(h.key);
		b->setText(QString::fromUtf8("● ") + (small ? QString() : h.label));
		QString tip = "<b>" + h.label.toHtmlEscaped() + "</b>: " + h.why.toHtmlEscaped();
		if (!h.fixes.isEmpty())
			tip += "<br>Click: " + h.fixes.first().second.toHtmlEscaped();
		b->setToolTip(tip);
		repolish(b, "level", h.level);
		b->show();
	}
	for (auto it = dots_.begin(); it != dots_.end(); ++it)
		if (!seen.contains(it.key()))
			it.value()->hide();
}

void Dock::rebuildBanners()
{
	QList<Engine::Banner> list = e_->banners();
	// compact: only what is actually broken
	if (compact()) {
		QList<Engine::Banner> keep;
		for (const auto &b : list)
			if (b.level >= 2)
				keep << b;
		list = keep;
	}
	while (list.size() > 3)
		list.removeLast(); // the worst three; the rest wait their turn
	QStringList keys;
	for (const auto &b : list)
		keys << b.id + "|" + b.text;
	if (keys == bannerKeys_)
		return;
	bannerKeys_ = keys;
	while (QLayoutItem *it = banners_->takeAt(0)) {
		if (it->widget())
			it->widget()->deleteLater();
		delete it;
	}
	for (const auto &b : list) {
		auto *f = new QFrame(this);
		f->setObjectName("banner");
		f->setProperty("level", b.level);
		auto *bl = new QVBoxLayout(f);
		bl->setContentsMargins(8, 5, 5, 6);
		bl->setSpacing(4);
		auto *top = new QHBoxLayout();
		auto *txt = new QLabel(b.text, f);
		txt->setTextFormat(Qt::RichText);
		txt->setWordWrap(true);
		top->addWidget(txt, 1);
		if (b.dismissable) {
			auto *x = new QToolButton(f);
			x->setText(QString::fromUtf8("✕"));
			x->setToolTip("Dismiss");
			QString id = b.id;
			connect(x, &QToolButton::clicked, this, [this, id]() { e_->dismissBanner(id); });
			top->addWidget(x, 0, Qt::AlignTop);
		}
		bl->addLayout(top);
		if (!b.actions.isEmpty()) {
			auto *row = new QHBoxLayout();
			row->setSpacing(4);
			for (const auto &a : b.actions) {
				auto *btn = new QPushButton(a.second, f);
				QString id = a.first;
				connect(btn, &QPushButton::clicked, this, [this, id]() { runFix(id); });
				row->addWidget(btn);
			}
			row->addStretch(1);
			bl->addLayout(row);
		}
		banners_->addWidget(f);
	}
}

/// The squad mates the two rows offer: in a Kennel.gg voice channel, the people live in it;
/// anywhere else everyone but those known not to be streaming.
void Dock::rebuildPeople()
{
	bool rosterLive = e_->rosterLive();
	QList<int> idx;
	QStringList names;
	for (size_t i = 0; i < e_->cfg.friends.size(); ++i) {
		const Friend &f = e_->cfg.friends[i];
		Engine::Feed st = e_->feedState(f);
		if (rosterLive ? st != Engine::Feed::Live : st == Engine::Feed::Off)
			continue;
		idx << (int)i;
		names << QString::fromStdString(f.name);
	}
	QString key = names.join("\n") + (rosterLive ? "|r" : "|");
	for (int i : idx)
		key += QString::number(i) + ",";
	if (key == peopleKey_)
		return;
	peopleKey_ = key;
	peopleIdx_ = idx;
	auto clear = [](FlowLayout *fl, QList<QPushButton *> &list) {
		while (QLayoutItem *it = fl->takeAt(0)) {
			if (it->widget())
				it->widget()->deleteLater();
			delete it;
		}
		list.clear();
	};
	clear(povFlow_, povBtns_);
	clear(dualFlow_, dualBtns_);
	auto make = [this](const QString &text, QWidget *parent) {
		auto *b = new QPushButton(text, parent);
		b->setObjectName("person");
		b->setCheckable(true);
		return b;
	};
	// main view: Me, each squad mate, Closest
	meBtn_ = make("Me", povBox_);
	meBtn_->setToolTip("Your own POV on stream.");
	connect(meBtn_, &QPushButton::clicked, this, [this]() {
		if (e_->applied())
			e_->applyNow(false, "button");
		refresh();
	});
	povFlow_->addWidget(meBtn_);
	for (int k = 0; k < idx.size(); k++) {
		auto *b = make(names[k], povBox_);
		int f = idx[k];
		connect(b, &QPushButton::clicked, this, [this, f]() {
			if (f < 0 || f >= (int)e_->cfg.friends.size())
				return;
			if (e_->cfg.nearEnabled) {
				// picking someone by hand means not following the closest one
				e_->cfg.nearEnabled = false;
				e_->cfg.save();
				e_->reloadConfig();
				e_->log("Following the squad mate you picked (Closest off).");
			}
			if (e_->applied() && e_->cfg.activeFriend == f)
				e_->applyNow(false, "button");
			else {
				e_->setActive(f); // switches on the spot when already showing someone
				if (!e_->applied())
					e_->applyNow(true, "button");
			}
			refresh();
		});
		povFlow_->addWidget(b);
		povBtns_ << b;
	}
	closestBtn_ = make("Closest", povBox_);
	connect(closestBtn_, &QPushButton::clicked, this, [this](bool on) {
		if (on && !e_->appConnected()) {
			// the NEARBY list is read by ClipHound: without it this does nothing, so say so
			e_->noteClosestNeedsApp();
			refresh();
			return;
		}
		e_->cfg.nearEnabled = on;
		e_->cfg.save();
		e_->reloadConfig(); // pushes the names and areas to ClipHound
		e_->log(on ? "Following the closest squad mate (NEARBY list)."
			   : "Following the squad mate you picked.");
		refresh();
	});
	povFlow_->addWidget(closestBtn_);
	// Dual POV: Off, each squad mate
	dualOffBtn_ = make("Off", dualBox_);
	dualOffBtn_->setToolTip("No Dual POV window.");
	connect(dualOffBtn_, &QPushButton::clicked, this, [this]() {
		if (e_->dualOn())
			e_->setDual(false, "dock");
		refresh();
	});
	dualFlow_->addWidget(dualOffBtn_);
	for (int k = 0; k < idx.size(); k++) {
		auto *b = make(names[k], dualBox_);
		int f = idx[k];
		b->setToolTip(names[k] + " in the small Dual POV window, until you press Off.");
		connect(b, &QPushButton::clicked, this, [this, f]() {
			if (f < 0 || f >= (int)e_->cfg.friends.size())
				return;
			if (e_->dualOn() && e_->cfg.dualFriend == f)
				e_->setDual(false, "dock");
			else
				e_->showInDual(f, "dock");
			refresh();
		});
		dualFlow_->addWidget(b);
		dualBtns_ << b;
	}
}

void Dock::refresh()
{
	filling_ = true;
	bool small = compact();
	if (compactAct_) {
		compactAct_->blockSignals(true);
		compactAct_->setChecked(e_->cfg.dockCompact);
		compactAct_->blockSignals(false);
		compactLiveAct_->blockSignals(true);
		compactLiveAct_->setChecked(e_->cfg.dockCompactLive);
		compactLiveAct_->blockSignals(false);
	}
	// ----- the pill
	state_->setText(QString::fromStdString(e_->stateText()));
	repolish(state_, "mode", e_->applied() ? "showing" : !e_->cfg.enabled ? "off" : "watching");

	refreshHealth();
	rebuildBanners();

	// ----- voice
	voiceRow_->setVisible(e_->cfg.voiceEnabled);
	if (e_->cfg.voiceEnabled) {
		QString t;
		if (e_->voiceHeardKind() == 0)
			t = "Voice: say \"hey kennel\" and a command.";
		else
			t = e_->voiceHeardAt().toString("HH:mm") + "  " + e_->voiceHeard();
		voiceLbl_->setText(t);
		voiceLbl_->setStyleSheet(e_->voiceHeardKind() == 2 ? "color: #e0b45c;" : "");
	}

	// ----- the people
	rebuildPeople();
	autoSwitch_->setChecked(e_->cfg.enabled);
	meBtn_->setChecked(!e_->applied());
	for (int k = 0; k < povBtns_.size() && k < peopleIdx_.size(); k++) {
		int f = peopleIdx_[k];
		bool showing = e_->applied() && e_->cfg.activeFriend == f;
		bool armed = !e_->applied() && e_->cfg.activeFriend == f;
		const Friend &fr = e_->cfg.friends[f];
		bool live = e_->feedState(fr) == Engine::Feed::Live;
		povBtns_[k]->setChecked(showing);
		repolish(povBtns_[k], "armed", armed);
		repolish(povBtns_[k], "live", live);
		QString n = QString::fromStdString(fr.name);
		povBtns_[k]->setToolTip(
			showing ? n + " is on stream. Press to come back to your own POV."
			: armed ? n + " is who goes on stream when you are downed" +
					  (e_->cfg.nearEnabled ? QString(" (unless someone is closer)") : "") +
					  ". Press to show them now."
				: "Show " + n + " now, and when you are downed.");
	}
	closestBtn_->setChecked(e_->cfg.nearEnabled);
	repolish(closestBtn_, "faded", !e_->appConnected());
	closestBtn_->setToolTip(
		e_->appConnected()
			? "When you go down, show whoever the game's NEARBY list says is closest. Needs each squad mate's "
			  "in-game name (Squad...)."
			: "Closest needs ClipHound running: it reads the NEARBY list in the corner of your game.");
	povEmpty_->setVisible(povBtns_.isEmpty());
	povEmpty_->setText(e_->cfg.friends.empty() ? "No squad mates yet: pop their streams out in Discord and press "
						     "Add pop-outs."
			   : e_->rosterLive()      ? "Nobody is live in your voice channel right now."
						   : "Nobody is streaming right now.");
	near_->setVisible(e_->cfg.nearEnabled && !small);
	near_->setText("Nearby: " + e_->nearbyStatus());
	// dual
	dualAuto_->setChecked(e_->cfg.dualAuto);
	dualOffBtn_->setChecked(!e_->dualOn());
	for (int k = 0; k < dualBtns_.size() && k < peopleIdx_.size(); k++)
		dualBtns_[k]->setChecked(e_->dualOn() && e_->cfg.dualFriend == peopleIdx_[k]);
	dualHead_->setText(e_->dualForced() ? "DUAL POV  ·  ON UNTIL OFF"
			   : e_->dualOn()   ? "DUAL POV  ·  VEHICLE"
					    : "DUAL POV");

	// ----- clips
	{
		bool on = e_->replaying();
		replay_->setText(on ? "Stop replay" : "Instant replay");
		highlights_->setText(on ? "Stop" : e_->highlightsBuilding() ? "Building..." : "Highlights");
		replay_->setChecked(false);
		replay_->setStyleSheet(on ? "QPushButton { border-left: 4px solid #ce6050; }" : "");
		QString lp = e_->clips.lastPath();
		const auto &h = e_->clips.history();
		if (lp.isEmpty() || h.empty())
			clip_->setText("No clips yet this session.");
		else {
			const Clips::Entry &last = h.back();
			QString title = last.title.isEmpty() ? QFileInfo(lp).completeBaseName() : last.title;
			clip_->setText("Saved " + last.when.toString("HH:mm") + ": <b>" + title.toHtmlEscaped() +
				       "</b>  ·  <a style=\"color:#c99a3b\" href=\"kennel:rename\">Rename</a>  ·  "
				       "<a style=\"color:#c99a3b\" href=\"kennel:replay\">Replay</a>");
		}
	}

	// ----- squad row, events, menu
	showPop_->blockSignals(true);
	showPop_->setChecked(e_->popoutsShown());
	showPop_->setText(e_->popoutsShown() ? "Tuck pop-outs" : "Show pop-outs");
	showPop_->setVisible(e_->cfg.popoutTuck && e_->cfg.popoutMonitor < 0 && !small);
	showPop_->blockSignals(false);
	eventsHead_->setVisible(!small);
	events_->setVisible(!small);
	last_->setVisible(!small);
	events_->clear();
	QStringList ev = e_->recentEvents();
	for (int i = ev.size() - 1; i >= 0 && ev.size() - i <= 8; i--)
		events_->addItem(ev[i]);
	if (events_->count() == 0)
		events_->addItem("events from the kill feed and the POV swap appear here");
	QString st = e_->appState();
	appAct_->setText(st == "connected" || st == "starting" ? "Stop ClipHound" : "Start ClipHound");
	filling_ = false;
}

void Dock::openClips(const QString &focusPath)
{
	auto *d = new ClipsDialog(e_, (QWidget *)obs_frontend_get_main_window());
	d->show();
	if (!focusPath.isEmpty())
		d->focusClip(focusPath);
}

void Dock::openWizard()
{
	if (wizard_) {
		wizard_->raise();
		wizard_->activateWindow();
		return;
	}
	auto *w = new SetupWizard(e_, (QWidget *)obs_frontend_get_main_window());
	w->setAttribute(Qt::WA_DeleteOnClose);
	connect(w, &SetupWizard::openSettingsPage, this, [this](const QString &page) { openSettings(page); });
	wizard_ = w;
	showOnScreen(w);
}

static QString tailOf(const QString &path, int lines)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
		return QString();
	QStringList all = QString::fromUtf8(f.readAll()).split('\n');
	if (all.size() > lines)
		all = all.mid(all.size() - lines);
	return all.join('\n');
}

void Dock::openLogs()
{
	auto *d = new QDialog((QWidget *)obs_frontend_get_main_window());
	d->setAttribute(Qt::WA_DeleteOnClose);
	d->setWindowTitle("Kennel.gg Wardogs - logs");
	d->setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
	d->resize(820, 600);
	auto *v = new QVBoxLayout(d);
	auto *txt = new QPlainTextEdit(d);
	txt->setReadOnly(true);
	txt->setLineWrapMode(QPlainTextEdit::NoWrap);
	QString appDir = QString::fromStdString(e_->cfg.appPath).isEmpty()
				 ? QString("C:/ProgramData/Kennel.gg/ClipHound")
				 : QFileInfo(QString::fromStdString(e_->cfg.appPath)).absolutePath();
	QString body = QString("=== Kennel.gg Wardogs plugin %1 ===\n").arg(PLUGIN_VERSION);
	body += QString("state: %1 | game source: %2 | squad mate: %3 | replay buffer: %4 | ClipHound: %5 | clip hotkeys: %6\n\n")
			.arg(QString::fromStdString(e_->stateText()), QString::fromStdString(e_->cfg.gameSource),
			     e_->cfg.active() ? QString::fromStdString(e_->cfg.active()->name) : "(none)",
			     obs_frontend_replay_buffer_active() ? "running" : "NOT running",
			     e_->appConnected() ? "connected" : "not connected",
			     QString::number(e_->cfg.clipHotkeys.size()));
	body += e_->recentLog().join('\n');
	body += "\n\n=== ClipHound (" + appDir + "/cliphound.log, last 200 lines) ===\n";
	QString ch = tailOf(appDir + "/cliphound.log", 200);
	body += ch.isEmpty() ? "(no log file - is ClipHound running from that folder?)" : ch;
	txt->setPlainText(body);
	v->addWidget(txt, 1);
	auto *row = new QHBoxLayout();
	auto *copy = new QPushButton("Copy all", d);
	auto *obsLogs = new QPushButton("Open OBS log folder", d);
	auto *appFolder = new QPushButton("Open ClipHound folder", d);
	auto *cfgFolder = new QPushButton("Open plugin config folder", d);
	for (auto *b : {copy, obsLogs, appFolder, cfgFolder})
		row->addWidget(b);
	row->addStretch(1);
	v->addLayout(row);
	connect(copy, &QPushButton::clicked, d, [txt, copy]() {
		QApplication::clipboard()->setText(txt->toPlainText());
		copy->setText("Copied");
	});
	connect(obsLogs, &QPushButton::clicked, d, []() {
		QDesktopServices::openUrl(QUrl::fromLocalFile(
			QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).section('/', 0, -2) +
			"/obs-studio/logs"));
	});
	connect(appFolder, &QPushButton::clicked, d,
		[appDir]() { QDesktopServices::openUrl(QUrl::fromLocalFile(appDir)); });
	connect(cfgFolder, &QPushButton::clicked, d,
		[]() { QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(Config::configDir()))); });
	showOnScreen(d);
}

void Dock::openSquad()
{
	if (squad_) {
		squad_->raise();
		squad_->activateWindow();
		return;
	}
	auto *dlg = new SquadPanel(e_, (QWidget *)obs_frontend_get_main_window());
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	squad_ = dlg;
	showOnScreen(dlg);
}

void Dock::openSettings(const QString &page)
{
	if (!settings_) {
		auto *dlg = new SettingsDialog(e_, (QWidget *)obs_frontend_get_main_window());
		dlg->setAttribute(Qt::WA_DeleteOnClose);
		settings_ = dlg;
		showOnScreen(dlg);
	} else {
		settings_->raise();
		settings_->activateWindow();
	}
	if (!page.isEmpty())
		static_cast<SettingsDialog *>(settings_.data())->showPage(page);
}
