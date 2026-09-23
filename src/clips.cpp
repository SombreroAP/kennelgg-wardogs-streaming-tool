#include "clips.h"
#include <cmath>
#include <QJsonDocument>
#include <QJsonArray>
#include <QRegularExpression>
#include "config.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QSet>
#include <obs-module.h>
#include <algorithm>
#include <cstring>
#include <obs-frontend-api.h>
#include <util/config-file.h>
#include <plugin-support.h>

Clips::Clips(QObject *parent) : QObject(parent)
{
	connect(&watchTimer_, &QTimer::timeout, this, &Clips::pollWatches);
}

static void collectPaths(obs_data_t *st, QStringList *out)
{
	for (obs_data_item_t *it = obs_data_first(st); it; obs_data_item_next(&it)) {
		if (obs_data_item_gettype(it) != OBS_DATA_STRING)
			continue;
		QString v = QString::fromUtf8(obs_data_item_get_string(it));
		if (v.size() < 3 ||
		    !(v.contains(":/") || v.contains(":\\") || v.startsWith("/") || v.startsWith("\\\\")))
			continue;
		QFileInfo fi(v);
		QString dir = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
		if (QDir(dir).exists() && !out->contains(dir))
			out->append(dir);
	}
}

QStringList Clips::discoverBacktrackFolders()
{
	QStringList out;
	obs_enum_sources(
		[](void *data, obs_source_t *src) {
			auto *o = (QStringList *)data;
			QString id = QString::fromUtf8(obs_source_get_id(src)),
				name = QString::fromUtf8(obs_source_get_name(src));
			if (!id.contains("backtrack", Qt::CaseInsensitive) &&
			    !id.contains("aitum", Qt::CaseInsensitive) &&
			    !name.contains("backtrack", Qt::CaseInsensitive))
				return true;
			obs_data_t *st = obs_source_get_settings(src);
			collectPaths(st, o);
			obs_data_release(st);
			// Backtrack keeps its recorder as a filter on the source
			obs_source_enum_filters(
				src,
				[](obs_source_t *, obs_source_t *f, void *d) {
					obs_data_t *fs = obs_source_get_settings(f);
					collectPaths(fs, (QStringList *)d);
					obs_data_release(fs);
				},
				o);
			return true;
		},
		&out);
	// filters named backtrack on any source
	obs_enum_sources(
		[](void *data, obs_source_t *src) {
			obs_source_enum_filters(
				src,
				[](obs_source_t *, obs_source_t *f, void *d) {
					QString id = QString::fromUtf8(obs_source_get_id(f));
					if (!id.contains("backtrack", Qt::CaseInsensitive) &&
					    !id.contains("aitum", Qt::CaseInsensitive))
						return;
					obs_data_t *fs = obs_source_get_settings(f);
					collectPaths(fs, (QStringList *)d);
					obs_data_release(fs);
				},
				data);
			return true;
		},
		&out);
	return out;
}

QString Clips::nameFor(const QDateTime &when, const QString &title, const QStringList &tags,
		       const QString &source) const
{
	QString name = nameTemplate;
	name.replace("{date}", when.toString("yyyy-MM-dd"));
	name.replace("{time}", when.toString("HH-mm-ss"));
	name.replace("{title}", safe(title));
	name.replace("{tags}", safe(tags.join("_")));
	name.replace("{source}", safe(source));
	name = safe(name);
	while (name.contains("__"))
		name.replace("__", "_");
	return name;
}

