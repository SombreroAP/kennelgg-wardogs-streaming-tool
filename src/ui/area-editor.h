#pragma once
#include <QColor>
#include <QImage>
#include <QRectF>
#include <QString>
#include <QVector>
#include <QWidget>

/// Every area the plugin and ClipHound read, drawn over one live picture of the game: click a box
/// (or pick it from the buttons above) to select it, drag inside it to move it, drag a corner or an
/// edge to resize it, or drag on empty picture to draw the selected one afresh. Areas are fractions
/// of the game source. An area with a fixed shape (the cash reader) keeps its shape in pixels.
class AreaEditor : public QWidget {
	Q_OBJECT
public:
	struct Area {
		QString key, label;
		QColor colour;
		QRectF r;            // fractions of the game source
		double aspect;       // width / height in pixels to keep, 0 = free
		bool active;         // drawn solid; an area whose feature is off is drawn faint
		bool locked = false; // can be picked but not moved: a press says so (lockedTouched)
	};
	explicit AreaEditor(QWidget *parent = nullptr);
	void setFrame(const QImage &img);
	void setAreas(const QVector<Area> &areas); // keeps the selection
	void select(const QString &key);
	QString selected() const { return sel_ >= 0 && sel_ < areas_.size() ? areas_[sel_].key : QString(); }
	QSize sizeHint() const override { return QSize(800, 450); }
	bool hasHeightForWidth() const override { return true; }
	int heightForWidth(int w) const override { return w * 9 / 16; }

signals:
	void areaChanged(const QString &key, QRectF r); // on release, after a move, resize or draw
	void selectedChanged(const QString &key);
	void lockedTouched(const QString &key); // a press on a locked area (it does not move)

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;

private:
	enum Mode { None, Move, Draw, Resize };
	enum Edge { L = 1, T = 2, R = 4, B = 8 };
	QImage img_;
	QVector<Area> areas_;
	int sel_ = -1;
	int mode_ = None, edges_ = 0;
	QPointF start_; // fraction where the drag began
	QRectF orig_;   // the area as it was when the drag began
	QRect imageRect() const;
	QPointF toFrac(const QPointF &p) const;
	QRectF toPix(const QRectF &f) const;
	int hitEdges(const QRectF &pix, const QPointF &p) const;
	QRectF fixAspect(QRectF r, int edges) const;
};
