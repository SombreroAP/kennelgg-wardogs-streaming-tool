#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <deque>
#include <QHash>
#include <vector>
#include <QSet>
#include <QTimer>
#include <QJsonObject>
#include <QList>

/// Replay-buffer clipping: save the buffer on demand, rename the file with tags, keep a log.
class Clips : public QObject {
	Q_OBJECT
public:
	struct Entry {
		QDateTime when;
		QString title;
		QStringList tags;
		QString path;
		// where the action is inside the file, in seconds before its end: the last kill (the one the
		// name carries as "@-Ns"), the first kill of a multi-kill, and how many. -1 = not known
		double momentS = -1, firstS = -1;
		int kills = 0;
		QJsonObject info; // what ClipHound knew: kind, description, killer, victim, icons, events
		QString pathV;    // the same moment from the vertical canvas's Backtrack, when there is one
	};
	explicit Clips(QObject *parent = nullptr);

	QString nameTemplate = "{title}_{tags}_{date}_{time}";
	/// Clips made within this many seconds of the last one are a run of rolling highlights: their
	/// files are named "[1 of 3]", "[2 of 3]", "[3 of 3]", earlier ones renamed as the run grows.
	int seriesWindowS = 45;
	QString folder;              // move clips here when set // {date} {time} {title} {tags} {source}
	bool autoStartReplay = true; // start the replay buffer when OBS loads / when a clip is asked for
	bool useReplay = true;       // save OBS's replay buffer
	QStringList hotkeys;         // OBS hotkeys to fire as well (Aitum Backtrack saves, anything else)
	QStringList
		watchFolders; // folders other tools (Aitum Backtrack) write clips into; new files after a trigger are renamed
	static QStringList discoverBacktrackFolders();
	static QList<QPair<QString, QString>> allHotkeys(); // (name, description)
	static bool fireHotkey(const QString &name);
	int minGapMs = 4000; // ignore clip requests closer than this

	/// Ask OBS to save the replay buffer; the rename happens when OBS reports the file.
	/// `moments`: when each kill happened, epoch seconds (ClipHound's clock, same PC); the file's
	/// end is the moment this is called, so each becomes an offset from the end. `info`: whatever
	/// else ClipHound knew, written into the clip's JSON sidecar.
	QString request(const QString &title, const QStringList &tags, const QString &source,
			const QList<double> &moments = {}, const QJsonObject &info = {});
	void onReplaySaved(); // wire to OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED
	void ensureReplayBuffer();
	enum class ReplayChange { None, Written, NeedsRestart };
	/// Write the replay buffer length into OBS's profile (both output modes).
	ReplayChange setReplaySeconds(int seconds);
	/// OBS's own replay-buffer length for the output mode in use, or 0 when unknown.
	static int readReplaySeconds();
	const std::deque<Entry> &history() const { return history_; }
	/// Give a saved clip a new title and/or tags: the file and sidecar are renamed to the title,
	/// the sidecar gains the words that were said (`spoken`) and the tags. An empty title keeps the
	/// name; `tags` null keeps the tags. Returns the path afterwards, or "" when the clip is not
	/// known or the file cannot move.
	QString relabel(const QString &path, const QString &title, const QString &spoken, const QStringList *tags);
	QString retitle(const QString &path, const QString &title, const QString &spoken)
	{
		return relabel(path, title, spoken, nullptr);
	}
	/// Only this session's clips are in history(); the sidecars of every clip in the folder are
	/// read for the labelling window, newest first, up to `max`.
	std::vector<Entry> allClips(int max = 300) const;
	QString lastPath() const { return history_.empty() ? QString() : history_.back().path; }

signals:
	void saved(const Entry &e);
	void logged(const QString &msg);

private:
	struct Pending {
		QDateTime when;
		QString title;
		QStringList tags;
		QString source;
		double momentS = -1, firstS = -1;
		int kills = 0;
		QJsonObject info;
	};
	std::deque<Pending> pending_;
	std::deque<Entry> history_;
	QHash<QString, QString> renamed_; // old path -> new, so a second label finds a clip the first one moved
	QDateTime lastRequest_;
	struct Watch {
		QDateTime since;
		QString title;
		QStringList tags;
		QSet<QString> seen;
		double momentS = -1, firstS = -1;
		int kills = 0;
		QJsonObject info;
		QString vertPath; // a vertical Backtrack file found before its horizontal partner
	};
	std::deque<Watch> watches_;
	/// Give a vertical Backtrack file to the entry it belongs with (the newest within 30 s of `when`).
	bool attachVertical(const QDateTime &when, const QString &path);
	QTimer watchTimer_;
	void pollWatches();
	/// "name" -> "name @-7.4s" when the moment is known: the marker Kennel Cut reads off the name.
	static QString withMoment(const QString &name, double momentS);
	/// Rename a clip and its .json sidecar together.
	static bool renameClip(const QString &from, const QString &to);
	/// The row in clips.csv and the JSON sidecar next to the file, for every clip however it was saved.
	void logEntry(const Entry &e);
	void writeSidecar(const Entry &e);
	QString nameFor(const QDateTime &when, const QString &title, const QStringList &tags,
			const QString &source) const;
	QString logFile() const;
	static QString safe(QString s);
	/// A clip file just got its name: put it in the current run, or start a new one, and renumber.
	void joinSeries(const QString &path, const QDateTime &when);

public:
	/// Go through every clip already in the clip folders, find the runs by the time in each file's
	/// name (or its modified time), and number them "[1 of 3]" ... the same way new ones are.
	/// Returns what was done, for the settings tab.
	QString numberPastClips();

private:
	struct Series {
		QDateTime last;
		QStringList paths;
	} series_;
};
