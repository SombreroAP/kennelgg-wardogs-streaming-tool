#pragma once
#include <QLayout>
#include <QStyle>
#include <QWidget>
#include <QList>
#include <algorithm>

/// Lays its widgets out left to right and wraps them onto the next line when the dock is narrow:
/// a row of squad-mate buttons that never pushes the dock wider than OBS gives it.
class FlowLayout : public QLayout {
public:
	explicit FlowLayout(QWidget *parent = nullptr, int spacing = 4) : QLayout(parent), space_(spacing)
	{
		setContentsMargins(0, 0, 0, 0);
	}
	~FlowLayout() override
	{
		while (QLayoutItem *it = takeAt(0))
			delete it;
	}
	void addItem(QLayoutItem *item) override { items_.append(item); }
	int count() const override { return (int)items_.size(); }
	QLayoutItem *itemAt(int i) const override { return i >= 0 && i < items_.size() ? items_[i] : nullptr; }
	QLayoutItem *takeAt(int i) override { return i >= 0 && i < items_.size() ? items_.takeAt(i) : nullptr; }
	Qt::Orientations expandingDirections() const override { return {}; }
	bool hasHeightForWidth() const override { return true; }
	int heightForWidth(int w) const override { return place(QRect(0, 0, w, 0), false); }
	void setGeometry(const QRect &r) override
	{
		QLayout::setGeometry(r);
		place(r, true);
	}
	QSize sizeHint() const override { return minimumSize(); }
	QSize minimumSize() const override
	{
		QSize s;
		for (QLayoutItem *it : items_)
			s = s.expandedTo(it->minimumSize());
		const QMargins m = contentsMargins();
		return s + QSize(m.left() + m.right(), m.top() + m.bottom());
	}

private:
	QList<QLayoutItem *> items_;
	int space_;
	int place(const QRect &r, bool apply) const
	{
		const QMargins m = contentsMargins();
		QRect area = r.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
		int x = area.x(), y = area.y(), line = 0;
		for (QLayoutItem *it : items_) {
			// isHidden, not isVisible: before the dock is on screen nothing is visible, yet it has to be laid out
			if (it->widget() && it->widget()->isHidden())
				continue;
			QSize sz = it->sizeHint();
			int next = x + sz.width() + space_;
			if (next - space_ > area.right() + 1 && line > 0) {
				x = area.x();
				y += line + space_;
				next = x + sz.width() + space_;
				line = 0;
			}
			if (apply)
				it->setGeometry(QRect(QPoint(x, y), sz));
			x = next;
			line = std::max(line, sz.height());
		}
		return y + line - r.y() + m.bottom();
	}
};
