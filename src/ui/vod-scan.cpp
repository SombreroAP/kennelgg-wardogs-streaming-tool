#include "ui/vod-scan.h"
#include "engine.h"
#include "i18n.h"
#include "ui/kmsg.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

static QString hms(double s)
{
	int t = (int)s;
	return t >= 3600 ? QString("%1:%2:%3")
				   .arg(t / 3600)
				   .arg(t % 3600 / 60, 2, 10, QChar('0'))
				   .arg(t % 60, 2, 10, QChar('0'))
			 : QString("%1:%2").arg(t / 60).arg(t % 60, 2, 10, QChar('0'));
}

VodScanDialog::VodScanDialog(Engine *engine, QWidget *parent) : QDialog(parent), e_(engine)
{
	setWindowTitle(tx("Scan a VOD (experimental)"));
	resize(760, 620);
	auto *v = new QVBoxLayout(this);
	{
		auto *head = new QHBoxLayout();
		auto *title = new QLabel(tx("<b>Scan a VOD</b>"), this);
		auto *badge = new QLabel(tx("EXPERIMENTAL"), this);
		badge->setStyleSheet(
			"QLabel { color: #121518; background: #c99a3b; font-weight: 700; padding: 1px 6px; "
			"border-radius: 3px; }");
		head->addWidget(title);
		head->addWidget(badge);
		head->addStretch(1);
		v->addLayout(head);
	}
	auto *intro = new QLabel(
		tx("Not streaming with the plugin on? Give it a recording from this PC or one of your own Twitch VODs and "
		   "ClipHound reads it many times faster than real time for the same highlights the plugin clips live: "
		   "your kills, multi-kills, headshots, long shots and big deaths. Then clip them all, or pick. "
		   "It is experimental: it may miss some or find a few that are not highlights."),
		this);
	intro->setWordWrap(true);
	v->addWidget(intro);
	auto *row = new QHBoxLayout();
	source_ = new QLineEdit(this);
	source_->setPlaceholderText(tx("Your Twitch VOD link (twitch.tv/videos/...) or a recording on this PC"));
	browse_ = new QPushButton(tx("Recording..."), this);
	scan_ = new QPushButton(tx("Scan"), this);
	scan_->setDefault(true);
	stop_ = new QPushButton(tx("Stop"), this);
	stop_->hide();
	row->addWidget(source_, 1);
	row->addWidget(browse_);
	row->addWidget(scan_);
	row->addWidget(stop_);
	v->addLayout(row);
	bar_ = new QProgressBar(this);
	bar_->setRange(0, 1000);
	bar_->hide();
	v->addWidget(bar_);
	status_ = new QLabel(this);
	status_->setWordWrap(true);
	v->addWidget(status_);
	table_ = new QTableWidget(0, 4, this);
	table_->setHorizontalHeaderLabels({tx("At"), tx("What"), tx("Kills"), tx("Score")});
	table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
	table_->verticalHeader()->hide();
	table_->setSelectionMode(QAbstractItemView::NoSelection);
	table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	v->addWidget(table_, 1);
	clipAll_ = new QCheckBox(tx("Clip everything it finds without asking"), this);
	clipAll_->setChecked(e_->cfg.vodClipAll);
	v->addWidget(clipAll_);
	auto *acts = new QHBoxLayout();
	clipTicked_ = new QPushButton(tx("Clip the ticked ones"), this);
	chapters_ = new QPushButton(tx("Copy YouTube chapters"), this);
	links_ = new QPushButton(tx("Copy Twitch links"), this);
	folder_ = new QPushButton(tx("Open the clips folder"), this);
	for (auto *b : {clipTicked_, chapters_, links_, folder_}) {
		b->setEnabled(false);
		acts->addWidget(b);
	}
	links_->hide();
	acts->addStretch(1);
	v->addLayout(acts);

	connect(browse_, &QPushButton::clicked, this, [this]() {
		QString f = QFileDialog::getOpenFileName(this, tx("A recording to scan"), e_->vodClipDir(),
							 tx("Videos (*.mp4 *.mkv *.mov *.flv *.ts);;All files (*)"));
		if (!f.isEmpty())
			source_->setText(f);
	});
	connect(scan_, &QPushButton::clicked, this, [this]() {
		QString src = source_->text().trimmed();
		if (src.isEmpty())
			return;
		if (!e_->appConnected()) {
			status_->setText(
				tx("ClipHound is not running: start it from the dock's menu, then scan again."));
			return;
		}
		moments_ = QJsonArray();
		twitchId_.clear();
		fill();
		scanId_ = e_->startVodScan(src);
		setScanning(true);
		status_->setText(tx("Starting..."));
	});
	connect(stop_, &QPushButton::clicked, this, [this]() { e_->cancelVodScan(); });
	connect(clipAll_, &QCheckBox::toggled, this, [this](bool on) {
		e_->cfg.vodClipAll = on;
		e_->cfg.save();
	});
	connect(clipTicked_, &QPushButton::clicked, this, [this]() { clip(ticked()); });
	connect(chapters_, &QPushButton::clicked, this, [this]() {
		// YouTube wants the first at 0:00, then one per highlight
		QStringList l{"0:00 " + tx("Start")};
		for (const auto &m : moments_)
			l << hms(m.toObject().value("start").toDouble()) + " " + m.toObject().value("title").toString();
		QApplication::clipboard()->setText(l.join('\n'));
		chapters_->setText(tx("Copied"));
	});
	connect(links_, &QPushButton::clicked, this, [this]() {
		QStringList l;
		for (const auto &m : moments_) {
			int t = (int)m.toObject().value("start").toDouble();
			l << QString("%1  https://www.twitch.tv/videos/%2?t=%3h%4m%5s")
					.arg(m.toObject().value("title").toString(), twitchId_)
					.arg(t / 3600)
					.arg(t % 3600 / 60)
					.arg(t % 60);
		}
		QApplication::clipboard()->setText(l.join('\n'));
		links_->setText(tx("Copied"));
	});
	connect(folder_, &QPushButton::clicked, this,
		[this]() { QDesktopServices::openUrl(QUrl::fromLocalFile(e_->vodClipDir())); });
	connect(e_, &Engine::vodProgress, this, &VodScanDialog::onProgress);
	connect(e_, &Engine::vodDone, this, &VodScanDialog::onDone);
	connect(e_, &Engine::vodClipDone, this, &VodScanDialog::onClipDone);
}

