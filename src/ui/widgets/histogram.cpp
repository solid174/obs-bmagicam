// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "histogram.hpp"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace bmagicam::ui {

HistogramView::HistogramView(QWidget *parent) : QWidget(parent)
{
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void HistogramView::set_histogram(const Histogram &histogram)
{
	histogram_ = histogram;
	update();
}

QSize HistogramView::sizeHint() const
{
	const int fh = fontMetrics().height();
	return {fh * 12, fh * 4};
}

QSize HistogramView::minimumSizeHint() const
{
	const int fh = fontMetrics().height();
	return {fh * 6, fh * 4};
}

void HistogramView::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	const QPalette::ColorGroup group = isEnabled() ? QPalette::Active : QPalette::Disabled;
	const int fh = fontMetrics().height();
	const QRectF area = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
	painter.setPen(QPen(palette().color(group, QPalette::Mid), 1.0));
	painter.setBrush(palette().color(group, QPalette::Base));
	painter.drawRoundedRect(area, fh * 0.3, fh * 0.3);
	if (histogram_.samples == 0)
		return;

	// The scale leaves out the end bins, where clipped highlights and crushed shadows pile up
	uint32_t peak = 1;
	for (const auto *channel : {&histogram_.luma, &histogram_.red, &histogram_.green, &histogram_.blue})
		peak = std::max(peak, *std::max_element(channel->begin() + 1, channel->end() - 1));

	const QRectF plot = area.adjusted(fh * 0.25, fh * 0.25, -fh * 0.25, -fh * 0.2);
	const auto path_for = [&](const std::array<uint32_t, Histogram::kBins> &bins) {
		QPainterPath path(QPointF(plot.left(), plot.bottom()));
		for (size_t bin = 0; bin < bins.size(); bin++) {
			const double x = plot.left() + plot.width() * (static_cast<double>(bin) + 0.5) / bins.size();
			const double level = std::min(1.0, static_cast<double>(bins[bin]) / peak);
			path.lineTo(x, plot.bottom() - plot.height() * level);
		}
		path.lineTo(plot.right(), plot.bottom());
		path.closeSubpath();
		return path;
	};

	// Red, green and blue are the picture's own channels, drawn in those colors as in the app; the rest follows the theme
	painter.setPen(Qt::NoPen);
	painter.setCompositionMode(QPainter::CompositionMode_Plus);
	painter.setBrush(QColor(200, 40, 40, 110));
	painter.drawPath(path_for(histogram_.red));
	painter.setBrush(QColor(40, 170, 40, 110));
	painter.drawPath(path_for(histogram_.green));
	painter.setBrush(QColor(50, 90, 220, 110));
	painter.drawPath(path_for(histogram_.blue));
	painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
	painter.setPen(QPen(palette().color(group, QPalette::Text), 1.2));
	painter.setBrush(Qt::NoBrush);
	painter.drawPath(path_for(histogram_.luma));
}

} // namespace bmagicam::ui
