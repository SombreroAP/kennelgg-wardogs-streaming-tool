#pragma once
#include <QMessageBox>
#include <QPushButton>
#include "i18n.h"

/// QMessageBox's static helpers, with the buttons in the plugin's language: Qt draws its standard
/// buttons (OK, Yes, Cancel...) in English unless OBS loaded Qt's own translation, and the plugin may
/// not be in OBS's language anyway. Same arguments as QMessageBox::warning() and friends.
namespace KMsg {
inline void localize(QMessageBox &b)
{
	for (QAbstractButton *btn : b.buttons()) {
		switch (b.standardButton(btn)) {
		case QMessageBox::Ok:
			btn->setText(tx("OK"));
			break;
		case QMessageBox::Cancel:
			btn->setText(tx("Cancel"));
			break;
		case QMessageBox::Yes:
			btn->setText(tx("Yes"));
			break;
		case QMessageBox::No:
			btn->setText(tx("No"));
			break;
		case QMessageBox::Close:
			btn->setText(tx("Close"));
			break;
		default:
			break;
		}
	}
}
inline QMessageBox::StandardButton show(QMessageBox::Icon icon, QWidget *parent, const QString &title,
					const QString &text, QMessageBox::StandardButtons buttons,
					QMessageBox::StandardButton def)
{
	QMessageBox b(icon, title, text, buttons, parent);
	if (def != QMessageBox::NoButton)
		b.setDefaultButton(def);
	localize(b);
	return (QMessageBox::StandardButton)b.exec();
}
inline QMessageBox::StandardButton warning(QWidget *parent, const QString &title, const QString &text,
					   QMessageBox::StandardButtons buttons = QMessageBox::Ok,
					   QMessageBox::StandardButton def = QMessageBox::NoButton)
{
	return show(QMessageBox::Warning, parent, title, text, buttons, def);
}
inline QMessageBox::StandardButton information(QWidget *parent, const QString &title, const QString &text,
					       QMessageBox::StandardButtons buttons = QMessageBox::Ok,
					       QMessageBox::StandardButton def = QMessageBox::NoButton)
{
	return show(QMessageBox::Information, parent, title, text, buttons, def);
}
inline QMessageBox::StandardButton question(QWidget *parent, const QString &title, const QString &text,
					    QMessageBox::StandardButtons buttons = QMessageBox::Yes | QMessageBox::No,
					    QMessageBox::StandardButton def = QMessageBox::NoButton)
{
	return show(QMessageBox::Question, parent, title, text, buttons, def);
}
} // namespace KMsg
