#include "stats-image.h"
#include <algorithm>
#include "i18n.h"
#include <QFontDatabase>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QStringList>

namespace StatsImage {

namespace {
QString g_display, g_label;
bool g_oneWeight = false;
const QColor kBone(232, 229, 221), kDim(154, 151, 143), kAmber(201, 154, 59), kOlive(157, 172, 98), kRed(214, 104, 88),
	kGraphite(12, 14, 16);

QString family(const QString &file, const QString &fallback)
{
	static QHash<QString, QString> loaded;
	if (!loaded.contains(file)) {
		// a full path: Qt will not load an application font from a relative one
		int id = QFontDatabase::addApplicationFont(QFileInfo(file).absoluteFilePath());
		QStringList fam = id >= 0 ? QFontDatabase::applicationFontFamilies(id) : QStringList();
		loaded.insert(file, fam.isEmpty() ? fallback : fam.first());
	}
	return loaded.value(file);
}

/// Text with a soft dark shadow under it, so it reads over the bright parts of any shot.
void shadowed(QPainter &p, const QRect &r, int flags, const QString &t, const QColor &c)
{
	p.setPen(QColor(0, 0, 0, 150));
	p.drawText(r.translated(0, 3), flags, t);
	p.setPen(c);
	p.drawText(r, flags, t);
}

QFont font(const QString &fam, int px, int weight = QFont::Bold, double spacing = 0)
{
	QFont f(fam);
	f.setPixelSize(px);
	// a one-weight display face (Bebas, Anton) is drawn at its own weight, never a faked bold
	f.setWeight(g_oneWeight ? QFont::Normal : (QFont::Weight)weight);
	if (spacing != 0)
		f.setLetterSpacing(QFont::AbsoluteSpacing, spacing);
	return f;
}
} // namespace

void setFonts(const QString &display, const QString &label, bool oneWeight)
{
	g_display = display;
	g_label = label;
	g_oneWeight = oneWeight;
}

QString roleOf(const Session &s, const QString &preset)
{
	struct R {
		const char *id;
		int role;
	};
	static const R roles[] = {{"fragger", Session::Combat},     {"medic", Session::Medical},
				  {"recon", Session::Recon},        {"logistics", Session::Logistics},
				  {"builder", Session::Building},   {"driver", Session::Transport},
				  {"objective", Session::Objective}};
	const char *best = nullptr;
	int64_t top = 0, total = 0;
	for (const R &r : roles) {
		total += s.roleEarned[r.role];
		if (s.roleEarned[r.role] > top) {
			top = s.roleEarned[r.role];
			best = r.id;
		}
	}
	if (best && total > 0) {
		// a support role only when it earned a real share: a stray revive does not make a fragger a medic
		if (QString(best) != "fragger" && top * 4 < total)
			return "fragger";
		return best;
	}
	if (!preset.isEmpty() && preset != "custom" && preset != "all-round")
		return preset;
	return "fragger";
}

QList<int> backgroundsFor(const QString &role)
{
	// the WARDOGS press kit (data/stats/bgN.jpg): 1 sunset helicopter, 2 ghillie sniper, 3 Little Bird close,
	// 4 dragging a downed mate, 5 river and base, 6 foundry, 7 wrecked town and containers, 8 cockpit, 9 town
	// and helicopter, 10 street with a rifle, 11 house interior, 12 supply drop, 13 container warehouse
	// firefight, 14 bridge assault, 15 burner phone over the bridge
	static const QHash<QString, QList<int>> fit = {
		{"fragger", {13, 10, 14, 11, 6}}, {"medic", {4, 5}},      {"recon", {2, 15, 1}},
		{"logistics", {12, 7, 5}},        {"builder", {6, 7, 5}}, {"driver", {14, 3, 9, 1, 8}},
		{"objective", {5, 9, 7, 14}},
	};
	QList<int> out = fit.value(role);
	for (int i = 1; i <= kBackgrounds; ++i)
		if (!out.contains(i))
			out.append(i);
	return out;
}

QImage render(const Session &s, const QString &name, const QString &dataDir, int bg, const QString &roleIn)
{
	const QString role = roleIn.isEmpty() ? roleOf(s, QString()) : roleIn;
	const int W = 1920, H = 1080;
	QImage img(W, H, QImage::Format_ARGB32_Premultiplied);
	img.fill(kGraphite);
	if (bg < 1 || bg > kBackgrounds)
		bg = backgroundsFor(role).first();
	// Chakra Petch (owner's pick, 26 Sep 2026): bold for names and numbers, semibold for the labels
	QString disp = family(g_display.isEmpty() ? dataDir + "/overlay/ChakraPetch-Bold.ttf" : g_display, "Arial");
	QString mono = family(g_label.isEmpty() ? dataDir + "/overlay/ChakraPetch-SemiBold.ttf" : g_label, "Arial");

	QPainter p(&img);
	p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);

