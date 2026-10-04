// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <QString>
#include <QVariantAnimation>
#include <QWidget>

#include <functional>
#include <vector>

class QLineEdit;

namespace bmagicam::ui {

// A horizontal scale under a fixed center needle, after Blackmagic Camera's own (docs/ui.md, "Controls"). The value
// shows in a box above the needle. Dragging the scale moves it from stop to stop, and so do the arrow keys and, while
// it has focus, the mouse wheel; a double click lets the user type a value. The stops are a list of values, such as the
// ISOs the phone supports, or a range in even steps. Drawn from the palette and the font only.
class Ruler : public QWidget {
public:
	explicit Ruler(QWidget *parent = nullptr);

	// The stops: a list of values in increasing order...
	void set_stops(std::vector<double> values);
	// ...or a range in even steps
	void set_range(double minimum, double maximum, double step);
	// Shows the stop nearest to the value, sliding there; does nothing while the user drags
	void set_value(double value);
	double value() const;

	// How a value reads, in the box and under the major ticks
	void set_format(std::function<QString(double)> format);
	// Leaves out the numbers, for Simple mode, with optional words under the two ends
	void set_plain(const QString &left, const QString &right);
	// Marks stops with a dot, such as the flicker-free shutters
	void set_marks(const std::vector<double> &values);
	// Distance between stops, in font heights
	void set_spacing(double em);

	// Hears changes the user makes: while dragging (final false), and once the change is complete
	std::function<void(double value, bool final)> changed;

	bool dragging() const { return dragging_; }

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void mouseDoubleClickEvent(QMouseEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;

private:
	int count() const;
	double value_at(int index) const;
	int nearest(double value) const;
	// A change by the user to the stop
	void choose(int index, bool final);
	void slide_to(double position);
	double spacing() const;
	int box_height() const;
	QString text_for(double value) const;
	void edit_value();

	std::vector<double> stops_;
	// The range, when there is no list
	double minimum_ = 0;
	double step_ = 1;
	int range_count_ = 1;

	int index_ = 0;
	// The fractional stop under the needle
	double position_ = 0;
	bool dragging_ = false;
	bool moved_ = false;
	double drag_x_ = 0;
	double drag_position_ = 0;
	QVariantAnimation slide_;

	std::function<QString(double)> format_;
	bool plain_ = false;
	QString left_;
	QString right_;
	std::vector<int> marks_;
	std::vector<double> mark_values_;
	double spacing_em_ = 1.0;
	QLineEdit *editor_ = nullptr;
};

} // namespace bmagicam::ui
