// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "frontend.hpp"

#include "add-camera-wizard.hpp"
#include "controls-dock.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QAction>
#include <QMainWindow>

namespace bmagicam::ui {

void load()
{
	auto dock = new ControlsDock();
	if (!obs_frontend_add_dock_by_id(kControlsDockId, obs_module_text("Dock.Title"), dock))
		delete dock;

	auto add_camera =
		static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(obs_module_text("Tools.AddCamera")));
	QObject::connect(add_camera, &QAction::triggered, [] {
		auto wizard = new AddCameraWizard(static_cast<QMainWindow *>(obs_frontend_get_main_window()));
		wizard->setAttribute(Qt::WA_DeleteOnClose);
		wizard->show();
	});
}

} // namespace bmagicam::ui