	// the press-kit shot, and a dark wash from the left and the bottom so the numbers read on any of them
	QImage back(dataDir + QString("/stats/bg%1.jpg").arg(bg));
	if (!back.isNull())
		p.drawImage(QRect(0, 0, W, H), back);
	QLinearGradient side(0, 0, 1560, 0);
	side.setColorAt(0.0, QColor(10, 12, 14, 240));
	side.setColorAt(0.62, QColor(10, 12, 14, 215));
	side.setColorAt(1.0, QColor(10, 12, 14, 0));
	p.fillRect(0, 0, W, H, side);
	QLinearGradient foot(0, H - 260, 0, H);
	foot.setColorAt(0.0, QColor(10, 12, 14, 0));
	foot.setColorAt(1.0, QColor(10, 12, 14, 210));
	p.fillRect(0, H - 260, W, 260, foot);
	p.fillRect(0, 0, 10, H, kAmber); // the brand's amber edge

	const int L = 110;
	// the mark and the name of the place
	QImage hound(dataDir + "/overlay/hound_mark.png");
	if (!hound.isNull())
		p.drawImage(QRect(L, 78, (int)(92.0 * hound.width() / hound.height()), 92), hound);
	p.setPen(kBone);
	p.setFont(font(disp, 56, QFont::Bold, 2));
	p.drawText(QPoint(L + 100, 136), "KENNEL.GG");
	p.setPen(kAmber);
	p.setFont(font(mono, 22, QFont::DemiBold, 5));
	p.drawText(QPoint(L + 104, 168), tx("WARDOGS  ·  SESSION STATS"));

	// who, and when
	QString who = (name.trimmed().isEmpty() ? QString("Wardog") : name.trimmed()).toUpper();
	// as big as fits: the name was drawn at a set size into a set width, and a long one was cut off
	// ("ADVENTURING BE", 28 Sep 2026)
	const int nameW = 1320;
	int namePx = 128;
	while (namePx > 56 && QFontMetrics(font(disp, namePx)).horizontalAdvance(who) > nameW)
		namePx -= 4;
	QFont nameFont = font(disp, namePx);
	if (QFontMetrics(nameFont).horizontalAdvance(who) > nameW)
		who = QFontMetrics(nameFont).elidedText(who, Qt::ElideRight, nameW);
	p.setPen(kBone);
	p.setFont(nameFont);
	p.drawText(QRect(L - 6, 200, nameW + 20, 150), Qt::AlignLeft | Qt::AlignVCenter, who);
	// the month in the plugin's language
	QString when = QLocale(QString::fromStdString(I18n::current())).toString(s.start, "d MMM yyyy").toUpper();
	if (s.activeMs >= 60000) {
		qint64 m = s.activeMs / 60000;
		when += "  ·  " +
			(m >= 60 ? tx("%1 H %2 MIN IN GAME").arg(m / 60).arg(m % 60) : tx("%1 MIN IN GAME").arg(m));
	}
	QString roleName;
	for (const Session::Preset &pr : Session::presets())
		if (role == pr.id)
			roleName = txv(pr.label).toUpper();
	QFont whenFont = font(mono, 24, QFont::DemiBold, 3);
	p.setFont(whenFont);
	int wx = L;
	if (!roleName.isEmpty()) {
		p.setPen(kAmber);
		p.drawText(QPoint(wx, 382), roleName);
		wx += QFontMetrics(whenFont).horizontalAdvance(roleName + "  ·  ");
		p.setPen(kDim);
		p.drawText(QPoint(wx - QFontMetrics(whenFont).horizontalAdvance("·  "), 382), "·");
	}
	p.setPen(kDim);
	p.drawText(QPoint(wx, 382), when);

