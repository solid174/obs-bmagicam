// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "frontend.hpp"

#include "add-camera-wizard.hpp"
#include "controls-dock.hpp"
#include "setup-guide.hpp"
#include "../source/camera-source.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QAction>
#include <QDockWidget>
#include <QMainWindow>

namespace bmagicam::ui {

void load()
{
	auto dock = new ControlsDock();
	if (!obs_frontend_add_dock_by_id(kControlsDockId, obs_module_text("Dock.Title"), dock))
		delete dock;

	set_camera_source_actions({
		[](obs_source_t *source) {
			auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
			if (auto container = main_window ? main_window->findChild<QDockWidget *>(kControlsDockId)
							 : nullptr) {
				container->show();
				container->raise();
			}
			if (ControlsDock *controls = ControlsDock::instance())
				controls->show_source(source);
		},
		[] {
			auto guide = new SetupGuideDialog(static_cast<QMainWindow *>(obs_frontend_get_main_window()));
			guide->setAttribute(Qt::WA_DeleteOnClose);
			guide->show();
		},
	});

	auto add_camera =
		static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(obs_module_text("Tools.AddCamera")));
	QObject::connect(add_camera, &QAction::triggered, [] {
		auto wizard = new AddCameraWizard(static_cast<QMainWindow *>(obs_frontend_get_main_window()));
		wizard->setAttribute(Qt::WA_DeleteOnClose);
		wizard->show();
	});
}

} // namespace bmagicam::ui
