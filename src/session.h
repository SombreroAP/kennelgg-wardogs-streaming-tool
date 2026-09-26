#pragma once
#include <QDateTime>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QList>
#include <QPair>
#include <cstdint>
#include <algorithm>

/// This session's numbers: kills and deaths from the kill feed (ClipHound), downs from the plugin's
/// own downed detection, and assists, revives, headshots, vehicles destroyed and money from the cash
/// HUD (the reward lines under your balance, read by hudocr). A session starts when OBS starts or
/// the stream goes live, or by hand from the dock.
struct Session {
	QDateTime start = QDateTime::currentDateTime();
	int kills = 0, deaths = 0, downs = 0;
	int hudKills = 0; // KILL / REVENGE KILL lines under the balance: counts kills ClipHound missed
	int assists = 0, revives = 0, headshots = 0, feedHeadshots = 0, vehicles = 0;
	int64_t earned = 0;     // every +$ line added up
	int64_t zoneEarned = 0; // of which control / hot zone presence
	int64_t spent = 0;      // every drop in your balance that lasted (a loadout, a purchase)
	int64_t activeMs = 0;   // time in game: only while your balance is on screen (not menus, map, loading)
	/// Money earned per minute in game; -1 until there is half a minute of it to go on.
	int64_t perMinute() const { return activeMs >= 30000 ? earned * 60000 / activeMs : -1; }
	bool haveBalance = false;
	int64_t balanceStart = 0, balanceNow = 0;
	int longestKillM = 0;
	QMap<QString, int> weapons; // your kills by weapon name

	void reset() { *this = Session(); }
	int killCount() const { return std::max(kills, hudKills); }
	int headshotCount() const { return std::max(headshots, feedHeadshots); }
	QString topWeapon() const;
	QString line() const;    // one line for the dock
	QString summary() const; // a few lines for the end-of-stream file
	QJsonObject json() const;
	/// What the on-stream bar can show, in its order: id and label. The settings pick from these.
	static const QList<QPair<QString, QString>> &elements();
	static QString defaultShow() { return "kda,revives,net,permin"; }
	int64_t net() const { return earned - spent; } // the session balance: up or down on the session
	static QString money(int64_t v, bool sign = false);
};
