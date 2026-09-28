#include "datafile.h"
#include "config.h"
#include "engine.h"
#include <obs-module.h>
#include <plugin-support.h>
#include <QFile>
#include <QFileInfo>

QString liveDataDir()
{
	return QString::fromStdString(Config::configDir()) + "/live";
}

QString liveDataVersion()
{
	QFile f(liveDataDir() + "/VERSION");
	if (!f.open(QIODevice::ReadOnly))
		return QString();
	return QString::fromUtf8(f.readAll()).trimmed();
}

bool samePatchLine(const QString &a, const QString &b)
{
	return a.section('.', 0, 1) == b.section('.', 0, 1);
}

char *kennel_file(const char *rel)
{
	QString v = liveDataVersion();
	bool use = !v.isEmpty() && Engine::isNewer(v, PLUGIN_VERSION) && samePatchLine(v, PLUGIN_VERSION);
	if (use) {
		QString p = liveDataDir() + "/data/" + QString::fromUtf8(rel);
		if (QFileInfo::exists(p))
			return bstrdup(p.toUtf8().constData());
	}
	return obs_module_file(rel);
}
