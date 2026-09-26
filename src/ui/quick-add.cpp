#include "ui/quick-add.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <QApplication>
#include <QClipboard>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>

namespace SquadInput {

std::string kickSlug(QString v)
{
	v = v.trimmed();
	int i = v.lastIndexOf('/');
	if (i >= 0)
		v = v.mid(i + 1);
	v = v.section('?', 0, 0).remove('@').toLower();
	return v.toStdString();
}

std::string youTubeId(QString v)
{
	v = v.trimmed();
	if (v.isEmpty())
		return "";
	QRegularExpression rxCh("(UC[A-Za-z0-9_-]{20,})"),
		rxVid("(?:v=|/live/|youtu\\.be/|/embed/)([A-Za-z0-9_-]{11})"), rxHandle("@([A-Za-z0-9._-]{3,})");
	QRegularExpressionMatch m;
	if ((m = rxCh.match(v)).hasMatch())
		return m.captured(1).toStdString();
	if ((m = rxVid.match(v)).hasMatch())
		return m.captured(1).toStdString();
	if ((m = rxHandle.match(v)).hasMatch())
		return ("@" + m.captured(1)).toStdString();
	if (v.contains("youtube.com") || v.contains("youtu.be"))
		return ""; // a link we do not understand
	if (QRegularExpression("^[A-Za-z0-9_-]{11}$").match(v).hasMatch())
		return v.toStdString(); // a bare video ID
	return ("@" + v).toStdString(); // a bare name: treat it as a handle
}

QString resolveYouTubeHandle(const QString &handle)
{
	QNetworkAccessManager nam;
	QNetworkRequest req(QUrl("https://www.youtube.com/" + handle));
	req.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Kennel.gg Wardogs Streaming Tool)");
	req.setRawHeader("Accept-Language", "en");
	QNetworkReply *rep = nam.get(req);
	QEventLoop loop;
	QTimer::singleShot(8000, &loop, &QEventLoop::quit);
	QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	loop.exec();
	QString id;
	if (rep->isFinished() && rep->error() == QNetworkReply::NoError) {
		QString page = QString::fromUtf8(rep->readAll());
		QRegularExpressionMatch m =
			QRegularExpression("\"(?:channelId|externalId)\":\"(UC[A-Za-z0-9_-]{20,})\"").match(page);
		if (m.hasMatch())
			id = m.captured(1);
	}
	rep->abort();
	rep->deleteLater();
	return id;
}

bool parse(const QString &text, FriendKind fallback, Friend &out, QString &err)
{
	QString t = text.trimmed();
	if (t.isEmpty())
		return false;
	QString low = t.toLower();
	// the first path segment after a host: "https://www.twitch.tv/pup?x" -> "pup"
	auto after = [&](const QString &host) {
		QString rest = t.mid(low.indexOf(host) + host.size()).section('?', 0, 0).section('#', 0, 0);
		QStringList parts = rest.split('/', Qt::SkipEmptyParts);
		if (!parts.isEmpty() && parts.first().compare("popout", Qt::CaseInsensitive) == 0)
			parts.removeFirst();
		return parts.isEmpty() ? QString() : parts.first();
	};
	FriendKind k = fallback;
	QString chan;
	if (low.contains("twitch.tv/")) {
		k = FriendKind::Twitch;
		chan = after("twitch.tv/").toLower();
	} else if (low.contains("kick.com/")) {
		k = FriendKind::Kick;
		chan = QString::fromStdString(kickSlug(after("kick.com/")));
	} else if (low.contains("youtube.com") || low.contains("youtu.be")) {
		k = FriendKind::YouTube;
		chan = QString::fromStdString(youTubeId(t));
	} else if (low.contains("vdo.ninja")) {
		k = FriendKind::VdoNinja;
		QUrlQuery q(QUrl(t.startsWith("http", Qt::CaseInsensitive) ? t : "https://" + t));
		chan = q.queryItemValue("push");
		if (chan.isEmpty())
			chan = q.queryItemValue("view");
	} else if (k == FriendKind::Twitch) {
		chan = t.toLower().remove('@');
	} else if (k == FriendKind::Kick) {
		chan = QString::fromStdString(kickSlug(t));
	} else if (k == FriendKind::YouTube) {
		chan = QString::fromStdString(youTubeId(t));
	} else {
		chan = t;
	}
	switch (k) {
	case FriendKind::Twitch:
		if (!QRegularExpression("^[a-z0-9_]{3,25}$").match(chan).hasMatch()) {
			err = "\"" + t + "\" is not a Twitch channel name";
			return false;
		}
		break;
	case FriendKind::Kick:
		if (!QRegularExpression("^[a-z0-9_-]{2,}$").match(chan).hasMatch()) {
			err = "\"" + t + "\" is not a Kick channel name";
			return false;
		}
		break;
	case FriendKind::YouTube:
		if (chan.isEmpty()) {
			err = "\"" + t + "\" is not a YouTube channel link, @handle or live video link";
			return false;
		}
		break;
	default:
		if (!QRegularExpression("^[A-Za-z0-9_-]{1,64}$").match(chan).hasMatch()) {
			err = "\"" + t + "\" is not a VDO.Ninja stream ID (letters, numbers, - and _)";
			return false;
		}
		k = FriendKind::VdoNinja;
		break;
	}
	out.kind = k;
	out.channel = chan.toStdString();
	out.name = (k == FriendKind::YouTube && chan.startsWith('@') ? chan.mid(1) : chan).toStdString();
	out.playing = true;
	return true;
}

} // namespace SquadInput

