#include "ui/area-editor.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

AreaEditor::AreaEditor(QWidget *parent) : QWidget(parent)
{
	setMouseTracking(true);
	setMinimumHeight(240);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void AreaEditor::setFrame(const QImage &img)
{
	img_ = img;
	update();
}

void AreaEditor::setAreas(const QVector<Area> &areas)
{
	QString keep = selected();
	if (mode_ != None && sel_ >= 0 && sel_ < areas_.size()) {
		// mid-drag: take everything but the area under the mouse
		Area mine = areas_[sel_];
		areas_ = areas;
		for (auto &a : areas_)
			if (a.key == mine.key)
				a.r = mine.r;
	} else
		areas_ = areas;
	sel_ = -1;
	for (int i = 0; i < areas_.size(); i++)
		if (areas_[i].key == keep)
			sel_ = i;
	update();
}

void AreaEditor::select(const QString &key)
{
	for (int i = 0; i < areas_.size(); i++)
		if (areas_[i].key == key && sel_ != i) {
			sel_ = i;
			emit selectedChanged(key);
		}
	update();
}

QRect AreaEditor::imageRect() const
{
	double ar = img_.isNull() ? 16.0 / 9.0 : (double)img_.width() / std::max(1, img_.height());
	int w = width(), h = (int)(w / ar);
	if (h > height()) {
		h = height();
		w = (int)(h * ar);
	}
	return QRect((width() - w) / 2, (height() - h) / 2, w, h);
}

QPointF AreaEditor::toFrac(const QPointF &p) const
{
	QRect ir = imageRect();
	return QPointF(std::clamp((p.x() - ir.x()) / std::max(1, ir.width()), 0.0, 1.0),
		       std::clamp((p.y() - ir.y()) / std::max(1, ir.height()), 0.0, 1.0));
}

QRectF AreaEditor::toPix(const QRectF &f) const
{
	QRect ir = imageRect();
	return QRectF(ir.x() + f.x() * ir.width(), ir.y() + f.y() * ir.height(), f.width() * ir.width(),
		      f.height() * ir.height());
}

int AreaEditor::hitEdges(const QRectF &pix, const QPointF &p) const
{
	const double g = 7; // grab margin in pixels, inside and outside the edge
	if (!pix.adjusted(-g, -g, g, g).contains(p))
		return 0;
	int e = 0;
	if (std::abs(p.x() - pix.left()) <= g)
		e |= L;
	if (std::abs(p.x() - pix.right()) <= g)
		e |= R;
	if (std::abs(p.y() - pix.top()) <= g)
		e |= T;
	if (std::abs(p.y() - pix.bottom()) <= g)
		e |= B;
	return e;
}

/// Keeps a fixed-shape area its shape (in pixels of the game source), anchored on the edge that
/// is not being dragged, and inside the picture.
QRectF AreaEditor::fixAspect(QRectF r, int edges) const
{
	if (sel_ < 0 || areas_[sel_].aspect <= 0)
		return r;
	double srcAr = img_.isNull() ? 16.0 / 9.0 : (double)img_.width() / std::max(1, img_.height());
	double k = areas_[sel_].aspect / srcAr; // width / height in fractions
	bool byWidth = (edges & (L | R)) || !(edges & (T | B));
	if (byWidth) {
		double h = r.width() / k;
		if (edges & T)
			r.setTop(r.bottom() - h);
		else
			r.setHeight(h);
	} else {
		double w = r.height() * k;
		if (edges & L)
			r.setLeft(r.right() - w);
		else
			r.setWidth(w);
	}
	// too big for the picture: shrink, keeping the shape
	double s = std::min({1.0, 1.0 / std::max(1e-6, r.width()), 1.0 / std::max(1e-6, r.height())});
	if (s < 1.0)
		r.setSize(r.size() * s);
	return r;
}

void AreaEditor::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.fillRect(rect(), QColor(18, 21, 24));
	QRect ir = imageRect();
	if (img_.isNull()) {
		p.setPen(QColor(154, 158, 147));
		p.drawText(
			ir, Qt::AlignCenter | Qt::TextWordWrap,
			"No picture of the game yet.\nPick your game source under General, and have the game running.");
		p.setPen(QColor(58, 62, 59));
		p.drawRect(ir.adjusted(0, 0, -1, -1));
	} else
		p.drawImage(ir, img_);
	QFont f = font();
	f.setBold(true);
	f.setPointSizeF(std::max(7.0, f.pointSizeF() - 0.5));
	p.setFont(f);
	QFontMetrics fm(f);
	// the selected one last, so it is on top
	QVector<int> order;
	for (int i = 0; i < areas_.size(); i++)
		if (i != sel_)
			order << i;
	if (sel_ >= 0)
		order << sel_;
	for (int i : order) {
		const Area &a = areas_[i];
		QRectF r = toPix(a.r);
		bool s = i == sel_;
		QColor c = a.colour;
		if (!a.active)
			c.setAlpha(130);
		QColor fill = c;
		fill.setAlpha(s ? 46 : 22);
		p.fillRect(r, fill);
		QPen pen(c, s ? 2.5 : 1.5);
		if (!a.active)
			pen.setStyle(Qt::DashLine);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		p.drawRect(r);
		// its name on a tag at the top-left corner, inside the picture
		QString t = a.locked ? a.label + QString::fromUtf8("  \u2022 automatic") : a.label;
		int tw = fm.horizontalAdvance(t) + 10, th = fm.height() + 4;
		QRectF tag(r.left(), r.top() - th, tw, th);
		if (tag.top() < ir.top())
			tag.moveTop(r.top());
		if (tag.right() > ir.right())
			tag.moveRight(ir.right());
		QColor bg = c;
		bg.setAlpha(s ? 235 : 190);
		p.fillRect(tag, bg);
		p.setPen(QColor(18, 21, 24));
		p.drawText(tag, Qt::AlignCenter, t);
		if (s && !a.locked) {
			// handles on the corners
			p.setBrush(QColor(236, 231, 219));
			p.setPen(QPen(c, 1.5));
			for (QPointF h : {r.topLeft(), r.topRight(), r.bottomLeft(), r.bottomRight()})
				p.drawRect(QRectF(h.x() - 4, h.y() - 4, 8, 8));
		}
	}
}

