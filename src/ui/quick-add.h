#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include "engine.h"

/// One box for squad mates who are not sharing in Discord: paste a Twitch, Kick or YouTube link,
/// a VDO.Ninja link, or just a name (several at once, split by commas, spaces or new lines), press
/// Add, and each becomes a squad mate ticked as playing, shown live or offline at once.
class QuickAdd : public QWidget {
	Q_OBJECT
public:
	explicit QuickAdd(Engine *engine, QWidget *parent = nullptr);
signals:
	void added();

private:
	Engine *e_;
	QLineEdit *edit_ = nullptr;
	QComboBox *platform_ = nullptr;
	QPushButton *add_ = nullptr;
	QLabel *result_ = nullptr;
	QStringList lastAdded_; // names added by the last press: their live state follows below
	QString lastNote_;
	void addNow();
	void showResult();
};

namespace SquadInput {
/// One pasted entry -> a squad mate (kind, channel, name). A link says its own platform; a bare
/// name takes `fallback`. False with a reason when it cannot be understood.
bool parse(const QString &text, FriendKind fallback, Friend &out, QString &err);
std::string kickSlug(QString v);
std::string youTubeId(QString v);
/// "@handle" -> "UC..." from the channel page; "" when it cannot be found. Blocks up to 8 s.
QString resolveYouTubeHandle(const QString &handle);
} // namespace SquadInput
