// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "ruler.hpp"

#include <QEasingCurve>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace bmagicam::ui {

namespace {

constexpr int kSlideMs = 150;
// Ticks closer than this are thinned out
constexpr double kMinTickPx = 4.0;
// Room between labels, in font heights
constexpr double kLabelGapEm = 1.2;
// Room between major ticks without labels, in font heights
constexpr double kPlainMajorEm = 3.0;

// The next step in 1, 2, 5, 10, 20, 25, 50, 100...
int next_nice(int every)
{
	int scale = 1;
	while (every >= scale * 10)
		scale *= 10;
	for (int nice : {1, 2, 5, 10}) {
		if (nice * scale > every)
			return nice * scale;
	}
	return every * 2;
}

double parse_number(QString text)
{
	text = text.trimmed();
	if (text.startsWith("1/"))
		text = text.mid(2);
	text.remove(QRegularExpression("[^0-9.,+\\-]"));
	text.replace(',', '.');
	bool ok = false;
	const double value = text.toDouble(&ok);
	return ok ? value : std::nan("");
}

} // namespace

Ruler::Ruler(QWidget *parent) : QWidget(parent)
{
	setFocusPolicy(Qt::StrongFocus);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	setCursor(Qt::SizeHorCursor);
	slide_.setDuration(kSlideMs);
	slide_.setEasingCurve(QEasingCurve::OutCubic);
	connect(&slide_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
		position_ = value.toDouble();
		update();
	});
	format_ = [](double value) {
		return QString::number(value);
	};
}

int Ruler::count() const
{
	return stops_.empty() ? range_count_ : static_cast<int>(stops_.size());
}

double Ruler::value_at(int index) const
{
	return stops_.empty() ? minimum_ + index * step_ : stops_[static_cast<size_t>(index)];
}

int Ruler::nearest(double value) const
{
	if (stops_.empty())
		return std::clamp(static_cast<int>(std::lround((value - minimum_) / step_)), 0, count() - 1);
	const auto after = std::lower_bound(stops_.begin(), stops_.end(), value);
	if (after == stops_.begin())
		return 0;
	if (after == stops_.end())
		return count() - 1;
	const auto before = after - 1;
	return static_cast<int>((value - *before <= *after - value ? before : after) - stops_.begin());
}

void Ruler::set_stops(std::vector<double> values)
{
	if (values.empty())
		values.push_back(0);
	if (values == stops_)
		return;
	const double current = value();
	stops_ = std::move(values);
	index_ = nearest(current);
	position_ = index_;
	set_marks(mark_values_);
	update();
}

void Ruler::set_range(double minimum, double maximum, double step)
{
	step = step > 0 ? step : 1;
	const int range_count = std::max(1, static_cast<int>(std::lround((maximum - minimum) / step)) + 1);
	if (stops_.empty() && minimum == minimum_ && step == step_ && range_count == range_count_)
		return;
	const double current = value();
	stops_.clear();
	minimum_ = minimum;
	step_ = step;
	range_count_ = range_count;
	index_ = nearest(current);
	position_ = index_;
	set_marks(mark_values_);
	update();
}

double Ruler::value() const
{
	return value_at(std::clamp(index_, 0, count() - 1));
}

void Ruler::set_value(double value)
{
	if (dragging_ || editor_)
		return;
	const int index = nearest(value);
	if (index == index_)
		return;
	index_ = index;
	slide_to(index);
}

void Ruler::set_format(std::function<QString(double)> format)
{
	format_ = std::move(format);
	update();
}

void Ruler::set_plain(const QString &left, const QString &right)
{
	plain_ = true;
	left_ = left;
	right_ = right;
	updateGeometry();
	update();
}

void Ruler::set_marks(const std::vector<double> &values)
{
	mark_values_ = values;
	marks_.clear();
	for (double value : values) {
		const int index = nearest(value);
		if (std::abs(value_at(index) - value) < 1e-6)
			marks_.push_back(index);
	}
	update();
}

void Ruler::set_spacing(double em)
{
	spacing_em_ = em;
	update();
}

double Ruler::spacing() const
{
	return std::max(1.0, spacing_em_ * fontMetrics().height());
}

int Ruler::box_height() const
{
	return plain_ ? 0 : static_cast<int>(fontMetrics().height() * 1.45);
}

