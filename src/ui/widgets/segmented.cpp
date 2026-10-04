// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "segmented.hpp"

#include <QEasingCurve>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace bmagicam::ui {

namespace {

constexpr int kSlideMs = 150;

} // namespace

Segmented::Segmented(QWidget *parent) : QWidget(parent)
{
	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking(true);
	QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	policy.setHeightForWidth(true);
	setSizePolicy(policy);
	slide_.setDuration(kSlideMs);
	slide_.setEasingCurve(QEasingCurve::OutCubic);
	connect(&slide_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
		highlight_ = value.toRectF();
		update();
	});
}

void Segmented::set_items(const QStringList &items, const QStringList &tooltips)
{
	if (items == items_ && tooltips == tooltips_)
		return;
	items_ = items;
	tooltips_ = tooltips;
	enabled_.assign(static_cast<size_t>(items.size()), true);
	if (selected_ >= count())
		selected_ = -1;
	updateGeometry();
	move_highlight(false);
}

void Segmented::set_selected(int index)
{
	index = index >= 0 && index < count() ? index : -1;
	if (index == selected_)
		return;
	const bool animate = selected_ >= 0 && index >= 0 && isVisible();
	selected_ = index;
	move_highlight(animate);
}

void Segmented::set_item_enabled(int index, bool enabled)
{
	if (index >= 0 && index < count() && enabled_[static_cast<size_t>(index)] != enabled) {
		enabled_[static_cast<size_t>(index)] = enabled;
		update();
	}
}

int Segmented::columns_for(int width) const
{
	if (items_.isEmpty())
		return 1;
	const int fh = fontMetrics().height();
	int widest = 0;
	for (const QString &item : items_)
		widest = std::max(widest, fontMetrics().horizontalAdvance(item));
	const int item_width = widest + fh;
	return std::clamp(width / std::max(1, item_width), 1, static_cast<int>(items_.size()));
}

std::vector<QRectF> Segmented::layout(int width) const
{
	std::vector<QRectF> rects;
	if (items_.isEmpty())
		return rects;
	const int fh = fontMetrics().height();
	int columns = columns_for(width);
	const int rows = (count() + columns - 1) / columns;
	// Rows share the items evenly, so the last row is not left with one
	columns = (count() + rows - 1) / rows;
	const double row_height = fh * 1.8;
	const double gap = fh * 0.3;
	for (int index = 0; index < count(); index++) {
		const int row = index / columns;
		const int column = index % columns;
		const int in_row = std::min(columns, count() - row * columns);
		const double item_width = static_cast<double>(width) / in_row;
		rects.emplace_back(column * item_width, row * (row_height + gap), item_width, row_height);
	}
	return rects;
}

int Segmented::heightForWidth(int width) const
{
	const std::vector<QRectF> rects = layout(width);
	return rects.empty() ? fontMetrics().height() * 2 : static_cast<int>(std::ceil(rects.back().bottom())) + 1;
}

QSize Segmented::sizeHint() const
{
	const int fh = fontMetrics().height();
	int total = 0;
	for (const QString &item : items_)
		total += fontMetrics().horizontalAdvance(item) + fh;
	const int width = std::max(total, fh * 6);
	return {width, heightForWidth(width)};
}

QSize Segmented::minimumSizeHint() const
{
	const int fh = fontMetrics().height();
	int widest = 0;
	for (const QString &item : items_)
		widest = std::max(widest, fontMetrics().horizontalAdvance(item));
	return {widest + fh, static_cast<int>(fh * 1.8)};
}

void Segmented::move_highlight(bool animate)
{
	const std::vector<QRectF> rects = layout(width());
	const QRectF target = selected_ >= 0 && selected_ < static_cast<int>(rects.size())
				      ? rects[static_cast<size_t>(selected_)]
				      : QRectF();
	slide_.stop();
	if (animate && !highlight_.isNull() && !target.isNull()) {
		slide_.setStartValue(highlight_);
		slide_.setEndValue(target);
		slide_.start();
	} else {
		highlight_ = target;
		update();
	}
}

void Segmented::resizeEvent(QResizeEvent *)
{
	move_highlight(false);
}

int Segmented::item_at(const QPointF &point) const
{
	const std::vector<QRectF> rects = layout(width());
	for (size_t index = 0; index < rects.size(); index++) {
		if (rects[index].contains(point))
			return static_cast<int>(index);
	}
	return -1;
}

