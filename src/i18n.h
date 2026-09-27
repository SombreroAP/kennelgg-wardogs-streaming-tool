#pragma once
#include <string>
#include <utility>
#include <vector>
#include <QString>

/// The plugin in the streamer's language: the settings, the dock, the setup, and everything that
/// shows on stream (the stingers, the replay tag, the session card, the stats image).
///
/// Every piece of text is written in English in the code and passed through tx("..."), which
/// returns it in the chosen language. The translations are data/i18n/<code>.json, one object of
/// English -> translated; a string missing there stays English. tools/i18n.py collects every
/// tx("...") and TX_NOOP("...") in src/ into data/i18n/en.json, the list the translations follow.
///
/// Placeholders are Qt's %1 %2 ...: build sentences with tx("Switched to %1").arg(name), never by
/// joining pieces, so each language can put the words in its own order.
namespace I18n {
/// The languages there is a translation for (the game's 14 interface languages), each in its own name.
const std::vector<std::pair<std::string, QString>> &languages();
/// "auto" -> the language OBS runs in (or English when there is no translation for it).
std::string resolve(const std::string &setting);
/// Load a language ("en", "ja", ...). English needs no file.
void load(const std::string &code);
/// The loaded language; the web overlays get it on their URL as &lang=.
const std::string &current();
QString translate(const char *english);
} // namespace I18n

inline QString tx(const char *english)
{
	return I18n::translate(english);
}
inline std::string txs(const char *english)
{
	return I18n::translate(english).toStdString();
}
/// Marks English text for translation where it is written down (a table) but translated later, by
/// tx() on a variable: tx(item.label) works for any string that appears somewhere as TX_NOOP("...").
#define TX_NOOP(s) s
/// tx() for text held in a QString or std::string (from a TX_NOOP table)
inline QString txv(const QString &english)
{
	return I18n::translate(english.toUtf8().constData());
}
