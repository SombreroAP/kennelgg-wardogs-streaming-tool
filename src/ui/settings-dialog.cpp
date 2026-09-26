#include "ui/settings-dialog.h"
#include "ui/area-editor.h"
#include "ui/quick-add.h"
#include "voice-phrases.h"
#include <QTextBrowser>
#include <QButtonGroup>
#include "ui/flow-layout.h"
#include <QListView>
#include <algorithm>
#include <QEventLoop>
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <QStandardItemModel>
#include <QScrollArea>
#include <QNetworkInterface>
#include <QJsonArray>
#include <QTimer>
#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMouseEvent>
#include <QPainter>
#include <QTabWidget>
#include <QComboBox>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QPixmap>
#include <QRegularExpression>
#include <QFileDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QJsonObject>
#include <QStandardPaths>
#include <QScrollBar>
#include <QFile>
#include <obs-frontend-api.h>
#include <QFileInfo>
#include <obs-module.h>
#include <plugin-support.h>

// ============================================================ FramePreview

FramePreview::FramePreview(QWidget *parent) : QLabel(parent)
{
	setMinimumHeight(240);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	setCursor(Qt::CrossCursor);
}

void FramePreview::setFrame(const QImage &img, const Match &m, double threshold, QRectF box)
{
	img_ = img;
	m_ = m;
	thr_ = threshold;
	box_ = box;
	update();
}

void FramePreview::setBox2(QRectF box)
{
	box2_ = box;
	update();
}

void FramePreview::setPicker(const QString &caption)
{
	picker_ = true;
	caption_ = caption;
	update();
}

QRect FramePreview::imageRect() const
{
	if (img_.isNull())
		return rect();
	double s = std::min((double)width() / img_.width(), (double)height() / img_.height());
	int w = (int)(img_.width() * s), h = (int)(img_.height() * s);
	return QRect((width() - w) / 2, (height() - h) / 2, w, h);
}

void FramePreview::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.fillRect(rect(), QColor(11, 14, 16));
	if (img_.isNull()) {
		p.setPen(QColor(139, 144, 150));
		p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap,
			   "No frame yet. Set the game source under General and keep this window open.");
		return;
	}
	QRect ir = imageRect();
	p.drawImage(ir, img_);
	auto draw = [&](QRectF f, QColor c, double w, bool dashed) {
		QRectF r(ir.x() + f.x() * ir.width(), ir.y() + f.y() * ir.height(), f.width() * ir.width(),
			 f.height() * ir.height());
		QPen pen(c, w);
		if (dashed)
			pen.setStyle(Qt::DashLine);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		p.drawRect(r);
	};
	bool match = m_.score >= thr_;
	if (!picker_ && m_.w > 0)
		draw(QRectF(m_.x, m_.y, m_.w, m_.h), match ? QColor(206, 96, 80) : QColor(139, 144, 150),
		     match ? 3 : 1.5, false);
	draw(box_, QColor(201, 154, 59), picker_ ? 2 : 1, true);
	if (box2_.width() > 0)
		draw(box2_, QColor(96, 176, 206), 2, true);
	if (dragging_ && drag_.width() > 0)
		draw(drag_, QColor(232, 229, 221), 1, true);
	p.setPen(QColor(232, 229, 221));
	QString txt =
		picker_    ? caption_
		: match    ? QString("Damage log header found (%1) - downed").arg(m_.score, 0, 'f', 3)
		: m_.w > 0 ? QString("Best candidate %1 is under the threshold - not downed").arg(m_.score, 0, 'f', 3)
			   : "Looking for the damage log header on the right of the game";
	p.fillRect(QRect(ir.x(), ir.bottom() - 20, ir.width(), 20), QColor(11, 14, 16, 170));
	p.drawText(QRect(ir.x(), ir.bottom() - 20, ir.width(), 20), Qt::AlignCenter, txt);
}

void FramePreview::mousePressEvent(QMouseEvent *ev)
{
	if (img_.isNull() || ev->button() != Qt::LeftButton)
		return;
	dragging_ = true;
	start_ = ev->pos();
	drag_ = QRectF();
}

void FramePreview::mouseMoveEvent(QMouseEvent *ev)
{
	if (!dragging_)
		return;
	QRect ir = imageRect();
	auto frac = [&](QPoint p) {
		return QPointF(std::clamp((p.x() - ir.x()) / (double)ir.width(), 0.0, 1.0),
			       std::clamp((p.y() - ir.y()) / (double)ir.height(), 0.0, 1.0));
	};
	drag_ = QRectF(frac(start_), frac(ev->pos())).normalized();
	update();
}

void FramePreview::mouseReleaseEvent(QMouseEvent *)
{
	if (!dragging_)
		return;
	dragging_ = false;
	if (drag_.width() > 0.01 && drag_.height() > 0.005) {
		box_ = drag_;
		emit boxChanged(box_);
	}
	drag_ = QRectF();
	update();
}

// ============================================================ friend dialog

namespace {
class FriendDialog : public QDialog {
public:
	Friend result;
	FriendDialog(const Friend *existing, const std::vector<std::pair<std::string, std::string>> &sources,
		     QWidget *parent, int defaultKbps)
		: QDialog(parent)
	{
		if (existing)
			result = *existing;
		else
			result.vdoKbps = defaultKbps;
		setWindowTitle(existing ? "Edit squad mate" : "Add a squad mate");
		form_ = new QFormLayout(this);
		name_ = new QLineEdit(QString::fromStdString(result.name), this);
		name_->setPlaceholderText("shown on the POV tag");
		form_->addRow("Name", name_);
		gameName_ = new QLineEdit(QString::fromStdString(result.gameName), this);
		gameName_->setPlaceholderText("same as the name above unless it differs in game");
		form_->addRow("In-game name", gameName_);
		kind_ = new QComboBox(this);
		kind_->addItems({"Twitch stream (~2 s, nothing for them to set up)",
				 "VDO.Ninja / WebRTC (~0.3 s, they open one link)", "OBS source I already have",
				 "Discord Go Live (~0.5-1 s, they Go Live in the call)", "(unused)",
				 "Kick stream (~2 s, nothing for them to set up)",
				 "YouTube live stream (~5 s+, nothing for them to set up)"});
		{ // slot 4 is retired: the row is kept so the numbers behind the other kinds stay put
			auto *m = qobject_cast<QStandardItemModel *>(kind_->model());
			if (m && m->item(4))
				m->item(4)->setEnabled(false);
			if (auto *lv = qobject_cast<QListView *>(kind_->view()))
				lv->setRowHidden(4, true);
		}
		form_->addRow("Comes in as", kind_);

		// Twitch
		twitch_ = new QLineEdit(this);
		twitch_->setPlaceholderText("channel name, e.g. sombrero");
		form_->addRow("Twitch channel", twitch_);
		// Kick
		kick_ = new QLineEdit(this);
		kick_->setPlaceholderText("channel name, as in kick.com/<name>");
		form_->addRow("Kick channel", kick_);
		// YouTube
		yt_ = new QLineEdit(this);
		yt_->setPlaceholderText("channel link, @handle, channel ID (UC...) or a live video link");
		form_->addRow("YouTube", yt_);
		// VDO.Ninja
		streamId_ = new QLineEdit(this);
		streamId_->setPlaceholderText("any word you both agree on, e.g. pup-pov");
		form_->addRow("Stream ID", streamId_);
		auto *q = new QHBoxLayout();
		res_ = new QComboBox(this);
		res_->addItems({"720p", "1080p", "1440p"});
		fps_ = new QComboBox(this);
		fps_->addItems({"30 fps", "60 fps"});
		kbps_ = new QSpinBox(this);
		kbps_->setRange(1000, 40000);
		kbps_->setSingleStep(1000);
		kbps_->setSuffix(" kbps max");
		codec_ = new QComboBox(this);
		codec_->addItems({"h264", "vp9", "av1"});
		q->addWidget(res_);
		q->addWidget(fps_);
		q->addWidget(kbps_);
		q->addWidget(codec_);
		qualityRow_ = new QWidget(this);
		qualityRow_->setLayout(q);
		form_->addRow("Quality", qualityRow_);
		auto *linkRow = new QHBoxLayout();
		link_ = new QLineEdit(this);
		link_->setReadOnly(true);
		auto *copy = new QPushButton("Copy", this);
		linkRow->addWidget(link_, 1);
		linkRow->addWidget(copy);
		linkWidget_ = new QWidget(this);
		linkWidget_->setLayout(linkRow);
		form_->addRow("Friend's link", linkWidget_);
		// OBS source
		source_ = new QComboBox(this);
		source_->setEditable(true);
		for (auto &s : sources)
			if (s.first != Config::webSourceName() && s.first != Config::overlaySourceName())
				source_->addItem(QString::fromStdString(s.first));
		source_->setCurrentText(QString::fromStdString(result.source));
		form_->addRow("OBS source", source_);
		// Discord window picker
		pick_ = new QComboBox(this);
		auto *pickRow = new QHBoxLayout();
		pickRow->addWidget(pick_, 1);
		auto *rescan = new QPushButton("Rescan", this);
		pickRow->addWidget(rescan);
		pickWidget_ = new QWidget(this);
		pickWidget_->setLayout(pickRow);
		pickLbl_ = new QLabel("Discord window", this);
		form_->addRow(pickLbl_, pickWidget_);

		trim_ = new QCheckBox("Show the game only, not Discord's window", this);
		trim_->setChecked(result.trim);
		trim_->setToolTip("Crops the flat grey and black edges of their Discord window away, each time "
				  "their feed goes up, so your stream shows their game picture and nothing else.");
		form_->addRow("Borders", trim_);

		hint_ = new QLabel(this);
		hint_->setWordWrap(true);
		form_->addRow(hint_);
		err_ = new QLabel(this);
		err_->setWordWrap(true);
		err_->setStyleSheet("color: #ce6050;");
		form_->addRow(err_);
		auto *bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
		form_->addRow(bb);
		connect(bb, &QDialogButtonBox::accepted, this, [this]() { save(); });
		connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
		connect(copy, &QPushButton::clicked, this,
			[this]() { QApplication::clipboard()->setText(link_->text()); });
		connect(rescan, &QPushButton::clicked, this, [this]() { fillPick(); });
		connect(kind_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { refresh(); });
		for (auto *le : {twitch_, streamId_})
			connect(le, &QLineEdit::textChanged, this, [this](const QString &) { updateLink(); });
		for (auto *cb : {res_, fps_, codec_})
			connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
				[this](int) { updateLink(); });
		connect(kbps_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { updateLink(); });

		// load
		if (result.kind == FriendKind::Twitch)
			twitch_->setText(QString::fromStdString(result.channel));
		if (result.kind == FriendKind::Kick)
			kick_->setText(QString::fromStdString(result.channel));
		if (result.kind == FriendKind::YouTube)
			yt_->setText(QString::fromStdString(result.channel));
		if (result.kind == FriendKind::VdoNinja)
			streamId_->setText(QString::fromStdString(result.channel));
		res_->setCurrentIndex(result.vdoHeight >= 1440 ? 2 : result.vdoHeight >= 1080 ? 1 : 0);
		fps_->setCurrentIndex(result.vdoFps >= 60 ? 1 : 0);
		kbps_->setValue(result.vdoKbps > 0 ? result.vdoKbps : defaultKbps);
		codec_->setCurrentText(QString::fromStdString(result.vdoCodec.empty() ? "h264" : result.vdoCodec));
		kind_->setCurrentIndex((int)result.kind);
		refresh();
		resize(640, 420);
	}

private:
	QFormLayout *form_;
	QLineEdit *name_, *gameName_, *twitch_, *streamId_, *link_, *kick_ = nullptr, *yt_ = nullptr;
	QComboBox *kind_, *source_, *pick_, *res_, *fps_, *codec_;
	QSpinBox *kbps_;
	QWidget *qualityRow_, *linkWidget_, *pickWidget_;
	QLabel *pickLbl_, *hint_, *err_;
	QCheckBox *trim_ = nullptr;
	FriendKind kind() const { return (FriendKind)kind_->currentIndex(); }
	Friend draft() const
	{
		Friend f = result;
		f.name = name_->text().trimmed().toStdString();
		f.gameName = gameName_->text().trimmed().toStdString();
		f.trim = trim_->isChecked();
		f.kind = kind();
		f.vdoHeight = res_->currentIndex() == 2 ? 1440 : res_->currentIndex() == 1 ? 1080 : 720;
		f.vdoFps = fps_->currentIndex() == 1 ? 60 : 30;
		f.vdoKbps = kbps_->value();
		f.vdoCodec = codec_->currentText().toStdString();
		if (f.kind == FriendKind::Twitch)
			f.channel = twitch_->text().trimmed().toLower().remove('@').toStdString();
		else if (f.kind == FriendKind::Kick)
			f.channel = SquadInput::kickSlug(kick_->text());
		else if (f.kind == FriendKind::YouTube)
			f.channel = SquadInput::youTubeId(yt_->text());
		else if (f.kind == FriendKind::VdoNinja)
			f.channel = streamId_->text().trimmed().toStdString();
		else if (f.kind == FriendKind::ObsSource)
			f.source = source_->currentText().trimmed().toStdString();
		else {
			QString v = pick_->currentData().toString();
			if (v.isEmpty() && pick_->isEditable()) {
				v = pick_->currentText().trimmed();
				if (v.startsWith('(')) // one of the "nothing found" lines
					v.clear();
			}
			f.channel = v.toStdString();
		}
		return f;
	}
	void fillPick()
	{
		pick_->clear();
		FriendKind k = kind();
		if (k == FriendKind::Discord) {
			// only popped-out streams: Discord titles those "<username>'s Stream", and every other
			// Discord window (the main one, the updater, a screen-share picker) is the wrong choice
			int first = -1;
			for (auto &w : Switcher::listProperty("window_capture", "window")) {
				QString v = QString::fromStdString(w.second), n = QString::fromStdString(w.first);
				bool discord = v.endsWith("Discord.exe", Qt::CaseInsensitive);
				if (!discord || !n.contains("stream", Qt::CaseInsensitive))
					continue;
				if (first < 0)
					first = pick_->count();
				pick_->addItem("Pop-out: " + n.mid(n.indexOf("]: ") + 3), v);
			}
			if (first < 0) {
				pick_->addItem(
					"(no popped-out stream open - in Discord, right-click their stream and Pop Out)",
					"");
				first = 0;
			}
			// the by-executable match is still there, at the bottom: it follows whichever Discord
			// window is up, which is what "not popped out" means
			pick_->addItem("Any Discord window (not popped out: the Discord window itself)",
				       Friend::anyDiscordWindow());
			pick_->setCurrentIndex(first);
		} else
			pick_->setEditable(false);
		if (!result.channel.empty()) {
			int i = pick_->findData(QString::fromStdString(result.channel));
			if (i >= 0)
				pick_->setCurrentIndex(i);
		}
	}
	void refresh()
	{
		FriendKind k = kind();
		form_->setRowVisible(twitch_, k == FriendKind::Twitch);
		form_->setRowVisible(kick_, k == FriendKind::Kick);
		form_->setRowVisible(yt_, k == FriendKind::YouTube);
		form_->setRowVisible(streamId_, k == FriendKind::VdoNinja);
		form_->setRowVisible(qualityRow_, k == FriendKind::VdoNinja);
		form_->setRowVisible(linkWidget_, k == FriendKind::VdoNinja);
		form_->setRowVisible(source_, k == FriendKind::ObsSource);
		form_->setRowVisible(pickWidget_, k == FriendKind::Discord);
		form_->setRowVisible(trim_, k == FriendKind::Discord);
		if (k == FriendKind::Discord)
			fillPick();
		err_->clear();
		switch (k) {
		case FriendKind::Twitch:
			hint_->setText(
				"A browser source named \"Kennel web\" plays this channel with its audio routed through OBS. They just need to be live; ask them to keep Twitch low-latency mode on. Their stream includes their mic.");
			break;
		case FriendKind::VdoNinja:
			hint_->setText(
				"Send them the link: they open it in Chrome or Edge, pick their game window or screen and tick \"Share system audio\". No mic is sent. Quality here is a ceiling; WebRTC settles lower by itself on a weak link. 1080p60 at 12000 is right for a wired line or fibre, 4000-6000 for a weak upload.");
			break;
		case FriendKind::Discord:
			hint_->setText(
				"They press Go Live in the call. Open their stream in Discord and pop it out into its own window. \"Any Discord window\" follows the pop-out automatically; pick a specific window only if you have several. On Save a Window Capture of it is created in your scene. 720p without Nitro. Its sound is not handled: Discord hands OBS one mix for the whole call, so it comes through whatever already carries Discord on your stream.");
			break;
		case FriendKind::Kick:
			hint_->setText(
				"Kick's own player in a browser source, like Twitch. Type the channel as it appears in the address bar (kick.com/<name>). They just need to be live. Their stream includes their mic; its sound is not played unless you tick that under Squad & POV.");
			break;
		case FriendKind::YouTube:
			hint_->setText(
				"YouTube's player in a browser source. Paste their channel link, @handle, channel ID or a link to the live video. With a channel, whatever they are streaming right now is shown; a video link shows that one stream. YouTube adds several seconds of delay of its own, and the channel has to allow embedding (most do).");
			break;
		default:
			hint_->setText(
				"Any source already in OBS: a capture card or a second PC. Its audio comes with it.");
		}
		updateLink();
	}
	void updateLink()
	{
		Friend f = draft();
		link_->setText(f.kind == FriendKind::VdoNinja && !f.channel.empty()
				       ? QString::fromStdString(Switcher::vdoPushUrl(f))
				       : QString());
	}
	void save()
	{
		Friend f = draft();
		if (f.kind == FriendKind::ObsSource && f.source.empty()) {
			err_->setText("Pick or type the OBS source name.");
			return;
		}
		if (f.kind == FriendKind::Twitch && f.channel.empty()) {
			err_->setText("Type the Twitch channel name.");
			return;
		}
		if (f.kind == FriendKind::Kick && f.channel.empty()) {
			err_->setText("Type the Kick channel name.");
			return;
		}
		if (f.kind == FriendKind::YouTube && f.channel.empty()) {
			err_->setText(
				"That does not look like a YouTube channel link, @handle, channel ID or video link.");
			return;
		}
		if (f.kind == FriendKind::YouTube && f.channel[0] == '@') {
			// a handle is only a name; the player needs the channel's ID, which the channel page
			// carries. One fetch, here, once.
			err_->setText("Looking up the channel ID for " + QString::fromStdString(f.channel) + "...");
			QString id = SquadInput::resolveYouTubeHandle(QString::fromStdString(f.channel));
			if (id.isEmpty()) {
				err_->setText("Could not find the channel ID for that handle (no internet, or YouTube "
					      "changed its page). Paste the channel ID (starts UC...) or a link to "
					      "the live video instead.");
				return;
			}
			f.channel = id.toStdString();
		}
		if (f.kind == FriendKind::VdoNinja && f.channel.empty()) {
			err_->setText("Type a stream ID (any word you both agree on).");
			return;
		}
		if (f.kind == FriendKind::Discord && f.channel.empty()) {
			err_->setText(
				"No popped-out stream to pick. In Discord, right-click their stream and choose Pop "
				"Out, then open this again - or choose \"Any Discord window\".");
			return;
		}
		if (f.kind == FriendKind::Discord && f.channel.empty())
			f.channel = "Discord:Chrome_WidgetWin_1:Discord.exe";
		if (f.kind == FriendKind::Discord) {
			// a picked pop-out is titled "<username>'s Stream": that username is who this is, so
			// it is the slot's name unless one was typed, and always the handle the watcher matches
			QString label = pick_->currentText();
			if (label.startsWith("Pop-out: ")) {
				QString owner = label.mid(9).trimmed();
				static const QRegularExpression suffix(
					QStringLiteral("\\s*(?:['\u2019\u2018]s?)?\\s*stream\\s*$"),
					QRegularExpression::CaseInsensitiveOption);
				QRegularExpressionMatch m = suffix.match(owner);
				if (m.hasMatch() && m.capturedStart() > 0)
					owner = owner.left(m.capturedStart()).trimmed();
				if (!owner.isEmpty()) {
					if (f.name.empty())
						f.name = owner.toLower().toStdString();
					// The slot's username is only ever the owner of the window picked. A typed
					// name that is somebody else means the wrong window was left selected (the
					// picker preselects the first pop-out): ask, rather than bind Cy to gazreyn.
					if (QString::fromStdString(f.name).compare(owner, Qt::CaseInsensitive) != 0) {
						QMessageBox box(
							QMessageBox::Question, "Kennel.gg Wardogs",
							"The window picked is " + owner +
								"'s stream, but the name is " +
								QString::fromStdString(f.name) +
								".\n\nA slot shows the window it is given, whoever it is named "
								"after. Which did you mean?",
							QMessageBox::NoButton, this);
						auto *useOwner =
							box.addButton("Name it " + owner, QMessageBox::AcceptRole);
						box.addButton("Keep " + QString::fromStdString(f.name) +
								      " on that window",
							      QMessageBox::ActionRole);
						auto *cancel = box.addButton(QMessageBox::Cancel);
						box.setDefaultButton(useOwner);
						box.exec();
						if (box.clickedButton() == cancel)
							return;
						if (box.clickedButton() == useOwner)
							f.name = owner.toLower().toStdString();
					}
					f.handle =
						QString::fromStdString(f.name).compare(owner, Qt::CaseInsensitive) == 0
							? owner.toLower().toStdString()
							: std::string(); // named after someone else: no username to match on
				}
			}
		}
		if (f.name.empty())
			f.name = f.kind == FriendKind::ObsSource ? f.source
				 : f.kind == FriendKind::Discord ? "Discord"
								 : f.channel;
		result = f;
		accept();
	}
};
} // namespace

// ============================================================ SettingsDialog

namespace {
/// A mouse wheel over the settings window used to change whatever spin box or drop-down happened to
/// be under the pointer - which is how a bridge port quietly became 47821 mid-session and ClipHound
/// could never connect again. Scrolling now moves the page; a control takes the wheel only once it
/// has been clicked into.
class WheelGuard : public QObject {
public:
	using QObject::QObject;
	bool eventFilter(QObject *o, QEvent *e) override
	{
		if (e->type() != QEvent::Wheel)
			return false;
		auto *w = qobject_cast<QWidget *>(o);
		if (!w || w->hasFocus())
			return false;
		e->ignore();
		return true; // let it fall through to the scroll area instead
	}
};
} // namespace

