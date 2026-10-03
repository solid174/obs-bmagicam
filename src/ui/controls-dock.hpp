// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <QWidget>

namespace bmagicam::ui {

// Contents of the Camera Controls dock.
class ControlsDock : public QWidget {
public:
	explicit ControlsDock(QWidget *parent = nullptr);
};

} // namespace bmagicam::ui