/// Clip files in a folder and its first level of subfolders (Backtrack can write into dated folders).
static QFileInfoList listClips(const QString &folder)
{
	static const QStringList exts{"*.mkv", "*.mp4", "*.mov", "*.flv", "*.ts"};
	QDir d(folder);
	QFileInfoList out = d.entryInfoList(exts, QDir::Files, QDir::Time);
	for (const QFileInfo &sub : d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
		out += QDir(sub.absoluteFilePath()).entryInfoList(exts, QDir::Files, QDir::Time);
	return out;
}

void Clips::pollWatches()
{
	QStringList folders = watchFolders + discoverBacktrackFolders();
	folders.removeDuplicates();
	QDateTime now = QDateTime::currentDateTime();
	for (auto it = watches_.begin(); it != watches_.end();) {
		Watch &w = *it;
		// every new file for this moment, not only the first: a vertical canvas's Backtrack writes
		// its own file next to the horizontal one, and both belong to the clip
		QList<QFileInfo> horiz, vert;
		for (const QString &folder : folders) {
			if (!QDir(folder).exists())
				continue;
			for (const QFileInfo &fi : listClips(folder)) {
				if (fi.lastModified() < w.since.addSecs(-2) || w.seen.contains(fi.absoluteFilePath()))
					continue;
				if (fi.lastModified().msecsTo(now) < 2000)
					continue; // still being written
				w.seen.insert(fi.absoluteFilePath());
				bool v = fi.absoluteFilePath().contains("vertical", Qt::CaseInsensitive) ||
					 fi.fileName().contains("portrait", Qt::CaseInsensitive);
				(v ? vert : horiz).append(fi);
			}
		}
		bool done = false;
		for (const QFileInfo &fi : horiz) {
			QDir d = fi.dir();
			QString name = withMoment(nameFor(w.since, w.title, w.tags, "backtrack"), w.momentS);
			QString target = d.filePath(name + "." + fi.suffix());
			int n = 2;
			while (QFile::exists(target))
				target = d.filePath(name + QString("_%1.").arg(n++) + fi.suffix());
			if (!QFile::rename(fi.absoluteFilePath(), target))
				continue;
			Entry e{w.since, w.title, w.tags, target, w.momentS, w.firstS, w.kills, w.info};
			e.pathV = w.vertPath; // a vertical file that came first
			w.vertPath.clear();
			history_.push_back(e);
			emit logged("Backtrack clip named: " + QFileInfo(target).fileName());
			joinSeries(target, w.since);
			e.path = history_.back().path; // joinSeries may have renamed it into the run
			logEntry(e);
			emit saved(e);
			done = true;
		}
		for (const QFileInfo &fi : vert) {
			QDir d = fi.dir();
			QString name =
				withMoment(nameFor(w.since, w.title, w.tags, "backtrack"), w.momentS) + " [vertical]";
			QString target = d.filePath(name + "." + fi.suffix());
			int n = 2;
			while (QFile::exists(target))
				target = d.filePath(name + QString("_%1.").arg(n++) + fi.suffix());
			if (!QFile::rename(fi.absoluteFilePath(), target))
				continue;
			emit logged("Vertical Backtrack clip named: " + QFileInfo(target).fileName());
			if (!attachVertical(w.since, target))
				w.vertPath = target; // its partner has not landed yet: kept for it
		}
		if (w.since.secsTo(now) > 90) {
			if (!w.vertPath.isEmpty()) {
				// only a vertical file ever came: better a portrait clip than none
				Entry e{w.since, w.title, w.tags, w.vertPath, w.momentS, w.firstS, w.kills, w.info};
				e.pathV = w.vertPath;
				history_.push_back(e);
				logEntry(e);
				emit saved(e);
			}
			done = true;
		}
		if (done && w.vertPath.isEmpty())
			it = watches_.erase(it);
		else
			++it;
	}
	if (watches_.empty())
		watchTimer_.stop();
}

bool Clips::attachVertical(const QDateTime &when, const QString &path)
{
	for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
		if (qAbs(it->when.secsTo(when)) > 30)
			continue;
		if (!it->pathV.isEmpty())
			continue; // already has one: an older clip in the window
		it->pathV = path;
		writeSidecar(*it);
		return true;
	}
	return false;
}

QString Clips::safe(QString s)
{
	static const QString bad = "\\/:*?\"<>|";
	for (QChar &c : s)
		if (bad.contains(c) || c.unicode() < 32)
			c = '-';
	return s.trimmed().left(80);
}

QString Clips::withMoment(const QString &name, double momentS)
{
	if (momentS < 0)
		return name;
	return name + QString(" @-%1s").arg(momentS, 0, 'f', 1);
}

