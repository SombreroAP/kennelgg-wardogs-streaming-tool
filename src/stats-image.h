#pragma once
#include <QImage>
#include <QString>
#include "session.h"

/// The session as a 1920 x 1080 picture for social media: a WARDOGS press-kit screenshot behind,
/// the Kennel.gg mark, the player's name and the session's numbers. Qt only (no OBS), so it can be
/// drawn and checked anywhere.
namespace StatsImage {

constexpr int kBackgrounds = 15; // data/stats/bg1.jpg .. bg15.jpg (WARDOGS press kit and social screenshots)

/// Other font files than the default Chakra Petch (display for names and numbers, label for the small caps).
void setFonts(const QString &display, const QString &label, bool oneWeight);

/// dataDir: the plugin's data folder (holds stats/ and overlay/ with the fonts and the hound mark).
/// bg: which background, 1-based; out of range picks one from the date.
QImage render(const Session &s, const QString &name, const QString &dataDir, int bg, const QString &role = QString());
/// The role the session was played as: what earned most (medic, recon, logistics, builder, driver, objective),
/// else fragger; `preset` (the bar's Quick pick, "custom" = none) when the session earned nothing yet.
QString roleOf(const Session &s, const QString &preset);
/// The press-kit backgrounds that fit a role best, best first (bg numbers); the rest follow for "Another".
QList<int> backgroundsFor(const QString &role);

} // namespace StatsImage
