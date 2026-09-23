#include "ui/squad.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QMessageBox>
#include <QTimer>
#include <QInputDialog>
#include <QComboBox>
#include <QSlider>
#include <QHeaderView>
#include <QApplication>
#include <algorithm>

SquadPanel::SquadPanel(Engine *engine, QWidget *parent) : QDialog(parent), e_(engine)
{
	setWindowTitle("Kennel.gg Wardogs - Squad");
	setMinimumWidth(460);
	auto *v = new QVBoxLayout(this);

	auto *how = new QLabel(
		"In Discord, right-click a squad mate's stream and choose <b>Pop Out</b>, then right-click it again and "
		"<b>Mute</b> it. Press Add pop-outs: every popped-out stream becomes a squad mate, named by their "
		"Discord username. <b>Tick who you are playing with</b>: outside a Kennel.gg voice channel the dock "
		"offers only them (inside one it offers whoever is live there, by itself). Type each one's "
		"<b>in-game name</b> in the table; Closest matches the NEARBY list against it.",
		this);
	how->setWordWrap(true);
	v->addWidget(how);

	add_ = new QPushButton("Add pop-outs", this);
	add_->setMinimumHeight(36);
	add_->setDefault(true);
	v->addWidget(add_);
	connect(add_, &QPushButton::clicked, this, &SquadPanel::addPopouts);
	auto *byHand = new QPushButton(
		"Add a squad mate another way: Twitch, Kick, YouTube, VDO.Ninja, an OBS source...", this);
	byHand->setToolTip(
		"The same window as Settings, Squad & POV, Add: a squad mate whose feed is a Twitch, Kick or "
		"YouTube stream, a VDO.Ninja link, or any source already in OBS.");
	v->addWidget(byHand);
	connect(byHand, &QPushButton::clicked, this, [this]() {
		if (addFriendByHand(e_, this))
			refresh();
	});
	result_ = new QLabel(this);
	result_->setWordWrap(true);
	result_->setStyleSheet("color: palette(mid);");
	v->addWidget(result_);

	list_ = new QTableWidget(0, 5, this);
	list_->setHorizontalHeaderLabels({"Playing", "Squad mate", "In-game name", "Feed", "Now"});
	list_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	list_->horizontalHeader()->setStretchLastSection(true);
	list_->verticalHeader()->hide();
	list_->setSelectionBehavior(QAbstractItemView::SelectRows);
	list_->setSelectionMode(QAbstractItemView::SingleSelection);
	list_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed |
			       QAbstractItemView::SelectedClicked);
	list_->setToolTip("Double-click an in-game name to change it.");
	v->addWidget(list_, 1);
	// the in-game name, typed straight into the table: what the NEARBY list is matched against
	connect(list_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *it) {
		if (filling_)
			return;
		int r = it->row();
		if (r < 0 || r >= (int)e_->cfg.friends.size())
			return;
		Friend &f = e_->cfg.friends[r];
		if (it->column() == 0) {
			bool on = it->checkState() == Qt::Checked;
			if (on != f.playing) {
				f.playing = on;
				e_->cfg.save();
				e_->log("Squad: " + QString::fromStdString(f.name) +
					(on ? " is playing this session." : " is not playing this session."));
				emit e_->stateChanged();
			}
			return;
		}
		if (it->column() != 2)
			return;
		QString v = it->text().trimmed();
		std::string want = (v.isEmpty() || v == QString::fromStdString(f.name)) ? "" : v.toStdString();
		if (want == f.gameName)
			return;
		f.gameName = want;
		e_->cfg.save();
		e_->pushAppConfig(); // ClipHound matches on these names
		e_->log("Squad: " + QString::fromStdString(f.name) + "'s in-game name is " +
			(want.empty() ? QString::fromStdString(f.name) + " (their Discord name)" : v) + ".");
	});
	auto *row = new QHBoxLayout();
	active_ = new QPushButton("Make active", this);
	dual_ = new QPushButton("Show in Dual POV", this);
	dual_->setToolTip("Their feed in the small Dual POV window, now, and it stays up until you turn it off.");
	active_->setToolTip("Who goes on stream when you are downed.");
	remove_ = new QPushButton("Remove", this);
	auto *tickLive = new QPushButton("Playing: whoever is live", this);
	tickLive->setToolTip("Tick everyone streaming right now and untick the rest.");
	auto *untickAll = new QPushButton("New session", this);
	untickAll->setToolTip("Untick everyone. Pop-outs you open tick their squad mate again by themselves.");
	auto setAll = [this](bool liveOnly) {
		for (auto &f : e_->cfg.friends)
			f.playing = liveOnly && e_->feedState(f) == Engine::Feed::Live;
		e_->cfg.save();
		e_->log(liveOnly ? "Squad: playing with whoever is live now." : "Squad: new session, nobody ticked.");
		emit e_->stateChanged();
	};
	connect(tickLive, &QPushButton::clicked, this, [setAll]() { setAll(true); });
	connect(untickAll, &QPushButton::clicked, this, [setAll]() { setAll(false); });
	row->addWidget(tickLive);
	row->addWidget(untickAll);
	row->addWidget(active_);
	row->addWidget(dual_);
	row->addWidget(remove_);
	row->addStretch(1);
	v->addLayout(row);
	connect(active_, &QPushButton::clicked, this, &SquadPanel::makeActive);
	connect(dual_, &QPushButton::clicked, this, &SquadPanel::showInDual);
	connect(remove_, &QPushButton::clicked, this, &SquadPanel::removeSelected);

	auto *form = new QFormLayout();
	roster_ = new QCheckBox("Also add whoever goes live in Kennel.gg voice, by themselves", this);
	roster_->setChecked(e_->cfg.rosterEnabled);
	roster_->setToolTip("The Kennel.gg Discord bot publishes who is in voice and who is sharing. With this on, a "
			    "slot appears when a squad mate goes live and goes away when they stop.");
	form->addRow(roster_);
	connect(roster_, &QCheckBox::toggled, this, [this](bool on) {
		e_->cfg.rosterEnabled = on;
		e_->cfg.save();
		e_->applyRosterConfig();
		if (!on)
			refresh();
	});
	auto *tuck = new QCheckBox("Keep pop-outs drawing: pin them on top, tucked to the screen edge", this);
	tuck->setChecked(e_->cfg.popoutTuck);
	tuck->setToolTip("Discord stops drawing a window the game completely covers, and its capture goes black. "
			 "Tucked to the edge with a few pixels showing it keeps drawing, and the capture still gets "
			 "the whole window. Needs the game in borderless windowed mode.");
	form->addRow(tuck);
	connect(tuck, &QCheckBox::toggled, this, [this](bool on) {
		e_->cfg.popoutTuck = on;
		e_->cfg.save();
		if (on)
			e_->watchPopouts();
		else
			e_->releaseAllPopouts();
	});
	auto *where = new QComboBox(this);
	where->addItem("Tucked to the edge of their own screen (a sliver showing)", -1);
	{
		int i = 0;
		for (const auto &m : Switcher::monitors()) {
			// the index first, on its own line: inside the call, which of ++i and i - 1 ran first
			// was up to the compiler
			where->addItem(QString("Parked on monitor %1 (%2), fully visible")
					       .arg(i + 1)
					       .arg(QString::fromStdString(m)),
				       i);
			i++;
		}
	}
	where->setCurrentIndex(std::max(0, where->findData(e_->cfg.popoutMonitor)));
	where->setToolTip("Parking them on a screen the game and OBS are not on keeps every pop-out visible, on top "
			  "and within reach, so Discord's own volume control on each one is a click away.");
	form->addRow("Pop-outs live", where);
	connect(where, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, where](int) {
		e_->cfg.popoutMonitor = where->currentData().toInt();
		e_->cfg.save();
		e_->releaseAllPopouts();
		e_->watchPopouts();
	});
	show_ = new QPushButton(this);
	show_->setCheckable(true);
	show_->setToolTip(
		"Bring the pop-outs back on screen to use their own controls, then press again to tuck them away.");
	form->addRow(show_);
	connect(show_, &QPushButton::clicked, this, [this](bool on) { e_->showPopouts(on); });
	rosterState_ = new QLabel(this);
	rosterState_->setStyleSheet("color: palette(mid);");
	form->addRow(rosterState_);
	me_ = new QLineEdit(QString::fromStdString(e_->cfg.myDiscord), this);
	me_->setPlaceholderText("so your own stream is never added, and only your channel counts");
	form->addRow("Your Discord username", me_);
	connect(me_, &QLineEdit::editingFinished, this, [this]() {
		std::string v = me_->text().trimmed().toLower().toStdString();
		if (v == e_->cfg.myDiscord)
			return;
		e_->cfg.myDiscord = v;
		e_->cfg.save();
		e_->syncRoster();
	});
	v->addLayout(form);

	connect(e_, &Engine::stateChanged, this, &SquadPanel::refresh);
	connect(&e_->roster, &Roster::polled, this, &SquadPanel::refresh);
	connect(&e_->roster, &Roster::changed, this, &SquadPanel::refresh);
	refresh();
}

