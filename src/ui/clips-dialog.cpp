#include "ui/clips-dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QHeaderView>
#include <QDesktopServices>
#include <QUrl>
#include <QRegularExpression>
#include <QFileInfo>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <obs-frontend-api.h>
#include "i18n.h"
#include "ui/kmsg.h"

static QString tagsText(const QStringList &t)
{
	return t.join(", ");
}

static QStringList tagsFrom(const QString &s)
{
	QStringList out;
	for (const QString &p : s.split(QRegularExpression("[,;]"), Qt::SkipEmptyParts)) {
		QString g = p.trimmed();
		if (!g.isEmpty())
			out << g;
	}
	return out;
}

ClipsDialog::ClipsDialog(Engine *e, QWidget *parent) : QDialog(parent), e_(e)
{
	setWindowTitle(tx("Kennel.gg Wardogs - clips"));
	setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
	setAttribute(Qt::WA_DeleteOnClose);
	resize(900, 560);
	auto *v = new QVBoxLayout(this);
	auto *intro =
		new QLabel(tx("Every clip with a title and tags. Change either and press Apply: the file is renamed to "
			      "the title, and both go into the clip's .json for Kennel Cut and the compilation."),
			   this);
	intro->setWordWrap(true);
	v->addWidget(intro);
	table_ = new QTableWidget(this);
	table_->setColumnCount(4);
	table_->setHorizontalHeaderLabels({tx("When"), tx("Title"), tx("Tags"), tx("File")});
	table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
	table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
	table_->setSelectionBehavior(QAbstractItemView::SelectRows);
	table_->setSelectionMode(QAbstractItemView::SingleSelection);
	table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table_->verticalHeader()->setVisible(false);
	v->addWidget(table_, 1);
	auto *f = new QFormLayout();
	title_ = new QLineEdit(this);
	title_->setPlaceholderText(tx("a few words: what happened"));
	tags_ = new QLineEdit(this);
	tags_->setPlaceholderText(
		tx("comma separated: %1").arg("highlight, funny, fail, ace, ...")); // tag words stay English: data
	spoken_ = new QLabel(this);
	spoken_->setWordWrap(true);
	spoken_->setStyleSheet("color:#7c8076");
	file_ = new QLabel(this);
	file_->setWordWrap(true);
	file_->setStyleSheet("color:#7c8076");
	f->addRow(tx("Title"), title_);
	f->addRow(tx("Tags"), tags_);
	f->addRow(tx("Said"), spoken_);
	f->addRow(tx("File"), file_);
	v->addLayout(f);
	auto *row = new QHBoxLayout();
	applyBtn_ = new QPushButton(tx("Apply"), this);
	applyBtn_->setDefault(true);
	openBtn_ = new QPushButton(tx("Show in folder"), this);
	playBtn_ = new QPushButton(tx("Play"), this);
	auto *refresh = new QPushButton(tx("Refresh"), this);
	auto *close = new QPushButton(tx("Close"), this);
	row->addWidget(applyBtn_);
	row->addWidget(playBtn_);
	row->addWidget(openBtn_);
	row->addWidget(refresh);
	row->addStretch(1);
	row->addWidget(close);
	v->addLayout(row);
	connect(table_, &QTableWidget::currentCellChanged, this, [this](int r, int, int, int) { pick(r); });
	connect(applyBtn_, &QPushButton::clicked, this, &ClipsDialog::apply);
	connect(title_, &QLineEdit::returnPressed, this, &ClipsDialog::apply);
	connect(tags_, &QLineEdit::returnPressed, this, &ClipsDialog::apply);
	connect(playBtn_, &QPushButton::clicked, this, [this]() {
		if (!current_.isEmpty())
			QDesktopServices::openUrl(QUrl::fromLocalFile(current_));
	});
	connect(openBtn_, &QPushButton::clicked, this, [this]() {
		if (!current_.isEmpty())
			QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(current_).absolutePath()));
	});
	connect(refresh, &QPushButton::clicked, this, &ClipsDialog::reload);
	connect(close, &QPushButton::clicked, this, &QDialog::close);
	reload();
}

