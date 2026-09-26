#pragma once
#include <QDialog>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QSlider>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QListWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QFormLayout>
#include <QGroupBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include "engine.h"

/// Shows the latest game frame with the found header and the capture box; drag to move the box.
class FramePreview : public QLabel {
	Q_OBJECT
public:
	explicit FramePreview(QWidget *parent = nullptr);
	void setFrame(const QImage &img, const Match &m, double threshold, QRectF box);
	/// A second box drawn in its own colour (the NEARBY area next to the kill-feed area).
	void setBox2(QRectF box);
	/// Area-picker mode: no detector rectangle, this caption along the bottom.
	void setPicker(const QString &caption);
signals:
	void boxChanged(QRectF frac);

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;

private:
	QImage img_;
	Match m_;
	double thr_ = 0.85;
	bool picker_ = false;
	QString caption_;
	QRectF box_, box2_, drag_;
	QPoint start_;
	bool dragging_ = false;
	QRect imageRect() const;
};

/// The "Add a squad mate" window on its own: Twitch, Kick, YouTube, VDO.Ninja or an OBS source. Adds
/// the squad mate to the config and makes their sources. True when one was added.
bool addFriendByHand(Engine *e, QWidget *parent);

class SettingsDialog : public QDialog {
	Q_OBJECT
public:
	explicit SettingsDialog(Engine *engine, QWidget *parent = nullptr);
	~SettingsDialog() override;
	/// Bring a tab to the front: general, squad, dual, clips, vertical, voice, look, advanced, logs, help.
	void showPage(const QString &key);

private:
	Engine *e_;
	// switch
	QComboBox *scene_ = nullptr, *game_ = nullptr, *gameDetect_ = nullptr;
	QTableWidget *friends_;
	QComboBox *sceneV_ = nullptr;
	QComboBox *lookPos_ = nullptr;
	QListWidget *onTop_ = nullptr, *onTopV_ = nullptr;
	QSpinBox *lookTopV_ = nullptr;
	QListWidget *mute_ = nullptr;
	QSpinBox *replaySecs_ = nullptr;
	QCheckBox *keepWarm_, *bringFront_, *invSwitch_ = nullptr, *pictureCheck_ = nullptr;
	QCheckBox *preload_ = nullptr, *friendAudio_ = nullptr;
	QCheckBox *rosterOn_ = nullptr, *rosterSources_ = nullptr;
	QLineEdit *rosterUrl_ = nullptr, *rosterChannel_ = nullptr;
	QLabel *rosterStatus_ = nullptr;
	// look
	QCheckBox *lookName_ = nullptr, *lookPlate_ = nullptr, *lookCam_ = nullptr, *lookGrain_ = nullptr,
		  *lookVig_ = nullptr, *lookMark_ = nullptr;
	QLineEdit *lookLabel_;
	QSlider *grain_;
	QPushButton *preview_;
	bool previewing_ = false;
	bool building_ =
		true; // widgets fire changed-signals while being given their saved values; ignore until all tabs exist
	// detect
	FramePreview *frame_;
	QProgressBar *meter_;
	QSlider *thr_ = nullptr;
	QLabel *thrLbl_, *tplLbl_;
	QSlider *hold_ = nullptr;
	QLabel *holdLbl_ = nullptr;
	QSpinBox *downFrames_, *upFrames_, *minDown_, *pollMs_;
	QSpinBox *downDelay_ = nullptr, *upDelay_ = nullptr;
	QCheckBox *auto_, *revive_;
	QCheckBox *wide_ = nullptr;
	QSlider *reviveThr_;
	QLabel *reviveLbl_;

