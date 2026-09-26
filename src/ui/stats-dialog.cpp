#include "ui/stats-dialog.h"
#include "stats-image.h"
#include <obs-module.h>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QPixmap>
#include <QPushButton>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

StatsDialog::StatsDialog(Engine *engine, QWidget *parent) : QDialog(parent), e_(engine)
{
	setWindowTitle("Kennel.gg Wardogs - session stats image");
	setAttribute(Qt::WA_DeleteOnClose);
	auto *v = new QVBoxLayout(this);
	preview_ = new QLabel(this);
	preview_->setFixedSize(768, 432);
	preview_->setAlignment(Qt::AlignCenter);
	v->addWidget(preview_);
	note_ = new QLabel(this);
	note_->setWordWrap(true);
	note_->setTextFormat(Qt::RichText);
	note_->setText("1920 x 1080, ready for X, Instagram, Bluesky or Discord. The name is your in-game name "
		       "(Settings, General).");
	v->addWidget(note_);
	auto *row = new QHBoxLayout();
	auto *again = new QPushButton("Another background", this);
	auto *save = new QPushButton("Save", this);
	auto *copy = new QPushButton("Copy", this);
	auto *folder = new QPushButton("Open folder", this);
	save->setDefault(true);
	row->addWidget(again);
	row->addStretch(1);
	row->addWidget(copy);
	row->addWidget(save);
	row->addWidget(folder);
	v->addLayout(row);

	bg_ = 1 + (int)QRandomGenerator::global()->bounded(StatsImage::kBackgrounds);
	draw();
	connect(again, &QPushButton::clicked, this, [this]() {
		// a different one each press, never the same twice running
		int next = bg_;
		while (next == bg_ && StatsImage::kBackgrounds > 1)
			next = 1 + (int)QRandomGenerator::global()->bounded(StatsImage::kBackgrounds);
		bg_ = next;
		saved_.clear();
		draw();
	});
	connect(save, &QPushButton::clicked, this, [this]() {
		QString p = this->save();
		note_->setText(p.isEmpty()
				       ? "Could not save the image."
				       : "Saved <b>" + QFileInfo(p).fileName().toHtmlEscaped() + "</b> in " +
						 QDir::toNativeSeparators(QFileInfo(p).absolutePath()).toHtmlEscaped());
	});
	connect(copy, &QPushButton::clicked, this, [this]() {
		QApplication::clipboard()->setImage(img_);
		note_->setText("Copied: paste it straight into a post or a Discord message.");
	});
	connect(folder, &QPushButton::clicked, this, [this]() {
		QString p = saved_.isEmpty() ? this->save() : saved_;
		if (!p.isEmpty())
			QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(p).absolutePath()));
	});
}

QString StatsDialog::name() const
{
	const Config &c = e_->cfg;
	for (const std::string &n : {c.appPlayerName, c.playerName, c.myDiscord})
		if (!n.empty())
			return QString::fromStdString(n);
	return QString();
}

void StatsDialog::draw()
{
	// the plugin's data folder, found from a file that is always in it
	char *d = obs_module_file("overlay/hound_mark.png");
	QString dataDir = d ? QFileInfo(QString::fromUtf8(d)).absoluteDir().absolutePath() : QString();
	bfree(d);
	if (dataDir.endsWith("/overlay"))
		dataDir.chop(8);
	img_ = StatsImage::render(e_->session(), name(), dataDir, bg_);
	preview_->setPixmap(
		QPixmap::fromImage(img_.scaled(preview_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}

/// Next to the clips, where the stream's other material is; else the Pictures folder.
QString StatsDialog::save()
{
	QString dir;
	const auto &h = e_->clips.history();
	for (auto it = h.rbegin(); it != h.rend() && dir.isEmpty(); ++it)
		if (!it->path.isEmpty())
			dir = QFileInfo(it->path).absolutePath();
	if (dir.isEmpty())
		dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
	QString p = dir + "/Session stats " + e_->session().start.toString("yyyy-MM-dd HH-mm") + ".png";
	if (!img_.save(p, "PNG"))
		return QString();
	saved_ = p;
	e_->log("Session stats image saved: " + QDir::toNativeSeparators(p));
	return p;
}