SettingsDialog::SettingsDialog(Engine *engine, QWidget *parent) : QDialog(parent), e_(engine)
{
	setWindowTitle("Kennel.gg Wardogs Streaming Tool");
	setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
	setSizeGripEnabled(true);
	setMinimumSize(560, 400); // it scrolls now, so it can be made genuinely small
	resize(900, 720);
	auto *v = new QVBoxLayout(this);
	// the same header as the dock, so the two read as one product
	{
		setObjectName("kennelSettings");
		setStyleSheet(
			"#kennelSettings QLabel#wordmark { color: #ece7db; font-family: \"Saira Condensed\"; font-size: 17pt; "
			"font-weight: 700; letter-spacing: 1px; } "
			"#kennelSettings QLabel#version { color: #7c8076; font-family: \"IBM Plex Mono\"; font-size: 8pt; } "
			"#kennelSettings QGroupBox { font-family: \"Saira Condensed\"; font-size: 12pt; font-weight: 700; "
			"letter-spacing: 1px; margin-top: 14px; } "
			"#kennelSettings QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; color: #c99a3b; }");
		auto *head = new QHBoxLayout();
		head->setSpacing(8);
		auto *mark = new QLabel(this);
		char *p = obs_module_file("brand/hound_mark.png");
		if (p) {
			QPixmap px(QString::fromUtf8(p));
			bfree(p);
			if (!px.isNull())
				mark->setPixmap(px.scaledToHeight(28, Qt::SmoothTransformation));
		}
		head->addWidget(mark);
		auto *wm = new QLabel("KENNEL.GG WARDOGS", this);
		wm->setObjectName("wordmark");
		head->addWidget(wm);
		head->addStretch(1);
		auto *ver = new QLabel(QString("v%1  ·  kennel.gg").arg(PLUGIN_VERSION), this);
		ver->setObjectName("version");
		head->addWidget(ver);
		v->addLayout(head);
	}
	auto *tabs = new QTabWidget(this);
	// Every tab scrolls. On a small screen, at 125 % Windows scaling or with a large font, the
	// contents used to be squeezed into whatever height was left instead of keeping their own.
	auto scrolled = [tabs](QWidget *page, const char *name) {
		auto *sa = new QScrollArea(tabs);
		sa->setWidget(page);
		sa->setWidgetResizable(true);
		sa->setFrameShape(QFrame::NoFrame);
		sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
		page->setMinimumWidth(560); // narrower than this and the two-column rows get silly
		tabs->addTab(sa, name);
	};
	tabs_ = tabs;
	for (int i = 0; i < PCount; i++) {
		pageW_[i] = new QWidget(this);
		pages_[i] = new QVBoxLayout(pageW_[i]);
	}
	buildGeneral();
	// each builder puts its groups on the page they belong to; what is left of its own page is empty
	for (QWidget *left : {buildSwitchTab(), buildClipsTab(), buildAppTab()})
		left->hide();
	pages_[PLook]->addWidget(buildLookTab());
	{
		auto *gd = new QGroupBox("Downed detection", pageW_[PAdvanced]);
		auto *gl = new QVBoxLayout(gd);
		gl->addWidget(buildDetectTab());
		pages_[PAdvanced]->insertWidget(0, gd, 1);
	}
	pages_[PDual]->addWidget(buildDualTab());
	pages_[PDetect]->addWidget(buildAreasTab(), 1);
	pages_[PVoice]->addWidget(buildVoiceTab());
	pages_[PGeneral]->addWidget(genAppBox_);
	for (int i : {PGeneral, PVertical, PLook, PAdvanced})
		pages_[i]->addStretch(1);
	scrolled(pageW_[PGeneral], "General");
	scrolled(pageW_[PSquad], "Squad && POV");
	scrolled(pageW_[PDual], "Dual POV");
	scrolled(pageW_[PClips], "Clips && replays");
	scrolled(pageW_[PDetect], "Detect areas");
	scrolled(pageW_[PVertical], "Vertical (beta)");
	scrolled(pageW_[PVoice], "Voice (beta)");
	scrolled(pageW_[PLook], "Stream look");
	scrolled(pageW_[PAdvanced], "Advanced");
	tabs->addTab(buildLogsTab(), "Logs"); // already a scrolling text view
	scrolled(buildAboutTab(), "Help");
	tabKeys_ = {"general", "squad", "dual",     "clips", "detect", "vertical",
		    "voice",   "look",  "advanced", "logs",  "help"};
	// every spin box and drop-down: no accidental changes from a scroll (see WheelGuard)
	{
		auto *guard = new WheelGuard(this);
		for (QWidget *w : findChildren<QWidget *>())
			if (qobject_cast<QSpinBox *>(w) || qobject_cast<QDoubleSpinBox *>(w) ||
			    qobject_cast<QComboBox *>(w) || qobject_cast<QSlider *>(w)) {
				w->setFocusPolicy(Qt::StrongFocus);
				w->installEventFilter(guard);
			}
	}
	building_ = false;
	fillSources(); // again, now that every tab that shows a source list exists
	v->addWidget(tabs, 1);
	auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
	v->addWidget(bb);
	connect(bb, &QDialogButtonBox::rejected, this, &QDialog::close);
	e_->wantPreview(true);
	connect(e_, &Engine::frameUpdated, this, [this]() {
		Match m = e_->lastGame();
		meter_->setValue(m.score < 0 ? 0 : (int)(m.score * 1000));
		frame_->setFrame(e_->lastFrame(), m, e_->cfg.threshold,
				 QRectF(e_->cfg.boxX, e_->cfg.boxY, e_->cfg.boxW, e_->cfg.boxH));
		if (areaEd_) {
			areaEd_->setFrame(e_->lastFrame());
			refreshAreas();
		}
	});
	connect(e_, &Engine::stateChanged, this, [this]() { updateAreas(); });
	connect(e_, &Engine::stateChanged, this, [this]() {
		tplLbl_->setText(!e_->hasTemplate()     ? "No template"
				 : e_->customTemplate() ? "Custom template"
							: "Built-in template");
	});
}

SettingsDialog::~SettingsDialog()
{
	e_->wantPreview(false);
	if (previewing_)
		e_->previewLook(false);
}

void SettingsDialog::showPage(const QString &key)
{
	int i = tabKeys_.indexOf(key);
	if (tabs_ && i >= 0 && i < tabs_->count())
		tabs_->setCurrentIndex(i);
}

static QLabel *muted(const QString &t, QWidget *p);

/// Who you are, your game and scene, and ClipHound: everything a new setup needs, on one page. The
/// game-and-scene group itself comes from the squad builder, the language row from detection.
void SettingsDialog::buildGeneral()
{
	QWidget *pg = pageW_[PGeneral];
	auto *you = new QGroupBox("You", pg);
	genYou_ = new QFormLayout(you);
	playerName_ = new QLineEdit(QString::fromStdString(e_->cfg.playerName), you);
	playerName_->setPlaceholderText("your name in WARDOGS, if left blank");
	playerName_->setToolTip("Shown to squad mates on the same network and on the highlights title card.");
	auto *me = new QHBoxLayout();
	discordUser_ = new QLineEdit(QString::fromStdString(e_->cfg.myDiscord), you);
	discordUser_->setPlaceholderText("the lower-case one under your display name");
	auto *detect = new QPushButton("Detect", you);
	detect->setToolTip("Ask the Discord app on this PC who it is logged in as.");
	me->addWidget(discordUser_, 1);
	me->addWidget(detect);
	pages_[PGeneral]->addWidget(you);
	// the in-game name row goes in first, from the kill-feed builder that owns the field
	genYou_->addRow("Name shown to others", playerName_);
	genYou_->addRow("Your Discord username", me);
	genYou_->addRow(muted("Your Discord username lets the Kennel.gg bot follow your voice channel and keeps "
			      "your own stream out of your squad.",
			      you));
	connect(playerName_, &QLineEdit::editingFinished, this, [this]() { saveAndApply(); });
	connect(discordUser_, &QLineEdit::editingFinished, this, [this]() {
		QString v = discordUser_->text().trimmed().toLower();
		if (v.toStdString() != e_->cfg.myDiscord)
			e_->setMyDiscord(v);
	});
	connect(detect, &QPushButton::clicked, this, [this]() { e_->detectDiscordUser(true); });
	connect(e_, &Engine::discordUserDetected, this, [this](const QString &u, bool) {
		if (!u.isEmpty() && discordUser_)
			discordUser_->setText(u);
	});
	genAppBox_ = new QGroupBox("ClipHound", pg);
	genApp_ = new QFormLayout(genAppBox_);
	genApp_->addRow(muted("ClipHound reads the kill feed for clips, the NEARBY list for Closest, and listens "
			      "for voice commands. It runs in the background; its connection settings are under "
			      "Advanced.",
			      genAppBox_));
}

