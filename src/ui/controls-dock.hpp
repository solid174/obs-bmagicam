// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <obs-frontend-api.h>
#include <obs.hpp>

#include <QTimer>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QScrollArea;
class QStackedWidget;
class QToolButton;

namespace bmagicam::ui {

class AdvancedPanel;
class CameraLink;
class SimplePanel;

// Contents of the Camera Controls dock (docs/ui.md, "Camera Controls dock"): a picker for the iPhone Camera sources in
// the scene collection (CTL-6), the chosen phone's state, Simple or Advanced controls, a menu and a status line.
class ControlsDock : public QWidget {
public:
	explicit ControlsDock(QWidget *parent = nullptr);
	~ControlsDock() override;

	// Shows this source's camera, for the Open Camera Controls button in its properties
	void show_source(obs_source_t *source);

	// The dock in the OBS window, if it was added
	static ControlsDock *instance();

private:
	static void source_list_changed(void *data, calldata_t *calldata);
	static void frontend_event(enum obs_frontend_event event, void *data);
	static void item_selected(void *data, calldata_t *calldata);

	void refresh_sources();
	void choose_source(const OBSWeakSource &source);
	void rebuild();
	void refresh_state();
	void set_advanced(bool advanced);
	void show_message(const QString &message);
	void follow_scene_selection();
	void open_menu();
	void reset_to_defaults();
	void restore_settings();
	void save_preset();
	// Runs a step on the phone in the background, then shows how it went
	void run_on_phone(const QString &running, std::function<bool()> step, const char *done_key,
			  const char *failed_key);

	QComboBox *picker_ = nullptr;
	QLabel *state_ = nullptr;
	QCheckBox *advanced_ = nullptr;
	QToolButton *menu_ = nullptr;
	QStackedWidget *pages_ = nullptr;
	QLabel *empty_ = nullptr;
	QLabel *waiting_ = nullptr;
	QScrollArea *scroll_ = nullptr;
	QLabel *status_line_ = nullptr;
	CameraLink *link_ = nullptr;
	SimplePanel *simple_ = nullptr;
	AdvancedPanel *advanced_panel_ = nullptr;

	OBSWeakSource source_;
	OBSWeakSource watched_scene_;
	QTimer state_timer_;
	QTimer message_timer_;
	QString message_;
};

} // namespace bmagicam::ui
