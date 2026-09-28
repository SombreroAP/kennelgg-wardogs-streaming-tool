#include "i18n.h"
#include "datafile.h"
#include <obs-module.h>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <memory>

namespace {
using Table = QHash<QString, QString>;
std::string g_code = "en";
// swapped whole on a change of language, so a worker thread reading it never sees it half built
std::shared_ptr<const Table> g_table = std::make_shared<Table>();
} // namespace

namespace I18n {

const std::vector<std::pair<std::string, QString>> &languages()
{
	static const std::vector<std::pair<std::string, QString>> list = {
		{"en", "English"},
		{"de", "Deutsch"},
		{"fr", QString::fromUtf8("Français")},
		{"es", QString::fromUtf8("Español")},
		{"it", "Italiano"},
		{"pt", QString::fromUtf8("Português (Brasil)")},
		{"pl", "Polski"},
		{"tr", QString::fromUtf8("Türkçe")},
		{"ru", QString::fromUtf8("Русский")},
		{"uk", QString::fromUtf8("Українська")},
		{"ja", QString::fromUtf8("日本語")},
		{"ko", QString::fromUtf8("한국어")},
		{"zh", QString::fromUtf8("简体中文")},
		{"zh-tw", QString::fromUtf8("繁體中文")},
	};
	return list;
}

std::string resolve(const std::string &setting)
{
	std::string want = setting;
	if (want.empty() || want == "auto") {
		// OBS's own language: "ja-JP", "zh-CN", "zh-TW", "pt-BR", ...
		const char *loc = obs_get_locale();
		std::string l = loc ? loc : "en-US";
		for (auto &c : l)
			c = (char)tolower((unsigned char)c);
		if (l.rfind("zh-tw", 0) == 0 || l.rfind("zh-hk", 0) == 0 || l.rfind("zh-hant", 0) == 0)
			want = "zh-tw";
		else
			want = l.substr(0, l.find('-'));
	}
	for (const auto &p : languages())
		if (p.first == want)
			return want;
	return "en";
}

void load(const std::string &code)
{
	std::atomic_store(&g_table, std::shared_ptr<const Table>(std::make_shared<const Table>()));
	auto table = std::make_shared<Table>();
	g_code = "en";
	if (code.empty() || code == "en")
		return;
	char *p = kennel_file(("i18n/" + code + ".json").c_str());
	if (!p) {
		blog(LOG_WARNING, "[kennel] no translation file for %s", code.c_str());
		return;
	}
	QFile f(QString::fromUtf8(p));
	bfree(p);
	if (!f.open(QIODevice::ReadOnly))
		return;
	QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
	for (auto it = o.begin(); it != o.end(); ++it) {
		QString t = it.value().toString();
		if (!t.isEmpty())
			table->insert(it.key(), t);
	}
	g_code = code;
	int n = (int)table->size();
	std::atomic_store(&g_table, std::shared_ptr<const Table>(std::move(table)));
	blog(LOG_INFO, "[kennel] language %s: %d strings", code.c_str(), n);
}

const std::string &current()
{
	return g_code;
}

QString translate(const char *english)
{
	QString en = QString::fromUtf8(english);
	std::shared_ptr<const Table> t = std::atomic_load(&g_table);
	if (t->isEmpty())
		return en;
	auto it = t->constFind(en);
	return it == t->constEnd() ? en : it.value();
}

} // namespace I18n
