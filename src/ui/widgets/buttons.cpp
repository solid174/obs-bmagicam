// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "buttons.hpp"

#include <QPainter>

#include <algorithm>

namespace bmagicam::ui {

namespace {

// The font scaled by the factor, whether the theme gives its size in points or pixels
QFont scaled(QFont font, double factor)
{
	if (font.pointSizeF() > 0)
		font.setPointSizeF(font.pointSizeF() * factor);
	else if (font.pixelSize() > 0)
		font.setPixelSize(static_cast<int>(font.pixelSize() * factor + 0.5));
	return font;
}

} // namespace

Chip::Chip(const QString &text, QWidget *parent) : QAbstractButton(parent)
{
	setText(text);
	setCheckable(true);
	setFocusPolicy(Qt::StrongFocus);
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QSize Chip::sizeHint() const
{
	const int fh = fontMetrics().height();
	return {fontMetrics().horizontalAdvance(text()) + fh * 3 / 2, fh * 3 / 2};
}

void Chip::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	const QPalette::ColorGroup group = isEnabled() ? QPalette::Active : QPalette::Disabled;
	const QRectF area = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
	const double radius = area.height() / 2;

	if (isChecked()) {
		painter.setPen(Qt::NoPen);
		painter.setBrush(palette().color(group, QPalette::Highlight));
	} else {
		painter.setPen(QPen(palette().color(group, underMouse() ? QPalette::Highlight : QPalette::Mid), 1.0));
		painter.setBrush(palette().color(group, QPalette::Button));
	}
	painter.drawRoundedRect(area, radius, radius);
	if (hasFocus()) {
		painter.setPen(
			QPen(palette().color(group, isChecked() ? QPalette::HighlightedText : QPalette::Highlight), 1.0,
			     Qt::DotLine));
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(area.adjusted(2, 2, -2, -2), radius - 2, radius - 2);
	}
	painter.setPen(palette().color(group, isChecked() ? QPalette::HighlightedText : QPalette::ButtonText));
	painter.drawText(area, Qt::AlignCenter, text());
}

Tile::Tile(const QString &label, QWidget *parent) : QAbstractButton(parent), label_(label)
{
	setCheckable(true);
	setFocusPolicy(Qt::StrongFocus);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	setAccessibleName(label);
}

QFont Tile::label_font() const
{
	return scaled(font(), 0.8);
}

QFont Tile::value_font() const
{
	QFont large = scaled(font(), 1.25);
	large.setBold(true);
	return large;
}

void Tile::set_value(const QString &value)
{
	if (value == value_)
		return;
	value_ = value;
	updateGeometry();
	update();
}

void Tile::set_automatic(bool automatic)
{
	if (automatic == automatic_)
		return;
	automatic_ = automatic;
	update();
}

QSize Tile::sizeHint() const
{
	const QFontMetrics label(label_font());
	const QFontMetrics value(value_font());
	const int fh = fontMetrics().height();
	const int width = std::max(label.horizontalAdvance(label_) + fh * 3 / 2,
				   value.horizontalAdvance(value_.isEmpty() ? QStringLiteral("0000") : value_)) +
			  fh;
	return {width, label.height() + value.height() + fh / 2};
}

QSize Tile::minimumSizeHint() const
{
	return {fontMetrics().height() * 3, sizeHint().height()};
}

void Tile::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	const QPalette::ColorGroup group = isEnabled() ? QPalette::Active : QPalette::Disabled;
	const int fh = fontMetrics().height();
	const QRectF area = QRectF(rect()).adjusted(1, 1, -1, -1);
	const double radius = fh * 0.3;

	if (isChecked()) {
		painter.setPen(Qt::NoPen);
		painter.setBrush(palette().color(group, QPalette::Highlight));
		painter.drawRoundedRect(area, radius, radius);
	} else if (underMouse() || hasFocus()) {
		painter.setPen(QPen(palette().color(group, QPalette::Highlight), 1.0));
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(area, radius, radius);
	}

	const QColor label_color =
		palette().color(group, isChecked() ? QPalette::HighlightedText : QPalette::PlaceholderText);
	const QColor value_color = palette().color(group, isChecked() ? QPalette::HighlightedText : QPalette::Text);
	const QFontMetrics label_metrics(label_font());

	painter.setFont(label_font());
	painter.setPen(label_color);
	const QRectF label_area(area.left(), area.top() + fh * 0.15, area.width(), label_metrics.height());
	painter.drawText(label_area, Qt::AlignHCenter | Qt::AlignTop, label_);

	if (automatic_) {
		// The "A" badge after the label
		const double size = label_metrics.height() * 0.9;
		const double x = label_area.center().x() + label_metrics.horizontalAdvance(label_) / 2.0 + fh * 0.2;
		const QRectF badge(x, label_area.top() + (label_metrics.height() - size) / 2, size, size);
		painter.setPen(QPen(label_color, 1.0));
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(badge, size * 0.25, size * 0.25);
		painter.drawText(badge, Qt::AlignCenter, QStringLiteral("A"));
	}

	painter.setFont(value_font());
	painter.setPen(value_color);
	const QRectF value_area(area.left(), label_area.bottom(), area.width(), area.bottom() - label_area.bottom());
	painter.drawText(value_area, Qt::AlignCenter,
			 QFontMetrics(value_font()).elidedText(value_, Qt::ElideRight, static_cast<int>(area.width())));
}

} // namespace bmagicam::ui
