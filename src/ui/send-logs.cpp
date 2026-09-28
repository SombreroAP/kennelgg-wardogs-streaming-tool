#include "ui/send-logs.h"
#include "engine.h"
#include "http.h"
#include "i18n.h"
#include <obs-frontend-api.h>
#include <plugin-support.h>
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVBoxLayout>

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

/// The newest OBS log (this session's), its last `bytes`.
static QString obsLogTail(qint64 bytes)
{
	QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).section('/', 0, -2) +
		 "/obs-studio/logs");
	QFileInfoList l = dir.entryInfoList({"*.txt"}, QDir::Files, QDir::Time);
	if (l.isEmpty())
		return QString();
	QFile f(l.first().absoluteFilePath());
	if (!f.open(QIODevice::ReadOnly))
		return QString();
	if (f.size() > bytes)
		f.seek(f.size() - bytes);
	return l.first().fileName() + "\n" + QString::fromUtf8(f.readAll());
}

/// The plugin's settings as sent with the logs: every key, minus anything secret (the account token, the
/// roster address with its key, passwords).
static QString settingsForLogs()
{
	QFile f(QString::fromStdString(Config::configDir()) + "/config.json");
	if (!f.open(QIODevice::ReadOnly))
		return QString();
	QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
	static const QRegularExpression secret("token|secret|password|passwd|pwd|key$|rosterurl|accountid",
					       QRegularExpression::CaseInsensitiveOption);
	for (const QString &k : o.keys())
		if (k.contains(secret))
			o.insert(k, "(removed)");
	return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Indented));
}

void SendLogs::open(Engine *e, QWidget *)
{
	auto *d = new QDialog((QWidget *)obs_frontend_get_main_window());
	d->setAttribute(Qt::WA_DeleteOnClose);
	d->setWindowTitle(tx("Send logs to Kennel.gg"));
	d->resize(520, 420);
	auto *v = new QVBoxLayout(d);
	auto *intro =
		new QLabel(tx("This sends what we need to see what went wrong: the plugin's log and settings "
			      "(without your account link or any key), this OBS session's log and ClipHound's log. "
			      "Never your clips, video, voice or chat. Logs are kept for 60 days."),
			   d);
	intro->setWordWrap(true);
	v->addWidget(intro);
	v->addWidget(new QLabel(tx("What happened? (optional, but it helps a lot)"), d));
	auto *note = new QPlainTextEdit(d);
	note->setPlaceholderText(tx("e.g. the POV got stuck on my squad mate at about 21:18"));
	v->addWidget(note, 1);
	auto *form = new QFormLayout();
	auto *contact = new QLineEdit(QString::fromStdString(e->cfg.myDiscord), d);
	contact->setPlaceholderText(tx("so we can get back to you"));
	form->addRow(tx("Your Discord name"), contact);
	v->addLayout(form);
	auto *result = new QLabel(d);
	result->setWordWrap(true);
	result->setTextInteractionFlags(Qt::TextSelectableByMouse);
	v->addWidget(result);
	auto *row = new QHBoxLayout();
	auto *copyRef = new QPushButton(tx("Copy the reference"), d);
	copyRef->hide();
	auto *cancel = new QPushButton(tx("Close"), d);
	auto *send = new QPushButton(tx("Send"), d);
	send->setDefault(true);
	row->addWidget(copyRef);
	row->addStretch(1);
	row->addWidget(cancel);
	row->addWidget(send);
	v->addLayout(row);
	QObject::connect(cancel, &QPushButton::clicked, d, &QDialog::close);
	QPointer<QDialog> alive(d);
	QObject::connect(send, &QPushButton::clicked, d, [e, d, alive, note, contact, result, send, copyRef]() {
		send->setEnabled(false);
		result->setText(tx("Sending..."));
		QString appDir = QFileInfo(e->cfg.appPath.empty() ? Engine::defaultAppPath()
								  : QString::fromStdString(e->cfg.appPath))
					 .absolutePath();
		QString plugin = QString("=== Kennel.gg Wardogs plugin %1 ===\n").arg(PLUGIN_VERSION);
		plugin +=
			QString("state: %1 | game source: %2 | squad mate: %3 | replay buffer: %4 | ClipHound: %5\n\n")
				.arg(QString::fromStdString(e->stateText()), QString::fromStdString(e->cfg.gameSource),
				     e->cfg.active() ? QString::fromStdString(e->cfg.active()->name) : "(none)",
				     obs_frontend_replay_buffer_active() ? "running" : "NOT running",
				     e->appConnected() ? "connected" : "not connected");
		plugin += e->recentLog().join('\n');
		QJsonObject o;
		o["version"] = QString(PLUGIN_VERSION);
		o["note"] = note->toPlainText().left(2000);
		o["contact"] = contact->text().trimmed().left(80);
		o["plugin_log"] = plugin;
		o["settings"] = settingsForLogs();
		o["cliphound_log"] = tailOf(appDir + "/cliphound.log", 4000);
		o["obs_log"] = obsLogTail(2 * 1024 * 1024);
		QByteArray body = QJsonDocument(o).toJson(QJsonDocument::Compact);
		if (body.size() > 4 * 1024 * 1024 - 4096) { // the server takes 4 MB: the OBS log gives way
			o["obs_log"] = obsLogTail(1024 * 1024);
			body = QJsonDocument(o).toJson(QJsonDocument::Compact);
		}
		QString hdr = "Content-Type: application/json\r\n";
		if (!e->cfg.accountToken.empty())
			hdr += "Authorization: Bearer " + QString::fromStdString(e->cfg.accountToken) + "\r\n";
		Http::requestAsync(
			e, "POST", "https://kennel.gg/api/stats/logs", body, hdr, 30000,
			QString("KennelggWardogsOBSTool/%1").arg(PLUGIN_VERSION),
			[e, alive, result, send, copyRef](Http::Result r) {
				QString code = QJsonDocument::fromJson(r.body).object().value("code").toString();
				if (r.ok && !code.isEmpty())
					e->log(tx("Logs sent to Kennel.gg: %1.").arg(code));
				if (!alive)
					return;
				if (r.ok && !code.isEmpty()) {
					result->setText(
						tx("Sent. Your reference is <b>%1</b> - mention it in "
						   "#obs-streaming-tool-chat on the Kennel.gg Discord so we can find "
						   "your logs.")
							.arg(code));
					copyRef->show();
					QObject::connect(copyRef, &QPushButton::clicked, copyRef, [code, copyRef]() {
						QApplication::clipboard()->setText(code);
						copyRef->setText(tx("Copied"));
					});
				} else {
					QString msg =
						QJsonDocument::fromJson(r.body).object().value("message").toString();
					result->setText(tx("Could not send (%1). Try again in a minute, or use Logs... "
							   "and Copy all.")
								.arg(msg.isEmpty() ? r.error : msg));
					send->setEnabled(true);
				}
			});
	});
	d->show();
	d->raise();
	d->activateWindow();
}