static QString platformName(FriendKind k)
{
	switch (k) {
	case FriendKind::Twitch:
		return "Twitch";
	case FriendKind::Kick:
		return "Kick";
	case FriendKind::YouTube:
		return "YouTube";
	case FriendKind::VdoNinja:
		return "VDO.Ninja";
	default:
		return "";
	}
}

QuickAdd::QuickAdd(Engine *engine, QWidget *parent) : QWidget(parent), e_(engine)
{
	auto *v = new QVBoxLayout(this);
	v->setContentsMargins(0, 0, 0, 0);
	auto *row = new QHBoxLayout();
	platform_ = new QComboBox(this);
	for (FriendKind k : {FriendKind::Twitch, FriendKind::Kick, FriendKind::YouTube, FriendKind::VdoNinja})
		platform_->addItem(platformName(k), (int)k);
	platform_->setToolTip("For names typed without a link. A pasted link picks its own platform.");
	edit_ = new QLineEdit(this);
	edit_->setPlaceholderText("Paste their link, or type their channel name - several at once is fine");
	edit_->setClearButtonEnabled(true);
	add_ = new QPushButton("Add", this);
	add_->setDefault(false);
	add_->setAutoDefault(false);
	row->addWidget(platform_);
	row->addWidget(edit_, 1);
	row->addWidget(add_);
	v->addLayout(row);
	result_ = new QLabel(this);
	result_->setWordWrap(true);
	result_->setTextFormat(Qt::RichText);
	result_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	result_->hide();
	v->addWidget(result_);

	// a pasted link shows which platform it is before Add is pressed
	connect(edit_, &QLineEdit::textChanged, this, [this](const QString &t) {
		QString low = t.toLower();
		FriendKind k = low.contains("twitch.tv")                                 ? FriendKind::Twitch
			       : low.contains("kick.com")                                ? FriendKind::Kick
			       : low.contains("youtube.com") || low.contains("youtu.be") ? FriendKind::YouTube
			       : low.contains("vdo.ninja")                               ? FriendKind::VdoNinja
											 : FriendKind::ObsSource;
		if (k != FriendKind::ObsSource)
			platform_->setCurrentIndex(platform_->findData((int)k));
	});
	connect(add_, &QPushButton::clicked, this, &QuickAdd::addNow);
	connect(edit_, &QLineEdit::returnPressed, this, &QuickAdd::addNow);
	connect(e_, &Engine::stateChanged, this, [this]() {
		if (!lastAdded_.isEmpty())
			showResult();
	});
}