bool Clips::renameClip(const QString &from, const QString &to)
{
	if (!QFile::rename(from, to))
		return false;
	QFileInfo fi(from), ti(to);
	QString sfrom = fi.dir().filePath(fi.completeBaseName() + ".json");
	QString sto = ti.dir().filePath(ti.completeBaseName() + ".json");
	if (QFile::exists(sfrom) && sfrom != sto)
		QFile::rename(sfrom, sto);
	return true;
}

QString Clips::relabel(const QString &given, const QString &title, const QString &spoken, const QStringList *tags)
{
	if (given.isEmpty())
		return QString();
	QString path = given;
	for (int i = 0; i < 8 && renamed_.contains(path); i++)
		path = renamed_.value(path); // a voice name and a typed note can both land on one clip
	Entry *e = nullptr;
	for (auto &h : history_)
		if (h.path == path)
			e = &h;
	Entry loaded;
	if (!e) {
		// a clip from an earlier session: its sidecar is what we know about it
		QFileInfo fi(path);
		QFile f(fi.dir().filePath(fi.completeBaseName() + ".json"));
		if (!fi.exists())
			return QString();
		QJsonObject o;
		if (f.open(QIODevice::ReadOnly))
			o = QJsonDocument::fromJson(f.readAll()).object();
		loaded.path = path;
		loaded.title = o.value("title").toString();
		for (const auto &t : o.value("tags").toArray())
			loaded.tags << t.toString();
		loaded.when = QDateTime::fromString(o.value("created").toString(), Qt::ISODate);
		if (!loaded.when.isValid())
			loaded.when = fi.lastModified();
		loaded.momentS = o.value("moment_s_from_end").toDouble(-1);
		loaded.firstS = o.value("first_s_from_end").toDouble(-1);
		loaded.kills = o.value("kills").toInt();
		loaded.info = o;
		e = &loaded;
	}
	QString t = safe(title).trimmed();
	QString to = path;
	if (!t.isEmpty() && t != safe(e->title)) {
		QFileInfo fi(path);
		// keep the run mark ("[2 of 3]") and the moment mark ("@-12s") the name may carry
		QString base = fi.completeBaseName();
		QString oldTitle = safe(e->title);
		QString newBase = base;
		if (!oldTitle.isEmpty() && base.contains(oldTitle))
			newBase.replace(base.indexOf(oldTitle), oldTitle.size(), t);
		else
			newBase = t + " - " + base;
		to = fi.dir().filePath(newBase + "." + fi.suffix());
		if (to != path && QFile::exists(to))
			to = fi.dir().filePath(newBase + " " + e->when.toString("HH-mm-ss") + "." + fi.suffix());
		if (to != path && !renameClip(path, to))
			return QString();
		if (to != path)
			renamed_[path] = to;
		e->path = to;
		e->title = title.trimmed();
		if (!e->pathV.isEmpty() && QFile::exists(e->pathV)) {
			QFileInfo vi(e->pathV);
			QString vto = vi.dir().filePath(newBase + " [vertical]." + vi.suffix());
			if (vto != e->pathV && QFile::rename(e->pathV, vto))
				e->pathV = vto;
		}
	}
	if (!spoken.isEmpty()) {
		e->info["spoken"] = spoken;
		if (!e->tags.contains("voice"))
			e->tags << "voice";
	}
	if (tags) {
		QStringList clean;
		for (QString g : *tags) {
			g = safe(g.trimmed().toLower()).replace(' ', '-');
			if (!g.isEmpty() && !clean.contains(g))
				clean << g;
		}
		e->tags = clean;
	}
	writeSidecar(*e);
	emit logged((spoken.isEmpty() ? "Clip labelled: " : "Clip named by voice: ") + QFileInfo(to).fileName() +
		    (e->tags.isEmpty() ? "" : "  [" + e->tags.join(", ") + "]"));
	return to;
}