void VodScanDialog::setScanning(bool on)
{
	scan_->setVisible(!on);
	stop_->setVisible(on);
	source_->setEnabled(!on);
	browse_->setEnabled(!on);
	bar_->setVisible(on);
	if (on)
		bar_->setValue(0);
}

void VodScanDialog::onProgress(const QJsonObject &o)
{
	if (o.value("id").toInt() != scanId_)
		return;
	double done = o.value("done_s").toDouble(), total = o.value("total_s").toDouble();
	bar_->setValue(total > 0 ? (int)(1000 * done / total) : 0);
	status_->setText(tx("%1 of %2 read  ·  %3x real time  ·  %4 highlights so far")
				 .arg(hms(done), hms(total), QString::number(o.value("speed").toDouble(), 'f', 1),
				      QString::number(o.value("found").toInt())));
}

void VodScanDialog::onDone(const QJsonObject &o)
{
	if (o.value("id").toInt() != scanId_)
		return;
	setScanning(false);
	QString err = o.value("error").toString();
	if (!err.isEmpty()) {
		status_->setText(err == "stopped" ? tx("Stopped.") : tx("The scan stopped: %1").arg(err));
		return;
	}
	moments_ = o.value("moments").toArray();
	twitchId_ = o.value("twitch_id").toString();
	links_->setVisible(!twitchId_.isEmpty());
	fill();
	status_->setText(tx("Done: %1 highlights in %2, read in %3.")
				 .arg(QString::number(moments_.size()), hms(o.value("duration").toDouble()),
				      hms(o.value("seconds").toDouble())));
	if (moments_.isEmpty())
		return;
	if (e_->cfg.vodClipAll) {
		QList<int> all;
		for (int i = 0; i < moments_.size(); ++i)
			all << i;
		clip(all);
	} else
		askWhatToClip();
}