QString Ruler::text_for(double value) const
{
	return format_ ? format_(value) : QString::number(value);
}

QSize Ruler::sizeHint() const
{
	const int fh = fontMetrics().height();
	int height = 0;
	if (plain_)
		height = static_cast<int>(fh * 1.5) + (left_.isEmpty() && right_.isEmpty() ? 0 : fh);
	else
		height = box_height() + static_cast<int>(fh * 2.2);
	return {fh * 14, height};
}

QSize Ruler::minimumSizeHint() const
{
	return {fontMetrics().height() * 6, sizeHint().height()};
}

void Ruler::slide_to(double position)
{
	slide_.stop();
	if (!isVisible()) {
		position_ = position;
		update();
		return;
	}
	slide_.setStartValue(position_);
	slide_.setEndValue(position);
	slide_.start();
}

void Ruler::choose(int index, bool final)
{
	index = std::clamp(index, 0, count() - 1);
	const bool moved = index != index_;
	index_ = index;
	if (changed && (moved || final))
		changed(value_at(index), final);
}

void Ruler::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);

	const QPalette::ColorGroup group = isEnabled() ? QPalette::Active : QPalette::Disabled;
	const QColor tick_color = palette().color(group, QPalette::PlaceholderText);
	const QColor text_color = palette().color(group, QPalette::Text);
	const QColor needle_color = palette().color(group, QPalette::Highlight);

	const int fh = fontMetrics().height();
	const double center = width() / 2.0;
	const double step_px = spacing();
	const double top = plain_ ? fh * 0.25 : box_height() + fh * 0.3;
	const double major_length = fh * 0.7;
	const double minor_length = fh * 0.35;

	const int minor_every = std::max(1, static_cast<int>(std::ceil(kMinTickPx / step_px)));
	int major_every = minor_every;
	if (plain_) {
		while (major_every * step_px < fh * kPlainMajorEm && major_every < count())
			major_every = next_nice(major_every);
	} else {
		int widest = 0;
		for (int index : {0, count() / 2, count() - 1})
			widest = std::max(widest, fontMetrics().horizontalAdvance(text_for(value_at(index))));
		while (major_every * step_px < widest + fh * kLabelGapEm && major_every < count())
			major_every = next_nice(major_every);
	}

	const int first = std::max(0, static_cast<int>(std::floor(position_ - center / step_px)) - 1);
	const int last = std::min(count() - 1, static_cast<int>(std::ceil(position_ + center / step_px)) + 1);
	for (int index = first; index <= last; index++) {
		const bool major = index % major_every == 0;
		if (!major && index % minor_every != 0)
			continue;
		const double x = center + (index - position_) * step_px;
		painter.setPen(QPen(tick_color, major ? 1.5 : 1.0));
		painter.drawLine(QPointF(x, top), QPointF(x, top + (major ? major_length : minor_length)));
		if (major && !plain_) {
			// Only labels that fit whole: a cut one reads as another number
			const QString label = text_for(value_at(index));
			const double half = fontMetrics().horizontalAdvance(label) / 2.0;
			if (x - half >= 0 && x + half <= width())
				painter.drawText(QRectF(x - major_every * step_px / 2, top + major_length + fh * 0.1,
							major_every * step_px, fh),
						 Qt::AlignHCenter | Qt::AlignTop, label);
		}
		if (std::find(marks_.begin(), marks_.end(), index) != marks_.end()) {
			painter.setPen(Qt::NoPen);
			painter.setBrush(needle_color);
			painter.drawEllipse(QPointF(x, top - fh * 0.18), fh * 0.12, fh * 0.12);
			painter.setBrush(Qt::NoBrush);
		}
	}

	if (plain_ && !(left_.isEmpty() && right_.isEmpty())) {
		painter.setPen(tick_color);
		const QRectF words(0, top + major_length + fh * 0.1, width(), fh);
		painter.drawText(words, Qt::AlignLeft | Qt::AlignTop, left_);
		painter.drawText(words, Qt::AlignRight | Qt::AlignTop, right_);
	}

	// The needle, and the value above it
	painter.setPen(QPen(needle_color, 2.0, Qt::SolidLine, Qt::RoundCap));
	painter.drawLine(QPointF(center, top - fh * 0.2), QPointF(center, top + major_length + fh * 0.15));

	if (!plain_ && !editor_) {
		const QString text = text_for(value());
		const double box_width = fontMetrics().horizontalAdvance(text) + fh;
		const QRectF box(center - box_width / 2, 1, box_width, box_height() - 2);
		painter.setPen(QPen(hasFocus() ? needle_color : tick_color, 1.0));
		painter.setBrush(palette().color(group, QPalette::Base));
		painter.drawRoundedRect(box, fh * 0.3, fh * 0.3);
		painter.setPen(text_color);
		painter.drawText(box, Qt::AlignCenter, text);
	}
}