std::vector<Clips::Entry> Clips::allClips(int max) const
{
	std::vector<Entry> out;
	QSet<QString> seen;
	// this session first, newest first
	for (auto it = history_.rbegin(); it != history_.rend(); ++it)
		if (!it->path.isEmpty() && !seen.contains(it->path)) {
			seen.insert(it->path);
			out.push_back(*it);
		}
	QStringList folders;
	if (!folder.isEmpty())
		folders << folder;
	folders << watchFolders;
	if (!history_.empty())
		folders << QFileInfo(history_.back().path).absolutePath();
	folders.removeDuplicates();
	for (const QString &dir : folders) {
		for (const QFileInfo &fi : listClips(dir)) {
			if ((int)out.size() >= max)
				break;
			if (seen.contains(fi.absoluteFilePath()))
				continue;
			QFile f(fi.dir().filePath(fi.completeBaseName() + ".json"));
			if (!f.open(QIODevice::ReadOnly))
				continue; // not one of ours
			QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
			Entry e;
			e.path = fi.absoluteFilePath();
			e.title = o.value("title").toString();
			for (const auto &t : o.value("tags").toArray())
				e.tags << t.toString();
			e.when = QDateTime::fromString(o.value("created").toString(), Qt::ISODate);
			if (!e.when.isValid())
				e.when = fi.lastModified();
			e.momentS = o.value("moment_s_from_end").toDouble(-1);
			e.kills = o.value("kills").toInt();
			e.info = o;
			seen.insert(e.path);
			out.push_back(e);
		}
	}
	std::stable_sort(out.begin(), out.end(), [](const Entry &a, const Entry &b) { return a.when > b.when; });
	return out;
}

void Clips::logEntry(const Entry &e)
{
	QFile f(logFile());
	if (f.open(QIODevice::Append | QIODevice::Text)) {
		QTextStream ts(&f);
		ts << e.when.toString(Qt::ISODate) << "," << safe(e.title) << "," << e.tags.join("|") << "," << e.path
		   << "," << (e.momentS < 0 ? QString() : QString::number(e.momentS, 'f', 1)) << ","
		   << (e.firstS < 0 ? QString() : QString::number(e.firstS, 'f', 1)) << "," << e.kills << "\n";
	}
	writeSidecar(e);
}

/// A JSON file next to the clip with everything known about it. Kennel Cut reads it when the clip
/// reaches the editing PC, so the cut lands on the kill instead of on a guess.
void Clips::writeSidecar(const Entry &e)
{
	if (e.path.isEmpty())
		return;
	QFileInfo fi(e.path);
	QJsonObject o = e.info;
	o["file"] = fi.fileName();
	o["title"] = e.title;
	o["tags"] = QJsonArray::fromStringList(e.tags);
	o["created"] = e.when.toString(Qt::ISODate);
	o["end_epoch"] = (double)e.when.toMSecsSinceEpoch() / 1000.0;
	if (e.momentS >= 0)
		o["moment_s_from_end"] = e.momentS;
	if (e.firstS >= 0)
		o["first_s_from_end"] = e.firstS;
	if (e.kills > 0)
		o["kills"] = e.kills;
	if (!e.pathV.isEmpty())
		o["vertical"] = QFileInfo(e.pathV).fileName();
	QFile f(fi.dir().filePath(fi.completeBaseName() + ".json"));
	if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
		f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
}

/// "name [2 of 3].mp4" -> "name", so a file can be renumbered as its run grows.
static QString stripRunMark(const QString &base)
{
	static const QRegularExpression mark(QStringLiteral("\\s*\\[\\d+ of \\d+\\]$"));
	QString b = base;
	b.remove(mark);
	return b;
}

void Clips::joinSeries(const QString &path, const QDateTime &when)
{
	if (path.isEmpty() || seriesWindowS <= 0)
		return;
	// within the window of the LAST clip, not the first: three clips 40 s apart are one run
	bool same = series_.last.isValid() && qAbs(series_.last.secsTo(when)) <= seriesWindowS;
	if (!same)
		series_.paths.clear();
	series_.last = when;
	series_.paths << path;
	int n = series_.paths.size();
	if (n < 2)
		return; // a run only exists once there is a second clip; a lone clip keeps its plain name
	for (int i = 0; i < n; i++) {
		QFileInfo fi(series_.paths[i]);
		if (!fi.exists())
			continue;
		QString base = stripRunMark(fi.completeBaseName());
		QString target =
			fi.dir().filePath(QString("%1 [%2 of %3].%4").arg(base).arg(i + 1).arg(n).arg(fi.suffix()));
		if (target == series_.paths[i])
			continue;
		if (!renameClip(series_.paths[i], target))
			continue; // still being written by something, or open in a player: try again next clip
		for (auto &e : history_)
			if (e.path == series_.paths[i])
				e.path = target;
		series_.paths[i] = target;
	}
	emit logged(QString("Rolling highlights: %1 clips inside %2 s, named 1 of %1 to %1 of %1.")
			    .arg(n)
			    .arg(seriesWindowS));
}