void VodScanDialog::askWhatToClip()
{
	QMessageBox box(
		QMessageBox::Question, tx("Scan a VOD"),
		tx("Found %1 highlights. Make clips of all of them, or pick the ones you want?").arg(moments_.size()),
		QMessageBox::NoButton, this);
	auto *all = box.addButton(tx("Clip them all"), QMessageBox::AcceptRole);
	box.addButton(tx("Let me pick"), QMessageBox::RejectRole);
	auto *always = new QCheckBox(tx("Always clip everything (do not ask again)"), &box);
	box.setCheckBox(always);
	box.exec();
	if (always->isChecked()) {
		clipAll_->setChecked(true);
	}
	if (box.clickedButton() == all) {
		QList<int> idx;
		for (int i = 0; i < moments_.size(); ++i)
			idx << i;
		clip(idx);
	} else
		status_->setText(tx("Tick the highlights you want, then Clip the ticked ones."));
}

void VodScanDialog::clip(const QList<int> &indices)
{
	if (indices.isEmpty())
		return;
	clipsWanted_ = indices.size();
	clipsDone_ = 0;
	clipTicked_->setEnabled(false);
	status_->setText(tx("Clipping %1 highlights into %2...").arg(QString::number(clipsWanted_), e_->vodClipDir()));
	e_->clipVod(indices);
}

void VodScanDialog::onClipDone(const QJsonObject &o)
{
	if (o.value("id").toInt() != scanId_)
		return;
	if (o.value("all").toBool()) {
		clipTicked_->setEnabled(true);
		status_->setText(
			tx("Done: %1 of %2 clips saved in %3.")
				.arg(QString::number(clipsDone_), QString::number(clipsWanted_), e_->vodClipDir()));
		return;
	}
	int i = o.value("index").toInt();
	if (!o.value("error").toString().isEmpty()) {
		status_->setText(tx("A clip could not be made: %1").arg(o.value("error").toString()));
		return;
	}
	clipsDone_++;
	if (i >= 0 && i < table_->rowCount() && table_->item(i, 1))
		table_->item(i, 1)->setText(table_->item(i, 1)->text() + "  ✓");
	status_->setText(
		tx("Clipping... %1 of %2 done.").arg(QString::number(clipsDone_), QString::number(clipsWanted_)));
}

void VodScanDialog::fill()
{
	table_->setRowCount(moments_.size());
	for (int i = 0; i < moments_.size(); ++i) {
		QJsonObject m = moments_[i].toObject();
		auto *at = new QTableWidgetItem(hms(m.value("start").toDouble()));
		at->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
		at->setCheckState(Qt::Checked);
		table_->setItem(i, 0, at);
		table_->setItem(i, 1, new QTableWidgetItem(m.value("title").toString()));
		table_->setItem(i, 2, new QTableWidgetItem(QString::number(m.value("kills").toInt())));
		table_->setItem(i, 3, new QTableWidgetItem(QString::number(m.value("score").toDouble(), 'f', 1)));
	}
	table_->resizeColumnToContents(0);
	bool any = !moments_.isEmpty();
	for (auto *b : {clipTicked_, chapters_, links_, folder_})
		b->setEnabled(any);
}

QList<int> VodScanDialog::ticked() const
{
	QList<int> out;
	for (int i = 0; i < table_->rowCount(); ++i)
		if (table_->item(i, 0) && table_->item(i, 0)->checkState() == Qt::Checked)
			out << i;
	return out;
}
