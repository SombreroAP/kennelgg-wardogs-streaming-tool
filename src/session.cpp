#include "session.h"
#include <QRegularExpression>
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
	if (heals)
		p << (heals == 1 ? tx("1 heal") : tx("%1 heals").arg(heals));
	if (spots)
		p << (spots == 1 ? tx("1 spot") : tx("%1 spots").arg(spots));
	if (supplies)
		p << tx("Supplies: %1").arg(supplies);
	if (builds)
		p << tx("Built: %1").arg(builds);
	if (transports)
		p << tx("Passengers: %1").arg(transports);
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
	if (heals || spots || supplies || builds || transports)
		l << tx("Heals %1   Spots %2   Supplies %3   Built %4   Passengers %5")
				.arg(QString::number(heals), QString::number(spots), QString::number(supplies),
				     QString::number(builds), QString::number(transports));
	{
		QStringList by;
		const char *names[RoleCount] = {TX_NOOP("combat"),
						TX_NOOP("medic"),
						TX_NOOP("recon"),
						TX_NOOP("logistics"),
						TX_NOOP("building"),
						TX_NOOP("transport"),
						TX_NOOP("objectives"),
						nullptr,
						nullptr};
		for (int i = 0; i < RoleCount; i++)
			if (names[i] && roleEarned[i] > 0)
				by << txv(names[i]) + " " + money(roleEarned[i]);
		if (!by.isEmpty())
			l << tx("Earned by role: %1").arg(by.join(", "));
	}
	if (longestKillM)
		l << tx("Longest kill %1 m").arg(longestKillM);
	if (!topWeapon().isEmpty())
		l << tx("Most kills with the %1 (%2)").arg(topWeapon()).arg(weapons.value(topWeapon()));
	return l.join("\n") + "\n";
}

Session::Role Session::role(const QString &r)
{
	static const QRegularExpression refund("REFUND"), medical("REVIV|RESUSC|HEAL|STIM|DEFIB|MEDIC|BANDAGE"),
		transport("PASSENGER|TRANSPORT|TAXI|DROP ?OFF|SPAWNED ON|SPAWN ON|PICK ?UP|EXTRACT"),
		building("BUILD|BUILT|CONSTRUCT|FORTIF|STRUCTURE|FOB|PLACED|DEPLOYED"),
		logistics("SUPPL|RESUPPL|REPAIR|AMMO|FUEL|LOGIST|PALLET|CRATE"),
		recon("SPOT|RECON|INTEL|SCOUT|MARKED|REVEAL|DETECT|UAV"),
		objective("ZONE|CAPTUR|OBJECTIV|PRESENCE|SECTOR|HOLD"),
		combat("KILL|HEADSHOT|ASSIST|DESTROY|DOWNED|ELIMIN|EXECUT|STREAK|LONG RANGE");
	if (r.contains(refund))
		return Refund;
	if (r.contains(medical))
		return Medical;
	if (r.contains(transport))
		return Transport;
	if (r.contains(building))
		return Building;
	if (r.contains(logistics))
		return Logistics;
	if (r.contains(recon))
		return Recon;
	if (r.contains(objective))
		return Objective;
	if (r.contains(combat))
		return Combat;
	return Other;
}

QString Session::why(const QString &r)
{
	switch (role(r)) {
	case Medical:
		return r.contains("HEAL") ? "HEAL" : "REVIVE";
	case Transport:
		return "TRANSPORT";
	case Building:
		return "BUILD";
	case Logistics:
		return "SUPPLY";
	case Recon:
		return "SPOT";
	case Objective:
		return "ZONE";
	case Refund:
		return "REFUND";
	case Combat:
		return r.contains("ASSIST")    ? "ASSIST"
		       : r.contains("KILL")    ? "KILL"
		       : r == "HEADSHOT"       ? "HEADSHOT"
		       : r.contains("DESTROY") ? "VEHICLE"
					       : "KILL";
	default:
		return "REWARD";
	}
}

const QList<Session::Preset> &Session::presets()
{
	static const QList<Preset> list = {
		{"fragger", TX_NOOP("Fragger"), "kda,kd,headshots,longest,net"},
		{"medic", TX_NOOP("Medic"), "revives,heals,medicCash,net"},
		{"recon", TX_NOOP("Recon"), "spots,reconCash,kda,net"},
		{"logistics", TX_NOOP("Logistics"), "supplies,logisticsCash,net,permin"},
		{"builder", TX_NOOP("Builder"), "builds,buildCash,net,permin"},
		{"driver", TX_NOOP("Driver"), "transports,transportCash,vehicles,net"},
		{"objective", TX_NOOP("Objective"), "objectiveCash,kda,net,permin"},
		{"all-round", TX_NOOP("All-round"), "kda,revives,net,permin"},
	};
	return list;
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
		{"heals", TX_NOOP("Heals (medic)")},
		{"spots", TX_NOOP("Enemies spotted (recon)")},
		{"supplies", TX_NOOP("Supplies delivered (logistics)")},
		{"builds", TX_NOOP("Things built (builder)")},
		{"transports", TX_NOOP("Passengers transported (driver)")},
		{"medicCash", TX_NOOP("Money from medic play")},
		{"reconCash", TX_NOOP("Money from recon")},
		{"logisticsCash", TX_NOOP("Money from logistics")},
		{"buildCash", TX_NOOP("Money from building")},
		{"transportCash", TX_NOOP("Money from transporting")},
		{"objectiveCash", TX_NOOP("Money from zones and objectives")},
		{"combatCash", TX_NOOP("Money from combat")},
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
	o["heals"] = heals;
	o["spots"] = spots;
	o["supplies"] = supplies;
	o["builds"] = builds;
	o["transports"] = transports;
	const char *cash[RoleCount] = {"combatCash",    "medicCash",     "reconCash", "logisticsCash", "buildCash",
				       "transportCash", "objectiveCash", nullptr,     nullptr};
	for (int i = 0; i < RoleCount; i++)
		if (cash[i])
			o[cash[i]] = (double)roleEarned[i];
	o["topWeapon"] = topWeapon();
	return o;
}
