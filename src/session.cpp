#include "session.h"
#include "i18n.h"
#include <QJsonArray>
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
	p << (killCount() == 1 ? tx("1 kill") : tx("%1 kills").arg(killCount()));
	p << (deaths == 1 ? tx("1 death") : tx("%1 deaths").arg(deaths));
	if (assists)
		p << (assists == 1 ? tx("1 assist") : tx("%1 assists").arg(assists));
	if (revives)
		p << (revives == 1 ? tx("1 revive") : tx("%1 revives").arg(revives));
	if (earned)
		p << tx("%1 earned").arg(money(earned));
	if (spent)
		p << tx("%1 spent").arg(money(spent));
	if (perMinute() >= 0)
		p << tx("%1/min").arg(money(perMinute()));
	return p.join("  ·  ");
}

QString Session::summary() const
{
	QStringList l;
	l << tx("Session from %1 to %2")
			.arg(start.toString("yyyy-MM-dd HH:mm"), QDateTime::currentDateTime().toString("HH:mm"));
	l << tx("Kills %1   Deaths %2   K/D %3   Downs %4")
			.arg(killCount())
			.arg(deaths)
			.arg(deaths ? QString::number((double)killCount() / deaths, 'f', 2)
				    : QString::number(killCount()))
			.arg(downs);
	l << tx("Assists %1   Revives %2   Headshots %3   Vehicles destroyed %4")
			.arg(assists)
			.arg(revives)
			.arg(headshotCount())
			.arg(vehicles);
	l << (zoneEarned
		      ? tx("Earned %1 (%2 from the zone)   Spent %3").arg(money(earned), money(zoneEarned), money(spent))
		      : tx("Earned %1   Spent %2").arg(money(earned), money(spent)));
	if (perMinute() >= 0)
		l << tx("%1 a minute over %2 min in game").arg(money(perMinute())).arg(activeMs / 60000);
	if (haveBalance)
		l << tx("Balance %1 -> %2 (%3)")
				.arg(money(balanceStart), money(balanceNow), money(balanceNow - balanceStart, true));
	if (longestKillM)
		l << tx("Longest kill %1 m").arg(longestKillM);
	if (!topWeapon().isEmpty())
		l << tx("Most kills with the %1 (%2)").arg(topWeapon()).arg(weapons.value(topWeapon()));
	return l.join("\n") + "\n";
}

const QList<QPair<QString, QString>> &Session::elements()
{
	static const QList<QPair<QString, QString>> list = {
		{"kda", TX_NOOP("K / D / A")},
		{"kills", TX_NOOP("Kills")},
		{"deaths", TX_NOOP("Deaths")},
		{"assists", TX_NOOP("Assists")},
		{"kd", TX_NOOP("K/D ratio")},
		{"downs", TX_NOOP("Downs")},
		{"revives", TX_NOOP("Revives")},
		{"headshots", TX_NOOP("Headshots")},
		{"vehicles", TX_NOOP("Vehicles destroyed")},
		{"net", TX_NOOP("Session balance (earned minus spent: green up, red down)")},
		{"earned", TX_NOOP("Money earned")},
		{"spent", TX_NOOP("Money spent")},
		{"permin", TX_NOOP("$ per minute in game")},
		{"balance", TX_NOOP("In-game balance change")},
		{"longest", TX_NOOP("Longest kill")},
	};
	return list;
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
	o["spent"] = (double)spent;
	o["spentText"] = money(spent);
	o["net"] = (double)net();
	o["netText"] = money(net(), true);
	QJsonArray log;
	for (const Money &m : moneyLog)
		log.append(QJsonObject{{"seq", m.seq}, {"amt", (double)m.amt}, {"why", m.why}});
	o["moneyLog"] = log;
	o["perMin"] = (double)perMinute();
	o["perMinText"] = perMinute() < 0 ? QString("-") : money(perMinute());
	o["activeMin"] = (double)activeMs / 60000.0;
	o["kdaText"] = QString("%1 / %2 / %3").arg(killCount()).arg(deaths).arg(assists);
	o["balance"] = haveBalance ? (double)balanceNow : QJsonValue();
	o["balanceChange"] = haveBalance ? (double)(balanceNow - balanceStart) : QJsonValue();
	o["balanceChangeText"] = haveBalance ? money(balanceNow - balanceStart, true) : QString();
	o["kd"] = deaths ? (double)killCount() / deaths : (double)killCount();
	o["longest"] = longestKillM;
	o["topWeapon"] = topWeapon();
	return o;
}
