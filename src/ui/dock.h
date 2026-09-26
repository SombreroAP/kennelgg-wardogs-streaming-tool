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
	void openSettings(const QString &page = QString());
	void openWizard();
	void openLogs();
	void openSquad();
	void openClips(const QString &focusPath = QString());
	/// A health or banner action: the engine's own, or a window the dock opens.
	void runFix(const QString &id);

private:
	Engine *e_;
	bool noteNext_ = false; // the next manual clip opens the note dialog
	bool filling_ = false;
	bool compact() const;
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
	QPushButton *replay_ = nullptr, *highlights_ = nullptr, *addPop_ = nullptr, *showPop_ = nullptr;
	QWidget *squadRow_ = nullptr, *eventsHead_ = nullptr;
	QListWidget *events_ = nullptr;
	QAction *appAct_ = nullptr, *compactAct_ = nullptr, *compactLiveAct_ = nullptr;
	QTimer tick_;
	QPointer<QDialog> settings_;
	QPointer<QWidget> wizard_;
	QPointer<QDialog> squad_;
};
