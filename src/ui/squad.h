#pragma once
#include <QDialog>
#include <QLabel>
#include <QTableWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QLineEdit>
#include <QComboBox>
#include "engine.h"
#include "ui/settings-dialog.h"

/// The Squad panel, one button from the dock: turn popped-out Discord streams into squad mates,
/// see who is on what, make one active, drop one. Built for a hand on the mouse mid-broadcast.
class SquadPanel : public QDialog {
	Q_OBJECT
public:
	explicit SquadPanel(Engine *engine, QWidget *parent = nullptr);
public slots:
	void refresh();

private:
	void addPopouts();
	void makeActive();
	void removeSelected();
	void showInDual();
	Engine *e_;
	QLabel *result_, *rosterState_;
	QTableWidget *list_;
	bool filling_ = false;
	int currentRow() const;
	QPushButton *add_, *active_, *remove_, *dual_, *show_ = nullptr;
	QCheckBox *roster_;
	QLineEdit *me_;
};