/// The moment a clip was made, from the "yyyy-MM-dd HH-mm-ss" every name carries (the {date}
/// {time} of the template), or the file's modified time when the name has none.
static QDateTime clipMoment(const QFileInfo &fi)
{
	static const QRegularExpression stamp(QStringLiteral("(\\d{4}-\\d{2}-\\d{2})[ _T](\\d{2})-(\\d{2})-(\\d{2})"));
	QRegularExpressionMatch m = stamp.match(fi.completeBaseName());
	if (m.hasMatch()) {
		QDateTime dt = QDateTime::fromString(m.captured(1) + " " + m.captured(2) + ":" + m.captured(3) + ":" +
							     m.captured(4),
						     "yyyy-MM-dd HH:mm:ss");
		if (dt.isValid())
			return dt;
	}
	return fi.lastModified();
}

QString Clips::numberPastClips()
{
	QStringList folders = watchFolders + discoverBacktrackFolders();
	if (!folder.isEmpty())
		folders.prepend(folder);
	folders.removeDuplicates();
	struct Item {
		QDateTime when;
		QString path;
	};
	std::vector<Item> items;
	QDateTime now = QDateTime::currentDateTime();
	for (const QString &f : folders) {
		if (!QDir(f).exists())
			continue;
		for (const QFileInfo &fi : listClips(f)) {
			if (fi.lastModified().secsTo(now) < 10)
				continue; // still being written
			items.push_back({clipMoment(fi), fi.absoluteFilePath()});
		}
	}
	std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) { return a.when < b.when; });
	// chain them: each within the window of the one before it
	std::vector<std::vector<Item>> runs;
	for (const Item &it : items) {
		if (!runs.empty() && runs.back().back().when.secsTo(it.when) <= seriesWindowS)
			runs.back().push_back(it);
		else
			runs.push_back({it});
	}
	int renamed = 0, runsFound = 0, alone = 0, failed = 0;
	for (auto &run : runs) {
		int n = (int)run.size();
		if (n < 2) {
			// a lone clip: take any old run mark off it, it is not part of one
			QFileInfo fi(run[0].path);
			QString base = stripRunMark(fi.completeBaseName());
			if (base != fi.completeBaseName()) {
				QString target = fi.dir().filePath(base + "." + fi.suffix());
				if (!QFile::exists(target) && renameClip(run[0].path, target))
					renamed++;
			}
			alone++;
			continue;
		}
		runsFound++;
		for (int i = 0; i < n; i++) {
			QFileInfo fi(run[i].path);
			QString base = stripRunMark(fi.completeBaseName());
			QString target = fi.dir().filePath(
				QString("%1 [%2 of %3].%4").arg(base).arg(i + 1).arg(n).arg(fi.suffix()));
			if (target == run[i].path)
				continue;
			if (QFile::exists(target) || !renameClip(run[i].path, target)) {
				failed++;
				continue;
			}
			for (auto &e : history_)
				if (e.path == run[i].path)
					e.path = target;
			renamed++;
		}
	}
	QString out = QString("%1 clips looked at in %2 folder%3: %4 run%5 of rolling highlights, %6 file%7 renamed, "
			      "%8 lone clip%9 left plain.")
			      .arg(items.size())
			      .arg(folders.size())
			      .arg(folders.size() == 1 ? "" : "s")
			      .arg(runsFound)
			      .arg(runsFound == 1 ? "" : "s")
			      .arg(renamed)
			      .arg(renamed == 1 ? "" : "s")
			      .arg(alone)
			      .arg(alone == 1 ? "" : "s");
	if (failed)
		out += QString(" %1 could not be renamed (open in a player, or the name is taken).").arg(failed);
	emit logged("Clips: " + out);
	return out;
}

