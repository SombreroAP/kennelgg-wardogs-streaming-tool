#pragma once
#include <QWizard>
#include <QWizardPage>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QListWidget>
#include <QLabel>
#include <QSpinBox>
#include <QGridLayout>
#include <QTimer>
#include "engine.h"

/// First-run setup: you → your game → your squad → clips → a live check that everything works.
/// The fields are written as you go, so the last page checks what you actually set. Everything
/// can be changed later in Settings.
class SetupWizard : public QWizard {
	Q_OBJECT
public:
	explicit SetupWizard(Engine *engine, QWidget *parent = nullptr);
	void accept() override;
signals:
	/// A fix on the check page, or an extra, lives in Settings: the dock opens it there.
	void openSettingsPage(const QString &page);

private:
	Engine *e_;
	QLineEdit *gameName_ = nullptr;
	QComboBox *lang_ = nullptr;
	QCheckBox *lookName_ = nullptr;
	QComboBox *game_ = nullptr;
	QComboBox *scene_ = nullptr;
	QLabel *gameHint_ = nullptr;
	QLabel *preview_ = nullptr, *previewNote_ = nullptr; // what the plugin sees of the game source
	QTimer previewTick_;
	void showPreview();
	QLabel *popResult_ = nullptr;
	QLabel *clipResult_ = nullptr;
	QPushButton *clipTest_ = nullptr, *swapTest_ = nullptr;
	QCheckBox *statsShare_ = nullptr;
	QLabel *swapResult_ = nullptr;
	QListWidget *squad_ = nullptr;
	QLineEdit *me_ = nullptr;
	QCheckBox *rosterOn_ = nullptr;
	QCheckBox *launchApp_ = nullptr, *clipDowned_ = nullptr;
	QSpinBox *replaySecs_ = nullptr;
	QGridLayout *checks_ = nullptr;
	QLabel *checkSummary_ = nullptr;
	QPushButton *voiceBtn_ = nullptr;
	QTimer checkTick_;
	void fillGame();
	void fillSquad();
	void fillChecks();
	void commit(); // the fields into the settings, and the settings into effect
	void runFix(const QString &id);
	QWizardPage *pageYou();
	QWizardPage *pageGame();
	QWizardPage *pageSquad();
	QWizardPage *pageClips();
	QWizardPage *pageCheck();
};