void Segmented::choose(int index)
{
	if (index < 0 || index >= count() || !enabled_[static_cast<size_t>(index)])
		return;
	set_selected(index);
	if (activated)
		activated(index);
}

bool Segmented::event(QEvent *event)
{
	if (event->type() == QEvent::ToolTip) {
		auto help = static_cast<QHelpEvent *>(event);
		const int index = item_at(help->pos());
		if (index >= 0 && index < tooltips_.size() && !tooltips_[index].isEmpty())
			QToolTip::showText(help->globalPos(), tooltips_[index], this);
		else if (!toolTip().isEmpty())
			QToolTip::showText(help->globalPos(), toolTip(), this);
		else
			QToolTip::hideText();
		return true;
	}
	return QWidget::event(event);
}

void Segmented::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	const int fh = fontMetrics().height();
	const double radius = fh * 0.35;
	const QPalette::ColorGroup group = isEnabled() ? QPalette::Active : QPalette::Disabled;
	const std::vector<QRectF> rects = layout(width());

	// One joined background per row
	painter.setPen(QPen(palette().color(group, QPalette::Mid), 1.0));
	painter.setBrush(palette().color(group, QPalette::Button));
	for (size_t index = 0; index < rects.size();) {
		QRectF row = rects[index];
		size_t next = index + 1;
		while (next < rects.size() && rects[next].top() == row.top())
			row = row.united(rects[next++]);
		painter.drawRoundedRect(row.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
		for (size_t inner = index + 1; inner < next; inner++) {
			const double x = rects[inner].left();
			painter.drawLine(QPointF(x, row.top() + fh * 0.35), QPointF(x, row.bottom() - fh * 0.35));
		}
		index = next;
	}

	if (!highlight_.isNull()) {
		painter.setPen(Qt::NoPen);
		painter.setBrush(palette().color(group, QPalette::Highlight));
		painter.drawRoundedRect(highlight_.adjusted(1.5, 1.5, -1.5, -1.5), radius, radius);
	}

	for (size_t index = 0; index < rects.size(); index++) {
		const bool chosen = static_cast<int>(index) == selected_;
		const bool enabled = isEnabled() && enabled_[index];
		const QPalette::ColorGroup item_group = enabled ? QPalette::Active : QPalette::Disabled;
		if (!chosen && enabled && static_cast<int>(index) == hovered_) {
			painter.setPen(QPen(palette().color(item_group, QPalette::Highlight), 1.0));
			painter.setBrush(Qt::NoBrush);
			painter.drawRoundedRect(rects[index].adjusted(1.5, 1.5, -1.5, -1.5), radius, radius);
		}
		painter.setPen(palette().color(item_group, chosen ? QPalette::HighlightedText : QPalette::ButtonText));
		const QString text = fontMetrics().elidedText(items_[static_cast<int>(index)], Qt::ElideRight,
							      static_cast<int>(rects[index].width() - fh * 0.4));
		painter.drawText(rects[index], Qt::AlignCenter, text);
	}

	if (hasFocus() && selected_ >= 0 && selected_ < static_cast<int>(rects.size())) {
		QPen pen(palette().color(group, QPalette::HighlightedText), 1.0, Qt::DotLine);
		painter.setPen(pen);
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(rects[static_cast<size_t>(selected_)].adjusted(3.5, 3.5, -3.5, -3.5), radius,
					radius);
	}
}

void Segmented::mousePressEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton)
		pressed_ = item_at(event->position());
}

void Segmented::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton && pressed_ >= 0 && item_at(event->position()) == pressed_)
		choose(pressed_);
	pressed_ = -1;
}

void Segmented::mouseMoveEvent(QMouseEvent *event)
{
	const int index = item_at(event->position());
	if (index != hovered_) {
		hovered_ = index;
		update();
	}
}

void Segmented::leaveEvent(QEvent *)
{
	hovered_ = -1;
	update();
}

void Segmented::keyPressEvent(QKeyEvent *event)
{
	int target = selected_;
	if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Up)
		target = selected_ <= 0 ? 0 : selected_ - 1;
	else if (event->key() == Qt::Key_Right || event->key() == Qt::Key_Down)
		target = std::min(count() - 1, selected_ + 1);
	else {
		QWidget::keyPressEvent(event);
		return;
	}
	choose(target);
}

} // namespace bmagicam::ui
