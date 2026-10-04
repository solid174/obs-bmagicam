// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <QDialog>

namespace bmagicam::ui {

// The steps on the phone with their screenshots (SET-1), for the wizard's first page and the Setup guide.
QWidget *setup_steps(QWidget *parent);

// The Setup guide from an iPhone Camera's properties: the steps, and whether a phone is found.
class SetupGuideDialog : public QDialog {
public:
	explicit SetupGuideDialog(QWidget *parent = nullptr);
	~SetupGuideDialog() override;

private:
	int listener_ = 0;
};

} // namespace bmagicam::ui
