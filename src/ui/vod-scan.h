#pragma once
#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>

class Engine;
class QLineEdit;
class QPushButton;
class QProgressBar;
class QLabel;
class QTableWidget;
class QCheckBox;

/// Scan a VOD (experimental, 0.30.0): a recording on this PC or the streamer's own Twitch VOD, read by
/// ClipHound at many times real time for the highlights the plugin clips live. The list that comes back
/// can be clipped whole or picked from; YouTube chapters and Twitch links are one click.
class VodScanDialog : public QDialog {
	Q_OBJECT
public:
	explicit VodScanDialog(Engine *engine, QWidget *parent = nullptr);

private:
	Engine *e_;
	int scanId_ = 0;
	QJsonArray moments_;
	QString twitchId_;
	int clipsWanted_ = 0, clipsDone_ = 0;
	QLineEdit *source_ = nullptr;
	QPushButton *browse_ = nullptr, *scan_ = nullptr, *stop_ = nullptr;
	QProgressBar *bar_ = nullptr;
	QLabel *status_ = nullptr;
	QTableWidget *table_ = nullptr;
	QPushButton *clipTicked_ = nullptr, *chapters_ = nullptr, *links_ = nullptr, *folder_ = nullptr;
	QCheckBox *clipAll_ = nullptr;
	void setScanning(bool on);
	void onProgress(const QJsonObject &o);
	void onDone(const QJsonObject &o);
	void onClipDone(const QJsonObject &o);
	void fill();
	void askWhatToClip();
	void clip(const QList<int> &indices);
	QList<int> ticked() const;
};
