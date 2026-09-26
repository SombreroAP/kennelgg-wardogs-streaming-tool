#pragma once
#include <QDateTime>
#include <QJsonObject>
#include <QMap>
#include <QString>
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
	static QString money(int64_t v, bool sign = false);
};
