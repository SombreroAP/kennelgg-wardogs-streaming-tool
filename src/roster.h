#pragma once
#include <QObject>
#include <QString>
#include <QList>
#include <QSet>
#include <QTimer>

/// Who is sitting in the squad's Discord voice channel, and who is sharing their screen.
///
/// Discord gives a third-party program no way to ask this locally: the client's RPC socket
/// puts voice reads behind a scope Discord hands out by hand, application by application.
/// A bot on the gateway can see it, so the Kennel.gg bot writes the roster out and this
/// polls that file. Nothing about the local machine is sent - it is a plain GET.
class Roster : public QObject {
	Q_OBJECT
public:
	struct Member {
		QString name;           // their Discord display name
		QString handle;         // their Discord username: what a popped-out share is titled with
		bool streaming = false; // they have gone live in the call
		bool camera = false;
		QString channel; // the voice channel they are in
		QString guild;   // the Discord server that channel is in
	};

	explicit Roster(QObject *parent = nullptr);

	/// Start polling `url` every `seconds`. An empty url stops it.
	void configure(const QString &url, int seconds, const QString &onlyChannel,
		       const QString &onlyGuild = QString());
	/// Every server the bot can see, from the last poll, and the link that adds it to another one.
	QStringList guilds() const { return guilds_; }
	QString inviteUrl() const { return invite_; }
	/// The server the bot is home to (Kennel.gg), and whether it listed that server's members.
	QString homeGuild() const { return home_; }
	QString joinUrl() const { return join_; } // the plugin's own invite into that server
	bool membersKnown() const { return membersKnown_; }
	/// The last poll succeeded. False until the first answer, and while the address cannot be read.
	bool healthy() const { return healthy_; }
	/// Is this Discord username a member of the home server, by the bot's last poll. The bot
	/// publishes hashes, not names, so this hashes the same way.
	bool isMember(const QString &handle) const;
	void poll(); // now, out of turn
	void stop();

	QList<Member> members() const { return members_; }
	/// Just the ones sharing their screen, in the order the bot listed them.
	QList<Member> streamers() const;
	QString status() const; // for the settings dialog ("off" when not polling)
	bool running() const { return timer_.isActive(); }

signals:
	/// The roster changed: somebody joined, left, went live or stopped.
	void changed();
	/// Polled and nothing moved, or the poll failed - `status()` says which.
	void polled();

private:
	QTimer timer_;
	QString url_, onlyChannel_, onlyGuild_, status_; // empty = off (worded by status(), in the chosen language)
	QStringList guilds_;
	QString invite_;
	QString home_, join_;
	QSet<QString> memberHashes_;
	bool membersKnown_ = false;
	QList<Member> members_;
	bool inFlight_ = false;
	bool healthy_ = false; // the last poll came back and parsed
};
