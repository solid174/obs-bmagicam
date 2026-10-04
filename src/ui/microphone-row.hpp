// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include "dock-kit.hpp"

#include <QTimer>

#include <memory>

class QComboBox;
class QLabel;
class QPushButton;

namespace bmagicam {
class MicrophoneSync;
}

namespace bmagicam::ui {

// The Microphone row (SYN-1, docs/ui.md): picks a microphone of the computer's, or another OBS audio source, and puts
// it in step with the iPhone's picture. Undo puts the previous Sync Offset back (SYN-2). Shown only when OBS has such
// a source.
class MicrophoneRow : public Row {
public:
	MicrophoneRow(PanelContext context, QWidget *parent = nullptr);
	~MicrophoneRow() override;

private:
	void refresh_sources();
	void start();
	void finish();
	void undo();

	PanelContext context_;
	QComboBox *microphones_;
	QPushButton *sync_;
	QPushButton *undo_;
	QLabel *result_;
	QTimer sources_timer_;
	QTimer listen_timer_;
	std::unique_ptr<MicrophoneSync> listening_;
	OBSWeakSource undo_source_;
	int64_t undo_offset_ = 0;
};

} // namespace bmagicam::ui