void Ruler::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton) {
		QWidget::mousePressEvent(event);
		return;
	}
	setFocus(Qt::MouseFocusReason);
	slide_.stop();
	dragging_ = true;
	moved_ = false;
	drag_x_ = event->position().x();
	drag_position_ = position_;
}

void Ruler::mouseMoveEvent(QMouseEvent *event)
{
	if (!dragging_)
		return;
	const double dx = event->position().x() - drag_x_;
	if (!moved_ && std::abs(dx) < 3)
		return;
	moved_ = true;
	position_ = std::clamp(drag_position_ - dx / spacing(), 0.0, static_cast<double>(count() - 1));
	update();
	choose(static_cast<int>(std::lround(position_)), false);
}

void Ruler::mouseReleaseEvent(QMouseEvent *event)
{
	if (!dragging_ || event->button() != Qt::LeftButton)
		return;
	dragging_ = false;
	if (moved_) {
		slide_to(index_);
		choose(index_, true);
		return;
	}
	// A click beside the needle moves to the stop clicked
	const double offset = event->position().x() - width() / 2.0;
	if (event->position().y() > box_height() && std::abs(offset) > spacing() / 2) {
		choose(static_cast<int>(std::lround(position_ + offset / spacing())), true);
		slide_to(index_);
	}
}

void Ruler::mouseDoubleClickEvent(QMouseEvent *event)
{
	if (!plain_ && event->position().y() <= box_height())
		edit_value();
}

void Ruler::wheelEvent(QWheelEvent *event)
{
	// Scrolling through the dock never changes a setting (UI-3)
	if (!hasFocus()) {
		event->ignore();
		return;
	}
	const int steps = event->angleDelta().y() / 120;
	if (steps != 0) {
		choose(index_ + steps, true);
		slide_to(index_);
	}
	event->accept();
}

void Ruler::keyPressEvent(QKeyEvent *event)
{
	const int page = std::max(1, count() / 10);
	int target = index_;
	switch (event->key()) {
	case Qt::Key_Left:
	case Qt::Key_Down:
		target -= 1;
		break;
	case Qt::Key_Right:
	case Qt::Key_Up:
		target += 1;
		break;
	case Qt::Key_PageDown:
		target -= page;
		break;
	case Qt::Key_PageUp:
		target += page;
		break;
	case Qt::Key_Home:
		target = 0;
		break;
	case Qt::Key_End:
		target = count() - 1;
		break;
	case Qt::Key_Return:
	case Qt::Key_Enter:
		if (!plain_)
			edit_value();
		return;
	default:
		QWidget::keyPressEvent(event);
		return;
	}
	choose(target, true);
	slide_to(index_);
}

void Ruler::edit_value()
{
	if (editor_)
		return;
	const int fh = fontMetrics().height();
	editor_ = new QLineEdit(this);
	editor_->setAlignment(Qt::AlignCenter);
	editor_->setText(text_for(value()));
	editor_->setGeometry(static_cast<int>(width() / 2.0 - fh * 3), 0, fh * 6, box_height());
	editor_->selectAll();
	editor_->show();
	editor_->setFocus(Qt::OtherFocusReason);
	connect(editor_, &QLineEdit::editingFinished, this, [this] {
		if (!editor_)
			return;
		const double typed = parse_number(editor_->text());
		editor_->deleteLater();
		editor_ = nullptr;
		if (!std::isnan(typed)) {
			choose(nearest(typed), true);
			slide_to(index_);
		}
		setFocus(Qt::OtherFocusReason);
		update();
	});
	update();
}

} // namespace bmagicam::ui
