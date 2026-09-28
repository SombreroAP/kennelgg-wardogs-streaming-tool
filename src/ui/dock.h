#pragma once
#include <QWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QListWidget>
#include <QToolButton>
#include <QCheckBox>
#include <QPointer>
#include <QDialog>
#include <QTimer>
#include <QMap>
#include "engine.h"

class FlowLayout;

/// The always-visible panel. Top to bottom: what is on stream, a health strip that says what is
/// wrong and fixes it, messages in place of pop-up windows, then one row of people for the main
/// view, one for Dual POV, and the clip buttons. Everything else is behind the ⋯ menu.
class Dock : public QWidget {
	Q_OBJECT
public:
	explicit Dock(Engine *engine, QWidget *parent = nullptr);
public slots:
	void refresh();
	/// Build the whole dock again (the language changed).
	void rebuild();
	void openSettings(const QString &page = QString());
	void openWizard();
	void openLogs();
	/// The plugin's, OBS's and ClipHound's logs to kennel.gg in one click (0.29.2), with a note; answers
	/// with a reference to quote in the Discord.
	void openSendLogs();
	void openSquad();
	void openClips(const QString &focusPath = QString());
	/// "Get stats image": the session as a picture for social media.
	void openStatsImage();
	/// What's new since this version, with Update now / Later / Skip this version. Opens by itself
	/// once per OBS start when a newer version is out and you are not live.
	void openWhatsNew();
	/// A health or banner action: the engine's own, or a window the dock opens.
	void runFix(const QString &id);

private:
	Engine *e_;
	bool noteNext_ = false; // the next manual clip opens the note dialog
	bool filling_ = false;
	bool compact() const;
	void build();
	void refreshHealth();
	void rebuildBanners();
	void rebuildPeople();
	void addPopouts();

	QLabel *state_ = nullptr, *last_ = nullptr, *clip_ = nullptr, *near_ = nullptr;
	QToolButton *menuBtn_ = nullptr, *saveBtn_ = nullptr;
	QMap<QString, QToolButton *> dots_;
	QVBoxLayout *banners_ = nullptr;
	QStringList bannerKeys_;
	QWidget *voiceRow_ = nullptr;
	QProgressBar *voiceBar_ = nullptr;
	QLabel *voiceLbl_ = nullptr;
	QCheckBox *autoSwitch_ = nullptr, *dualAuto_ = nullptr, *magPack_ = nullptr;
	QLabel *appLine_ = nullptr;
	QLabel *session_ = nullptr; // this session: kills, deaths, assists, revives, money
	QLabel *dualHead_ = nullptr;
	FlowLayout *povFlow_ = nullptr, *dualFlow_ = nullptr;
	QWidget *povBox_ = nullptr, *dualBox_ = nullptr;
	QLabel *povEmpty_ = nullptr;
	QPushButton *meBtn_ = nullptr, *closestBtn_ = nullptr, *dualOffBtn_ = nullptr;
	QList<QPushButton *> povBtns_, dualBtns_;
	QList<int> peopleIdx_;
	QString peopleKey_;
	QToolButton *replay_ = nullptr;
	QPushButton *highlights_ = nullptr, *addPop_ = nullptr, *showPop_ = nullptr;
	// the session bar on stream (on/off, lit while it shows), and its two small companions
	QPushButton *sessionBtn_ = nullptr, *sessionReset_ = nullptr, *sessionImage_ = nullptr;
	QWidget *sessionRow_ = nullptr;
	QWidget *squadRow_ = nullptr, *eventsHead_ = nullptr;
	QListWidget *events_ = nullptr;
	QAction *appAct_ = nullptr, *compactAct_ = nullptr, *compactLiveAct_ = nullptr;
	QTimer tick_;
	QPointer<QDialog> settings_;
	QPointer<QWidget> wizard_;
	QPointer<QDialog> squad_;
	QPointer<QDialog> whatsNew_;
};
