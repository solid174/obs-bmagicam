// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "flow-layout.hpp"

#include <QWidget>

#include <algorithm>

namespace bmagicam::ui {

FlowLayout::FlowLayout(QWidget *parent) : QLayout(parent)
{
	setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout()
{
	while (QLayoutItem *item = takeAt(0))
		delete item;
}

void FlowLayout::addItem(QLayoutItem *item)
{
	items_.append(item);
}

int FlowLayout::count() const
{
	return static_cast<int>(items_.size());
}

QLayoutItem *FlowLayout::itemAt(int index) const
{
	return index >= 0 && index < items_.size() ? items_.at(index) : nullptr;
}

QLayoutItem *FlowLayout::takeAt(int index)
{
	return index >= 0 && index < items_.size() ? items_.takeAt(index) : nullptr;
}

Qt::Orientations FlowLayout::expandingDirections() const
{
	return {};
}

bool FlowLayout::hasHeightForWidth() const
{
	return true;
}

int FlowLayout::heightForWidth(int width) const
{
	return arrange(QRect(0, 0, width, 0), true);
}

QSize FlowLayout::minimumSize() const
{
	QSize size;
	for (const QLayoutItem *item : items_)
		size = size.expandedTo(item->minimumSize());
	const QMargins margins = contentsMargins();
	return size + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
}

QSize FlowLayout::sizeHint() const
{
	return minimumSize();
}

void FlowLayout::setGeometry(const QRect &rect)
{
	QLayout::setGeometry(rect);
	arrange(rect, false);
}

int FlowLayout::gap() const
{
	const int value = spacing();
	if (value >= 0)
		return value;
	const QWidget *owner = parentWidget();
	return owner ? owner->fontMetrics().height() / 3 : 4;
}

int FlowLayout::arrange(const QRect &rect, bool measure) const
{
	const QMargins margins = contentsMargins();
	const QRect area = rect.adjusted(margins.left(), margins.top(), -margins.right(), -margins.bottom());
	int x = area.x();
	int y = area.y();
	int line_height = 0;
	for (QLayoutItem *item : items_) {
		if (item->isEmpty())
			continue;
		const QSize size = item->sizeHint();
		if (x > area.x() && x + size.width() > area.right() + 1) {
			x = area.x();
			y += line_height + gap();
			line_height = 0;
		}
		if (!measure)
			item->setGeometry(QRect(QPoint(x, y), size));
		x += size.width() + gap();
		line_height = std::max(line_height, size.height());
	}
	return y + line_height - rect.y() + margins.bottom();
}

} // namespace bmagicam::ui
