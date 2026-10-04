// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "frontend.hpp"

#include "add-camera-wizard.hpp"
#include "beauty-panel.hpp"
#include "remote-dialog.hpp"
#include "controls-dock.hpp"
#include "setup-guide.hpp"
#include "../filter/beautify-filter.hpp"
#include "../remote/remote-api.hpp"
#include "../remote/remote-server.hpp"
#include "../remote/remote-settings.hpp"
#include "../source/camera-source.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QMainWindow>

namespace bmagicam::ui {

namespace {

// Dialogs from a filter's properties open over the window that shows them
QWidget *dialog_parent()
{
	QWidget *window = QApplication::activeWindow();
	return window ? window : static_cast<QWidget *>(obs_frontend_get_main_window());
}

// Remote Control starts once OBS has loaded its sources, follows the theme, and stops before OBS exits
void frontend_event(enum obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		remote::set_theme(theme_palette());
		remote::Server::instance().apply(load_remote_settings());
		break;
	case OBS_FRONTEND_EVENT_THEME_CHANGED:
		remote::set_theme(theme_palette());
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		remote::Server::instance().stop();
		obs_frontend_remove_event_callback(frontend_event, nullptr);
		break;
	default:
		break;
	}
}

} // namespace

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

	set_beautify_actions({
		[] { return ask_beauty_style_name(dialog_parent()); },
		[](const std::string &name) { return confirm_delete_beauty_style(dialog_parent(), name); },
	});

	obs_frontend_add_event_callback(frontend_event, nullptr);

	auto add_camera =
		static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(obs_module_text("Tools.AddCamera")));
	QObject::connect(add_camera, &QAction::triggered, [] {
		auto wizard = new AddCameraWizard(static_cast<QMainWindow *>(obs_frontend_get_main_window()));
		wizard->setAttribute(Qt::WA_DeleteOnClose);
		wizard->show();
	});

	auto remote_control =
		static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(obs_module_text("Tools.Remote")));
	QObject::connect(remote_control, &QAction::triggered, [] {
		auto dialog = new RemoteDialog(static_cast<QMainWindow *>(obs_frontend_get_main_window()));
		dialog->setAttribute(Qt::WA_DeleteOnClose);
		dialog->show();
	});
}

} // namespace bmagicam::ui
