// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <QLayout>
#include <QList>

namespace bmagicam::ui {

// Lays widgets out from left to right and wraps them into more lines when the width runs out, so rows of buttons fit
// a narrow dock in every language.
class FlowLayout : public QLayout {
public:
	explicit FlowLayout(QWidget *parent = nullptr);
	~FlowLayout() override;

	void addItem(QLayoutItem *item) override;
	int count() const override;
	QLayoutItem *itemAt(int index) const override;
	QLayoutItem *takeAt(int index) override;

	Qt::Orientations expandingDirections() const override;
	bool hasHeightForWidth() const override;
	int heightForWidth(int width) const override;
	QSize minimumSize() const override;
	QSize sizeHint() const override;
	void setGeometry(const QRect &rect) override;

private:
	int gap() const;
	// Places the items in the rectangle, or only measures; returns the height used
	int arrange(const QRect &rect, bool measure) const;

	QList<QLayoutItem *> items_;
};

} // namespace bmagicam::ui