static QLabel *muted(const QString &t, QWidget *p)
{
	auto *l = new QLabel(t, p);
	l->setWordWrap(true);
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

QWidget *SettingsDialog::buildSwitchTab()
{
	auto *w = new QWidget(this);
	auto *v = new QVBoxLayout(w);

	auto *g1 = new QGroupBox("Your game and scene", w);
	auto *f1 = new QFormLayout(g1);
	genGame_ = f1;
	game_ = new QComboBox(g1);
	scene_ = new QComboBox(g1);
	auto *refresh = new QPushButton("Refresh", g1);
	auto *mkGame = new QPushButton("Create Game Capture", g1);
	auto *gr = new QHBoxLayout();
	gr->addWidget(game_, 1);
	gr->addWidget(refresh);
	gr->addWidget(mkGame);
	connect(mkGame, &QPushButton::clicked, this, [this]() {
		std::string e = e_->sw.createGameCapture(e_->cfg);
		if (!e.empty()) {
			QMessageBox::warning(this, "Kennel.gg Wardogs", QString::fromStdString(e));
			return;
		}
		e_->cfg.save();
		fillSources();
		e_->reloadConfig();
	});
	f1->addRow("Your game source", gr);
	f1->addRow("Scene", scene_);
	f1->addRow(muted(
		"The game source is watched for the damage log (rendered on its own, so it can stay under the squad mate's feed). Squad mates are shown on top of it in this scene, and only this scene. Browser sources for Twitch / VDO.Ninja and the look overlay are created here when first needed.",
		g1));
	pages_[PGeneral]->addWidget(g1);
	connect(refresh, &QPushButton::clicked, this, [this]() { fillSources(); });
	connect(game_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
		if (gameDetect_ && i >= 0 && gameDetect_->currentText() != game_->currentText()) {
			gameDetect_->blockSignals(true);
			gameDetect_->setCurrentText(game_->currentText());
			gameDetect_->blockSignals(false);
		}
		saveAndApply();
	});
	connect(scene_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { saveAndApply(); });
	// ----- the vertical canvas, beta
	auto *gv = new QGroupBox("Vertical canvas (beta)", w);
	auto *fv = new QFormLayout(gv);
	verticalOn_ = new QCheckBox("Also swap and replay on a vertical canvas", gv);
	verticalOn_->setChecked(e_->cfg.verticalEnabled);
	fv->addRow(verticalOn_);
	sceneV_ = new QComboBox(gv);
	fv->addRow("Vertical scene", sceneV_);
	fv->addRow(muted(
		"For a second, portrait canvas (OBS 32's own canvases, as Aitum Stream Suite makes them): pick the "
		"vertical scene your portrait stream shows. The POV swap happens there as well - the squad mate's feed "
		"full-height, sides cropped, the look overlay in its portrait form - and the instant replay plays "
		"there too, full width and centred. Same sources, so nothing is decoded twice. Beta: tell us what "
		"you see.",
		gv));
	{
		auto *tr = new QHBoxLayout();
		lookTopV_ = new QSpinBox(gv);
		lookTopV_->setRange(0, 90);
		lookTopV_->setSuffix(" %");
		lookTopV_->setValue(e_->cfg.lookTopV);
		lookTopV_->setToolTip(
			"Where the POV tag sits on the portrait canvas, as a share of its height from the top. "
			"Lower it to clear a camera or a title at the top.");
		tr->addWidget(new QLabel("POV tag height", gv));
		tr->addWidget(lookTopV_);
		tr->addWidget(muted("of the way down the portrait canvas", gv), 1);
		fv->addRow(tr);
		connect(lookTopV_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { saveAndApply(); });
	}
	onTopV_ = new QListWidget(gv);
	onTopV_->setMaximumHeight(140);
	onTopV_->setDragDropMode(QAbstractItemView::InternalMove);
	fv->addRow("Always on top here", onTopV_);
	fv->addRow(muted("The vertical scene's own camera and alerts: they are lifted back over the squad mate, the "
			 "overlay and the replay every time, like the main scene's list above. Guessed once; tick what "
			 "is missing. Empty, and the main list's names are used if the scene has them.",
			 gv));
	connect(onTopV_, &QListWidget::itemChanged, this, [this](QListWidgetItem *) { saveAndApply(); });
	connect(onTopV_->model(), &QAbstractItemModel::rowsMoved, this, [this]() { saveAndApply(); });
	pages_[PVertical]->addWidget(gv);
	connect(verticalOn_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	connect(sceneV_, &QComboBox::currentIndexChanged, this, [this](int) { saveAndApply(); });

	auto *g2 = new QGroupBox("Squad mates", w);
	auto *h2 = new QHBoxLayout(g2);
	friends_ = new QTableWidget(0, 3, g2);
	friends_->setHorizontalHeaderLabels({"Name", "Comes in as", "Source / channel"});
	friends_->horizontalHeader()->setStretchLastSection(true);
	friends_->setSelectionBehavior(QAbstractItemView::SelectRows);
	friends_->setSelectionMode(QAbstractItemView::SingleSelection);
	friends_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	friends_->verticalHeader()->hide();
	h2->addWidget(friends_, 1);
	auto *fb = new QVBoxLayout();
	auto *add = new QPushButton("Add...", g2);
	auto *edit = new QPushButton("Edit...", g2);
	auto *rem = new QPushButton("Remove", g2);
	auto *act = new QPushButton("Make active", g2);
	auto *testBtn = new QPushButton("Test feed", g2);
	testBtn->setToolTip("Watches the selected squad mate's feed for two seconds and tells you how many "
			    "new pictures a second are actually arriving.");
	for (auto *b : {add, edit, rem, act, testBtn})
		fb->addWidget(b);
	fb->addStretch(1);
	h2->addLayout(fb);
	pages_[PSquad]->addWidget(g2, 1);
	connect(testBtn, &QPushButton::clicked, this, [this]() { testFeed(); });
	connect(add, &QPushButton::clicked, this, [this]() { editFriend(-1); });
	connect(edit, &QPushButton::clicked, this, [this]() { editFriend(friends_->currentRow()); });
	connect(friends_, &QTableWidget::cellDoubleClicked, this, [this](int r, int) { editFriend(r); });
	connect(rem, &QPushButton::clicked, this, [this]() {
		int r = friends_->currentRow();
		if (r < 0 || r >= (int)e_->cfg.friends.size())
			return;
		Friend f = e_->cfg.friends[r];
		// the sources we made for them go too, unless they say otherwise: a season of squad mates
		// otherwise leaves a scene full of dead captures
		std::vector<std::string> mine = Switcher::friendSourceNames(e_->cfg, f);
		if (!mine.empty()) {
			QStringList list;
			for (const auto &n : mine)
				list << QString::fromStdString(n);
			QMessageBox box(QMessageBox::Question, "Kennel.gg Wardogs",
					"Remove " + QString::fromStdString(f.name) +
						"?\n\nThese sources were made for them:\n  " + list.join("\n  "),
					QMessageBox::NoButton, this);
			QPushButton *both = box.addButton("Remove and delete the sources", QMessageBox::AcceptRole);
			QPushButton *justOne = box.addButton("Remove, keep the sources", QMessageBox::ActionRole);
			box.addButton(QMessageBox::Cancel);
			box.setDefaultButton(both);
			box.exec();
			if (box.clickedButton() != both && box.clickedButton() != justOne)
				return;
			e_->releasePopout(f);
			if (box.clickedButton() == both)
				e_->sw.removeFriendSources(e_->cfg, f);
		}
		e_->cfg.friends.erase(e_->cfg.friends.begin() + r);
		if (e_->cfg.activeFriend >= (int)e_->cfg.friends.size())
			e_->cfg.activeFriend = std::max(0, (int)e_->cfg.friends.size() - 1);
		e_->cfg.save();
		e_->armPopoutWatch();
		e_->log("Squad: removed " + QString::fromStdString(f.name) + ".");
		fillFriends();
		emit e_->stateChanged();
	});
	connect(act, &QPushButton::clicked, this, [this]() {
		int r = friends_->currentRow();
		if (r >= 0) {
			e_->setActive(r);
			fillFriends();
		}
	});

	auto *gc = new QGroupBox("Show whoever is closest", w);
	auto *fc = new QFormLayout(gc);
	nearOn_ = new QCheckBox(
		"When you go down, show the squad mate the game says is nearest (needs ClipHound running)", gc);
	nearOn_->setChecked(e_->cfg.nearEnabled);
	fc->addRow(nearOn_);
	nearFollow_ = new QCheckBox("Keep following the nearest one while you are down", gc);
	nearFollow_->setChecked(e_->cfg.nearFollow);
	fc->addRow(nearFollow_);
	nearMax_ = new QSpinBox(gc);
	nearMax_->setRange(0, 100); // the NEARBY list never shows more than 100 m
	nearMax_->setSuffix(" m or closer");
	nearMax_->setSpecialValueText("any distance");
	nearMax_->setValue(e_->cfg.nearMaxM);
	fc->addRow("Swap over only for someone", nearMax_);
	connect(nearMax_, &QSpinBox::editingFinished, this, [this]() { saveAndApply(); });
	auto *cdRow = new QHBoxLayout();
	nearCooldown_ = new QSlider(Qt::Horizontal, gc);
	nearCooldown_->setRange(3, 30);
	nearCooldown_->setValue(std::clamp(e_->cfg.nearCooldownS, 3, 30));
	nearCooldown_->setTickPosition(QSlider::TicksBelow);
	nearCooldown_->setTickInterval(3);
	nearCdLbl_ = new QLabel(gc);
	cdRow->addWidget(nearCooldown_, 1);
	cdRow->addWidget(nearCdLbl_);
	fc->addRow("Wait between swaps", cdRow);
	auto showCd = [this]() {
		int v = nearCooldown_->value();
		nearCdLbl_->setText(QString("%1 s").arg(v) + (v <= 6    ? "  (follows them as they move)"
							      : v >= 15 ? "  (settles on one feed and stays)"
									: ""));
	};
	showCd();
	connect(nearCooldown_, &QSlider::valueChanged, this, [showCd](int) { showCd(); });
	connect(nearCooldown_, &QSlider::sliderReleased, this, [this]() { saveAndApply(); });
	connect(nearCooldown_, &QSlider::actionTriggered, this,
		[this](int a) { // keyboard and click-on-groove changes never send sliderReleased
			if (a != QAbstractSlider::SliderMove)
				QTimer::singleShot(0, this, [this]() { saveAndApply(); });
		});
	fc->addRow(muted(
		"How long the feed stays on one squad mate before it may swap to a closer one, while you are down and they are running to you. 4 s is the default: low values follow whoever is nearest as they move, high values pick one and leave it. The swap the moment you go down never waits.",
		gc));
	nearLbl_ = new QLabel(e_->nearbyStatus(), gc);
	nearLbl_->setWordWrap(true);
	fc->addRow("Nearby now", nearLbl_);
	fc->addRow(muted(
		"The moment you go down, ClipHound reads the NEARBY list in the bottom-right corner of your game and tells the plugin who is how far away, so the POV you cut to is the squad mate who can actually revive you. Nothing is read while you are up, so it costs nothing between fights. While this is on, the dock's Closest button is lit and the squad mate shown follows the nearest one by itself; press a squad mate on the dock (or untick this) to choose yourself. It needs ClipHound running, the blue NEARBY box set under Advanced, and each squad mate's in-game name typed in the Squad window. Without a reading, the squad mate picked on the dock is used as before.",
		gc));
	pages_[PSquad]->addWidget(gc);
	connect(nearOn_, &QCheckBox::toggled, this, [this](bool on) {
		if (on && !e_->appConnected()) {
			auto r = QMessageBox::question(
				this, "Kennel.gg Wardogs",
				"Closest needs ClipHound running: it reads the NEARBY list in the corner of your game. It is not running, so Closest stays off.\n\nStart it now? Tick this again once the dock says ClipHound is connected.",
				QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes);
			nearOn_->blockSignals(true);
			nearOn_->setChecked(false);
			nearOn_->blockSignals(false);
			if (r == QMessageBox::Yes)
				e_->launchApp();
			return;
		}
		saveAndApply();
	});
	connect(nearFollow_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	connect(e_, &Engine::stateChanged, this, [this]() {
		if (nearLbl_)
			nearLbl_->setText(e_->nearbyStatus());
		if (nearOn_ && nearOn_->isChecked() != e_->cfg.nearEnabled) {
			nearOn_->blockSignals(true);
			nearOn_->setChecked(e_->cfg.nearEnabled); // ticked from the dock
			nearOn_->blockSignals(false);
		}
	});

	auto *gRoster = new QGroupBox("Squad from Discord", w);
	auto *fRoster = new QFormLayout(gRoster);
	rosterOn_ = new QCheckBox("Fill squad slots from who is sharing in Discord", gRoster);
	rosterOn_->setChecked(e_->cfg.rosterEnabled);
	fRoster->addRow(rosterOn_);
	rosterUrl_ = new QLineEdit(QString::fromStdString(e_->cfg.rosterUrl), gRoster);
	rosterUrl_->setPlaceholderText("https://kennel.gg/api/voice-....json");
	rosterUrl_->hide(); // the Kennel.gg bot's: squad automation is driven by it, not by a copy
	rosterChannel_ = new QLineEdit(QString::fromStdString(e_->cfg.rosterChannel), gRoster);
	rosterChannel_->setPlaceholderText("(any voice channel)");
	rosterChannel_->hide(); // the channel is whichever one you are sitting in
	rosterSources_ = new QCheckBox("Also make their Discord capture for them", gRoster);
	rosterSources_->setChecked(e_->cfg.rosterAddSources);
	fRoster->addRow(rosterSources_);
	rosterStatus_ = new QLabel(gRoster);
	rosterStatus_->setWordWrap(true);
	fRoster->addRow(rosterStatus_);
	auto *rosterNote = new QLabel(
		"Discord will not tell a plugin who is in a call, so the Kennel Ops bot in the Kennel.gg Discord "
		"publishes it. It is for members of that server: the Discord username from Setup is checked "
		"against it, and the bot then follows whichever voice channel you are in. When somebody goes live in the call a squad slot "
		"appears with their Discord name on it, and it goes away again when they stop. Slots you "
		"added yourself are never touched.",
		gRoster);
	rosterNote->setWordWrap(true);
	rosterNote->setStyleSheet("color: palette(mid);");
	fRoster->addRow(rosterNote);
	auto showRoster = [this]() {
		QString s = e_->rosterStatus();
		QStringList live;
		for (const auto &m : e_->roster.streamers())
			live << m.name;
		if (!live.isEmpty())
			s += "  -  sharing: " + live.join(", ");
		rosterStatus_->setText(s);
	};
	connect(&e_->roster, &Roster::changed, this, showRoster);
	connect(&e_->roster, &Roster::polled, this, showRoster);
	showRoster();
	pages_[PSquad]->addWidget(gRoster);

	auto *g3 = new QGroupBox("Sound while a squad mate is on screen", w);
	auto *v3 = new QVBoxLayout(g3);
	friendAudio_ = new QCheckBox(
		"Play the squad mate's sound while they are on screen (Twitch, Kick, YouTube and VDO.Ninja)", g3);
	friendAudio_->setChecked(e_->cfg.friendAudio);
	v3->addWidget(friendAudio_);
	v3->addWidget(muted(
		"On by default for Twitch, Kick, YouTube and VDO.Ninja squad mates: whoever is on screen is the one feed with sound, and your own game sound (ticked below) is muted meanwhile. Discord is different. Discord hands OBS one mix for the whole call, so a Discord squad mate's sound is not handled by the plugin: nothing of yours is muted while they are shown, and their sound comes through whatever already carries Discord on your stream.",
		g3));
	auto *h3 = new QHBoxLayout();
	mute_ = new QListWidget(g3);
	h3->addWidget(mute_, 1);
	h3->addWidget(
		muted("What of YOURS is muted while a Twitch, Kick, YouTube or VDO.Ninja squad mate is on screen; never for a Discord one. Your game's sound is ticked by default (the game source when it carries audio, otherwise Desktop Audio), so their POV comes with their sound and not yours on top. Untick it to hear both. Do NOT tick your microphone; it keeps going either way. Ticked inputs are put back exactly as they were when you are revived.",
		      g3),
		1);
	v3->addLayout(h3, 1);
	pages_[PSquad]->addWidget(g3, 1);
	connect(mute_, &QListWidget::itemChanged, this, [this](QListWidgetItem *) { saveAndApply(); });
	connect(friendAudio_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	connect(e_, &Engine::stateChanged, this, [this]() {
		if (friendAudio_ && friendAudio_->isChecked() != e_->cfg.friendAudio) {
			friendAudio_->blockSignals(true);
			friendAudio_->setChecked(e_->cfg.friendAudio); // pressed on the dock, or the hotkey
			friendAudio_->blockSignals(false);
		}
	});

	auto *gTop = new QGroupBox("Always on top", w);
	auto *vTop = new QVBoxLayout(gTop);
	auto *hTop = new QHBoxLayout();
	onTop_ = new QListWidget(gTop);
	hTop->addWidget(onTop_, 1);
	hTop->addWidget(
		muted("Tick your face cam and your alerts. They are lifted back over the top every time the plugin shows a squad mate, brings up the Dual POV window or adds a source, so nothing of ours ever covers your camera or an alert. The order here is the order they stack, first is the topmost - drag to change it. Your camera and anything that looks like alerts are ticked for you to begin with.",
		      gTop),
		1);
	vTop->addLayout(hTop, 1);
	pages_[PSquad]->addWidget(gTop, 1);
	onTop_->setDragDropMode(QAbstractItemView::InternalMove);
	connect(onTop_, &QListWidget::itemChanged, this, [this](QListWidgetItem *) { saveAndApply(); });
	connect(onTop_->model(), &QAbstractItemModel::rowsMoved, this, [this]() { saveAndApply(); });

	auto *g4 = new QGroupBox("Extras", w);
	auto *v4 = new QVBoxLayout(g4);
	bringFront_ = new QCheckBox("Move the squad mate's feed to the top of the scene when shown", g4);
	invSwitch_ = new QCheckBox("Magazine packing / inventory POV switching: show a squad mate while your inventory "
				   "screen is open (needs ClipHound)",
				   g4);
	invSwitch_->setChecked(e_->cfg.invSwitch);
	invSwitch_->setToolTip(
		"Repacking magazines means standing still with the inventory open. ClipHound reads the "
		"\"COMBINE AMMO\" hint on that screen; after two seconds of it a squad mate's POV goes on "
		"stream, and yours comes back when it closes.");
	v4->addWidget(invSwitch_);
	connect(invSwitch_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	keepWarm_ = new QCheckBox(
		"Keep the squad mate's feed warm: leave the source on but invisible and muted, so the player never reconnects (instant switch)",
		g4);
	preload_ = new QCheckBox(
		"Keep every squad mate's feed loaded and playing, hidden and silent, so there is no black screen while it starts (uses their bandwidth for each one)",
		g4);
	preload_->setChecked(e_->cfg.preloadFeeds);
	v4->addWidget(bringFront_);
	v4->addWidget(preload_);
	connect(preload_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	v4->addWidget(keepWarm_);
	pictureCheck_ =
		new QCheckBox("Only show a Discord squad mate while their capture has a game picture in it: never "
			      "Discord's call screen, a text channel or a \"stream ended\" card",
			      g4);
	pictureCheck_->setToolTip(
		"The capture is looked at every second or two, under the hide filter, so it works before "
		"it goes on stream. Somebody in the call who is not streaming, or a Discord window that is "
		"not on their stream, is left off the dock and never swapped in until a game picture is "
		"there. A webcam or a shared desktop that fills the capture counts as a picture.");
	pictureCheck_->setChecked(e_->cfg.pictureCheck);
	v4->addWidget(pictureCheck_);
	connect(pictureCheck_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	pages_[PSquad]->addWidget(g4);
	connect(rosterOn_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	connect(rosterSources_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	connect(rosterUrl_, &QLineEdit::editingFinished, this, [this]() { saveAndApply(); });
	connect(rosterChannel_, &QLineEdit::editingFinished, this, [this]() { saveAndApply(); });
	connect(bringFront_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	connect(keepWarm_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });

	bringFront_->setChecked(e_->cfg.bringToFront);
	keepWarm_->setChecked(e_->cfg.keepWarm);
	fillSources();
	fillFriends();
	return w;
}

QWidget *SettingsDialog::buildLookTab()
{
	auto *w = new QWidget(this);
	auto *v = new QVBoxLayout(w);
	auto *g = new QGroupBox("Over a squad mate's feed", w);
	auto *f = new QFormLayout(g);
	lookName_ = new QCheckBox("Name tag  (\"POV · PUP\", Kennel colours)", g);
	auto *nameRow = new QHBoxLayout();
	lookLabel_ = new QLineEdit(QString::fromStdString(e_->cfg.lookLabel), g);
	lookLabel_->setMaximumWidth(120);
	lookPlate_ = new QCheckBox("dark plate", g);
	nameRow->addWidget(new QLabel("prefix", g));
	nameRow->addWidget(lookLabel_);
	nameRow->addWidget(lookPlate_);
	lookPos_ = new QComboBox(g);
	// the game draws its map bottom-left and the score along the bottom, so the tag starts halfway up
	for (auto &p : {std::pair<const char *, const char *>{"middle left", "ml"},
			{"middle", "mc"},
			{"top left", "tl"},
			{"top centre", "tc"},
			{"bottom left", "bl"},
			{"bottom right", "br"}})
		lookPos_->addItem(p.first, p.second);
	int pi = lookPos_->findData(QString::fromStdString(e_->cfg.lookPos));
	lookPos_->setCurrentIndex(pi >= 0 ? pi : 0);
	nameRow->addWidget(new QLabel("at", g));
	nameRow->addWidget(lookPos_);
	nameRow->addStretch(1);
	f->addRow(lookName_);
	f->addRow("", nameRow);
	lookCam_ = new QCheckBox("Camcorder frame  (viewfinder corners, blinking REC, running timer, battery)", g);
	f->addRow(lookCam_);
	lookGrain_ = new QCheckBox("Film grain", g);
	grain_ = new QSlider(Qt::Horizontal, g);
	grain_->setRange(5, 100);
	grain_->setValue(e_->cfg.grainAmount);
	f->addRow(lookGrain_, grain_);
	lookVig_ = new QCheckBox("Vignette  (darkened edges)", g);
	f->addRow(lookVig_);
	lookMark_ = new QCheckBox("Kennel.gg mark  (small and faint, bottom-right)", g);
	f->addRow(lookMark_);
	preview_ = new QPushButton("Preview look in OBS", g);
	f->addRow(preview_);
	f->addRow(muted(
		"Drawn by a browser source named \"Kennel look\" that the plugin adds to your scene and shows on top of the squad mate while you are downed. Nothing touches their feed itself, so it is the same for a Discord pop-out, Twitch or VDO.Ninja.",
		g));
	v->addWidget(g);
	v->addStretch(1);
	lookName_->setChecked(e_->cfg.lookName);
	lookPlate_->setChecked(e_->cfg.lookPlate);
	lookCam_->setChecked(e_->cfg.lookCam);
	lookGrain_->setChecked(e_->cfg.lookGrain);
	lookVig_->setChecked(e_->cfg.lookVignette);
	lookMark_->setChecked(e_->cfg.lookMark);
	auto relook = [this]() {
		saveAndApply();
		if (previewing_ || e_->applied())
			e_->previewLook(true);
	};
	connect(lookPos_, &QComboBox::currentIndexChanged, this, [relook](int) { relook(); });
	for (auto *c : {lookName_, lookPlate_, lookCam_, lookGrain_, lookVig_, lookMark_})
		connect(c, &QCheckBox::toggled, this, [relook](bool) { relook(); });
	connect(lookLabel_, &QLineEdit::editingFinished, this, relook);
	connect(grain_, &QSlider::sliderReleased, this, relook);
	connect(preview_, &QPushButton::clicked, this, [this]() {
		previewing_ = !previewing_;
		e_->previewLook(previewing_);
		preview_->setText(previewing_ ? "Hide preview" : "Preview look in OBS");
	});
	return w;
}

QWidget *SettingsDialog::buildDetectTab()
{
	auto *w = new QWidget(this);
	auto *v = new QVBoxLayout(w);
	auto *gs = new QHBoxLayout();
	gameDetect_ = new QComboBox(w);
	gameDetect_->setToolTip("The OBS source that shows WARDOGS - the same setting as under General.");
	auto *gsRefresh = new QPushButton("Refresh", w);
	auto *gsLabel = new QLabel("Your game source", w);
	gsLabel->hide();
	gs->addWidget(gsLabel);
	gs->addWidget(gameDetect_, 1);
	gs->addWidget(gsRefresh);
	// the game source is chosen once, under General; this copy stays hidden so the two agree
	for (QWidget *hide : {(QWidget *)gameDetect_, (QWidget *)gsRefresh})
		hide->hide();
	delete gs;
	connect(gsRefresh, &QPushButton::clicked, this, [this]() { fillSources(); });
	connect(gameDetect_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
		if (building_ || !game_ || gameDetect_->currentText().isEmpty())
			return;
		game_->setCurrentText(gameDetect_->currentText()); // one setting, two places to change it
		saveAndApply();
	});
	frame_ = new FramePreview(w);
	frame_->setMinimumHeight(260); // inside a scrolling tab a picture with no floor collapses
	v->addWidget(frame_, 1);
	meter_ = new QProgressBar(w);
	meter_->setRange(0, 1000);
	meter_->setFormat("match %v / 1000");
	v->addWidget(meter_);
	connect(frame_, &FramePreview::boxChanged, this, [this](QRectF r) {
		// the box a custom template is cut from (the other areas are on Settings, Detect areas)
		Config &c = e_->cfg;
		c.boxX = r.x();
		c.boxY = r.y();
		c.boxW = r.width();
		c.boxH = r.height();
		c.save();
		updateAreas();
	});

	auto *row = new QHBoxLayout();
	auto *learn = new QPushButton("Learn my HUD (press while downed)", w);
	learn->setToolTip(
		"Cuts the damage-log header out of your own screen, so it matches exactly. The best thing to press if you are never detected as downed.");
	auto *cap = new QPushButton("Capture from the box instead", w);
	auto *builtin = new QPushButton("Use built-in template", w);
	auto *saveFrame = new QPushButton("Save a frame...", w);
	saveFrame->setToolTip(
		"Writes a PNG of your game source as the plugin sees it. Do this while downed and send it to Sombrero if the damage log is never found.");
	tplLbl_ = new QLabel(w);
	row->addWidget(learn);
	row->addWidget(cap);
	row->addWidget(builtin);
	row->addWidget(saveFrame);
	row->addWidget(tplLbl_);
	row->addStretch(1);
	v->addLayout(row);
	{
		// the damage-log wording is the game's language; auto tries every wording until one matches
		auto *lrow = new QHBoxLayout();
		auto *ll = new QLabel("Game language", w);
		auto *lang = new QComboBox(w);
		lang->addItem("Auto (found from the downed screen)", "auto");
		for (const auto &l : Engine::gameLanguages())
			lang->addItem(l.second, QString::fromStdString(l.first));
		lang->setToolTip(
			"The language your game is in. ClipHound reads the weapon name off your HUD in all 14 of the "
			"game's languages. The downed screen is recognised in English, Spanish and French so far; in "
			"another language every known wording is searched, and a frame saved while downed (Advanced) "
			"sent in a ticket in the Kennel.gg Discord adds yours. Auto finds English, Spanish or French "
			"from the downed screen by itself.");
		int li = lang->findData(QString::fromStdString(e_->cfg.gameLang));
		lang->setCurrentIndex(li < 0 ? 0 : li);
		lrow->addWidget(ll);
		lrow->addWidget(lang);
		if (!e_->cfg.gameLangFound.empty() && e_->cfg.gameLang == "auto")
			lrow->addWidget(
				new QLabel(QString("found: %1").arg(Engine::langName(e_->cfg.gameLangFound)), w));
		lrow->addStretch(1);
		ll->hide(); // the form row carries the label
		genGame_->addRow("Game language", lrow);
		connect(lang, &QComboBox::currentIndexChanged, this, [this, lang](int) {
			if (building_)
				return;
			e_->cfg.gameLang = lang->currentData().toString().toStdString();
			e_->cfg.gameLangFound.clear(); // a fresh start for auto
			e_->cfg.save();
			e_->loadTemplates();
			e_->pushAppConfig(); // ClipHound reads the HUD's weapon names in it
			e_->log(e_->cfg.gameLang == "auto"
					? "Game language: auto, every wording searched until one matches."
					: "Game language: " + Engine::langName(e_->cfg.gameLang) + ".");
		});
	}
	wide_ = new QCheckBox("Look over the whole frame, at more sizes (slower; for a HUD the normal search misses)",
			      w);
	wide_->setChecked(e_->cfg.wideSearch);
	v->addWidget(wide_);
	connect(wide_, &QCheckBox::toggled, this, [this](bool on) {
		if (building_)
			return;
		e_->cfg.wideSearch = on;
		e_->cfg.save();
		e_->applySearchWidth();
		e_->log(on ? "Damage-log search widened to the whole frame."
			   : "Damage-log search back to the usual area.");
	});
	connect(learn, &QPushButton::clicked, this, [this]() {
		Match m = e_->lastGame();
		QImage img = e_->grabNative();
		if (img.isNull()) {
			QMessageBox::warning(this, "Kennel.gg Wardogs",
					     "Could not render the game source. Pick it above first.");
			return;
		}
		if (m.w <= 0 || m.score < 0.55) {
			QMessageBox::warning(
				this, "Kennel.gg Wardogs",
				QString("Nothing that looks like the damage log is on screen right now (best %1).\n\n"
					"Press this while you are DOWNED, with the damage log showing. If it still "
					"finds nothing, tick \"Look over the whole frame\" and try again, or use "
					"\"Save a frame...\" and send it to Sombrero.")
					.arg(m.score < 0 ? 0 : m.score, 0, 'f', 2));
			return;
		}
		QRectF r(m.x, m.y, m.w, m.h);
		QRect px((int)(r.x() * img.width()), (int)(r.y() * img.height()), (int)(r.width() * img.width()),
			 (int)(r.height() * img.height()));
		px.adjust(-px.width() / 20 - 2, -px.height() / 5 - 2, px.width() / 20 + 2, px.height() / 5 + 2);
		px &= QRect(0, 0, img.width(), img.height());
		QDialog d(this);
		d.setWindowTitle("Learn my HUD");
		auto *v2 = new QVBoxLayout(&d);
		auto *pic = new QLabel(&d);
		pic->setAlignment(Qt::AlignCenter);
		pic->setStyleSheet("background:#0b0e10; padding:8px;");
		QImage crop = img.copy(px);
		pic->setPixmap(QPixmap::fromImage(
			crop.scaledToWidth(std::min(700, crop.width() * 3), Qt::FastTransformation)));
		v2->addWidget(pic);
		v2->addWidget(muted(QString("This is what the plugin found, scoring %1. If that is your \"VIEW DAMAGE "
					    "LOG\" header, learn it: from then on it is matched against your own "
					    "screen instead of somebody else's, and the score goes near 1.")
					    .arg(m.score, 0, 'f', 2),
				    &d));
		auto *bb2 = new QDialogButtonBox(&d);
		auto *ok = bb2->addButton("Learn this", QDialogButtonBox::AcceptRole);
		bb2->addButton(QDialogButtonBox::Cancel);
		v2->addWidget(bb2);
		connect(bb2, &QDialogButtonBox::accepted, &d, &QDialog::accept);
		connect(bb2, &QDialogButtonBox::rejected, &d, &QDialog::reject);
		(void)ok;
		d.resize(760, 320);
		if (d.exec() != QDialog::Accepted)
			return;
		QString err = e_->learnTemplate(img, r);
		if (!err.isEmpty())
			QMessageBox::warning(this, "Kennel.gg Wardogs", err);
		else
			QMessageBox::information(
				this, "Kennel.gg Wardogs",
				"Learned. Get downed once more and watch the bar: it should go well past the "
				"threshold now. Put the threshold back to 0.85 if you lowered it.");
	});
	connect(saveFrame, &QPushButton::clicked, this, [this]() {
		QString r = e_->saveFrame();
		if (!r.startsWith("/") && !r.contains(":/") && !r.contains(":\\")) {
			QMessageBox::warning(this, "Kennel.gg Wardogs", r);
			return;
		}
		QMessageBox m(this);
		m.setWindowTitle("Kennel.gg Wardogs");
		m.setIcon(QMessageBox::Information);
		m.setText("Saved a picture of your game source.");
		m.setInformativeText(
			r +
			"\n\nIf the damage log is never found, do this while you are DOWNED and send that file to Sombrero: it shows exactly what the plugin is looking at.");
		auto *open = m.addButton("Open the folder", QMessageBox::AcceptRole);
		m.addButton(QMessageBox::Close);
		m.exec();
		if (m.clickedButton() == open)
			QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(r).absolutePath()));
	});
	connect(cap, &QPushButton::clicked, this, [this]() { e_->captureTemplate(); });
	connect(builtin, &QPushButton::clicked, this, [this]() { e_->useBuiltInTemplate(); });
	tplLbl_->setText(!e_->hasTemplate()     ? "No template"
			 : e_->customTemplate() ? "Custom template"
						: "Built-in template");
	v->addWidget(muted(
		"WARDOGS shows the damage log (\"B  VIEW DAMAGE LOG\" and the body silhouette) the whole time you are downed - it only goes away while the Escape menu is open - and hides it when you are revived. (With the menu open the plugin sees no log, so it comes back to your POV until you close it.) The built-in template comes from one particular screen; the spacing between the key hint and the wording differs between HUDs, so if your bar peaks a little short of the threshold, press \"Learn my HUD\" while downed and it will use your own header from then on. the plugin looks for that header anywhere on the right of your game source, at any HUD size, with a template cut from a real frame. Nothing to set up: get downed once and watch the bar go red (~0.9). What is behind the see-through panel - sky, smoke, a muzzle flash - barely moves the score: the match runs on the picture with its local brightness taken out, and once you are down the log is held while it scores above the hold level in the place it was found. Only if it never locks on: drag the dotted box tightly around the header while downed and press Capture.",
		w));

	auto *g = new QGroupBox("Tuning", w);
	auto *f = new QFormLayout(g);
	auto *thrRow = new QHBoxLayout();
	thr_ = new QSlider(Qt::Horizontal, g);
	thr_->setRange(50, 99);
	thr_->setValue((int)std::lround(e_->cfg.threshold * 100));
	thrLbl_ = new QLabel(QString::number(e_->cfg.threshold, 'f', 2), g);
	thrRow->addWidget(thr_, 1);
	thrRow->addWidget(thrLbl_);
	thrRow->addWidget(muted("your header scores well clear of everything else; 0.80 is the default", g));
	f->addRow("Match threshold", thrRow);
	auto *holdRow = new QHBoxLayout();
	hold_ = new QSlider(Qt::Horizontal, g);
	hold_->setRange(0, 30);
	hold_->setValue((int)std::lround(e_->cfg.holdDrop * 100));
	holdLbl_ = new QLabel(QString::number(e_->cfg.threshold - e_->cfg.holdDrop, 'f', 2), g);
	holdRow->addWidget(hold_, 1);
	holdRow->addWidget(holdLbl_);
	holdRow->addWidget(muted("once you are down the log counts as still there down to this score, in the\n"
				 "place it was found - so a bright sky or smoke over the panel cannot say you are up",
				 g));
	f->addRow("Hold down to", holdRow);
	auto holdSync = [this]() {
		holdLbl_->setText(
			QString::number(std::max(0.50, thr_->value() / 100.0 - hold_->value() / 100.0), 'f', 2));
	};
	connect(hold_, &QSlider::valueChanged, this, holdSync);
	connect(thr_, &QSlider::valueChanged, this, holdSync);

	auto *frRow = new QHBoxLayout();
	downFrames_ = new QSpinBox(g);
	downFrames_->setRange(1, 30);
	downFrames_->setValue(e_->cfg.downFrames);
	upFrames_ = new QSpinBox(g);
	upFrames_->setRange(1, 60);
	upFrames_->setValue(e_->cfg.upFrames);
	frRow->addWidget(downFrames_);
	frRow->addWidget(upFrames_);
	frRow->addWidget(muted("polls to confirm down / up (10 per second; 2 / 1 = 0.2 s down, 0.1 s up)", g));
	frRow->addStretch(1);
	f->addRow("Confirm frames", frRow);
	auto *dlRow = new QHBoxLayout();
	downDelay_ = new QSpinBox(g);
	downDelay_->setRange(0, 15000);
	downDelay_->setSingleStep(250);
	downDelay_->setSuffix(" ms");
	downDelay_->setValue(e_->cfg.downDelayMs);
	upDelay_ = new QSpinBox(g);
	upDelay_->setRange(0, 15000);
	upDelay_->setSingleStep(250);
	upDelay_->setSuffix(" ms");
	upDelay_->setValue(e_->cfg.upDelayMs);
	dlRow->addWidget(new QLabel("show squad mate after", g));
	dlRow->addWidget(downDelay_);
	dlRow->addWidget(new QLabel("   back to me after", g));
	dlRow->addWidget(upDelay_);
	dlRow->addWidget(muted("(0 = instant). A quick revive inside the first delay never switches at all.", g));
	dlRow->addStretch(1);
	f->addRow("Switch delays", dlRow);
	connect(downDelay_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { saveAndApply(); });
	connect(upDelay_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { saveAndApply(); });
	auto *psRow = new QHBoxLayout();
	povStinger_ = new QCheckBox("\"SWITCHING POV\" stinger over every swap", g);
	povStinger_->setChecked(e_->cfg.povStinger);
	povStinger_->setToolTip("The same animated panels as the instant replay, saying SWITCHING POV and whose view "
				"comes next. The swap happens while they cover the screen, so viewers never see a cut. "
				"The show-squad-mate delay above still holds: the stinger's run-up comes out of it.");
	povMin_ = new QSpinBox(g);
	povMin_->setRange(0, 30);
	povMin_->setSuffix(" s");
	povMin_->setValue(e_->cfg.povMinS);
	psRow->addWidget(povStinger_);
	psRow->addWidget(new QLabel("   keep a POV up at least", g));
	psRow->addWidget(povMin_);
	psRow->addStretch(1);
	f->addRow("Swaps", psRow);
	connect(povStinger_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	connect(povMin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { saveAndApply(); });
	auto *mdRow = new QHBoxLayout();
	minDown_ = new QSpinBox(g);
	minDown_->setRange(0, 20000);
	minDown_->setSingleStep(250);
	minDown_->setValue(e_->cfg.minDownMs);
	pollMs_ = new QSpinBox(g);
	pollMs_->setRange(100, 1000);
	pollMs_->setSingleStep(50);
	pollMs_->setValue(e_->cfg.pollMs);
	mdRow->addWidget(minDown_);
	mdRow->addWidget(muted("ms minimum on the squad mate (stops flicker during the revive animation)", g));
	mdRow->addWidget(pollMs_);
	mdRow->addWidget(muted("ms between polls", g));
	mdRow->addStretch(1);
	f->addRow("Timing", mdRow);
	auto_ = new QCheckBox("Switch automatically when the damage log is detected", g);
	auto_->setChecked(e_->cfg.autoDetect);
	f->addRow(auto_);
	revive_ = new QCheckBox(
		"Watch the squad mate's feed for \"REVIVING\": when they are on you, switch back the instant the damage log goes (no confirm delay, 10 polls / s)",
		g);
	revive_->setChecked(e_->cfg.watchRevive);
	f->addRow(revive_);
	auto *rvRow = new QHBoxLayout();
	reviveThr_ = new QSlider(Qt::Horizontal, g);
	reviveThr_->setRange(50, 99);
	reviveThr_->setValue((int)std::lround(e_->cfg.reviveThreshold * 100));
	reviveLbl_ = new QLabel(QString::number(e_->cfg.reviveThreshold, 'f', 2), g);
	rvRow->addWidget(reviveThr_, 1);
	rvRow->addWidget(reviveLbl_);
	f->addRow("Revive match threshold", rvRow);
	f->addRow(muted(
		"Hotkeys live in OBS Settings → Hotkeys: \"Kennel.gg Wardogs: show squad mate's POV / back to me\", \"...capture damage-log template\" and \"...save a clip now\". On a two-PC setup send them from the gaming PC with KeyBridge.",
		g));
	v->addWidget(g);

	connect(thr_, &QSlider::valueChanged, this, [this](int val) {
		thrLbl_->setText(QString::number(val / 100.0, 'f', 2) +
				 (val < 75 ? "  -  too low: this matches almost anything, so you will be shown as"
					     " downed all the time. Leave it near 0.85 and use \"Save a frame\""
					     " instead."
					   : ""));
		thrLbl_->setStyleSheet(val < 75 ? "color: #ce6050;" : "");
	});
	connect(thr_, &QSlider::sliderReleased, this, [this]() { saveAndApply(); });
	connect(reviveThr_, &QSlider::valueChanged, this,
		[this](int val) { reviveLbl_->setText(QString::number(val / 100.0, 'f', 2)); });
	connect(reviveThr_, &QSlider::sliderReleased, this, [this]() { saveAndApply(); });
	for (auto *s : {downFrames_, upFrames_, minDown_, pollMs_})
		connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { saveAndApply(); });
	connect(auto_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	connect(revive_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	return w;
}

QWidget *SettingsDialog::buildClipsTab()
{
	auto *w = new QWidget(this);
	auto *v = new QVBoxLayout(w);
	auto *g1 = new QGroupBox("Replay clips", w);
	auto *f1 = new QFormLayout(g1);
	useReplay_ = new QCheckBox("Save OBS's replay buffer on a clip", g1);
	useReplay_->setChecked(e_->cfg.clipUseReplay);
	f1->addRow(useReplay_);
	autoReplay_ = new QCheckBox("Start OBS's replay buffer automatically (clips need it running)", g1);
	replaySecs_ = new QSpinBox(g1);
	replaySecs_->setRange(5, 300);
	replaySecs_->setSuffix(" s");
	replaySecs_->setValue(e_->cfg.replaySeconds);
	auto *rsRow = new QHBoxLayout();
	rsRow->addWidget(replaySecs_);
	rsRow->addWidget(muted("how far back every clip reaches: OBS's own replay-buffer length (Settings -> "
			       "Output -> Replay Buffer). Yours to set, here or there; the plugin only reads it, "
			       "and writes it into OBS when you change it here. Sombrero uses 45 s.",
			       g1),
			 1);
	f1->addRow("Clip length", rsRow);
	connect(replaySecs_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
		if (building_)
			return;
		e_->setReplaySecondsByUser(v);
	});
	connect(e_, &Engine::stateChanged, this, [this]() {
		if (replaySecs_ && !building_ && replaySecs_->value() != e_->cfg.replaySeconds) {
			replaySecs_->blockSignals(true);
			replaySecs_->setValue(e_->cfg.replaySeconds);
			replaySecs_->blockSignals(false);
		}
	});
	autoReplay_->setChecked(e_->cfg.autoStartReplay);
	f1->addRow(autoReplay_);
	nameTpl_ = new QLineEdit(QString::fromStdString(e_->cfg.clipNameTemplate), g1);
	f1->addRow("File name", nameTpl_);
	f1->addRow(muted(
		"Placeholders: {title} {tags} {date} {time} {source}. Default puts what happened first, e.g. Double kill (2 players)_multikill_2026-09-09_07-36-14.mkv. Every clip is also logged to clips.csv.",
		g1));
	clipDowned_ =
		new QCheckBox("Also save a clip whenever you get downed (the moment before is in the buffer)", g1);
	clipDowned_->setChecked(e_->cfg.clipOnDowned);
	f1->addRow(clipDowned_);
	f1->addRow(muted(
		"The dock's Save clip button and the hotkey \"Kennel.gg Wardogs: save a clip now\" save one by hand.",
		g1));
	pages_[PClips]->addWidget(g1);

	auto *gh = new QGroupBox("Also fire OBS hotkeys on a clip (Aitum Backtrack, anything else)", w);
	auto *vh = new QVBoxLayout(gh);
	hotkeyFilter_ = new QLineEdit(gh);
	hotkeyFilter_->setPlaceholderText("filter, e.g. backtrack");
	vh->addWidget(hotkeyFilter_);
	hotkeyList_ = new QListWidget(gh);
	hotkeyList_->setMaximumHeight(140);
	vh->addWidget(hotkeyList_);
	auto *bfRow = new QHBoxLayout();
	backtrackFolder_ = new QLineEdit(QString::fromStdString(e_->cfg.backtrackFolder), gh);
	backtrackFolder_->setPlaceholderText(
		"Backtrack's output folder (found automatically from its sources when blank)");
	auto *bfBrowse = new QPushButton("Browse...", gh);
	bfRow->addWidget(new QLabel("Name their files too:", gh));
	bfRow->addWidget(backtrackFolder_, 1);
	bfRow->addWidget(bfBrowse);
	vh->addLayout(bfRow);
	connect(bfBrowse, &QPushButton::clicked, this, [this]() {
		QString d =
			QFileDialog::getExistingDirectory(this, "Backtrack output folder", backtrackFolder_->text());
		if (!d.isEmpty()) {
			backtrackFolder_->setText(d);
			saveAndApply();
		}
	});
	connect(backtrackFolder_, &QLineEdit::editingFinished, this, [this]() { saveAndApply(); });
	vh->addWidget(muted(
		"Tick the hotkeys OBS should press for you on every clip: with Aitum Backtrack that is its \"Save\" hotkey for the source you want (Backtrack names its own files, so the file-name template above does not apply to those). Untick the replay buffer above to clip with Backtrack alone.",
		gh));
	pages_[PClips]->addWidget(gh);
	auto *gr = new QGroupBox("Rolling highlights", w);
	auto *fr = new QFormLayout(gr);
	seriesS_ = new QSpinBox(gr);
	seriesS_->setRange(0, 600);
	seriesS_->setSuffix(" s");
	seriesS_->setValue(e_->cfg.clipSeriesS);
	seriesS_->setToolTip("Clips made within this many seconds of the previous one are a run, named [1 of 3], "
			     "[2 of 3], [3 of 3]. 0 turns it off.");
	fr->addRow("Clips this close are a run", seriesS_);
	connect(seriesS_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
		if (building_)
			return;
		e_->cfg.clipSeriesS = v;
		e_->cfg.save();
		e_->reloadConfig();
	});
	auto *pastRow = new QHBoxLayout();
	auto *past = new QPushButton("Number past clips into runs", gr);
	past->setToolTip("Goes through every clip already in the clip folders, finds the runs by the time in each "
			 "file's name, and renames them [1 of 3] and so on. Twitch clips are not touched: their "
			 "titles cannot be changed once they exist.");
	pastResult_ = new QLabel(gr);
	pastResult_->setWordWrap(true);
	pastRow->addWidget(past);
	pastRow->addWidget(pastResult_, 1);
	fr->addRow(pastRow);
	connect(past, &QPushButton::clicked, this, [this]() { pastResult_->setText(e_->clips.numberPastClips()); });
	// what ClipHound does to the file afterwards, now that it knows where the kills are
	auto *trimRow = new QHBoxLayout();
	clipTrim_ = new QCheckBox("Trim each clip so it starts", gr);
	clipTrim_->setChecked(e_->cfg.clipTrim);
	clipTrimLead_ = new QSpinBox(gr);
	clipTrimLead_->setRange(3, 30);
	clipTrimLead_->setValue(e_->cfg.clipTrimLeadS);
	clipTrimLead_->setSuffix(" s before the first kill");
	clipTrimLead_->setEnabled(e_->cfg.clipTrim);
	connect(clipTrim_, &QCheckBox::toggled, clipTrimLead_, &QSpinBox::setEnabled);
	trimRow->addWidget(clipTrim_);
	trimRow->addWidget(clipTrimLead_, 1);
	fr->addRow(trimRow);
	runMerge_ = new QCheckBox("Merge a run into one clip of continuous action (the overlap between clips cut once)",
				  gr);
	runMerge_->setChecked(e_->cfg.runMerge);
	fr->addRow(runMerge_);
	auto *gapRow = new QHBoxLayout();
	runCutGaps_ = new QCheckBox("...and cut the dead space out of it: a gap longer than", gr);
	runCutGaps_->setChecked(e_->cfg.runCutGaps);
	runGap_ = new QSpinBox(gr);
	runGap_->setRange(5, 60);
	runGap_->setValue(e_->cfg.runGapS);
	runGap_->setSuffix(" s between kills is dropped");
	runGap_->setEnabled(e_->cfg.runCutGaps);
	connect(runCutGaps_, &QCheckBox::toggled, runGap_, &QSpinBox::setEnabled);
	gapRow->addWidget(runCutGaps_);
	gapRow->addWidget(runGap_, 1);
	fr->addRow(gapRow);
	fr->addRow(muted(
		"Trimming is a straight cut of the file with no re-encode, done by ClipHound seconds after the clip lands; "
		"the end of the file is left alone. A merged run is written next to its clips as \"... [run of 3].mp4\", "
		"re-encoded on the GPU, with the seams placed by matching the clips' sound so nothing repeats. The "
		"original clips are kept. Cutting the dead space is off by default: it turns a run into the kills alone.",
		gr));
	pages_[PClips]->addWidget(gr);

	auto *gi = new QGroupBox("Instant replay", w);
	auto *fi = new QFormLayout(gi);
	auto spin = [gi](int lo, int hi, int val, const QString &suffix) {
		auto *sb = new QSpinBox(gi);
		sb->setRange(lo, hi);
		sb->setValue(val);
		sb->setSuffix(suffix);
		return sb;
	};
	replayPre_ = spin(0, 30, e_->cfg.replayPreS, " s before the first kill");
	replayPost_ = spin(0, 30, e_->cfg.replayPostS, " s after the last kill");
	replayScale_ = spin(25, 100, e_->cfg.replayScale, " % of the screen");
	replayVol_ = spin(0, 100, e_->cfg.replayVolume, " % volume");
	replayCool_ = spin(30, 900, e_->cfg.replayCooldownS, " s between chat replays");
	fi->addRow("Starts", replayPre_);
	fi->addRow("Ends", replayPost_);
	fi->addRow("Size", replayScale_);
	replayStinger_ = new QCheckBox(
		"Stinger: an animated \"INSTANT REPLAY\" wipe into each replay, and a quick one out of it", gi);
	replayStinger_->setChecked(e_->cfg.replayStinger);
	replayStinger_->setToolTip(
		"A transparent browser source on top of your scene (Kennel.gg · Replay stinger) plays it; the "
		"replay switches on and off while it covers the screen, so there is never a cut or a black frame.");
	fi->addRow(replayStinger_);
	replayStingerSound_ = new QCheckBox("Whoosh sound on the stingers (instant replay and SWITCHING POV; goes "
					    "out on stream)",
					    gi);
	replayStingerSound_->setChecked(e_->cfg.replayStingerSound);
	fi->addRow(replayStingerSound_);
	{
		auto *vr = new QHBoxLayout();
		stingerVol_ = new QSlider(Qt::Horizontal, gi);
		stingerVol_->setRange(0, 100);
		stingerVol_->setSingleStep(5);
		stingerVol_->setPageStep(10);
		stingerVol_->setValue(std::clamp(e_->cfg.stingerVolume, 0, 100));
		auto *vl = new QLabel(gi);
		vl->setMinimumWidth(44);
		auto show = [vl, this]() {
			vl->setText(QString("%1 %").arg(stingerVol_->value()));
		};
		show();
		vr->addWidget(stingerVol_, 1);
		vr->addWidget(vl);
		fi->addRow("Whoosh volume", vr);
		connect(stingerVol_, &QSlider::valueChanged, this, [show](int) { show(); });
		connect(stingerVol_, &QSlider::sliderReleased, this, [this]() { saveAndApply(); });
		connect(stingerVol_, &QSlider::actionTriggered, this, [this](int a) {
			if (a != QAbstractSlider::SliderMove) // keyboard and clicks on the groove
				QTimer::singleShot(0, this, [this]() { saveAndApply(); });
		});
		connect(replayStingerSound_, &QCheckBox::toggled, stingerVol_, &QWidget::setEnabled);
		stingerVol_->setEnabled(e_->cfg.replayStingerSound);
	}
	replayPip_ = new QCheckBox("Shrink the game to a small LIVE window, bottom right, while it plays", gi);
	replayPip_->setChecked(e_->cfg.replayPip);
	replayPip_->setToolTip("Instant replays only (not the highlights reel), main canvas only. The game "
			       "capture shrinks into the corner over the replay, then grows back to full size "
			       "when the replay ends. Needs the game capture in the plugin's scene.");
	fi->addRow(replayPip_);
	connect(replayPip_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	connect(replayStinger_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	connect(replayStingerSound_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	replaySound_ = new QCheckBox("Play the clip's sound (it carries your mic and the game from a minute ago)", gi);
	replaySound_->setChecked(e_->cfg.replaySound);
	fi->addRow(replaySound_);
	fi->addRow("Sound level", replayVol_);
	replayVol_->setEnabled(e_->cfg.replaySound);
	connect(replaySound_, &QCheckBox::toggled, replayVol_, &QSpinBox::setEnabled);
	replayHw_ = new QCheckBox("Decode replays on the GPU (try it if a replay stutters as it starts)", gi);
	replayHw_->setChecked(e_->cfg.replayHwDecode);
	fi->addRow(replayHw_);
	replayLabel_ = new QLineEdit(QString::fromStdString(e_->cfg.replayLabel), gi);
	replayLabel_->setPlaceholderText("Instant replay");
	fi->addRow("Frame says", replayLabel_);
	replayChat_ = new QCheckBox("Subscribers, VIPs and moderators can play it from chat", gi);
	replayChat_->setChecked(e_->cfg.replayChat);
	fi->addRow(replayChat_);
	auto *chatRow = new QHBoxLayout();
	replayWord_ = new QLineEdit(QString::fromStdString(e_->cfg.replayWord), gi);
	replayWord_->setPlaceholderText("!replay");
	replayWord_->setToolTip("What they type. Case does not matter; anything after the word is ignored.");
	replayWord_->setMaximumWidth(140);
	chatRow->addWidget(new QLabel("They type", gi));
	chatRow->addWidget(replayWord_);
	chatRow->addSpacing(12);
	chatRow->addWidget(new QLabel("Cooldown", gi));
	chatRow->addWidget(replayCool_, 1);
	fi->addRow("Chat trigger", chatRow);
	chatClips_ = new QComboBox(gi);
	chatClips_->addItems({"Off", "Only big moments", "Normal", "Small moments too"});
	chatClips_->setCurrentIndex(std::clamp(e_->cfg.chatClips, 0, 3));
	chatClips_->setToolTip(
		"ClipHound watches your Twitch chat. When several different people write at once - a few times "
		"the chat's usual pace - or a few of them ask for a clip (\"clip it\"), the last moments are saved "
		"as a clip called \"Chat went wild\" with the word they spammed (\"KEKW\"). Commands, bots and "
		"your own lines never count. At most one a minute. Needs the Twitch login under Twitch clips.");
	fi->addRow("Clip when chat goes wild", chatClips_);
	connect(chatClips_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { saveAndApply(); });
	chatKick_ = new QLineEdit(QString::fromStdString(e_->cfg.chatKick), gi);
	chatKick_->setPlaceholderText("your Kick channel (optional)");
	chatYouTube_ = new QLineEdit(QString::fromStdString(e_->cfg.chatYouTube), gi);
	chatYouTube_->setPlaceholderText("your YouTube channel or @handle (optional)");
	fi->addRow("Twitch chat",
		   muted("Your Twitch chat is read through the Twitch login under Twitch clips, below.", gi));
	fi->addRow("Kick chat", chatKick_);
	fi->addRow("YouTube chat", chatYouTube_);
	highlightsFolder_ = new QLineEdit(QString::fromStdString(e_->cfg.highlightsFolder), gi);
	highlightsFolder_->setPlaceholderText("<clip folder>\\highlights");
	fi->addRow("Highlights folder", highlightsFolder_);
	highlightsAuto_ = new QCheckBox("Build the session's highlights compilation when the stream stops", gi);
	highlightsAuto_->setChecked(e_->cfg.highlightsAuto);
	fi->addRow(highlightsAuto_);
	ytChapters_ = new QCheckBox("Write YouTube chapters for the VOD when the stream stops", gi);
	ytChapters_->setChecked(e_->cfg.ytChapters);
	ytChapters_->setToolTip(
		"Every clip saved while you stream becomes a line such as \"1:02:14 Triple kill with the Galil\", "
		"timed from when the stream started. When it stops, the list is saved next to your clips and the "
		"dock's menu has Copy YouTube chapters: paste it into the VOD's description and YouTube turns it "
		"into chapters.");
	fi->addRow(ytChapters_);
	connect(ytChapters_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	sessionTrack_ =
		new QCheckBox("Count this session: kills, deaths, downs, assists, revives, headshots, vehicles and "
			      "money earned",
			      gi);
	sessionTrack_->setChecked(e_->cfg.sessionTrack);
	sessionTrack_->setToolTip(
		"Kills and deaths come from the kill feed (ClipHound), downs from the downed screen, and assists, "
		"revives, headshots, vehicles and money from the reward lines under your balance, top right. Those "
		"reward names are read in English; money is read in any language. The dock shows the count, "
		"\"Show on stream\" adds an overlay, and a summary is saved next to your clips when the stream "
		"stops. A new session starts when you go live, or from the dock's Reset.");
	fi->addRow(sessionTrack_);
	connect(sessionTrack_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	sessionShow_ = new QListWidget(gi);
	sessionShow_->setDragDropMode(QAbstractItemView::InternalMove);
	sessionShow_->setMaximumHeight(150);
	sessionShow_->setToolTip("Tick what the \"This session\" bar shows on stream; drag to change the order. "
				 "It changes on stream at once.");
	{
		QStringList chosen = QString::fromStdString(e_->cfg.sessionShow).split(',', Qt::SkipEmptyParts);
		auto add = [this](const QString &id, const QString &label, bool on) {
			auto *it = new QListWidgetItem(label, sessionShow_);
			it->setData(Qt::UserRole, id);
			it->setFlags(it->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
			it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
		};
		// the chosen ones first, in their order, then the rest
		for (const QString &id : chosen)
			for (const auto &el : Session::elements())
				if (el.first == id.trimmed())
					add(el.first, el.second, true);
		for (const auto &el : Session::elements())
			if (!chosen.contains(el.first))
				add(el.first, el.second, false);
	}
	sessionOverlayOn_ = new QCheckBox("Show the \"This session\" bar on stream (it slides in and out)", gi);
	sessionOverlayOn_->setChecked(e_->cfg.sessionOverlayOn);
	fi->addRow(sessionOverlayOn_);
	sessionOverlayPos_ = new QComboBox(gi);
	sessionOverlayPos_->addItem("Top left, under the game's team emblem", "tl");
	sessionOverlayPos_->addItem("Bottom centre, between the score and your weapon", "bc");
	sessionOverlayPos_->setCurrentIndex(
		std::max(0, sessionOverlayPos_->findData(QString::fromStdString(e_->cfg.sessionOverlayPos))));
	fi->addRow("Where", sessionOverlayPos_);
	sessionOverlayMode_ = new QComboBox(gi);
	sessionOverlayMode_->addItem("Always on", "always");
	sessionOverlayMode_->addItem("Slides in when a number changes, out 20 s later", "pop");
	sessionOverlayMode_->setCurrentIndex(
		std::max(0, sessionOverlayMode_->findData(QString::fromStdString(e_->cfg.sessionOverlayMode))));
	fi->addRow("When", sessionOverlayMode_);
	connect(sessionOverlayOn_, &QCheckBox::toggled, this, [this](bool on) {
		saveAndApply();
		if (on && !e_->hasSessionOverlay())
			e_->addSessionOverlay(); // first time on: put it in the scene
	});
	for (auto *cb : {sessionOverlayPos_, sessionOverlayMode_})
		connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { saveAndApply(); });
	fi->addRow("On-stream bar shows", sessionShow_);
	statsShare_ = new QCheckBox("Share my session stats with kennel.gg for the public leaderboards", gi);
	statsShare_->setChecked(e_->cfg.statsConsent == 1);
	statsShare_->setToolTip(
		"Needs this PC linked to your kennel.gg account (below). At the end of each stream your account gets the "
		"session's kills, deaths, assists, revives, headshots, vehicles, downs, money earned and spent, time in "
		"game, longest kill and top weapon. Never your clips, video, voice or chat. Off until you tick it.");
	auto *delStats = new QPushButton("Delete what I have shared", gi);
	auto *shareRow = new QHBoxLayout();
	shareRow->addWidget(statsShare_, 1);
	shareRow->addWidget(delStats);
	fi->addRow("Leaderboards", shareRow);
	accountLbl_ = new QLabel(gi);
	accountLbl_->setWordWrap(true);
	accountBtn_ = new QPushButton(gi);
	auto *accPage = new QPushButton("My kennel.gg account", gi);
	auto *accRow = new QHBoxLayout();
	accRow->addWidget(accountLbl_, 1);
	accRow->addWidget(accountBtn_);
	accRow->addWidget(accPage);
	fi->addRow("kennel.gg account", accRow);
	connect(accountBtn_, &QPushButton::clicked, this, [this]() {
		if (e_->accountLinked())
			e_->unlinkAccount();
		else
			e_->linkAccount();
	});
	connect(accPage, &QPushButton::clicked, this,
		[]() { QDesktopServices::openUrl(QUrl("https://kennel.gg/account/")); });
	connect(e_, &Engine::stateChanged, this, [this]() { showAccount(); });
	showAccount();
	connect(statsShare_, &QCheckBox::toggled, this, [this](bool on) {
		if (!building_)
			e_->setStatsConsent(on);
	});
	connect(delStats, &QPushButton::clicked, this, [this]() {
		if (QMessageBox::question(
			    this, "Kennel.gg Wardogs",
			    "Delete every session this PC has shared with kennel.gg? Sharing is also turned off.") !=
		    QMessageBox::Yes)
			return;
		statsShare_->setChecked(false);
		e_->setStatsConsent(false);
		e_->deleteSharedStats();
	});
	connect(sessionShow_, &QListWidget::itemChanged, this, [this](QListWidgetItem *) { saveAndApply(); });
	connect(sessionShow_->model(), &QAbstractItemModel::rowsMoved, this, [this]() { saveAndApply(); });
	highlightsMax_ = spin(3, 30, e_->cfg.highlightsMax, " clips at most");
	fi->addRow("Compilation", highlightsMax_);
	fi->addRow(muted(
		"The compilation is cut by ClipHound on this PC: each clip's action (5 s before the first kill to "
		"3 s after the last) is pre-cut as the clip lands, held back whenever OBS drops frames, and at the "
		"end the best ones are joined with a title card and a kennel.gg card. Drop music you may use into a "
		"music folder inside the highlights folder and one track is laid under it. Play highlights builds it "
		"first when there is nothing newer than your last clip.",
		gi));
	fi->addRow(muted(
		"Instant replay plays the last highlight back on the stream, cut down to the action, framed and tagged, sized "
		"under your camera and alerts (the always-on-top list under Squad & POV). The dock button and a hotkey play "
		"it; so can chat, once per cooldown. The clip carries whatever the stream carried, your mic included, so its "
		"sound is off unless you tick it on. Play highlights plays the newest video in the highlights folder, full screen.",
		gi));
	pages_[PClips]->addWidget(gi);
	for (auto *sb : {replayPre_, replayPost_, replayScale_, replayVol_, replayCool_})
		connect(sb, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { saveAndApply(); });
	connect(replayChat_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	connect(replaySound_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	connect(replayHw_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	connect(highlightsAuto_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	for (auto *c : {clipTrim_, runMerge_, runCutGaps_})
		connect(c, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	for (auto *sb : {clipTrimLead_, runGap_})
		connect(sb, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { saveAndApply(); });
	connect(highlightsMax_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { saveAndApply(); });
	for (auto *le : {chatKick_, chatYouTube_, highlightsFolder_, replayLabel_, replayWord_})
		connect(le, &QLineEdit::editingFinished, this, [this]() { saveAndApply(); });
	connect(hotkeyFilter_, &QLineEdit::textChanged, this, [this](const QString &) { fillHotkeys(); });
	connect(hotkeyList_, &QListWidget::itemChanged, this, [this](QListWidgetItem *) { saveAndApply(); });
	connect(useReplay_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	fillHotkeys();

	auto *g2 = new QGroupBox("ClipHound connection", w);
	auto *f2 = new QFormLayout(g2);
	auto *br = new QHBoxLayout();
	bridgeOn_ = new QCheckBox("Bridge on, port", g2);
	bridgeOn_->setChecked(e_->cfg.bridgeEnabled);
	bridgePort_ = new QSpinBox(g2);
	bridgePort_->setRange(1024, 65535);
	bridgePort_->setValue(e_->cfg.bridgePort);
	br->addWidget(bridgeOn_);
	br->addWidget(bridgePort_);
	br->addWidget(muted("47820 unless something else on this PC wants it. ClipHound is told the new "
			    "number and restarted when you change it - if it is running. If it is not, put "
			    "this back to 47820.",
			    g2),
		      1);
	br->addStretch(1);
	f2->addRow(br);
	auto *ap = new QHBoxLayout();
	appPath_ = new QLineEdit(QString::fromStdString(e_->cfg.appPath), g2);
	appPath_->setPlaceholderText("C:\\...\\ClipHound\\ClipHound.bat");
	auto *browse = new QPushButton("Browse...", g2);
	auto *launchNow = new QPushButton("Start now", g2);
	ap->addWidget(appPath_, 1);
	ap->addWidget(browse);
	ap->addWidget(launchNow);
	f2->addRow("App", ap);
	launchApp_ = new QCheckBox("Start it when OBS starts", g2);
	launchApp_->setChecked(e_->cfg.launchApp);
	genApp_->addRow(launchApp_);
	closeApp_ = new QCheckBox("Close it when OBS closes", g2);
	closeApp_->setChecked(e_->cfg.closeAppWithObs);
	genApp_->addRow(closeApp_);
	connect(closeApp_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	f2->addRow(muted(
		"The app reads the kill feed (OCR) and asks the plugin for clips over ws://127.0.0.1:<port>. The plugin sends it native-resolution crops of the game source and POV events; the app sends clip requests with tags. Downed detection stays in the plugin.",
		g2));
	pages_[PAdvanced]->addWidget(g2);
	connect(browse, &QPushButton::clicked, this, [this]() {
		QString p = QFileDialog::getOpenFileName(this, "Companion app", appPath_->text(),
							 "Programs (*.exe *.bat *.cmd);;All files (*)");
		if (!p.isEmpty()) {
			appPath_->setText(p);
			saveAndApply();
		}
	});
	connect(launchNow, &QPushButton::clicked, this, [this]() {
		saveAndApply();
		e_->launchApp();
	});

	auto *g3 = new QGroupBox("Recent clips", w);
	auto *v3 = new QVBoxLayout(g3);
	clipList_ = new QListWidget(g3);
	v3->addWidget(clipList_);
	pages_[PClips]->addWidget(g3, 1);
	auto fillClips = [this]() {
		clipList_->clear();
		auto &h = e_->clips.history();
		for (auto it = h.rbegin(); it != h.rend(); ++it)
			clipList_->addItem(it->when.toString("HH:mm:ss") + "  " + it->title + "  [" +
					   it->tags.join(", ") + "]  " + QFileInfo(it->path).fileName());
	};
	fillClips();
	connect(e_, &Engine::stateChanged, this, fillClips);

	for (auto *c : {autoReplay_, clipDowned_, bridgeOn_, launchApp_})
		connect(c, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	connect(nameTpl_, &QLineEdit::editingFinished, this, [this]() { saveAndApply(); });
	connect(appPath_, &QLineEdit::editingFinished, this, [this]() { saveAndApply(); });
	connect(bridgePort_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { saveAndApply(); });
	return w;
}

QWidget *SettingsDialog::buildAppTab()
{
	auto *w = new QWidget(this);
	auto *v = new QVBoxLayout(w);
	auto *g = new QGroupBox("Kill-feed clips (ClipHound)", w);
	auto *f = new QFormLayout(g);
	appName_ = new QLineEdit(QString::fromStdString(e_->cfg.appPlayerName), g);
	appName_->setPlaceholderText("exactly as it appears in the kill feed");
	appName_->setToolTip(
		"Your name exactly as the kill feed shows it. Left blank, ClipHound cannot tell your kills "
		"from anyone else's.");
	genYou_->insertRow(0, "Your name in WARDOGS", appName_);
	auto *libRow = new QHBoxLayout();
	appLibrary_ = new QLineEdit(QString::fromStdString(e_->cfg.appLibrary), g);
	appLibrary_->setPlaceholderText("blank = no index; otherwise index.csv and Resolve metadata are kept here");
	auto *libBrowse = new QPushButton("Browse...", g);
	libRow->addWidget(appLibrary_, 1);
	libRow->addWidget(libBrowse);
	f->addRow("Clip library index", libRow);
	f->addRow(muted(
		"The clip files themselves go where the replay buffer saves them. The library is an optional index of what happened in each clip.",
		g));
	appEveryKill_ = new QCheckBox(
		"Clip every kill I get (otherwise only notable ones: 120 m+, headshots, vehicles, explosives, multi-kills)",
		g);
	appEveryKill_->setChecked(e_->cfg.appEveryKill);
	f->addRow(appEveryKill_);
	appMulti_ = new QDoubleSpinBox(g);
	appMulti_->setRange(2, 120);
	appMulti_->setDecimals(0);
	appMulti_->setSuffix(" s");
	appMulti_->setValue(e_->cfg.appMultikillWindow > 0 ? e_->cfg.appMultikillWindow : 30);
	f->addRow("Multi-kill window", appMulti_);
	f->addRow(muted(
		"Kills within this many seconds of each other count as one multi-kill (double, triple...). Default 30 s.",
		g));
	pages_[PClips]->addWidget(g);

	{
		// how often ClipHound reads the kill feed: on Settings, Detect areas, under the kill-feed box
		feedRate_ = new QWidget(w);
		auto *rr = new QHBoxLayout(feedRate_);
		rr->setContentsMargins(0, 0, 0, 0);
		appFps_ = new QSpinBox(feedRate_);
		appFps_->setRange(3, 15);
		appFps_->setSuffix(" times a second");
		appFps_->setValue(e_->cfg.appFps > 0 ? e_->cfg.appFps : 10);
		appFps_->setToolTip(
			"Reading faster gets the clip sooner; a kill is decided about three quarters of a "
			"second after it appears whatever the rate, so 10 a second is plenty and 5 costs half "
			"the CPU.");
		rr->addWidget(new QLabel("Read the kill feed", feedRate_));
		rr->addWidget(appFps_);
		rr->addStretch(1);
		connect(appFps_, &QSpinBox::editingFinished, this, [this]() {
			e_->cfg.appFps = appFps_->value();
			e_->cfg.save();
			e_->pushAppConfig();
		});
	}

	auto *gt = new QGroupBox("Twitch clips", w);
	auto *ft = new QFormLayout(gt);
	appTwitch_ = new QCheckBox("Create a Twitch clip on notable kills", gt);
	appTwitch_->setChecked(e_->cfg.appTwitchEnabled);
	ft->addRow(appTwitch_);
	appBroadcaster_ = new QLineEdit(QString::fromStdString(e_->cfg.appBroadcaster), gt);
	appBroadcaster_->setPlaceholderText("the channel that is live, e.g. sombrero");
	ft->addRow("Channel to clip", appBroadcaster_);
	auto *tRow = new QHBoxLayout();
	twitchLbl_ = new QLabel(gt);
	twitchLbl_->setWordWrap(true);
	twitchLogin_ = new QPushButton("Log in with Twitch...", gt);
	twitchLogout_ = new QPushButton("Log out", gt);
	tRow->addWidget(twitchLbl_, 1);
	tRow->addWidget(twitchLogin_);
	tRow->addWidget(twitchLogout_);
	ft->addRow("Clipping account", tRow);
	twitchMarkers_ = new QCheckBox("Add a Twitch stream marker for every clip, so the VOD can be scrubbed moment "
				       "to moment",
				       gt);
	twitchMarkers_->setChecked(e_->cfg.twitchMarkers);
	twitchMarkers_->setToolTip(
		"Twitch shows markers on the VOD's timeline and in its Highlighter. The account logged in here must be "
		"the channel itself or one of its editors, and a login from before 0.20.0 has to be made again once.");
	ft->addRow(twitchMarkers_);
	connect(twitchMarkers_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	ft->addRow(muted(
		"Log in as the account that should own the clips (a bot account such as InfoKennel works). A code appears and is copied; twitch.tv/activate opens, paste the code, done. Needs ClipHound running.",
		gt));
	pages_[PClips]->addWidget(gt);

	auto push = [this]() {
		Config &c = e_->cfg;
		c.appPlayerName = appName_->text().trimmed().toStdString();
		c.appLibrary = appLibrary_->text().trimmed().toStdString();
		c.appBroadcaster = appBroadcaster_->text().trimmed().toLower().remove('@').toStdString();
		c.appTwitchEnabled = appTwitch_->isChecked();
		c.appEveryKill = appEveryKill_->isChecked();
		c.appMultikillWindow = appMulti_->value();
		e_->pushAppConfig();
	};
	connect(appEveryKill_, &QCheckBox::toggled, this, [push](bool) { push(); });
	connect(appMulti_, &QDoubleSpinBox::editingFinished, this, push);
	connect(appName_, &QLineEdit::editingFinished, this, push);
	connect(appLibrary_, &QLineEdit::editingFinished, this, push);
	connect(appBroadcaster_, &QLineEdit::editingFinished, this, push);
	connect(appTwitch_, &QCheckBox::toggled, this, [push](bool) { push(); });
	connect(libBrowse, &QPushButton::clicked, this, [this, push]() {
		QString d = QFileDialog::getExistingDirectory(this, "Clip library folder", appLibrary_->text());
		if (!d.isEmpty()) {
			appLibrary_->setText(d);
			push();
		}
	});
	connect(twitchLogin_, &QPushButton::clicked, this, [this]() { e_->twitchLogin(); });
	connect(twitchLogout_, &QPushButton::clicked, this, [this]() { e_->twitchLogout(); });
	connect(e_, &Engine::appConfigReceived, this, [this]() {
		appName_->setText(QString::fromStdString(e_->cfg.appPlayerName));
		appLibrary_->setText(QString::fromStdString(e_->cfg.appLibrary));
		appBroadcaster_->setText(QString::fromStdString(e_->cfg.appBroadcaster));
		appTwitch_->blockSignals(true);
		appTwitch_->setChecked(e_->cfg.appTwitchEnabled);
		appTwitch_->blockSignals(false);
		appEveryKill_->blockSignals(true);
		appEveryKill_->setChecked(e_->cfg.appEveryKill);
		appEveryKill_->blockSignals(false);
		appMulti_->blockSignals(true);
		appMulti_->setValue(e_->cfg.appMultikillWindow > 0 ? e_->cfg.appMultikillWindow : 30);
		appMulti_->blockSignals(false);
	});
	connect(e_, &Engine::twitchStatusChanged, this, [this]() { refreshAppTab(); });
	connect(e_, &Engine::stateChanged, this, [this]() { refreshAppTab(); });
	refreshAppTab();
	return w;
}

/// What ClipHound sees in the NEARBY box: our own crop of it, and the text it read there.
void SettingsDialog::showNearbyTest()
{
	auto *d = new QDialog(this);
	d->setAttribute(Qt::WA_DeleteOnClose);
	d->setWindowTitle("NEARBY test read");
	d->setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
	auto *v = new QVBoxLayout(d);
	auto *pic = new QLabel(d);
	pic->setAlignment(Qt::AlignCenter);
	pic->setStyleSheet("background: #0b0e10;");
	pic->setMinimumHeight(160);
	QImage f = e_->lastFrame();
	const Config &c = e_->cfg;
	if (!f.isNull()) {
		QRect r((int)(c.nearX * f.width()), (int)(c.nearY * f.height()), (int)(c.nearW * f.width()),
			(int)(c.nearH * f.height()));
		r &= QRect(0, 0, f.width(), f.height());
		if (r.width() > 4 && r.height() > 4) {
			QImage crop = f.copy(r);
			pic->setPixmap(QPixmap::fromImage(
				crop.scaledToWidth(std::min(760, crop.width() * 4), Qt::FastTransformation)));
		}
	} else
		pic->setText("No frame from the game source yet.");
	v->addWidget(pic);
	v->addWidget(muted("The blue box, as the plugin sees it. If this is not the NEARBY list, drag the box again "
			   "under Advanced while the game is showing.",
			   d));
	auto *out = new QPlainTextEdit(d);
	out->setReadOnly(true);
	out->setMinimumHeight(150);
	out->setPlainText("Asking ClipHound to read it...");
	v->addWidget(out, 1);
	auto *row = new QHBoxLayout();
	auto *again = new QPushButton("Read again", d);
	auto *close = new QPushButton("Close", d);
	row->addStretch(1);
	row->addWidget(again);
	row->addWidget(close);
	v->addLayout(row);
	connect(close, &QPushButton::clicked, d, &QDialog::close);
	connect(again, &QPushButton::clicked, d, [this, out]() {
		out->setPlainText("Asking ClipHound to read it...");
		e_->nearbyTest();
	});
	connect(e_, &Engine::nearbyTested, d, [this, out](const QJsonObject &o) {
		if (o.contains("error")) {
			out->setPlainText(o.value("error").toString() +
					  ".\nStart it from the dock, then press Read again.");
			return;
		}
		auto list = [&o](const char *k) {
			QStringList v;
			for (auto x : o.value(k).toArray())
				v << x.toString();
			return v;
		};
		QStringList names = list("names"), texts = list("texts"), dists = list("dists");
		QStringList found;
		for (auto x : o.value("found").toArray())
			found << x.toObject().value("match").toString() + " " +
					 QString::number(x.toObject().value("dist").toInt()) + " m";
		QString t;
		t += QString("Rows found in the box: %1     distance chips found: %2\n")
			     .arg(o.value("rows").toInt())
			     .arg(o.value("chips").toInt());
		t += "Names read:   " + (texts.isEmpty() ? QString("(nothing)") : texts.join("  |  ")) + "\n";
		t += "Metres read:  " + (dists.isEmpty() ? QString("(nothing)") : dists.join("  |  ")) + "\n";
		t += "Looking for:  " + (names.isEmpty() ? QString("(no in-game names set)") : names.join(", ")) +
		     "\n\n";
		if (!found.isEmpty())
			t += "Matched: " + found.join(", ") + "\nThis is working.\n";
		else if (o.value("rows").toInt() == 0)
			t += "No rows of text were found in the box. It is probably not over the NEARBY list, or the list is empty right now (be in a match, with squad mates near you).\n";
		else if (texts.isEmpty())
			t += "Rows were found but no text came out of them. Try dragging the box a little wider, and make sure it is not covering the map or the score bar.\n";
		else
			t += "Text was read but it does not match any squad mate's in-game name. Set each squad mate's in-game name in the Squad window to exactly what is shown above.\n";
		if (!o.value("saved").toString().isEmpty())
			t += "\nClipHound saved what it looked at: " + o.value("saved").toString();
		out->setPlainText(t);
	});
	d->resize(820, 560);
	d->show();
	e_->nearbyTest();
}

namespace {
struct DualPreset {
	const char *id, *label;
	double x, y, w;
};
// Measured on 1600x900 frames of the tank (driver and gunner), the Havoc pilot seat and the Havoc
// gunner's CAM view. Top-left sits between the team chat and the kill feed and covers no HUD; the
// CAM view is a framed picture, so the window goes inside the frame, clear of the compass.
const DualPreset kDualPresets[] = {
	{"tank-driver", "Tank - I drive, show my gunner", 0.012, 0.19, 0.26},
	{"tank-gunner", "Tank - I am the gunner, show my driver", 0.012, 0.19, 0.26},
	{"havoc-pilot", "Havoc - I fly, show my gunner", 0.012, 0.19, 0.26},
	{"havoc-gunner", "Havoc - I am the gunner (CAM view), show my pilot", 0.19, 0.075, 0.20},
	{"custom", "Custom - drag the box on the picture", 0, 0, 0},
};
} // namespace

QWidget *SettingsDialog::buildDualTab()
{
	auto *w = new QWidget(this);
	auto *v = new QVBoxLayout(w);
	v->addWidget(muted(
		"Two of you in a tank or a Havoc? Show your own POV as usual and put your crew mate's feed in a small window over it, placed where the game draws nothing. Pick who, pick the seat you are in, and the window goes where that seat's HUD leaves room. The window is picture only; the swap when you go down still takes the whole screen and the window steps aside for it. Hotkey: OBS Settings → Hotkeys → \"dual POV window on / off\".",
		w));
	auto *g = new QGroupBox("Dual POV", w);
	auto *f = new QFormLayout(g);
	dualOn_ = new QCheckBox("Dual POV window up from the moment OBS starts (forced, until you turn it off)", g);
	dualOn_->setToolTip(
		"Off: the window comes up only when you ask - the dock button, a voice command, or the "
		"vehicle detector below. It used to tick itself whenever the window happened to be up while "
		"settings were saved, which forced it on at every start.");
	f->addRow(dualOn_);
	dualFriend_ = new QComboBox(g);
	f->addRow("Crew mate to show", dualFriend_);
	dualPreset_ = new QComboBox(g);
	for (const auto &p : kDualPresets)
		dualPreset_->addItem(p.label, p.id);
	f->addRow("Vehicle and seat", dualPreset_);
	auto *pos = new QHBoxLayout();
	dualX_ = new QDoubleSpinBox(g);
	dualY_ = new QDoubleSpinBox(g);
	dualW_ = new QDoubleSpinBox(g);
	for (auto *sb : {dualX_, dualY_, dualW_}) {
		sb->setRange(0, 100);
		sb->setDecimals(1);
		sb->setSuffix(" %");
	}
	dualW_->setRange(8, 60);
	pos->addWidget(new QLabel("left", g));
	pos->addWidget(dualX_);
	pos->addWidget(new QLabel("top", g));
	pos->addWidget(dualY_);
	pos->addWidget(new QLabel("width", g));
	pos->addWidget(dualW_);
	pos->addWidget(muted("of the canvas; the height keeps 16:9", g));
	pos->addStretch(1);
	f->addRow("Window", pos);
	auto *op = new QHBoxLayout();
	dualOpacity_ = new QSlider(Qt::Horizontal, g);
	dualOpacity_->setRange(10, 100);
	auto *opLbl = new QLabel(g);
	op->addWidget(dualOpacity_, 1);
	op->addWidget(opLbl);
	f->addRow("Opacity", op);
	dualAuto_ = new QCheckBox(
		"Turn the window on by itself when I get in a vehicle (needs ClipHound running; it goes off when you get out either way)",
		g);
	f->addRow(dualAuto_);
	dualKeep_ = new QCheckBox("Leave the window up when I get out of the vehicle (off: it goes by itself)", g);
	f->addRow(dualKeep_);
	dualLook_ = new QCheckBox("A small frame and their name on the window (uses the effects from Stream look)", g);
	f->addRow(dualLook_);
	connect(dualLook_, &QCheckBox::toggled, this, [this](bool on) {
		if (building_)
			return;
		e_->cfg.dualLook = on;
		e_->cfg.save();
		if (e_->dualOn())
			e_->setDual(true, "look changed");
	});
	dualNameScale_ = new QSpinBox(g);
	dualNameScale_->setRange(25, 400);
	dualNameScale_->setSuffix(" %");
	dualNameScale_->setSingleStep(10);
	dualNameScale_->setToolTip(
		"How big the name on the dual window is. 100 is the default; go up if it is hard to read.");
	f->addRow("Name size on the window", dualNameScale_);
	connect(dualNameScale_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
		if (building_)
			return;
		e_->cfg.dualNameScale = v;
		e_->cfg.save();
		if (e_->dualOn())
			e_->setDual(true, "name size changed");
	});
	connect(dualKeep_, &QCheckBox::toggled, this, [this](bool on) {
		if (building_)
			return;
		e_->cfg.dualKeep = on;
		e_->cfg.save();
		e_->pushAppConfig();
	});
	f->addRow(muted(
		"ClipHound reads the keybind list the game draws bottom-right in a vehicle - CYCLE WEAPON, DEPLOY SMOKE, COLLECTIVE LIFT and so on - which says which seat you are in, and the window comes up with that seat's placement. Drag the blue box round that list on the picture below if it is not already over it.",
		g));
	dualState_ = new QLabel(g);
	dualState_->setWordWrap(true);
	f->addRow("Now", dualState_);
	v->addWidget(g);
	auto *pr = new QHBoxLayout();
	dragWin_ = new QRadioButton("the window (dashed)", w);
	dragKeys_ = new QRadioButton("the keybind list (blue)", w);
	dragWin_->setChecked(true);
	pr->addWidget(new QLabel("Dragging on the picture sets:", w));
	pr->addWidget(dragWin_);
	pr->addWidget(dragKeys_);
	pr->addStretch(1);
	v->addLayout(pr);
	dualPick_ = new FramePreview(w);
	dualPick_->setMinimumHeight(220);
	dualPick_->setPicker(
		"The dashed box is the dual POV window over your game. Drag on the picture to place it (switches to Custom).");
	v->addWidget(dualPick_, 1);
	v->addWidget(muted(
		"Their feed is whatever you set up for them: a Discord pop-out, Twitch, Kick, YouTube or VDO.Ninja. A Twitch feed runs a couple of seconds behind you; Discord and VDO.Ninja are the ones for a tight crew. If they are also the squad mate the POV swap uses, that keeps working.",
		w));

	dualToUi();
	connect(dualOn_, &QCheckBox::toggled, this, [this](bool on) {
		if (building_)
			return;
		dualFromUi(false);
		e_->setDual(on, "Dual POV tab");
	});
	connect(dualFriend_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
		if (!building_)
			dualFromUi(false);
	});
	connect(dualPreset_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
		if (!building_)
			dualFromUi(true);
	});
	for (auto *sb : {dualX_, dualY_, dualW_})
		connect(sb, &QDoubleSpinBox::editingFinished, this, [this]() {
			dualPreset_->blockSignals(true);
			dualPreset_->setCurrentIndex(dualPreset_->findData("custom"));
			dualPreset_->blockSignals(false);
			dualFromUi(false);
		});
	connect(dualOpacity_, &QSlider::valueChanged, this, [opLbl](int v) { opLbl->setText(QString("%1 %").arg(v)); });
	connect(dualOpacity_, &QSlider::sliderReleased, this, [this]() { dualFromUi(false); });
	opLbl->setText(QString("%1 %").arg(dualOpacity_->value()));
	connect(dualPick_, &FramePreview::boxChanged, this, [this](QRectF r) {
		Config &c = e_->cfg;
		if (dragKeys_ && dragKeys_->isChecked()) {
			c.vehX = r.x();
			c.vehY = r.y();
			c.vehW = r.width();
			c.vehH = r.height();
			c.save();
			e_->pushAppConfig();
			return;
		}
		c.dualX = r.x();
		c.dualY = r.y();
		c.dualW = r.width();
		c.dualPreset = "custom";
		dualToUi();
		dualFromUi(false);
	});
	connect(dualAuto_, &QCheckBox::toggled, this, [this](bool on) {
		if (building_)
			return;
		if (on && !e_->appConnected())
			QMessageBox::information(
				this, "Kennel.gg Wardogs",
				"Automatic Dual POV needs ClipHound running - it reads the vehicle keybind list. Start it from the dock; the setting is kept.");
		e_->cfg.dualAuto = on;
		e_->cfg.save();
		e_->pushAppConfig();
	});
	connect(e_, &Engine::frameUpdated, this, [this]() {
		if (!dualPick_)
			return;
		QImage img = e_->lastFrame();
		double h = img.isNull() ? e_->cfg.dualW * 9 / 16
					: e_->cfg.dualW * 9.0 / 16.0 * img.width() / img.height();
		dualPick_->setFrame(img, Match(), 1.0, QRectF(e_->cfg.dualX, e_->cfg.dualY, e_->cfg.dualW, h));
		dualPick_->setBox2(QRectF(e_->cfg.vehX, e_->cfg.vehY, e_->cfg.vehW, e_->cfg.vehH));
	});
	connect(e_, &Engine::stateChanged, this, [this]() {
		if (dualState_)
			dualState_->setText(
				(e_->dualOn()
					 ? "window is up" +
						   QString(e_->applied() ? " (stepped aside for the POV swap)" : "")
					 : QString("off")) +
				(e_->cfg.dualAuto ? "   ·   vehicle seat read: " + (e_->vehicleSeat().isEmpty()
											    ? QString("nothing yet")
											    : e_->vehicleSeat())
						  : QString("")));
		if (dualFriend_)
			fillDualFriends(); // who is live changes; the list follows, only rebuilt when it differs
	});
	return w;
}

void SettingsDialog::fillDualFriends()
{
	const Config &c = e_->cfg;
	// the same rule as the dock's pickers: in a Kennel.gg voice channel only the people live in it,
	// anywhere else the whole squad with a dot on the ones known to be streaming. The one already
	// chosen is kept whatever its state, so a setting is never lost to a moment offline
	bool live = e_->rosterLive();
	QStringList names{"(none)"};
	QList<int> idx{-1};
	for (size_t i = 0; i < c.friends.size(); i++) {
		const Friend &f = c.friends[i];
		Engine::Feed st = e_->feedState(f);
		bool chosen = (int)i == c.dualFriend;
		if (live && st != Engine::Feed::Live && !chosen)
			continue;
		QString label = QString::fromStdString(f.name);
		if (st == Engine::Feed::Live)
			label += "  \u25cf";
		else if (live && chosen)
			label += "  (not live now)";
		names << label;
		idx << (int)i;
	}
	QStringList shown;
	for (int i = 0; i < dualFriend_->count(); i++)
		shown << dualFriend_->itemText(i);
	bool was = building_;
	building_ = true;
	if (shown != names) {
		dualFriend_->clear();
		for (int i = 0; i < names.size(); i++)
			dualFriend_->addItem(names[i], idx[i]);
	}
	dualFriend_->setCurrentIndex(std::max(0, dualFriend_->findData(c.dualFriend)));
	building_ = was;
}

void SettingsDialog::dualToUi()
{
	const Config &c = e_->cfg;
	bool was = building_;
	building_ = true;
	dualOn_->setChecked(c.dualEnabled);
	fillDualFriends();
	int pi = dualPreset_->findData(QString::fromStdString(c.dualPreset));
	dualPreset_->setCurrentIndex(pi < 0 ? 0 : pi);
	dualX_->setValue(c.dualX * 100);
	dualY_->setValue(c.dualY * 100);
	dualW_->setValue(c.dualW * 100);
	dualOpacity_->setValue(c.dualOpacity);
	dualAuto_->setChecked(c.dualAuto);
	dualKeep_->setChecked(c.dualKeep);
	dualLook_->setChecked(c.dualLook);
	dualNameScale_->setValue(c.dualNameScale);
	dualState_->setText(e_->dualOn() ? "window is up" : "off");
	building_ = was;
}

void SettingsDialog::dualFromUi(bool preset)
{
	Config &c = e_->cfg;
	c.dualEnabled = dualOn_->isChecked();
	c.dualFriend = dualFriend_->currentData().toInt();
	c.dualPreset = dualPreset_->currentData().toString().toStdString();
	if (preset) {
		for (const auto &p : kDualPresets)
			if (c.dualPreset == p.id && p.w > 0) {
				c.dualX = p.x;
				c.dualY = p.y;
				c.dualW = p.w;
			}
		dualToUi();
	} else {
		c.dualX = dualX_->value() / 100;
		c.dualY = dualY_->value() / 100;
		c.dualW = dualW_->value() / 100;
	}
	c.dualOpacity = dualOpacity_->value();
	c.save();
	if (e_->dualOn())
		e_->setDual(true, "settings changed");
}

// ============================================================ Detect areas

namespace {
struct AreaDef {
	const char *key, *label;
	QColor colour;
	QRectF def;       // the default, as fractions of the game source
	double aspect;    // fixed width / height in pixels, 0 = free
	const char *what; // one line on what reads it
};
const QVector<AreaDef> &areaDefs()
{
	static const QVector<AreaDef> d = {
		{"feed", "Kill feed", QColor(224, 180, 87), QRectF(0.0, 0.42, 0.24, 0.16), 0,
		 "ClipHound reads the kill feed here: your kills, deaths and the clips they make. Round the list of kills, left side about half way down, with a little margin."},
		{"dmg", "Damage log", QColor(239, 90, 76), QRectF(0.45, 0.15, 0.55, 0.80), 0,
		 "The plugin looks for the downed screen's \"VIEW DAMAGE LOG\" header in here, ten times a second. Keep it generous: anywhere the header can appear on your screen."},
		{"cash", "Cash", QColor(95, 208, 122), QRectF(), 0.42 / 0.22,
		 "The session stats read your balance and the reward lines under it (KILL +$1,750, ASSIST, REVIVED TEAMMATE) here, four times a second. It sits on the balance by itself at any resolution, so there is no need to move it."},
		{"invC", "Inventory: COMBINE AMMO", QColor(180, 140, 255), QRectF(0.88, 0.262, 0.10, 0.05), 0,
		 "The \"COMBINE AMMO\" hint above the storage grid: when ClipHound reads it, the inventory is open and a squad mate's POV goes up while you pack magazines."},
		{"invT", "Inventory tab", QColor(210, 184, 255), QRectF(0.082, 0.010, 0.085, 0.038), 0,
		 "The INVENTORY tab, top left: the second sign that the inventory is open."},
		{"near", "NEARBY", QColor(95, 169, 190), QRectF(0.80, 0.79, 0.19, 0.14), 0,
		 "The NEARBY list, bottom right: which squad mates are next to you and how far. \"Show whoever is closest\" (Squad & POV) reads it while you are down. Leave room above it for a full squad."},
		{"veh", "Vehicle keys", QColor(169, 184, 110), QRectF(0.86, 0.60, 0.14, 0.25), 0,
		 "The keybind list the game draws bottom right in a vehicle (CYCLE WEAPON, DEPLOY SMOKE...): it says which seat you are in, and opens the dual window by itself if that is on."},
	};
	return d;
}
} // namespace

QWidget *SettingsDialog::buildAreasTab()
{
	auto *w = new QWidget(this);
	auto *v = new QVBoxLayout(w);
	v->addWidget(
		muted("Every area the plugin and ClipHound read, on a live picture of your game. Click a box (or its "
		      "button) to pick it, drag it to move it, drag a corner or an edge to resize it, or drag on the "
		      "picture to draw the picked one again. Changes take effect at once. Faint dashed boxes belong to "
		      "features that are off.",
		      w));
	auto *btns = new FlowLayout();
	auto *group = new QButtonGroup(w);
	group->setExclusive(true);
	for (const auto &d : areaDefs()) {
		auto *b = new QPushButton(QString::fromUtf8("■  ") + d.label, w);
		b->setCheckable(true);
		b->setStyleSheet(
			QString("QPushButton { color: %1; } QPushButton:checked { color: #121518; background: %1; "
				"border-color: %1; }")
				.arg(d.colour.name()));
		group->addButton(b);
		btns->addWidget(b);
		areaBtns_[d.key] = b;
		QString key = d.key;
		connect(b, &QPushButton::clicked, this, [this, key]() {
			areaEd_->select(key);
			refreshAreas();
		});
	}
	v->addLayout(btns);
	areaEd_ = new AreaEditor(w);
	areaEd_->setMinimumHeight(300);
	v->addWidget(areaEd_, 1);
	connect(areaEd_, &AreaEditor::areaChanged, this, [this](const QString &key, QRectF r) { setArea(key, r); });
	connect(areaEd_, &AreaEditor::selectedChanged, this, [this](const QString &) { refreshAreas(); });
	auto *row = new QHBoxLayout();
	auto *reset = new QPushButton("Reset this area", w);
	auto *resetAll = new QPushButton("Reset all areas", w);
	areaTest_ = new QPushButton("Test read", w);
	areaTest_->setToolTip("Read the NEARBY area once, right now, and show what ClipHound sees there.");
	row->addWidget(reset);
	row->addWidget(resetAll);
	row->addWidget(areaTest_);
	row->addStretch(1);
	v->addLayout(row);
	if (feedRate_)
		v->addWidget(feedRate_);
	cashUnlock_ = new QCheckBox("Move it anyway (only for a custom HUD scale or an unusual layout)", w);
	cashUnlock_->setChecked(e_->cfg.cashCustom);
	v->addWidget(cashUnlock_);
	connect(cashUnlock_, &QCheckBox::toggled, this, [this](bool) { refreshAreas(); });
	connect(areaEd_, &AreaEditor::lockedTouched, this, [this](const QString &key) {
		if (key != "cash")
			return;
		cashNagged_ = true;
		refreshAreas();
	});
	areaStatus_ = new QLabel(w);
	areaStatus_->setWordWrap(true);
	areaStatus_->setTextFormat(Qt::RichText);
	v->addWidget(areaStatus_);
	connect(areaTest_, &QPushButton::clicked, this, [this]() { showNearbyTest(); });
	connect(reset, &QPushButton::clicked, this, [this]() {
		QString key = areaEd_->selected();
		for (const auto &d : areaDefs())
			if (key == d.key)
				setArea(key, d.def); // an empty rect puts the cash area back on automatic
	});
	connect(resetAll, &QPushButton::clicked, this, [this]() {
		for (const auto &d : areaDefs())
			setArea(d.key, d.def);
	});
	areaEd_->setFrame(e_->lastFrame());
	refreshAreas();
	areaEd_->select("feed");
	refreshAreas();
	return w;
}

void SettingsDialog::setArea(const QString &key, QRectF r)
{
	Config &c = e_->cfg;
	auto put = [&r](double &x, double &y, double &w, double &h) {
		x = r.x();
		y = r.y();
		w = r.width();
		h = r.height();
	};
	bool app = false;
	if (key == "feed") {
		put(c.feedX, c.feedY, c.feedW, c.feedH);
		app = true;
	} else if (key == "dmg") {
		put(c.dmgX, c.dmgY, c.dmgW, c.dmgH);
		e_->applySearchWidth();
	} else if (key == "cash") {
		c.cashCustom = !r.isEmpty();
		if (c.cashCustom)
			put(c.cashX, c.cashY, c.cashW, c.cashH);
	} else if (key == "invC") {
		put(c.invCX, c.invCY, c.invCW, c.invCH);
		app = true;
	} else if (key == "invT") {
		put(c.invTX, c.invTY, c.invTW, c.invTH);
		app = true;
	} else if (key == "near") {
		put(c.nearX, c.nearY, c.nearW, c.nearH);
		app = true;
	} else if (key == "veh") {
		put(c.vehX, c.vehY, c.vehW, c.vehH);
		app = true;
	}
	c.save();
	if (app)
		e_->pushAppConfig();
	e_->log("Detect areas: " + key + " set to x " + QString::number(r.x(), 'f', 3) + " y " +
		QString::number(r.y(), 'f', 3) + " w " + QString::number(r.width(), 'f', 3) + " h " +
		QString::number(r.height(), 'f', 3) + (key == "cash" && r.isEmpty() ? " (automatic)" : "") + ".");
	refreshAreas();
	updateAreas();
}

void SettingsDialog::refreshAreas()
{
	if (!areaEd_)
		return;
	const Config &c = e_->cfg;
	QImage f = e_->lastFrame();
	double W = f.isNull() ? 1920 : f.width(), H = f.isNull() ? 1080 : f.height();
	QVector<AreaEditor::Area> a;
	for (const auto &d : areaDefs()) {
		QString k = d.key;
		QRectF r = k == "feed"   ? QRectF(c.feedX, c.feedY, c.feedW, c.feedH)
			   : k == "dmg"  ? (c.wideSearch ? QRectF(0, 0, 1, 1) : QRectF(c.dmgX, c.dmgY, c.dmgW, c.dmgH))
			   : k == "cash" ? e_->cashArea(W, H)
			   : k == "invC" ? QRectF(c.invCX, c.invCY, c.invCW, c.invCH)
			   : k == "invT" ? QRectF(c.invTX, c.invTY, c.invTW, c.invTH)
			   : k == "near" ? QRectF(c.nearX, c.nearY, c.nearW, c.nearH)
					 : QRectF(c.vehX, c.vehY, c.vehW, c.vehH);
		bool active = k == "feed"                    ? true
			      : k == "dmg"                   ? c.autoDetect
			      : k == "cash"                  ? c.sessionTrack
			      : (k == "invC" || k == "invT") ? c.invSwitch
			      : k == "near"                  ? c.nearEnabled
							     : (c.dualAuto && c.dual() != nullptr);
		AreaEditor::Area ar{k, QString::fromUtf8(d.label), d.colour, r, d.aspect, active};
		ar.locked = k == "cash" && !(cashUnlock_ && cashUnlock_->isChecked());
		a.push_back(ar);
	}
	areaEd_->setAreas(a);
	QString sel = areaEd_->selected();
	for (auto it = areaBtns_.begin(); it != areaBtns_.end(); ++it)
		it.value()->setChecked(it.key() == sel);
	areaTest_->setVisible(sel == "near");
	if (cashUnlock_)
		cashUnlock_->setVisible(sel == "cash");
	if (sel != "cash")
		cashNagged_ = false;
	if (feedRate_)
		feedRate_->setVisible(sel == "feed");
	QString what, live;
	for (const auto &d : areaDefs())
		if (sel == d.key)
			what = QString::fromUtf8(d.what);
	if (sel == "feed")
		live = e_->appConnected() ? "ClipHound is reading it."
					  : "ClipHound is not running, so nothing reads it.";
	else if (sel == "dmg") {
		Match m = e_->lastGame();
		live = c.wideSearch
			       ? "\"Search the whole screen\" is on (Advanced), so the whole picture is searched and this "
				 "box is not used."
			       : QString("Best match now: %1 (downed at %2 and above).")
					 .arg(m.score < 0 ? 0.0 : m.score, 0, 'f', 2)
					 .arg(c.threshold, 0, 'f', 2);
	} else if (sel == "cash") {
		if (cashNagged_ && !(cashUnlock_ && cashUnlock_->isChecked()))
			live = "No need to move this. It finds your balance by itself, top right, at 900p, 1080p, 1440p and 4K "
			       "and in every game language. ";
		QString st = e_->cashStatus();
		const Session &s = e_->session();
		live += st == "off"                     ? "Session stats are off (Clips & replays)."
			: st.isEmpty() && s.haveBalance ? "Reading your balance: " + Session::money(s.balanceNow) + "."
			: st.isEmpty()                  ? "Reading."
							: "Not reading: " + st + ".";
		live += c.cashCustom ? "  (Your own area; Reset puts it back on automatic.)"
				     : "  (Automatic: the corner the reader was measured on.)";
	} else if (sel == "invC" || sel == "invT")
		live = !c.invSwitch             ? "Magazine packing / inventory switching is off (Squad & POV)."
		       : e_->inventoryWatched() ? "ClipHound watches it three times a second."
						: "Watched while Auto switch is on and you have a squad mate.";
	else if (sel == "near")
		live = (c.nearEnabled ? QString() : "\"Show whoever is closest\" is off (Squad & POV). ") +
		       "Reading: " + e_->nearbyStatus();
	else if (sel == "veh")
		live = c.dualAuto ? "Read while the dual window can open by itself."
				  : "The dual window does not open by itself (Dual POV), so this is not read.";
	areaStatus_->setText("<span style=\"color:#9a9e93\">" + what.toHtmlEscaped() + "</span><br>" +
			     live.toHtmlEscaped());
}

void SettingsDialog::updateAreas()
{
	const Config &c = e_->cfg;
	auto fmt = [](double x, double y, double w, double h) {
		return QString("x %1  y %2  w %3  h %4")
			.arg(x, 0, 'f', 2)
			.arg(y, 0, 'f', 2)
			.arg(w, 0, 'f', 2)
			.arg(h, 0, 'f', 2);
	};
	if (areaLbl_)
		areaLbl_->setText(
			"Kill feed: " + fmt(c.feedX, c.feedY, c.feedW, c.feedH) +
			(e_->appConnected() ? "     (sent to ClipHound)" : "     (ClipHound is not running)"));
	if (nearLbl2_)
		nearLbl2_->setText("NEARBY list: " + fmt(c.nearX, c.nearY, c.nearW, c.nearH) +
				   "     reading: " + e_->nearbyStatus());
}

void SettingsDialog::refreshAppTab()
{
	if (!twitchLbl_)
		return;
	QJsonObject t = e_->twitchStatus();
	QString st = t.value("state").toString();
	bool connected = e_->appConnected();
	if (!connected)
		twitchLbl_->setText("ClipHound is not running (start it from General, or the dock's menu)");
	else if (st == "code")
		twitchLbl_->setText("Go to " + t.value("verification_uri").toString() + " and enter code  " +
				    t.value("user_code").toString());
	else if (st == "ok" && !t.value("login").toString().isEmpty())
		twitchLbl_->setText("Logged in as " + t.value("login").toString() +
				    (t.contains("markers") && !t.value("markers").toBool() && e_->cfg.twitchMarkers
					     ? " - log in again once so stream markers can be added"
					     : ""));
	else if (st == "error")
		twitchLbl_->setText("Login failed: " + t.value("error").toString());
	else if (t.contains("has_app_id") && !t.value("has_app_id").toBool())
		twitchLbl_->setText("Twitch login is not available in this build yet");
	else
		twitchLbl_->setText("Not logged in");
	twitchLogin_->setEnabled(connected && st != "code");
	twitchLogout_->setEnabled(connected && st == "ok" && !t.value("login").toString().isEmpty());
	if (st == "code") {
		static QString shown;
		QString code = t.value("user_code").toString();
		if (shown != code) {
			shown = code;
			QDesktopServices::openUrl(QUrl(t.value("verification_uri").toString()));
			QApplication::clipboard()->setText(code);
		}
	}
}

static QString tailFile(const QString &path, int lines)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
		return QString();
	QStringList all = QString::fromUtf8(f.readAll()).split('\n');
	if (all.size() > lines)
		all = all.mid(all.size() - lines);
	return all.join('\n');
}

QWidget *SettingsDialog::buildLogsTab()
{
	auto *w = new QWidget(this);
	auto *v = new QVBoxLayout(w);
	logView_ = new QPlainTextEdit(w);
	logView_->setReadOnly(true);
	logView_->setLineWrapMode(QPlainTextEdit::NoWrap);
	v->addWidget(logView_, 1);
	auto *row = new QHBoxLayout();
	auto *copy = new QPushButton("Copy all", w);
	auto *refresh = new QPushButton("Refresh", w);
	auto *obsLogs = new QPushButton("Open OBS log folder", w);
	auto *cfgFolder = new QPushButton("Open plugin config folder", w);
	for (auto *b : {copy, refresh, obsLogs, cfgFolder})
		row->addWidget(b);
	row->addStretch(1);
	v->addLayout(row);
	v->addWidget(muted(
		"Paste this to Sombrero when something misbehaves: it has the plugin's state, its recent log and the tail of ClipHound's log.",
		w));
	connect(copy, &QPushButton::clicked, this, [this, copy]() {
		QApplication::clipboard()->setText(logView_->toPlainText());
		copy->setText("Copied");
	});
	connect(refresh, &QPushButton::clicked, this, [this]() { refreshLogs(); });
	connect(obsLogs, &QPushButton::clicked, this, []() {
		QDesktopServices::openUrl(QUrl::fromLocalFile(
			QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).section('/', 0, -2) +
			"/obs-studio/logs"));
	});
	connect(cfgFolder, &QPushButton::clicked, this,
		[]() { QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(Config::configDir()))); });
	connect(e_, &Engine::logged, this, [this](const QString &) { refreshLogs(); });
	refreshLogs();
	return w;
}

void SettingsDialog::refreshLogs()
{
	if (!logView_)
		return;
	QString appDir =
		QFileInfo(e_->cfg.appPath.empty() ? Engine::defaultAppPath() : QString::fromStdString(e_->cfg.appPath))
			.absolutePath();
	QString body = QString("=== Kennel.gg Wardogs plugin %1 ===\n").arg(PLUGIN_VERSION);
	body += QString("state: %1 | game source: %2 | squad mate: %3 | replay buffer: %4 | ClipHound: %5 | clip hotkeys: %6 | twitch: %7\n\n")
			.arg(QString::fromStdString(e_->stateText()), QString::fromStdString(e_->cfg.gameSource),
			     e_->cfg.active() ? QString::fromStdString(e_->cfg.active()->name) : "(none)",
			     obs_frontend_replay_buffer_active() ? "running" : "NOT running",
			     e_->appConnected() ? "connected" : "not connected",
			     QString::number(e_->cfg.clipHotkeys.size()),
			     e_->twitchStatus().value("state").toString() + " " +
				     e_->twitchStatus().value("login").toString());
	body += e_->recentLog().join('\n');
	body += "\n\n=== ClipHound (" + appDir + "/cliphound.log, last 200 lines) ===\n";
	QString ch = tailFile(appDir + "/cliphound.log", 200);
	body += ch.isEmpty() ? "(no log file - is ClipHound running from that folder?)" : ch;
	bool atEnd = logView_->verticalScrollBar()->value() >= logView_->verticalScrollBar()->maximum() - 4;
	logView_->setPlainText(body);
	if (atEnd)
		logView_->verticalScrollBar()->setValue(logView_->verticalScrollBar()->maximum());
}

QWidget *SettingsDialog::buildVoiceTab()
{
	auto *w = new QWidget(this);
	auto *v = new QVBoxLayout(w);
	auto *g = new QGroupBox("Voice control (beta) - English only", w);
	auto *f = new QFormLayout(g);
	voiceOn_ = new QCheckBox("Listen to my microphone (through ClipHound)", g);
	voiceOn_->setChecked(e_->cfg.voiceEnabled);
	f->addRow(voiceOn_);
	f->addRow(muted("Your microphone's sound goes from OBS to ClipHound on this PC, where it is turned into "
			"words. It is never recorded and never leaves the PC: the speech models run locally. The "
			"first time you switch this on ClipHound downloads them (about 120 MB), which takes a minute. "
			"English only for now. This is a beta: the log shows every command as it was heard, and a "
			"session's log is what tells us what to tune.",
			g));
	voiceMic_ = new QComboBox(g);
	voiceMic_->addItem("Auto (the first microphone in OBS)", "");
	{
		struct Acc {
			QComboBox *box;
		} acc{voiceMic_};
		obs_enum_sources(
			[](void *p, obs_source_t *s) -> bool {
				auto *a = (Acc *)p;
				uint32_t flags = obs_source_get_output_flags(s);
				if ((flags & OBS_SOURCE_AUDIO) && !(flags & OBS_SOURCE_VIDEO)) {
					QString n = obs_source_get_name(s);
					a->box->addItem(n, n);
				}
				return true;
			},
			&acc);
	}
	int mi = voiceMic_->findData(QString::fromStdString(e_->cfg.voiceMic));
	voiceMic_->setCurrentIndex(mi < 0 ? 0 : mi);
	f->addRow("Microphone source", voiceMic_);
	voiceWake_ = new QLineEdit(QString::fromStdString(e_->cfg.voiceWake), g);
	voiceWake_->setPlaceholderText("hey kennel");
	voiceWake_->setMaximumWidth(200);
	voiceWake_->setToolTip(
		"Say this first, then the command: \"hey kennel, replay\". Both words are needed, so \"kennel\" in "
		"conversation (\"the dog kennel\") does not wake it.");
	f->addRow("Wake phrase", voiceWake_);
	voiceChime_ = new QCheckBox("Chime when the wake phrase is heard", g);
	voiceChime_->setChecked(e_->cfg.voiceChime);
	voiceChime_->setToolTip(
		"A soft two-note chime a moment after \"hey kennel\", through this PC's speakers and "
		"into the stream's mix, so you and your viewers know it is listening: hey kennel, chime, "
		"then the command. The command can also follow straight on without a pause.");
	{
		auto *cr = new QHBoxLayout();
		cr->addWidget(voiceChime_);
		voiceChimeVol_ = new QSpinBox(g);
		voiceChimeVol_->setRange(5, 100);
		voiceChimeVol_->setSuffix(" %");
		voiceChimeVol_->setValue(e_->cfg.voiceChimeVol);
		voiceChimeVol_->setToolTip("How loud, on your PC and on the stream alike.");
		cr->addWidget(new QLabel("volume", g));
		cr->addWidget(voiceChimeVol_);
		voiceChimeWhere_ = new QComboBox(g);
		voiceChimeWhere_->addItem("through this PC's speakers", "pc");
		voiceChimeWhere_->addItem("into the stream through OBS", "obs");
		voiceChimeWhere_->addItem("both", "both");
		voiceChimeWhere_->setToolTip(
			"One device, so it is not heard twice. Through the speakers: you hear it, and if OBS captures your "
			"desktop audio the stream hears that same copy. Through OBS: it goes straight into the mix and you "
			"hear it only if you monitor OBS. Both: pick this only if your desktop audio is NOT captured, or "
			"the recording gets it twice.");
		int wi = voiceChimeWhere_->findData(QString::fromStdString(e_->cfg.voiceChimeWhere));
		voiceChimeWhere_->setCurrentIndex(wi < 0 ? 0 : wi);
		cr->addWidget(voiceChimeWhere_);
		cr->addStretch(1);
		f->addRow(cr);
	}
	voiceTones_ = new QCheckBox(
		"Tones after a command: rising when it was taken, falling when the words were not understood", g);
	voiceTones_->setChecked(e_->cfg.voiceTones);
	voiceTones_->setToolTip("Through the same device and at the same volume as the chime.");
	f->addRow(voiceTones_);
	voiceStatus_ = new QLabel(e_->voiceStatus().isEmpty() ? "not listening" : e_->voiceStatus(), g);
	voiceStatus_->setWordWrap(true);
	f->addRow("Status", voiceStatus_);
	auto *allCmds = new QPushButton("All voice commands and the ways to say them...", g);
	f->addRow(allCmds);
	connect(allCmds, &QPushButton::clicked, this, [this]() { showVoicePhrases(); });
	v->addWidget(g);

	auto *gc = new QGroupBox("Commands - each can be switched off", w);
	auto *fc = new QFormLayout(gc);
	voiceCommands_ = new QCheckBox("Voice commands on", gc);
	voiceCommands_->setChecked(e_->cfg.voiceCommands);
	fc->addRow(voiceCommands_);
	fc->addRow(muted("Say \"hey kennel\", then the ask. The words do not have to be exact: \"hey kennel replay\", "
			 "\"hey kennel play that back\" and \"hey kennel run it back\" all play the replay.",
			 gc));
	auto mk = [&](QCheckBox *&box, const char *label, bool on, const char *hint) {
		box = new QCheckBox(label, gc);
		box->setChecked(on);
		box->setToolTip(hint);
		fc->addRow(box, muted(hint, gc));
	};
	mk(voiceCmdReplay_, "Kennel - instant replay", e_->cfg.voiceCmdReplay,
	   "\"replay\", \"instant replay\", \"play that back\", \"run it back\": plays the last highlight.");
	mk(voiceCmdClip_, "Kennel - clip that", e_->cfg.voiceCmdClip,
	   "\"clip that\", \"clip it\", \"save that\": saves a clip of the replay buffer (as long as OBS keeps it).");
	fc->addRow(muted("Kennel - clip replay: \"clip replay\", \"clip and replay\": saves the clip and plays it back "
			 "straight away (needs both clip and instant replay on).",
			 gc));
	voiceNames_ = new QCheckBox("    ... and the sentence after \"clip that\" becomes the file name", gc);
	voiceNames_->setChecked(e_->cfg.voiceNames);
	fc->addRow(
		voiceNames_,
		muted("\"hey kennel, clip that, he fell off the roof\" saves \"He Fell Off The Roof - date time.mp4\". "
		      "Clips from the dock or the hotkey take the last sentence you said.",
		      gc));
	mk(voiceCmdDual_, "Kennel - force dual point of view", e_->cfg.voiceCmdDual,
	   "\"dual\", \"dual pov\", \"split screen\": Dual POV on; \"dual off\" turns it off.");
	mk(voiceCmdForce_, "Kennel - force squad mate point of view", e_->cfg.voiceCmdForce,
	   "\"squad mate pov\", \"show my squad mate\", \"show his pov\": the chosen squad mate on stream now, "
	   "downed or not. \"kennel back\" / \"kennel my pov\" returns to you.");
	mk(voiceCmdChange_, "Kennel - change squad mate point of view", e_->cfg.voiceCmdChange,
	   "\"change squad mate\", \"next squad mate\": the next one with a picture; \"show bouga\" / \"switch to "
	   "bouga\" picks by name, loosely matched, spoken numbers work (\"bouga three four\").");
	mk(voiceCmdClosest_, "Kennel - show closest squad mate point of view", e_->cfg.voiceCmdClosest,
	   "\"closest\", \"nearest squad mate\", \"who's closest\": the nearest squad mate from the NEARBY list "
	   "(needs ClipHound's NEARBY reading).");
	v->addWidget(gc);
	v->addStretch(1);
	for (auto *c : {voiceOn_, voiceNames_, voiceCommands_, voiceChime_, voiceTones_, voiceCmdReplay_, voiceCmdClip_,
			voiceCmdDual_, voiceCmdForce_, voiceCmdChange_, voiceCmdClosest_})
		connect(c, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	connect(voiceMic_, &QComboBox::currentIndexChanged, this, [this](int) { saveAndApply(); });
	connect(voiceWake_, &QLineEdit::editingFinished, this, [this]() { saveAndApply(); });
	connect(voiceChimeVol_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { saveAndApply(); });
	connect(voiceChimeWhere_, &QComboBox::currentIndexChanged, this, [this](int) { saveAndApply(); });
	connect(e_, &Engine::stateChanged, this, [this]() {
		if (voiceStatus_)
			voiceStatus_->setText(
				e_->voiceStatus().isEmpty()
					? (e_->cfg.voiceEnabled ? "waiting for ClipHound" : "not listening")
					: e_->voiceStatus());
	});
	return w;
}

void SettingsDialog::showVoicePhrases()
{
	auto *d = new QDialog(this);
	d->setAttribute(Qt::WA_DeleteOnClose);
	d->setWindowTitle("Kennel.gg Wardogs - voice commands");
	d->setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
	d->resize(760, 640);
	auto *v = new QVBoxLayout(d);
	QString wake = QString::fromStdString(e_->cfg.voiceWake).toHtmlEscaped();
	QString html = "<p>Say <b>" + wake +
		       "</b>, then any of these. The words do not have to be exact: what you say is scored against "
		       "every phrasing below and the closest wins. You can pause after <b>" +
		       wake + "</b> (a chime says it is listening) or say it all in one breath. English only.</p>";
	for (const auto &g : kVoicePhrases) {
		html += "<h3 style=\"margin-bottom:2px\">" + QString(g.title).toHtmlEscaped() + "</h3>";
		QString what = QString(g.what);
		if (!what.isEmpty())
			html += "<p style=\"margin-top:0;color:#7c8076\">" + what.toHtmlEscaped() +
				" &nbsp;<i>(switch: " + QString(g.gate).toHtmlEscaped() + ")</i></p>";
		else
			html += "<p style=\"margin-top:0;color:#7c8076\"><i>(switch: " +
				QString(g.gate).toHtmlEscaped() + ")</i></p>";
		html += "<p style=\"margin-top:0\">";
		QStringList parts = QString(g.phrases).split('|', Qt::SkipEmptyParts);
		for (int i = 0; i < parts.size(); i++)
			html += (i ? " &middot; " : "") + QString("<b>") + wake + " " + parts[i].toHtmlEscaped() +
				"</b>";
		html += "</p>";
	}
	html += "<p style=\"color:#7c8076\">Names are matched loosely (\"show bouga\" finds bouga34) and spoken numbers "
		"become digits. After \"clip that\", whatever you say next becomes the clip's title.</p>";
	auto *t = new QTextBrowser(d);
	t->setOpenExternalLinks(false);
	t->setHtml(html);
	v->addWidget(t, 1);
	auto *close = new QPushButton("Close", d);
	connect(close, &QPushButton::clicked, d, &QDialog::close);
	auto *row = new QHBoxLayout();
	row->addStretch(1);
	row->addWidget(close);
	v->addLayout(row);
	d->show();
}

QWidget *SettingsDialog::buildAboutTab()
{
	auto *w = new QWidget(this);
	auto *v = new QVBoxLayout(w);
	auto *ver = new QGroupBox("This build", w);
	auto *vf = new QFormLayout(ver);
	auto *vl = new QLabel(QString("<b>Version %1</b>&nbsp; &nbsp;built for OBS 30+, Windows").arg(PLUGIN_VERSION),
			      ver);
	vl->setTextInteractionFlags(Qt::TextSelectableByMouse);
	vf->addRow(vl);
	auto *ur = new QHBoxLayout();
	updateLbl_ = new QLabel(e_->updateState().isEmpty() ? "not checked yet" : e_->updateState(), ver);
	updateLbl_->setWordWrap(true);
	updateLbl_->setTextFormat(Qt::RichText);
	updateLbl_->setOpenExternalLinks(true);
	auto *checkBtn = new QPushButton("Check now", ver);
	ur->addWidget(updateLbl_, 1);
	ur->addWidget(checkBtn);
	vf->addRow("Newer build", ur);
	updateAuto_ = new QCheckBox("Check for a newer build when OBS starts", ver);
	updateAuto_->setChecked(e_->cfg.updateCheck);
	vf->addRow(updateAuto_);
	vf->addRow(muted(
		"The check asks kennel.gg for a small file saying what the latest build is. Nothing about you is sent, there is no account, and it never installs anything: when there is a newer build the dock says so and links to the download.",
		ver));
	v->addWidget(ver);

	// The tool is also how people find the community - so say who made it and why, up front.
	auto *about = new QGroupBox("About The Kennel", w);
	auto *av = new QVBoxLayout(about);
	auto *al = new QLabel(
		"<p><b>The Kennel [KNL]</b> is the community hub for WARDOGS, built by <b>Sombrero</b> - a place "
		"for people who want to get better at the game and play it with others who feel the same. "
		"<i>Community &middot; Education &middot; Progress.</i></p>"
		"<p>At <a href=\"https://kennel.gg\">kennel.gg</a> you will find guides written from actual play, the "
		"Bootcamp (drills on economy, roles and loadouts), a loadout builder, a leaderboard, and a Discord "
		"where squads form, scrims get organised and questions get straight answers.</p>"
		"<p>This plugin is one of the things we make for streamers in that community: it is free, it is ours, "
		"and it is built from what people actually ask for. If it helps your stream, come and say so.</p>"
		"<p><a href=\"https://kennel.gg\">kennel.gg</a> &nbsp;&middot;&nbsp; "
		"<a href=\"https://discord.gg/vHqDR9HHcM\">Discord</a> &nbsp;&middot;&nbsp; "
		"<a href=\"https://twitch.tv/sombrero\">twitch.tv/sombrero</a> &nbsp;&middot;&nbsp; "
		"<a href=\"https://x.com/Smb_GG\">@Smb_GG</a></p>",
		about);
	al->setWordWrap(true);
	al->setTextFormat(Qt::RichText);
	al->setOpenExternalLinks(true);
	al->setTextInteractionFlags(Qt::TextBrowserInteraction);
	av->addWidget(al);
	v->addWidget(about);

	connect(checkBtn, &QPushButton::clicked, this, [this]() { e_->checkForUpdate(true); });
	connect(updateAuto_, &QCheckBox::toggled, this, [this](bool on) {
		if (building_)
			return;
		e_->cfg.updateCheck = on;
		e_->cfg.save();
	});
	auto showUpdate = [this]() {
		if (!updateLbl_)
			return;
		QString t = e_->updateState().isEmpty() ? "not checked yet" : e_->updateState();
		if (e_->updateAvailable() && !e_->newVersionUrl().isEmpty())
			t += "  &nbsp;<a href=\"" + e_->newVersionUrl().toHtmlEscaped() + "\">get it</a>";
		if (e_->updateAvailable() && !e_->newVersionNotes().isEmpty())
			t += "<br>" + e_->newVersionNotes().toHtmlEscaped();
		updateLbl_->setText(t);
	};
	showUpdate();
	connect(e_, &Engine::updateChecked, this, showUpdate);

	auto *l = new QLabel(w);
	l->setWordWrap(true);
	l->setTextInteractionFlags(Qt::TextSelectableByMouse);
	l->setText(
		"<h3>Kennel.gg Wardogs Streaming Tool</h3>"
		"<p>Downed in WARDOGS? Your stream shows a squad mate's POV (video and game audio) until you are back up. Your mic is never touched.</p>"
		"<ol>"
		"<li><b>General:</b> your name in WARDOGS, your game source and scene, the game language.</li>"
		"<li><b>Squad &amp; POV:</b> squad mates, Closest, what is muted, what stays on top.</li>"
		"<li><b>Get downed once</b>: the dock's pill turns red and your stream shows the squad mate.</li>"
		"<li><b>Stream look:</b> name tag, camcorder frame, grain, vignette. Preview them in OBS.</li>"
		"<li>The <b>Kennel.gg Wardogs dock</b> (View → Docks) shows what is on stream, what needs fixing, and "
		"the buttons.</li>"
		"</ol>"
		"<p><b>Ways to control it.</b> The dock's buttons. OBS hotkeys (Settings → Hotkeys, \"Kennel.gg "
		"Wardogs\"): swap, Dual POV, save a clip, instant replay, highlights. The Stream Deck plugin, from "
		"<a href=\"https://kennel.gg/streaming/\">kennel.gg/streaming</a>: squad mate, cycle, clip, replay, clip and "
		"replay, voice, Dual POV, back to me. Voice (beta): \"hey kennel\" and a command. Chat: "
		"subscribers, VIPs and moderators type <code>!replay</code> (Clips &amp; replays).</p>"
		"<p><b>Squad mate feeds.</b> Twitch: nothing for them to do, ~2 s behind with low-latency mode, includes their mic. "
		"VDO.Ninja: they open one link in Chrome/Edge and share their game window with system audio, ~0.3 s, no mic. "
		"Discord Go Live (~0.5-1 s, 720p without Nitro): they Go Live in the call, you pop their stream out into its own window, add a Window Capture of it "
		"(Windows 10 method, keep it unminimised) and press Add pop-outs. Its sound is not handled: Discord hands OBS one mix for the whole call.</p>"
		"<p><b>Timing the switch back.</b> While a squad mate is on screen, the plugin also watches their feed for the word REVIVING and the progress ring. "
		"When it sees it, the switch back fires the instant the damage log disappears from your own game, with no confirmation delay. "
		"Your own feed is the trigger because it has no latency; the squad mate's feed only arms it.</p>"
		"<p>Settings and templates: <code>" +
		QString::fromStdString(Config::configDir()) + "</code></p>");
	v->addWidget(l);
	v->addStretch(1);
	return w;
}

void SettingsDialog::fillHotkeys()
{
	if (!hotkeyList_)
		return;
	hotkeyList_->blockSignals(true);
	hotkeyList_->clear();
	QString flt = hotkeyFilter_ ? hotkeyFilter_->text().trimmed().toLower() : QString();
	auto all = Clips::allHotkeys();
	auto rank = [](const QPair<QString, QString> &h) {
		QString t = (h.first + " " + h.second).toLower();
		if (t.contains("backtrack") && t.contains("save"))
			return 0;
		if (t.contains("backtrack"))
			return 1;
		if (t.contains("replay") || t.contains("clip"))
			return 2;
		return 3;
	};
	std::stable_sort(all.begin(), all.end(),
			 [&](const QPair<QString, QString> &x, const QPair<QString, QString> &y) {
				 return rank(x) < rank(y);
			 });
	for (auto &hk : all) {
		if (hk.first.startsWith("kennel.") || hk.first.startsWith("OBSBasic.") ||
		    hk.first.startsWith("libobs."))
			continue;
		bool on = std::find(e_->cfg.clipHotkeys.begin(), e_->cfg.clipHotkeys.end(), hk.first.toStdString()) !=
			  e_->cfg.clipHotkeys.end();
		QString label = hk.second.isEmpty() ? hk.first : hk.second + "   ·  " + hk.first;
		if (!flt.isEmpty() && !label.toLower().contains(flt) && !on)
			continue;
		auto *it = new QListWidgetItem(label, hotkeyList_);
		it->setData(Qt::UserRole, hk.first);
		it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
		it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
	}
	hotkeyList_->blockSignals(false);
}

void SettingsDialog::fillSources()
{
	auto inputs = Switcher::inputs();
	auto scenes = Switcher::sceneNames();
	game_->blockSignals(true);
	scene_->blockSignals(true);
	mute_->blockSignals(true);
	game_->clear();
	QString chosen = QString::fromStdString(e_->cfg.gameSource);
	if (!chosen.isEmpty())
		game_->addItem(chosen);
	for (auto &i : inputs)
		if (i.first != e_->cfg.gameSource && i.first != Config::webSourceName() &&
		    i.first != Config::overlaySourceName())
			game_->addItem(QString::fromStdString(i.first));
	game_->setCurrentIndex(chosen.isEmpty() ? -1 : 0);
	if (gameDetect_) {
		gameDetect_->blockSignals(true);
		gameDetect_->clear();
		for (int i = 0; i < game_->count(); i++)
			gameDetect_->addItem(game_->itemText(i));
		gameDetect_->setCurrentIndex(game_->currentIndex());
		gameDetect_->blockSignals(false);
	}
	scene_->clear();
	for (auto &s : scenes)
		scene_->addItem(QString::fromStdString(s));
	QString want = QString::fromStdString(e_->cfg.sceneName);
	if (want.isEmpty()) {
		obs_source_t *cs = obs_frontend_get_current_scene();
		if (cs) {
			want = obs_source_get_name(cs);
			obs_source_release(cs);
		}
	}
	int si = scene_->findText(want);
	scene_->setCurrentIndex(si < 0 ? 0 : si);
	if (sceneV_) {
		sceneV_->blockSignals(true);
		sceneV_->clear();
		sceneV_->addItem("(none)", "");
		for (const auto &cs : Switcher::otherCanvasScenes()) {
			QString label = cs.first.empty() ? QString::fromStdString(cs.second)
							 : QString::fromStdString(cs.first) + "  /  " +
								   QString::fromStdString(cs.second);
			sceneV_->addItem(label,
					 QString::fromStdString(cs.first) + "\n" + QString::fromStdString(cs.second));
		}
		QString have = QString::fromStdString(e_->cfg.canvasV) + "\n" + QString::fromStdString(e_->cfg.sceneV);
		if (!e_->cfg.sceneV.empty() && sceneV_->findData(have) < 0) {
			// the canvas may have been renamed: the scene name alone, on any canvas
			int alt = -1;
			for (int i = 1; i < sceneV_->count() && alt < 0; i++)
				if (sceneV_->itemData(i).toString().section('\n', 1) ==
				    QString::fromStdString(e_->cfg.sceneV))
					alt = i;
			if (alt >= 0)
				have = sceneV_->itemData(alt).toString();
			else
				sceneV_->addItem(QString::fromStdString(e_->cfg.sceneV) + "  (not found now)", have);
		}
		sceneV_->setCurrentIndex(std::max(0, sceneV_->findData(have)));
		sceneV_->blockSignals(false);
	}
	mute_->clear();
	for (auto &i : inputs) {
		if (i.first == Config::webSourceName() || i.first == Config::overlaySourceName())
			continue;
		obs_source_t *src = obs_get_source_by_name(i.first.c_str());
		if (!src)
			continue;
		bool audio = (obs_source_get_output_flags(src) & OBS_SOURCE_AUDIO) != 0;
		obs_source_release(src);
		if (!audio)
			continue;
		QString label = QString::fromStdString(i.first);
		if (i.second.find("input_capture") != std::string::npos)
			label += "   ·  MICROPHONE - leave unticked";
		else if (i.second.find("output_capture") != std::string::npos)
			label += "   ·  desktop audio";
		auto *it = new QListWidgetItem(label, mute_);
		it->setData(Qt::UserRole, QString::fromStdString(i.first));
		it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
		bool on = std::find(e_->cfg.muteWhileDowned.begin(), e_->cfg.muteWhileDowned.end(), i.first) !=
			  e_->cfg.muteWhileDowned.end();
		it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
	}
	if (onTop_) {
		onTop_->blockSignals(true);
		onTop_->clear();
		// once, so a new user's camera and alerts are covered without them having to think about it
		if (!e_->cfg.onTopV1) {
			e_->cfg.onTopV1 = true;
			e_->cfg.onTop = e_->sw.guessOnTop(e_->cfg);
			e_->cfg.save();
		}
		std::vector<std::pair<std::string, std::string>> items = e_->sw.sceneItems(e_->cfg);
		auto add = [&](const std::string &name, const std::string &id, bool on) {
			QString label = QString::fromStdString(name);
			if (!id.empty())
				label += "   ·  " + QString::fromStdString(id);
			auto *it = new QListWidgetItem(label, onTop_);
			it->setData(Qt::UserRole, QString::fromStdString(name));
			it->setFlags(it->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
			it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
		};
		// ticked ones first, in their stacking order, then the rest of the scene
		for (const auto &n : e_->cfg.onTop) {
			auto f = std::find_if(items.begin(), items.end(), [&](const auto &p) { return p.first == n; });
			add(n, f != items.end() ? f->second : std::string("not in this scene"), true);
		}
		for (const auto &[name, id] : items) {
			if (std::find(e_->cfg.onTop.begin(), e_->cfg.onTop.end(), name) != e_->cfg.onTop.end())
				continue;
			if (name.rfind("Kennel", 0) == 0) // ours; it is what they sit on top of
				continue;
			add(name, id, false);
		}
		onTop_->blockSignals(false);
	}
	if (onTopV_) {
		onTopV_->blockSignals(true);
		onTopV_->clear();
		if (e_->cfg.verticalOn() && !e_->cfg.onTopVSeeded) {
			std::vector<std::string> g = e_->sw.guessOnTopV(e_->cfg);
			if (!g.empty()) { // an empty guess is not a decision: try again next time the scene has items
				e_->cfg.onTopV = g;
				e_->cfg.onTopVSeeded = true;
				e_->cfg.save();
			}
		}
		std::vector<std::pair<std::string, std::string>> items = e_->sw.sceneItemsV(e_->cfg);
		auto add = [&](const std::string &name, const std::string &id, bool on) {
			QString label = QString::fromStdString(name);
			if (!id.empty())
				label += "   ·  " + QString::fromStdString(id);
			auto *it = new QListWidgetItem(label, onTopV_);
			it->setData(Qt::UserRole, QString::fromStdString(name));
			it->setFlags(it->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
			it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
		};
		for (const auto &n : e_->cfg.onTopV) {
			auto f = std::find_if(items.begin(), items.end(), [&](const auto &p) { return p.first == n; });
			add(n, f != items.end() ? f->second : std::string("not in this scene"), true);
		}
		for (const auto &[name, id] : items) {
			if (std::find(e_->cfg.onTopV.begin(), e_->cfg.onTopV.end(), name) != e_->cfg.onTopV.end())
				continue;
			if (name.rfind("Kennel", 0) == 0)
				continue;
			add(name, id, false);
		}
		onTopV_->blockSignals(false);
	}
	game_->blockSignals(false);
	scene_->blockSignals(false);
	mute_->blockSignals(false);
}

void SettingsDialog::fillFriends()
{
	friends_->setRowCount(0);
	for (size_t i = 0; i < e_->cfg.friends.size(); i++) {
		auto &f = e_->cfg.friends[i];
		int r = friends_->rowCount();
		friends_->insertRow(r);
		QString name = QString::fromStdString(f.name) + ((int)i == e_->cfg.activeFriend ? "   ●" : "");
		const char *kind = f.kind == FriendKind::Twitch     ? "Twitch stream"
				   : f.kind == FriendKind::Kick     ? "Kick stream"
				   : f.kind == FriendKind::YouTube  ? "YouTube live"
				   : f.kind == FriendKind::VdoNinja ? "VDO.Ninja (WebRTC)"
				   : f.kind == FriendKind::Discord  ? "Discord Go Live"
								    : "OBS source";
		friends_->setItem(r, 0, new QTableWidgetItem(name));
		friends_->setItem(r, 1, new QTableWidgetItem(kind));
		friends_->setItem(r, 2,
				  new QTableWidgetItem(QString::fromStdString(
					  f.kind == FriendKind::ObsSource || f.ownsSources() ? f.source : f.channel)));
	}
}

bool addFriendByHand(Engine *e, QWidget *parent)
{
	FriendDialog dlg(nullptr, Switcher::inputs(), parent, e->cfg.vdoBitrateKbps);
	if (dlg.exec() != QDialog::Accepted)
		return false;
	Friend f = dlg.result;
	if (f.kind == FriendKind::Discord)
		QTimer::singleShot(900, e, [e, f]() { e->sw.trimToContent(e->cfg, f); });
	if (f.ownsSources()) {
		std::string err = e->sw.createFriendSources(e->cfg, f);
		if (!err.empty()) {
			QMessageBox::warning(parent, "Kennel.gg Wardogs",
					     "Could not set up the sources: " + QString::fromStdString(err));
			return false;
		}
	}
	e->cfg.friends.push_back(f);
	if (e->cfg.friends.size() == 1) {
		e->cfg.activeFriend = 0;
		e->setActive(0);
	}
	e->cfg.save();
	e->log("Squad mate added: " + QString::fromStdString(f.name) + ".");
	emit e->stateChanged();
	return true;
}

void SettingsDialog::editFriend(int row)
{
	Friend *existing = row >= 0 && row < (int)e_->cfg.friends.size() ? &e_->cfg.friends[row] : nullptr;
	if (row >= 0 && !existing)
		return;
	if (!existing) {
		// Add means the same thing here as on the Squad panel: every popped-out Discord stream
		// becomes a squad mate by itself. The by-hand dialog is for everything else.
		QStringList added;
		QString what = e_->addPopouts(&added);
		fillFriends();
		if (!added.isEmpty()) {
			QMessageBox box(QMessageBox::Information, "Kennel.gg Wardogs", what, QMessageBox::NoButton,
					this);
			auto *done = box.addButton("Done", QMessageBox::AcceptRole);
			box.addButton("Add someone else by hand...", QMessageBox::ActionRole);
			box.setDefaultButton(done);
			box.exec();
			emit e_->stateChanged();
			if (box.clickedButton() == done)
				return;
		}
	}
	FriendDialog dlg(existing, Switcher::inputs(), this, e_->cfg.vdoBitrateKbps);
	if (dlg.exec() != QDialog::Accepted)
		return;
	Friend f = dlg.result;
	if (f.kind == FriendKind::Discord)
		// give the capture a moment to have a picture, then crop Discord's window off it
		QTimer::singleShot(900, this, [this, f]() { e_->sw.trimToContent(e_->cfg, f); });
	if (f.ownsSources()) {
		std::string e = e_->sw.createFriendSources(e_->cfg, f);
		if (!e.empty()) {
			QMessageBox::warning(this, "Kennel.gg Wardogs",
					     "Could not set up the sources: " + QString::fromStdString(e));
			return;
		}
	}
	if (existing)
		*existing = f;
	else {
		e_->cfg.friends.push_back(f);
		row = (int)e_->cfg.friends.size() - 1;
		if (e_->cfg.friends.size() == 1)
			e_->cfg.activeFriend = 0;
	}
	e_->cfg.save();
	fillFriends();
	friends_->selectRow(row);
	if (row == e_->cfg.activeFriend)
		e_->setActive(row);
	emit e_->stateChanged();
}

void SettingsDialog::collect()
{
	if (building_ || !game_ || !scene_ || !mute_ || !lookName_ || !thr_ || !autoReplay_ || !bridgeOn_ ||
	    !playerName_)
		return;
	Config &c = e_->cfg;
	c.gameSource = game_->currentText().toStdString();
	c.sceneName = scene_->currentText().toStdString();
	if (onTop_) {
		c.onTop.clear();
		for (int i = 0; i < onTop_->count(); i++)
			if (onTop_->item(i)->checkState() == Qt::Checked)
				c.onTop.push_back(onTop_->item(i)->data(Qt::UserRole).toString().toStdString());
	}
	c.muteWhileDowned.clear();
	for (int i = 0; i < mute_->count(); i++)
		if (mute_->item(i)->checkState() == Qt::Checked)
			c.muteWhileDowned.push_back(mute_->item(i)->data(Qt::UserRole).toString().toStdString());
	c.bringToFront = bringFront_->isChecked();
	c.playerName = playerName_->text().trimmed().toStdString();
	if (sceneV_) {
		QString d = sceneV_->currentData().toString();
		c.canvasV = d.section('\n', 0, 0).toStdString();
		c.sceneV = d.section('\n', 1).toStdString();
	}
	if (verticalOn_)
		c.verticalEnabled = verticalOn_->isChecked();
	if (lookTopV_)
		c.lookTopV = lookTopV_->value();
	if (onTopV_) {
		c.onTopV.clear();
		for (int i = 0; i < onTopV_->count(); i++)
			if (onTopV_->item(i)->checkState() == Qt::Checked)
				c.onTopV.push_back(onTopV_->item(i)->data(Qt::UserRole).toString().toStdString());
	}
	c.rosterEnabled = rosterOn_->isChecked();
	c.rosterUrl = Config::kennelRosterUrl();
	c.rosterChannel.clear(); // whichever channel you are in
	c.rosterAddSources = rosterSources_->isChecked();
	c.keepWarm = keepWarm_->isChecked();
	if (pictureCheck_)
		c.pictureCheck = pictureCheck_->isChecked();
	if (invSwitch_)
		c.invSwitch = invSwitch_->isChecked();
	c.preloadFeeds = preload_ ? preload_->isChecked() : c.preloadFeeds;
	c.friendAudio = friendAudio_ ? friendAudio_->isChecked() : c.friendAudio;
	c.lookName = lookName_->isChecked();
	c.lookPlate = lookPlate_->isChecked();
	if (lookPos_)
		c.lookPos = lookPos_->currentData().toString().toStdString();
	c.lookCam = lookCam_->isChecked();
	c.lookGrain = lookGrain_->isChecked();
	c.lookVignette = lookVig_->isChecked();
	c.lookMark = lookMark_ ? lookMark_->isChecked() : c.lookMark;
	c.lookLabel = lookLabel_->text().trimmed().isEmpty() ? "POV" : lookLabel_->text().trimmed().toStdString();
	c.grainAmount = grain_->value();
	c.threshold = thr_->value() / 100.0;
	c.holdDrop = hold_->value() / 100.0;
	c.reviveThreshold = reviveThr_->value() / 100.0;
	c.downFrames = downFrames_->value();
	c.upFrames = upFrames_->value();
	c.minDownMs = minDown_->value();
	c.downDelayMs = downDelay_ ? downDelay_->value() : c.downDelayMs;
	c.upDelayMs = upDelay_ ? upDelay_->value() : c.upDelayMs;
	if (povStinger_)
		c.povStinger = povStinger_->isChecked();
	if (povMin_)
		c.povMinS = povMin_->value();
	c.pollMs = pollMs_->value();
	c.autoDetect = auto_->isChecked();
	c.watchRevive = revive_->isChecked();
	c.autoStartReplay = autoReplay_->isChecked();
	if (replaySecs_)
		c.clipUseReplay = useReplay_ ? useReplay_->isChecked() : true;
	c.backtrackFolder = backtrackFolder_ ? backtrackFolder_->text().trimmed().toStdString() : c.backtrackFolder;
	if (hotkeyList_) {
		// keep ticked hotkeys that are filtered out of view
		for (int i = 0; i < hotkeyList_->count(); i++) {
			auto *it = hotkeyList_->item(i);
			std::string n = it->data(Qt::UserRole).toString().toStdString();
			bool has = std::find(c.clipHotkeys.begin(), c.clipHotkeys.end(), n) != c.clipHotkeys.end();
			if (it->checkState() == Qt::Checked && !has)
				c.clipHotkeys.push_back(n);
			else if (it->checkState() != Qt::Checked && has)
				c.clipHotkeys.erase(std::remove(c.clipHotkeys.begin(), c.clipHotkeys.end(), n),
						    c.clipHotkeys.end());
		}
	}
	c.clipOnDowned = clipDowned_->isChecked();
	c.clipNameTemplate = nameTpl_->text().trimmed().isEmpty() ? "{title}_{tags}_{date}_{time}"
								  : nameTpl_->text().trimmed().toStdString();
	c.clipFolder = clipFolder_ ? clipFolder_->text().trimmed().toStdString() : c.clipFolder;
	c.bridgeEnabled = bridgeOn_->isChecked();
	c.bridgePort = bridgePort_->value();
	c.appPath = appPath_->text().trimmed().toStdString();
	c.launchApp = launchApp_->isChecked();
	c.closeAppWithObs = closeApp_ ? closeApp_->isChecked() : true;
	c.nearEnabled = nearOn_ ? nearOn_->isChecked() : c.nearEnabled;
	c.nearFollow = nearFollow_ ? nearFollow_->isChecked() : c.nearFollow;
	c.nearCooldownS = nearCooldown_ ? nearCooldown_->value() : c.nearCooldownS;
	c.nearMaxM = nearMax_ ? nearMax_->value() : c.nearMaxM;
	c.appFps = appFps_ ? appFps_->value() : c.appFps;
	if (replayPre_) {
		c.replayPreS = replayPre_->value();
		c.replayPostS = replayPost_->value();
		c.replayScale = replayScale_->value();
		c.replayVolume = replayVol_->value();
		c.replayCooldownS = replayCool_->value();
		c.replayChat = replayChat_->isChecked();
		if (chatClips_)
			c.chatClips = chatClips_->currentIndex();
		if (replayStinger_)
			c.replayStinger = replayStinger_->isChecked();
		if (replayStingerSound_)
			c.replayStingerSound = replayStingerSound_->isChecked();
		if (stingerVol_)
			c.stingerVolume = stingerVol_->value();
		if (replayPip_)
			c.replayPip = replayPip_->isChecked();
		if (twitchMarkers_)
			c.twitchMarkers = twitchMarkers_->isChecked();
		if (ytChapters_)
			c.ytChapters = ytChapters_->isChecked();
		if (sessionTrack_)
			c.sessionTrack = sessionTrack_->isChecked();
		if (sessionOverlayOn_)
			c.sessionOverlayOn = sessionOverlayOn_->isChecked();
		if (sessionOverlayPos_)
			c.sessionOverlayPos = sessionOverlayPos_->currentData().toString().toStdString();
		if (sessionOverlayMode_)
			c.sessionOverlayMode = sessionOverlayMode_->currentData().toString().toStdString();
		if (sessionShow_) {
			QStringList ids;
			for (int i = 0; i < sessionShow_->count(); ++i)
				if (sessionShow_->item(i)->checkState() == Qt::Checked)
					ids << sessionShow_->item(i)->data(Qt::UserRole).toString();
			c.sessionShow = ids.join(',').toStdString();
		}
		c.replayWord = replayWord_->text().trimmed().isEmpty() ? "!replay"
								       : replayWord_->text().trimmed().toStdString();
		c.replaySound = replaySound_->isChecked();
		c.replayHwDecode = replayHw_->isChecked();
		c.highlightsAuto = highlightsAuto_->isChecked();
		c.highlightsMax = highlightsMax_->value();
		c.clipTrim = clipTrim_->isChecked();
		c.clipTrimLeadS = clipTrimLead_->value();
		c.runMerge = runMerge_->isChecked();
		c.runCutGaps = runCutGaps_->isChecked();
		c.runGapS = runGap_->value();
		c.chatKick = chatKick_->text().trimmed().toStdString();
		c.chatYouTube = chatYouTube_->text().trimmed().toStdString();
		c.highlightsFolder = highlightsFolder_->text().trimmed().toStdString();
		c.replayLabel = replayLabel_->text().trimmed().isEmpty() ? "Instant replay"
									 : replayLabel_->text().trimmed().toStdString();
	}
	if (voiceOn_) {
		c.voiceEnabled = voiceOn_->isChecked();
		c.voiceMic = voiceMic_->currentData().toString().toStdString();
		c.voiceWake = voiceWake_->text().trimmed().toLower().isEmpty()
				      ? "kennel"
				      : voiceWake_->text().trimmed().toLower().toStdString();
		c.voiceNames = voiceNames_->isChecked();
		c.voiceCommands = voiceCommands_->isChecked();
		c.voiceChime = voiceChime_->isChecked();
		c.voiceTones = voiceTones_->isChecked();
		c.voiceChimeVol = voiceChimeVol_->value();
		c.voiceChimeWhere = voiceChimeWhere_->currentData().toString().toStdString();
		c.voiceCmdReplay = voiceCmdReplay_->isChecked();
		c.voiceCmdClip = voiceCmdClip_->isChecked();
		c.voiceCmdDual = voiceCmdDual_->isChecked();
		c.voiceCmdForce = voiceCmdForce_->isChecked();
		c.voiceCmdChange = voiceCmdChange_->isChecked();
		c.voiceCmdClosest = voiceCmdClosest_->isChecked();
	}
}

/// Counts how many different pictures a feed actually delivers in two seconds. A feed that looks
/// choppy either is not arriving at the rate you think (network or sender) or is arriving fine and
/// being drawn badly (this PC) - and there is no way to tell those apart by eye.
void SettingsDialog::testFeed()
{
	int r = friends_->currentRow();
	if (r < 0 || r >= (int)e_->cfg.friends.size()) {
		QMessageBox::information(this, "Kennel.gg Wardogs", "Pick a squad mate in the list first.");
		return;
	}
	const Friend &f = e_->cfg.friends[r];
	std::string src = e_->cfg.sourceFor(f);
	if (src.empty() || !e_->sw.feedHash(src)) {
		QMessageBox::information(this, "Kennel.gg Wardogs",
					 "No picture from '" + QString::fromStdString(src) +
						 "' - it is not in the scene, or nothing is coming in yet.");
		return;
	}
	auto *count = new int(0);
	auto *last = new uint64_t(0);
	auto *ticks = new int(0);
	auto *t = new QTimer(this);
	t->setInterval(8); // ~125 looks a second: enough to tell 30 from 60
	connect(t, &QTimer::timeout, this, [this, t, src, count, last, ticks, name = f.name]() {
		uint64_t h = e_->sw.feedHash(src);
		if (h && h != *last) {
			if (*last)
				(*count)++;
			*last = h;
		}
		if (++*ticks < 250)
			return;
		t->stop();
		t->deleteLater();
		double fps = *count / 2.0;
		QString msg = QString("%1's feed is delivering about %2 new pictures a second.")
				      .arg(QString::fromStdString(name))
				      .arg(fps, 0, 'f', 0);
		msg += fps >= 50   ? "\n\nThat is a full-rate feed. If it still looks choppy the feed is fine and "
				     "the drawing is not: check this PC's OBS Stats for rendering lag, and its GPU load."
		       : fps >= 25 ? "\n\nThat is a 30-ish feed. It will look like half frames next to your own "
				     "game. If the sender is on 60, their network or ours is not carrying it."
				   : "\n\nThat is well under 30: the feed itself is not arriving properly. Try a "
				     "lower quality on their side.";
		QMessageBox::information(this, "Kennel.gg Wardogs", msg);
		delete count;
		delete last;
		delete ticks;
	});
	t->start();
	e_->log("Measuring " + QString::fromStdString(f.name) + "'s feed for two seconds...");
}

void SettingsDialog::showAccount()
{
	if (!accountLbl_)
		return;
	if (e_->accountLinked()) {
		QStringList miss = e_->accountMissing();
		accountLbl_->setText(
			"Linked as " + QString::fromStdString(e_->cfg.accountName) +
			(miss.isEmpty() ? QString(e_->accountComplete() ? " (Discord, Steam and Twitch connected)" : "")
					: " - still to connect: " + miss.join(", ")));
		accountBtn_->setText("Unlink");
	} else if (!e_->linkCode().isEmpty()) {
		accountLbl_->setText("Sign in on the kennel.gg page that opened (code " + e_->linkCode() + ")");
		accountBtn_->setText("Open the page again");
	} else {
		accountLbl_->setText(
			"Not linked. The leaderboards take stats only from a PC linked to a kennel.gg account "
			"(the same account as wagers and the Cash Cup).");
		accountBtn_->setText("Link kennel.gg account");
	}
}

void SettingsDialog::saveAndApply()
{
	if (building_)
		return;
	collect();
	e_->cfg.save();
	e_->reloadConfig();
	// and to ClipHound at once: chat replays, chat clips and markers are its to act on, and it heard
	// of a change only at the next unrelated push before
	e_->pushAppConfig();
	emit e_->stateChanged(); // the session bar and the Stream Deck redraw from the new settings
}
