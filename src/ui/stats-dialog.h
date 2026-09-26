#pragma once
#include <QDialog>
#include <QImage>
#include <QLabel>
#include "engine.h"

/// "Get stats image": the session as a picture for social media, previewed with a new press-kit
/// background on request, then saved next to the clips or copied to paste straight into a post.
class StatsDialog : public QDialog {
	Q_OBJECT
public:
	explicit StatsDialog(Engine *engine, QWidget *parent = nullptr);

private:
	Engine *e_;
	QImage img_;
	int bg_ = 0;
	QLabel *preview_ = nullptr, *note_ = nullptr;
	QString saved_;
	void draw();
	QString save();
	QString name() const;
};