	// The tabs are by what you are trying to do. The builders below make their groups and hand each
	// one to the page it belongs on; General collects who you are, your game and ClipHound.
	enum Page { PGeneral, PSquad, PDual, PClips, PVertical, PVoice, PLook, PAdvanced, PCount };
	QWidget *pageW_[PCount] = {};
	QVBoxLayout *pages_[PCount] = {};
	QTabWidget *tabs_ = nullptr;
	QStringList tabKeys_;
	QFormLayout *genYou_ = nullptr, *genGame_ = nullptr, *genApp_ = nullptr;
	QGroupBox *genAppBox_ = nullptr;
	QLineEdit *discordUser_ = nullptr;
	void buildGeneral();
	QWidget *buildSwitchTab();
	QWidget *buildLookTab();
	QWidget *buildDetectTab();
	QWidget *buildAboutTab();
	QLabel *updateLbl_ = nullptr;
	QCheckBox *updateAuto_ = nullptr;
	QWidget *buildClipsTab();
	QWidget *buildAppTab();
	QWidget *buildLogsTab();
	QWidget *buildDualTab();
	QWidget *buildVoiceTab();
	void showVoicePhrases();
	QComboBox *dualFriend_ = nullptr, *dualPreset_ = nullptr;
	QCheckBox *dualOn_ = nullptr, *dualAuto_ = nullptr, *dualKeep_ = nullptr;
	QCheckBox *dualLook_ = nullptr;
	QSpinBox *dualNameScale_ = nullptr;
	QSpinBox *seriesS_ = nullptr;
	QSpinBox *replayPre_ = nullptr, *replayPost_ = nullptr, *replayScale_ = nullptr, *replayVol_ = nullptr,
		 *replayCool_ = nullptr;
	QCheckBox *replayChat_ = nullptr, *replaySound_ = nullptr, *replayHw_ = nullptr, *highlightsAuto_ = nullptr;
	QComboBox *chatClips_ = nullptr;
	QCheckBox *replayStinger_ = nullptr, *replayStingerSound_ = nullptr;
	QCheckBox *twitchMarkers_ = nullptr, *ytChapters_ = nullptr, *sessionTrack_ = nullptr;
	QListWidget *sessionShow_ = nullptr; // what the on-stream bar shows: ticked, in this order
	QCheckBox *sessionOverlayOn_ = nullptr, *statsShare_ = nullptr;
	QLabel *accountLbl_ = nullptr;
	QPushButton *accountBtn_ = nullptr;
	void showAccount();
	QComboBox *sessionOverlayPos_ = nullptr, *sessionOverlayMode_ = nullptr;
	QSpinBox *highlightsMax_ = nullptr, *clipTrimLead_ = nullptr, *runGap_ = nullptr;
	QCheckBox *clipTrim_ = nullptr, *runMerge_ = nullptr, *runCutGaps_ = nullptr;
	QLineEdit *chatKick_ = nullptr, *chatYouTube_ = nullptr, *highlightsFolder_ = nullptr, *replayLabel_ = nullptr,
		  *replayWord_ = nullptr;
	QLabel *pastResult_ = nullptr;
	QCheckBox *voiceOn_ = nullptr, *voiceNames_ = nullptr, *voiceCommands_ = nullptr, *voiceChime_ = nullptr,
		  *voiceTones_ = nullptr;
	QCheckBox *voiceCmdReplay_ = nullptr, *voiceCmdClip_ = nullptr, *voiceCmdDual_ = nullptr,
		  *voiceCmdForce_ = nullptr, *voiceCmdChange_ = nullptr, *voiceCmdClosest_ = nullptr;
	QComboBox *voiceMic_ = nullptr;
	QSpinBox *voiceChimeVol_ = nullptr;
	QComboBox *voiceChimeWhere_ = nullptr;
	QCheckBox *verticalOn_ = nullptr;
	QLineEdit *voiceWake_ = nullptr;
	QLabel *voiceStatus_ = nullptr;
	QRadioButton *dragWin_ = nullptr, *dragKeys_ = nullptr;
	QDoubleSpinBox *dualX_ = nullptr, *dualY_ = nullptr, *dualW_ = nullptr;
	QSlider *dualOpacity_ = nullptr;
	FramePreview *dualPick_ = nullptr;
	QLabel *dualState_ = nullptr;
	void dualToUi();
	void fillDualFriends(); // the crew-mate list, narrowed to who is live when you are in Kennel.gg voice
	void dualFromUi(bool preset);
	QPlainTextEdit *logView_ = nullptr;
	void refreshLogs();
	QLineEdit *appName_ = nullptr, *appLibrary_ = nullptr, *appBroadcaster_ = nullptr, *clipFolder_ = nullptr;
	QCheckBox *appTwitch_ = nullptr, *appEveryKill_ = nullptr;
	QDoubleSpinBox *appMulti_ = nullptr;
	FramePreview *feedPick_ = nullptr;
	QRadioButton *pickTpl_ = nullptr, *pickNear_ = nullptr;
	QLabel *nearLbl2_ = nullptr;
	QLabel *areaLbl_ = nullptr;
	QSpinBox *appFps_ = nullptr;
	void updateAreas();
	void showNearbyTest();
	// closest squad mate (the game's NEARBY list)
	QCheckBox *nearOn_ = nullptr, *nearFollow_ = nullptr;
	QSlider *nearCooldown_ = nullptr;
	QSpinBox *nearMax_ = nullptr;
	QLabel *nearCdLbl_ = nullptr;
	QLabel *nearLbl_ = nullptr;
	QLabel *twitchLbl_ = nullptr;
	QPushButton *twitchLogin_ = nullptr, *twitchLogout_ = nullptr;
	void refreshAppTab();
	QLineEdit *playerName_ = nullptr;
	QCheckBox *bridgeOn_ = nullptr, *launchApp_ = nullptr, *autoReplay_ = nullptr, *clipDowned_ = nullptr;
	QSpinBox *bridgePort_;
	QLineEdit *appPath_, *nameTpl_;
	QListWidget *clipList_;
	QCheckBox *useReplay_ = nullptr;
	QCheckBox *closeApp_ = nullptr;
	QListWidget *hotkeyList_ = nullptr;
	QLineEdit *backtrackFolder_ = nullptr;
	QLineEdit *hotkeyFilter_ = nullptr;
	void fillHotkeys();
	void fillSources();
	void fillFriends();
	void editFriend(int row);
	void collect(); // UI -> cfg
	void saveAndApply();
	void testFeed();
};