	// the numbers: three rows of three
	struct Cell {
		QString value, label;
		QColor colour;
	};
	auto num = [](int v) {
		return QString::number(v);
	};
	QString kd = s.deaths ? QString::number((double)s.killCount() / s.deaths, 'f', 2) : num(s.killCount());
	auto cash = [](int64_t v) {
		return Session::money(v);
	};
	const QString kda = QString("%1 / %2 / %3").arg(s.killCount()).arg(s.deaths).arg(s.assists);
	const QString perMin = s.perMinute() >= 0 ? Session::money(s.perMinute()) : QString("-");
	const QString longest = s.longestKillM ? tx("%1 m").arg(s.longestKillM) : QString("-");
	// what the night was about first: a medic's card leads with revives and heals, a driver's with
	// passengers, a fragger's with kills
	QList<Cell> cells;
	if (role == "medic")
		cells = {{num(s.revives), tx("Revives"), kBone},
			 {num(s.heals), tx("Heals"), kOlive},
			 {cash(s.roleEarned[Session::Medical]), tx("Money from medic play"), kOlive},
			 {kda, tx("K / D / A"), kBone},
			 {cash(s.earned), tx("Earned"), kOlive},
			 {perMin, tx("$ per minute"), kOlive},
			 {cash(s.spent), tx("Spent"), kRed},
			 {num(s.headshotCount()), tx("Headshots"), kBone},
			 {num(s.vehicles), tx("Vehicles destroyed"), kBone}};
	else if (role == "recon")
		cells = {{num(s.spots), tx("Enemies spotted"), kBone},
			 {cash(s.roleEarned[Session::Recon]), tx("Money from recon"), kOlive},
			 {kda, tx("K / D / A"), kBone},
			 {longest, tx("Longest kill"), kBone},
			 {num(s.headshotCount()), tx("Headshots"), kBone},
			 {perMin, tx("$ per minute"), kOlive},
			 {cash(s.earned), tx("Earned"), kOlive},
			 {cash(s.spent), tx("Spent"), kRed},
			 {num(s.revives), tx("Revives"), kBone}};
	else if (role == "logistics")
		cells = {{num(s.supplies), tx("Supplies delivered"), kBone},
			 {cash(s.roleEarned[Session::Logistics]), tx("Money from logistics"), kOlive},
			 {num(s.revives), tx("Revives"), kBone},
			 {kda, tx("K / D / A"), kBone},
			 {cash(s.earned), tx("Earned"), kOlive},
			 {perMin, tx("$ per minute"), kOlive},
			 {cash(s.spent), tx("Spent"), kRed},
			 {num(s.headshotCount()), tx("Headshots"), kBone},
			 {num(s.vehicles), tx("Vehicles destroyed"), kBone}};
	else if (role == "builder")
		cells = {{num(s.builds), tx("Things built"), kBone},
			 {cash(s.roleEarned[Session::Building]), tx("Money from building"), kOlive},
			 {num(s.revives), tx("Revives"), kBone},
			 {kda, tx("K / D / A"), kBone},
			 {cash(s.earned), tx("Earned"), kOlive},
			 {perMin, tx("$ per minute"), kOlive},
			 {cash(s.spent), tx("Spent"), kRed},
			 {num(s.headshotCount()), tx("Headshots"), kBone},
			 {num(s.vehicles), tx("Vehicles destroyed"), kBone}};
	else if (role == "driver")
		cells = {{num(s.transports), tx("Passengers transported"), kBone},
			 {cash(s.roleEarned[Session::Transport]), tx("Money from transporting"), kOlive},
			 {num(s.vehicles), tx("Vehicles destroyed"), kBone},
			 {kda, tx("K / D / A"), kBone},
			 {cash(s.earned), tx("Earned"), kOlive},
			 {perMin, tx("$ per minute"), kOlive},
			 {cash(s.spent), tx("Spent"), kRed},
			 {num(s.revives), tx("Revives"), kBone},
			 {num(s.headshotCount()), tx("Headshots"), kBone}};
	else if (role == "objective")
		cells = {{cash(s.roleEarned[Session::Objective]), tx("Money from zones and objectives"), kOlive},
			 {kda, tx("K / D / A"), kBone},
			 {num(s.revives), tx("Revives"), kBone},
			 {cash(s.earned), tx("Earned"), kOlive},
			 {perMin, tx("$ per minute"), kOlive},
			 {cash(s.spent), tx("Spent"), kRed},
			 {num(s.headshotCount()), tx("Headshots"), kBone},
			 {longest, tx("Longest kill"), kBone},
			 {num(s.vehicles), tx("Vehicles destroyed"), kBone}};
	else
		cells = {{kda, tx("K / D / A"), kBone},
			 {kd, tx("K/D"), kBone},
			 {num(s.headshotCount()), tx("Headshots"), kBone},
			 {cash(s.earned), tx("Earned"), kOlive},
			 {cash(s.spent), tx("Spent"), kRed},
			 {perMin, tx("$ per minute"), kOlive},
			 {longest, tx("Longest kill"), kBone},
			 {num(s.revives), tx("Revives"), kBone},
			 {num(s.vehicles), tx("Vehicles destroyed"), kBone}};
	const int top = 430, colW = 390, rowH = 158;
	for (int i = 0; i < cells.size(); ++i) {
		int x = L + (i % 3) * colW, y = top + (i / 3) * rowH;
		p.setPen(QColor(232, 229, 221, 40));
		p.drawLine(x, y, x + colW - 40, y);
		// each value and label as big as its column allows: "$184,500" at the set size ran into the next
		// column, and so did "MONEY FROM ZONES AND OBJECTIVES"
		const int room = colW - 40;
		int vpx = 88;
		while (vpx > 44 && QFontMetrics(font(disp, vpx)).horizontalAdvance(cells[i].value) > room)
			vpx -= 4;
		p.setFont(font(disp, vpx));
		shadowed(p, QRect(x, y + 6, room + 10, 100), Qt::AlignLeft | Qt::AlignVCenter, cells[i].value,
			 cells[i].colour);
		QString label = cells[i].label.toUpper();
		QFont lf = font(mono, 20, QFont::DemiBold, 4);
		for (int lpx = 20; lpx > 14 && QFontMetrics(lf).horizontalAdvance(label) > room; lpx -= 2)
			lf = font(mono, lpx - 2, QFont::DemiBold, lpx > 16 ? 3 : 2);
		label = QFontMetrics(lf).elidedText(label, Qt::ElideRight, room);
		p.setFont(lf);
		shadowed(p, QRect(x + 2, y + 110, room + 10, 32), Qt::AlignLeft | Qt::AlignVCenter, label,
			 QColor(176, 172, 163));
	}

	// the gun of the night, then where it came from
	if (!s.topWeapon().isEmpty()) {
		p.setFont(font(disp, 40));
		shadowed(p, QRect(L, top + 3 * rowH + 14, 1400, 56), Qt::AlignLeft | Qt::AlignVCenter,
			 tx("MOST KILLS WITH THE %1 (%2)")
				 .arg(s.topWeapon().toUpper())
				 .arg(s.weapons.value(s.topWeapon())),
			 kBone);
	}
	QFont foot2 = font(mono, 22, QFont::DemiBold, 3);
	p.setFont(foot2);
	QString site = "KENNEL.GG/STREAMING", tool = "   ·   KENNEL.GG WARDOGS STREAMING TOOL";
	int sw = QFontMetrics(foot2).horizontalAdvance(site);
	p.setPen(kAmber);
	p.drawText(QPoint(L, H - 50), site);
	p.setPen(kDim);
	p.drawText(QPoint(L + sw, H - 50), tool);
	p.end();
	return img;
}

} // namespace StatsImage
