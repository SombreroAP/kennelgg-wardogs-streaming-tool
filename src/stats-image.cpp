#include "stats-image.h"
#include <QFontDatabase>
#include <QFileInfo>
#include <QHash>
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

QImage render(const Session &s, const QString &name, const QString &dataDir, int bg)
{
	const int W = 1920, H = 1080;
	QImage img(W, H, QImage::Format_ARGB32_Premultiplied);
	img.fill(kGraphite);
	if (bg < 1 || bg > kBackgrounds)
		bg = 1 + (int)(s.start.date().toJulianDay() % kBackgrounds);
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
	p.drawText(QPoint(L + 104, 168), "WARDOGS  ·  SESSION STATS");

	// who, and when
	QString who = name.trimmed().isEmpty() ? QString("Wardog") : name.trimmed();
	int namePx = who.size() > 16 ? 96 : 128;
	p.setPen(kBone);
	p.setFont(font(disp, namePx));
	p.drawText(QRect(L - 6, 200, 1100, 150), Qt::AlignLeft | Qt::AlignVCenter, who.toUpper());
	QString when = s.start.toString("d MMM yyyy").toUpper();
	if (s.activeMs >= 60000) {
		qint64 m = s.activeMs / 60000;
		when += m >= 60 ? QString("  ·  %1 H %2 MIN IN GAME").arg(m / 60).arg(m % 60)
				: QString("  ·  %1 MIN IN GAME").arg(m);
	}
	p.setPen(kDim);
	p.setFont(font(mono, 24, QFont::DemiBold, 3));
	p.drawText(QPoint(L, 382), when);

	// the numbers: three rows of three
	struct Cell {
		QString value, label;
		QColor colour;
	};
	auto num = [](int v) {
		return QString::number(v);
	};
	QString kd = s.deaths ? QString::number((double)s.killCount() / s.deaths, 'f', 2) : num(s.killCount());
	QList<Cell> cells = {
		{QString("%1 / %2 / %3").arg(s.killCount()).arg(s.deaths).arg(s.assists), "K / D / A", kBone},
		{kd, "K/D", kBone},
		{num(s.revives), "Revives", kBone},
		{Session::money(s.earned), "Earned", kOlive},
		{Session::money(s.spent), "Spent", kRed},
		{s.perMinute() >= 0 ? Session::money(s.perMinute()) : QString("-"), "$ per minute", kOlive},
		{num(s.headshotCount()), "Headshots", kBone},
		{s.longestKillM ? QString("%1 m").arg(s.longestKillM) : QString("-"), "Longest kill", kBone},
		{num(s.vehicles), "Vehicles destroyed", kBone},
	};
	const int top = 430, colW = 390, rowH = 158;
	for (int i = 0; i < cells.size(); ++i) {
		int x = L + (i % 3) * colW, y = top + (i / 3) * rowH;
		p.setPen(QColor(232, 229, 221, 40));
		p.drawLine(x, y, x + colW - 40, y);
		p.setFont(font(disp, cells[i].value.size() > 9 ? 70 : 88));
		shadowed(p, QRect(x, y + 6, colW - 20, 100), Qt::AlignLeft | Qt::AlignVCenter, cells[i].value,
			 cells[i].colour);
		p.setFont(font(mono, 20, QFont::DemiBold, 4));
		shadowed(p, QRect(x + 2, y + 110, colW, 32), Qt::AlignLeft | Qt::AlignVCenter, cells[i].label.toUpper(),
			 QColor(176, 172, 163));
	}

	// the gun of the night, then where it came from
	if (!s.topWeapon().isEmpty()) {
		p.setFont(font(disp, 40));
		shadowed(p, QRect(L, top + 3 * rowH + 14, 1400, 56), Qt::AlignLeft | Qt::AlignVCenter,
			 QString("MOST KILLS WITH THE %1 (%2)")
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
