#include "session.h"
#include <QStringList>
#include <algorithm>

QString Session::money(int64_t v, bool sign)
{
	QString digits = QString::number(v < 0 ? -v : v);
	for (int i = (int)digits.size() - 3; i > 0; i -= 3)
		digits.insert(i, ',');
	return QString(v < 0 ? "-" : (sign && v > 0 ? "+" : "")) + "$" + digits;
}

QString Session::topWeapon() const
{
	QString best;
	int n = 0;
	for (auto it = weapons.begin(); it != weapons.end(); ++it)
		if (it.value() > n) {
			n = it.value();
			best = it.key();
		}
	return best;
}

QString Session::line() const
{
	QStringList p;
	p << QString("%1 kill%2").arg(killCount()).arg(killCount() == 1 ? "" : "s");
	p << QString("%1 death%2").arg(deaths).arg(deaths == 1 ? "" : "s");
	if (assists)
		p << QString("%1 assist%2").arg(assists).arg(assists == 1 ? "" : "s");
	if (revives)
		p << QString("%1 revive%2").arg(revives).arg(revives == 1 ? "" : "s");
	if (earned)
		p << money(earned) + " earned";
	return p.join("  ·  ");
}

QString Session::summary() const
{
	QStringList l;
	l << "Session from " + start.toString("yyyy-MM-dd HH:mm") + " to " +
			QDateTime::currentDateTime().toString("HH:mm");
	l << QString("Kills %1   Deaths %2   K/D %3   Downs %4")
			.arg(killCount())
			.arg(deaths)
			.arg(deaths ? QString::number((double)killCount() / deaths, 'f', 2)
				    : QString::number(killCount()))
			.arg(downs);
	l << QString("Assists %1   Revives %2   Headshots %3   Vehicles destroyed %4")
			.arg(assists)
			.arg(revives)
			.arg(headshotCount())
			.arg(vehicles);
	l << "Earned " + money(earned) + (zoneEarned ? " (" + money(zoneEarned) + " from the zone)" : "");
	if (haveBalance)
		l << "Balance " + money(balanceStart) + " -> " + money(balanceNow) + " (" +
				money(balanceNow - balanceStart, true) + ")";
	if (longestKillM)
		l << QString("Longest kill %1 m").arg(longestKillM);
	if (!topWeapon().isEmpty())
		l << QString("Most kills with the %1 (%2)").arg(topWeapon()).arg(weapons.value(topWeapon()));
	return l.join("\n") + "\n";
}

QJsonObject Session::json() const
{
	QJsonObject o;
	o["since"] = start.toString(Qt::ISODate);
	o["kills"] = killCount();
	o["deaths"] = deaths;
	o["downs"] = downs;
	o["assists"] = assists;
	o["revives"] = revives;
	o["headshots"] = headshotCount();
	o["vehicles"] = vehicles;
	o["earned"] = (double)earned;
	o["earnedText"] = money(earned);
	o["zoneEarned"] = (double)zoneEarned;
	o["balance"] = haveBalance ? (double)balanceNow : QJsonValue();
	o["balanceChange"] = haveBalance ? (double)(balanceNow - balanceStart) : QJsonValue();
	o["balanceChangeText"] = haveBalance ? money(balanceNow - balanceStart, true) : QString();
	o["kd"] = deaths ? (double)killCount() / deaths : (double)killCount();
	o["longest"] = longestKillM;
	o["topWeapon"] = topWeapon();
	return o;
}
