// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <QWizard>

#include <string>

namespace bmagicam::ui {

// Tools → Add iPhone Camera...: walks through preparing the phone, finds it, asks for a stream preset and adds the
// camera to the current scene (docs/ui.md, "Add iPhone Camera wizard").
class AddCameraWizard : public QWizard {
public:
	explicit AddCameraWizard(QWidget *parent = nullptr);

	// What the pages chose
	struct Choice {
		std::string phone_id;
		std::string phone_name;
		std::string address;
		std::string preset;
		std::string look;
		// A Beautify style to attach the filter with; empty for none
		std::string beauty;
		// Set up for streaming once the phone is connected (CTL-8)
		bool set_up = true;
	};
	Choice choice;

	void accept() override;
};

} // namespace bmagicam::ui
