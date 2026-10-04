// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <QAbstractButton>

namespace bmagicam::ui {

// A small rounded toggle, filled while it is on: Auto on Brightness, Warmth and Focus (docs/ui.md, "Controls").
class Chip : public QAbstractButton {
public:
	explicit Chip(const QString &text, QWidget *parent = nullptr);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override { return sizeHint(); }

protected:
	void paintEvent(QPaintEvent *event) override;
};

// A parameter tile, after the strip in Blackmagic Camera: a small label over a large value, an "A" badge while the
// camera sets the value itself, filled with the highlight color while it is selected.
class Tile : public QAbstractButton {
public:
	Tile(const QString &label, QWidget *parent = nullptr);

	void set_value(const QString &value);
	void set_automatic(bool automatic);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	QFont value_font() const;
	QFont label_font() const;

	QString label_;
	QString value_;
	bool automatic_ = false;
};

} // namespace bmagicam::ui
