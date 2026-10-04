// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <QStringList>
#include <QVariantAnimation>
#include <QWidget>

#include <functional>
#include <vector>

namespace bmagicam::ui {

// Joined buttons of which one is selected (docs/ui.md, "Controls"): stabilization Off and On, the lenses, the looks.
// The highlight slides to a new selection. Items that do not fit in one row wrap into more rows of equal buttons.
class Segmented : public QWidget {
public:
	explicit Segmented(QWidget *parent = nullptr);

	void set_items(const QStringList &items, const QStringList &tooltips = {});
	int count() const { return static_cast<int>(items_.size()); }
	// -1 selects nothing
	void set_selected(int index);
	int selected() const { return selected_; }
	void set_item_enabled(int index, bool enabled);

	// Hears a selection the user makes
	std::function<void(int index)> activated;

	bool hasHeightForWidth() const override { return true; }
	int heightForWidth(int width) const override;
	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	bool event(QEvent *event) override;
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void leaveEvent(QEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;

private:
	int columns_for(int width) const;
	std::vector<QRectF> layout(int width) const;
	int item_at(const QPointF &point) const;
	void choose(int index);
	void move_highlight(bool animate);

	QStringList items_;
	QStringList tooltips_;
	std::vector<bool> enabled_;
	int selected_ = -1;
	int pressed_ = -1;
	int hovered_ = -1;
	QRectF highlight_;
	QVariantAnimation slide_;
};

} // namespace bmagicam::ui