QString Clips::logFile() const
{
	return QString::fromStdString(Config::configFile("clips.csv"));
}

/// OBS keeps the replay buffer's length in the profile, under whichever output mode is in use. Set
/// both, and restart the buffer if it is running so the new length takes. Returns true if changed.
/// OBS keeps the replay buffer's length in the profile, under whichever output mode is in use.
/// Writes both and says whether the buffer has to be restarted for the new length to take - the
/// caller does that, on a timer: obs_frontend_replay_buffer_stop() returns before the buffer has
/// actually stopped, so starting it on the next line silently leaves it off, and then there are no
/// clips at all.
Clips::ReplayChange Clips::setReplaySeconds(int seconds)
{
	seconds = std::clamp(seconds, 5, 300);
	config_t *prof = obs_frontend_get_profile_config();
	if (!prof)
		return ReplayChange::None;
	bool changed = false;
	for (const char *section : {"SimpleOutput", "AdvOut"}) {
		if ((int)config_get_uint(prof, section, "RecRBTime") != seconds) {
			config_set_uint(prof, section, "RecRBTime", (uint64_t)seconds);
			changed = true;
		}
	}
	if (!changed)
		return ReplayChange::None;
	config_save_safe(prof, "tmp", nullptr);
	return obs_frontend_replay_buffer_active() ? ReplayChange::NeedsRestart : ReplayChange::Written;
}

int Clips::readReplaySeconds()
{
	config_t *prof = obs_frontend_get_profile_config();
	if (!prof)
		return 0;
	const char *mode = config_get_string(prof, "Output", "Mode");
	const char *section = (mode && strcmp(mode, "Advanced") == 0) ? "AdvOut" : "SimpleOutput";
	return (int)config_get_uint(prof, section, "RecRBTime");
}

void Clips::ensureReplayBuffer()
{
	if (obs_frontend_replay_buffer_active())
		return;
	obs_frontend_replay_buffer_start();
	if (obs_frontend_replay_buffer_active())
		emit logged("Replay buffer started (Kennel needs it for clips).");
	else
		emit logged(
			"REPLAY BUFFER IS OFF and could not be started: enable it in OBS Settings → Output → Replay Buffer (60-120 s), then restart OBS. Until then clips only fire your hotkeys.");
}

QList<QPair<QString, QString>> Clips::allHotkeys()
{
	QList<QPair<QString, QString>> out;
	obs_enum_hotkeys(
		[](void *data, obs_hotkey_id, obs_hotkey_t *hk) {
			auto *o = (QList<QPair<QString, QString>> *)data;
			const char *n = obs_hotkey_get_name(hk), *d = obs_hotkey_get_description(hk);
			if (n && *n)
				o->append({QString::fromUtf8(n), QString::fromUtf8(d ? d : "")});
			return true;
		},
		&out);
	std::sort(out.begin(), out.end(), [](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
		return a.second.toLower() < b.second.toLower();
	});
	return out;
}

bool Clips::fireHotkey(const QString &name)
{
	struct Ctx {
		QByteArray name;
		obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
	} ctx{name.toUtf8()};
	obs_enum_hotkeys(
		[](void *data, obs_hotkey_id id, obs_hotkey_t *hk) {
			auto *c = (Ctx *)data;
			if (strcmp(obs_hotkey_get_name(hk), c->name.constData()) == 0) {
				c->id = id;
				return false;
			}
			return true;
		},
		&ctx);
	if (ctx.id == OBS_INVALID_HOTKEY_ID)
		return false;
	obs_hotkey_trigger_routed_callback(ctx.id, true);
	obs_hotkey_trigger_routed_callback(ctx.id, false);
	return true;
}

