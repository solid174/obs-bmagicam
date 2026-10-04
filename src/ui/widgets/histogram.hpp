// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include "../../source/histogram.hpp"

#include <QWidget>

namespace bmagicam::ui {

// The luma and RGB histogram of the received picture, like the one in Blackmagic Camera, for judging exposure.
class HistogramView : public QWidget {
public:
	explicit HistogramView(QWidget *parent = nullptr);

	void set_histogram(const Histogram &histogram);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	Histogram histogram_;
};

} // namespace bmagicam::ui