void SquadPanel::refresh()
{
	// never while a name is being typed: the rebuild would throw the edit away
	if (QWidget *fw = QApplication::focusWidget())
		if (qobject_cast<QLineEdit *>(fw) && list_->isAncestorOf(fw))
			return;
	int sel = currentRow();
	filling_ = true;
	list_->setRowCount(0);
	auto cell = [](const QString &t, bool editable = false) {
		auto *it = new QTableWidgetItem(t);
		if (!editable)
			it->setFlags(it->flags() & ~Qt::ItemIsEditable);
		return it;
	};
	for (size_t i = 0; i < e_->cfg.friends.size(); ++i) {
		const Friend &f = e_->cfg.friends[i];
		int r = list_->rowCount();
		list_->insertRow(r);
		auto *play = new QTableWidgetItem();
		play->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
		play->setCheckState(f.playing ? Qt::Checked : Qt::Unchecked);
		play->setToolTip("Playing with them this session: the dock offers them.");
		list_->setItem(r, 0, play);
		QString name = QString::fromStdString(f.name);
		if ((int)i == e_->cfg.activeFriend)
			name += "  (active)";
		list_->setItem(r, 1, cell(name));
		auto *game = cell(QString::fromStdString(f.gameName.empty() ? f.name : f.gameName), true);
		if (f.gameName.empty())
			game->setForeground(QColor("#8a8e84")); // their Discord name, a guess until confirmed
		game->setToolTip(f.gameName.empty() ? "Their Discord name, used until you type their in-game name."
						    : "Their in-game name, as the NEARBY list shows it.");
		list_->setItem(r, 2, game);
		QString where;
		switch (f.kind) {
		case FriendKind::Discord:
			where = f.onPopout()            ? "Discord pop-out"
				: f.sharesDiscordCall() ? "Discord window (not popped out)"
							: "Discord window";
			break;
		case FriendKind::Twitch:
			where = "Twitch";
			break;
		case FriendKind::Kick:
			where = "Kick";
			break;
		case FriendKind::YouTube:
			where = "YouTube";
			break;
		case FriendKind::VdoNinja:
			where = "VDO.Ninja";
			break;
		case FriendKind::ObsSource:
			where = "OBS source";
			break;
		}
		if (f.fromRoster)
			where += ", from Kennel.gg voice";
		if (f.kind == FriendKind::Discord && !f.handle.empty() &&
		    QString::fromStdString(f.handle).compare(QString::fromStdString(f.name), Qt::CaseInsensitive) != 0)
			where += " (" + QString::fromStdString(f.handle) + "'s stream!)";
		list_->setItem(r, 3, cell(where));
		QString now = e_->feedStateText(f);
		if (e_->applied() && (int)i == e_->cfg.activeFriend)
			now = "on stream";
		else if ((int)i == e_->cfg.dualFriend && e_->dualOn())
			now = "in Dual POV";
		list_->setItem(r, 4, cell(now));
	}
	if (sel >= 0 && sel < list_->rowCount())
		list_->selectRow(sel);
	filling_ = false;
	bool any = !e_->cfg.friends.empty();
	active_->setEnabled(any);
	remove_->setEnabled(any);
	dual_->setEnabled(any);
	show_->blockSignals(true);
	show_->setChecked(e_->popoutsShown());
	show_->setText(e_->popoutsShown() ? "Tuck pop-outs away again" : "Show pop-outs (to mute or adjust them)");
	show_->blockSignals(false);
	show_->setVisible(e_->cfg.popoutTuck && e_->cfg.popoutMonitor < 0);
	rosterState_->setText(e_->cfg.rosterEnabled ? "Discord voice: " + e_->rosterStatus() : "");
	rosterState_->setVisible(e_->cfg.rosterEnabled);
}