void ClipsDialog::reload()
{
	rows_ = e_->clips.allClips();
	table_->setRowCount((int)rows_.size());
	for (int i = 0; i < (int)rows_.size(); i++) {
		const auto &e = rows_[(size_t)i];
		table_->setItem(i, 0, new QTableWidgetItem(e.when.toString("ddd HH:mm:ss")));
		table_->setItem(i, 1, new QTableWidgetItem(e.title));
		table_->setItem(i, 2, new QTableWidgetItem(tagsText(e.tags)));
		table_->setItem(i, 3, new QTableWidgetItem(QFileInfo(e.path).fileName()));
	}
	table_->resizeColumnToContents(0);
	int keep = -1;
	for (int i = 0; i < (int)rows_.size(); i++)
		if (rows_[(size_t)i].path == current_)
			keep = i;
	if (keep < 0 && !rows_.empty())
		keep = 0;
	if (keep >= 0) {
		table_->setCurrentCell(keep, 1);
		pick(keep);
	}
}

void ClipsDialog::pick(int row)
{
	if (row < 0 || row >= (int)rows_.size()) {
		current_.clear();
		return;
	}
	const auto &e = rows_[(size_t)row];
	current_ = e.path;
	title_->setText(e.title);
	tags_->setText(tagsText(e.tags));
	spoken_->setText(e.info.value("spoken").toString());
	file_->setText(e.path);
}

void ClipsDialog::focusClip(const QString &path)
{
	for (int i = 0; i < (int)rows_.size(); i++)
		if (rows_[(size_t)i].path == path) {
			table_->setCurrentCell(i, 1);
			pick(i);
			break;
		}
	title_->setFocus();
	title_->selectAll();
}

void ClipsDialog::apply()
{
	if (current_.isEmpty())
		return;
	QStringList tags = tagsFrom(tags_->text());
	QString to = e_->clips.relabel(current_, title_->text(), QString(), &tags);
	if (to.isEmpty()) {
		KMsg::warning(this, "Kennel.gg Wardogs",
			      tx("Could not rename that clip. Is it open in a player, or already gone?"));
		return;
	}
	current_ = to;
	reload();
	emit e_->stateChanged();
}

// ----- the small one -----

ClipNoteDialog::ClipNoteDialog(Engine *e, const QString &path, QWidget *parent) : QDialog(parent), e_(e), path_(path)
{
	setWindowTitle(tx("Kennel.gg Wardogs - clip saved"));
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
	auto *v = new QVBoxLayout(this);
	auto *l = new QLabel(tx("Saved: %1\nWhat was it? (Enter to keep going)").arg(QFileInfo(path).fileName()), this);
	l->setWordWrap(true);
	v->addWidget(l);
	auto *f = new QFormLayout();
	title_ = new QLineEdit(this);
	title_->setPlaceholderText(tx("a few words"));
	tags_ = new QLineEdit(this);
	tags_->setPlaceholderText("highlight, funny, fail, ..."); // tag words stay English: data
	f->addRow(tx("Title"), title_);
	f->addRow(tx("Tags"), tags_);
	v->addLayout(f);
	auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	v->addWidget(bb);
	bb->button(QDialogButtonBox::Ok)->setText(tx("OK"));
	bb->button(QDialogButtonBox::Cancel)->setText(tx("Cancel"));
	connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(bb, &QDialogButtonBox::accepted, this, [this]() {
		QStringList tags = tagsFrom(tags_->text());
		if (!tags.contains("manual"))
			tags.prepend("manual");
		QString to = e_->clips.relabel(path_, title_->text(), QString(),
					       tags_->text().trimmed().isEmpty() ? nullptr : &tags);
		if (to.isEmpty() && !title_->text().trimmed().isEmpty())
			e_->log(tx("Could not rename %1 - is it open somewhere?").arg(QFileInfo(path_).fileName()));
		emit e_->stateChanged();
		accept();
	});
	title_->setFocus();
	resize(420, sizeHint().height());
}