void QuickAdd::addNow()
{
	const QStringList entries = edit_->text().split(QRegularExpression("[\\s,;]+"), Qt::SkipEmptyParts);
	if (entries.isEmpty())
		return;
	FriendKind fallback = (FriendKind)platform_->currentData().toInt();
	QStringList added, notes, unread;
	for (const QString &entry : entries) {
		Friend f;
		f.vdoKbps = e_->cfg.vdoBitrateKbps;
		QString err;
		if (!SquadInput::parse(entry, fallback, f, err)) {
			if (!err.isEmpty())
				notes << err.toHtmlEscaped() + ".";
			unread << entry;
			continue;
		}
		if (f.kind == FriendKind::YouTube && !f.channel.empty() && f.channel[0] == '@') {
			// the player needs the channel's ID, which only its page carries
			result_->setText("Looking up " + QString::fromStdString(f.channel).toHtmlEscaped() +
					 " on YouTube...");
			result_->show();
			QApplication::processEvents();
			QString id = SquadInput::resolveYouTubeHandle(QString::fromStdString(f.channel));
			if (id.isEmpty()) {
				notes << "Could not find the YouTube channel " +
						 QString::fromStdString(f.channel).toHtmlEscaped() +
						 " (paste a link to their channel or live video instead).";
				unread << entry;
				continue;
			}
			f.channel = id.toStdString();
		}
		bool dup = false, sameName = false;
		for (const auto &o : e_->cfg.friends) {
			if (o.kind == f.kind && QString::fromStdString(o.channel).compare(
							QString::fromStdString(f.channel), Qt::CaseInsensitive) == 0)
				dup = true;
			if (QString::fromStdString(o.name).compare(QString::fromStdString(f.name),
								   Qt::CaseInsensitive) == 0)
				sameName = true;
		}
		if (dup) {
			notes << QString::fromStdString(f.name).toHtmlEscaped() + " is already in your squad.";
			continue;
		}
		if (sameName) // somebody of that name on Discord already: keep both apart on the dock
			f.name += " (" + platformName(f.kind).toStdString() + ")";
		e_->cfg.friends.push_back(f);
		added << QString::fromStdString(f.name);
		if (f.kind == FriendKind::VdoNinja) {
			QString link = QString::fromStdString(Switcher::vdoPushUrl(f));
			QApplication::clipboard()->setText(link);
			notes << "Send " + QString::fromStdString(f.name).toHtmlEscaped() +
					 " this link to open while they play (copied): <br><code>" +
					 link.toHtmlEscaped() + "</code>";
		}
	}
	if (!added.isEmpty()) {
		if (e_->cfg.friends.size() == (size_t)added.size())
			e_->setActive(0); // the first squad mate is the one shown
		e_->cfg.save();
		e_->log("Squad mate" + QString(added.size() == 1 ? "" : "s") + " added: " + added.join(", ") + ".");
		e_->webLiveTick(); // live or offline now, not in a minute
		emit e_->stateChanged();
		emit this->added(); // the signal, not the list above
	}
	lastAdded_ = added;
	lastNote_ = notes.join("<br>");
	edit_->setText(unread.join(" ")); // what could not be read stays, to be fixed
	showResult();
}

void QuickAdd::showResult()
{
	QStringList parts;
	for (const QString &n : lastAdded_)
		for (const auto &f : e_->cfg.friends)
			if (QString::fromStdString(f.name) == n) {
				QString st = e_->feedStateText(f);
				QString kind = platformName(f.kind);
				parts << "<b>" + n.toHtmlEscaped() + "</b> (" + kind +
						 (st == "live" ? ", <span style=\"color:#8f9c5a\">live now</span>"
						  : st == "not streaming" ? ", offline"
									  : "") +
						 ")";
			}
	QString text;
	if (!parts.isEmpty())
		text = "Added " + parts.join(", ") + ". They are ticked as playing.";
	if (!lastNote_.isEmpty())
		text += (text.isEmpty() ? "" : "<br>") + lastNote_;
	result_->setText(text);
	result_->setVisible(!text.isEmpty());
}