void AreaEditor::mousePressEvent(QMouseEvent *ev)
{
	if (ev->button() != Qt::LeftButton)
		return;
	QPointF pos = ev->position();
	start_ = toFrac(pos);
	// the selected area's edges first, then whichever box is under the mouse (smallest wins, so a
	// small box inside a big one can still be picked), then drawing the selected one afresh
	if (sel_ >= 0 && !areas_[sel_].locked) {
		int e = hitEdges(toPix(areas_[sel_].r), pos);
		if (e) {
			mode_ = Resize;
			edges_ = e;
			orig_ = areas_[sel_].r;
			return;
		}
	}
	int best = -1;
	double bestArea = 1e9;
	for (int i = 0; i < areas_.size(); i++) {
		QRectF r = toPix(areas_[i].r);
		if (r.contains(pos) && r.width() * r.height() < bestArea) {
			best = i;
			bestArea = r.width() * r.height();
		}
	}
	if (best >= 0) {
		if (best != sel_) {
			sel_ = best;
			emit selectedChanged(areas_[sel_].key);
		}
		if (areas_[sel_].locked) {
			emit lockedTouched(areas_[sel_].key);
			update();
			return;
		}
		mode_ = Move;
		orig_ = areas_[sel_].r;
	} else if (sel_ >= 0 && areas_[sel_].locked && imageRect().contains(pos.toPoint())) {
		emit lockedTouched(areas_[sel_].key);
	} else if (sel_ >= 0 && imageRect().contains(pos.toPoint())) {
		mode_ = Draw;
		orig_ = areas_[sel_].r;
	}
	update();
}

void AreaEditor::mouseMoveEvent(QMouseEvent *ev)
{
	QPointF pos = ev->position();
	if (mode_ == None) {
		// the cursor says what a press would do
		int e = sel_ >= 0 && !areas_[sel_].locked ? hitEdges(toPix(areas_[sel_].r), pos) : 0;
		if ((e & (L | T)) == (L | T) || (e & (R | B)) == (R | B))
			setCursor(Qt::SizeFDiagCursor);
		else if ((e & (R | T)) == (R | T) || (e & (L | B)) == (L | B))
			setCursor(Qt::SizeBDiagCursor);
		else if (e & (L | R))
			setCursor(Qt::SizeHorCursor);
		else if (e & (T | B))
			setCursor(Qt::SizeVerCursor);
		else {
			bool over = false;
			for (const auto &a : areas_)
				if (toPix(a.r).contains(pos))
					over = true;
			setCursor(over ? Qt::SizeAllCursor : (sel_ >= 0 ? Qt::CrossCursor : Qt::ArrowCursor));
		}
		return;
	}
	if (sel_ < 0)
		return;
	QPointF f = toFrac(pos);
	QRectF r = orig_;
	const double minW = 0.01, minH = 0.01;
	if (mode_ == Move) {
		double dx = f.x() - start_.x(), dy = f.y() - start_.y();
		r.moveTo(std::clamp(orig_.x() + dx, 0.0, 1.0 - orig_.width()),
			 std::clamp(orig_.y() + dy, 0.0, 1.0 - orig_.height()));
	} else if (mode_ == Draw) {
		r = QRectF(start_, f).normalized();
		r.setWidth(std::max(r.width(), minW));
		r.setHeight(std::max(r.height(), minH));
		r = fixAspect(r, R | B);
	} else if (mode_ == Resize) {
		if (edges_ & L)
			r.setLeft(std::min(f.x(), orig_.right() - minW));
		if (edges_ & R)
			r.setRight(std::max(f.x(), orig_.left() + minW));
		if (edges_ & T)
			r.setTop(std::min(f.y(), orig_.bottom() - minH));
		if (edges_ & B)
			r.setBottom(std::max(f.y(), orig_.top() + minH));
		r = fixAspect(r, edges_);
	}
	// inside the picture
	if (r.left() < 0)
		r.moveLeft(0);
	if (r.top() < 0)
		r.moveTop(0);
	if (r.right() > 1)
		r.moveRight(1);
	if (r.bottom() > 1)
		r.moveBottom(1);
	areas_[sel_].r = r;
	update();
}

void AreaEditor::mouseReleaseEvent(QMouseEvent *ev)
{
	if (ev->button() != Qt::LeftButton || mode_ == None)
		return;
	int was = mode_;
	mode_ = None;
	edges_ = 0;
	if (sel_ >= 0 && (was != Move || areas_[sel_].r != orig_) && areas_[sel_].r.width() > 0.005)
		emit areaChanged(areas_[sel_].key, areas_[sel_].r);
	update();
}