int SquadPanel::currentRow() const
{
	return list_->currentRow();
}

void SquadPanel::addPopouts()
{
	add_->setEnabled(false);
	QStringList added;
	result_->setText(e_->addPopouts(&added));
	QTimer::singleShot(400, this, [this]() { add_->setEnabled(true); });
	e_->noteAddedPopouts(added); // the dock reminds to mute them; no questions here
	refresh();
	// the new rows' in-game names are right there to type into
	for (int r = 0; r < list_->rowCount(); r++)
		if (r < (int)e_->cfg.friends.size() &&
		    added.contains(QString::fromStdString(e_->cfg.friends[r].name))) {
			list_->selectRow(r);
			break;
		}
}

void SquadPanel::showInDual()
{
	int r = currentRow();
	if (r < 0 || r >= (int)e_->cfg.friends.size())
		return;
	if (e_->dualOn() && e_->cfg.dualFriend == r)
		e_->setDual(false, "squad panel");
	else
		e_->showInDual(r);
	refresh();
}

void SquadPanel::makeActive()
{
	int r = currentRow();
	if (r < 0 || r >= (int)e_->cfg.friends.size())
		return;
	e_->setActive(r);
	refresh();
}

void SquadPanel::removeSelected()
{
	int r = currentRow();
	if (r < 0 || r >= (int)e_->cfg.friends.size())
		return;
	Friend f = e_->cfg.friends[r];
	if (QMessageBox::question(this, "Kennel.gg Wardogs",
				  "Remove " + QString::fromStdString(f.name) + " and the sources made for them?") !=
	    QMessageBox::Yes)
		return;
	if (e_->applied() && r == e_->cfg.activeFriend)
		e_->applyNow(false, "squad mate removed");
	e_->releasePopout(f);
	e_->sw.removeFriendSources(e_->cfg, f);
	e_->cfg.friends.erase(e_->cfg.friends.begin() + r);
	if (e_->cfg.activeFriend >= (int)e_->cfg.friends.size())
		e_->cfg.activeFriend = std::max(0, (int)e_->cfg.friends.size() - 1);
	e_->cfg.save();
	e_->armPopoutWatch();
	e_->log("Squad: removed " + QString::fromStdString(f.name) + ".");
	emit e_->stateChanged();
}