QString Clips::request(const QString &title, const QStringList &tags, const QString &source,
		       const QList<double> &moments, const QJsonObject &info)
{
	QDateTime now = QDateTime::currentDateTime();
	// the file ends about now (the buffer is saved as of this call): every kill becomes "this many
	// seconds before the end", which survives the file being moved, renamed or synced
	double momentS = -1, firstS = -1;
	int kills = 0;
	{
		double endS = (double)now.toMSecsSinceEpoch() / 1000.0;
		double last = -1, first = -1;
		for (double m : moments) {
			if (m <= 0 || m > endS)
				continue;
			if (last < 0 || m > last)
				last = m;
			if (first < 0 || m < first)
				first = m;
			kills++;
		}
		if (last > 0) {
			momentS = std::round((endS - last) * 10) / 10;
			firstS = std::round((endS - first) * 10) / 10;
		}
	}
	if (lastRequest_.isValid() && lastRequest_.msecsTo(now) < minGapMs)
		return "ignored: too soon after the last clip";
	lastRequest_ = now;
	QStringList missed;
	for (const QString &hk : hotkeys)
		if (!fireHotkey(hk))
			missed << hk;
	if (!hotkeys.isEmpty() && missed.size() < hotkeys.size()) {
		// something else (Backtrack) is writing a file: give it our name when it appears
		Watch w;
		w.since = now;
		w.title = title;
		w.tags = tags;
		w.momentS = momentS;
		w.firstS = firstS;
		w.kills = kills;
		w.info = info;
		QStringList folders = watchFolders + discoverBacktrackFolders();
		for (const QString &folder : folders)
			for (const QFileInfo &fi :
			     QDir(folder).entryInfoList({"*.mkv", "*.mp4", "*.mov", "*.flv", "*.ts"}, QDir::Files))
				w.seen.insert(fi.absoluteFilePath());
		watches_.push_back(w);
		if (!watchTimer_.isActive())
			watchTimer_.start(1000);
	}
	if (!missed.isEmpty())
		emit logged("Clip hotkeys not found in OBS (plugin missing?): " + missed.join(", "));
	else if (!hotkeys.isEmpty())
		emit logged(QString("Fired %1 clip hotkey(s) for '%2'.")
				    .arg(hotkeys.size())
				    .arg(title.isEmpty() ? "(untitled)" : title));
	if (!useReplay)
		return hotkeys.isEmpty()
			       ? "no clip method: turn on the replay buffer or pick a hotkey (Settings, Clips & replays)"
			       : "";
	if (!obs_frontend_replay_buffer_active()) {
		if (!autoStartReplay)
			return "the replay buffer is not running (Settings → Output → Replay Buffer)";
		ensureReplayBuffer();
		return "replay buffer was off; started it - this moment is lost, the next one will save";
	}
	pending_.push_back({now, title, tags, source, momentS, firstS, kills, info});
	obs_frontend_replay_buffer_save();
	emit logged(QString("Clip requested: %1 [%2]").arg(title.isEmpty() ? "(untitled)" : title, tags.join(", ")));
	return "";
}

void Clips::onReplaySaved()
{
	char *last = obs_frontend_get_last_replay();
	QString path = last ? QString::fromUtf8(last) : QString();
	bfree(last);
	if (path.isEmpty())
		return;
	Pending p;
	if (!pending_.empty()) {
		p = pending_.front();
		pending_.pop_front();
	} else {
		p.when = QDateTime::currentDateTime();
		p.title = "manual";
	}
	QFileInfo fi(path);
	QString name = withMoment(nameFor(p.when, p.title, p.tags, p.source), p.momentS);
	QDir outDir = fi.dir();
	if (!folder.isEmpty()) {
		QDir want(folder);
		if (want.exists() || want.mkpath("."))
			outDir = want;
		else
			emit logged("Clip folder does not exist and could not be created: " + folder);
	}
	QString target = outDir.filePath(name + "." + fi.suffix());
	int n = 2;
	while (QFile::exists(target) && target != path)
		target = outDir.filePath(name + QString("_%1.").arg(n++) + fi.suffix());
	QString finalPath = path;
	if (!name.isEmpty() && QFile::rename(path, target))
		finalPath = target;
	Entry e{p.when, p.title, p.tags, finalPath, p.momentS, p.firstS, p.kills, p.info};
	history_.push_back(e);
	joinSeries(finalPath, p.when);
	finalPath = history_.back().path; // joinSeries may have renamed it into the run
	e.path = finalPath;
	while (history_.size() > 200)
		history_.pop_front();
	logEntry(e);
	emit logged("Clip saved: " + QFileInfo(finalPath).fileName());
	emit saved(e);
}
