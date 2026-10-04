// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "frontend.hpp"

#include "add-camera-wizard.hpp"
#include "controls-dock.hpp"
#include "dock-kit.hpp"
#include "setup-guide.hpp"
#include "../filter/beautify-filter.hpp"
#include "../filter/style-library.hpp"
#include "../source/camera-source.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QInputDialog>
#include <QMainWindow>
#include <QMessageBox>

namespace bmagicam::ui {

namespace {

// Dialogs from a filter's properties open over the window that shows them
QWidget *dialog_parent()
{
	QWidget *window = QApplication::activeWindow();
	return window ? window : static_cast<QWidget *>(obs_frontend_get_main_window());
}

std::string ask_style_name()
{
	QWidget *parent = dialog_parent();
	for (;;) {
		bool ok = false;
		const QString name = QInputDialog::getText(parent, text("Beautify.SaveStyle.Title"),
							   text("Beautify.SaveStyle.Name"), QLineEdit::Normal,
							   QString(), &ok)
					     .trimmed();
		if (!ok || name.isEmpty())
			return {};
		// The built-in styles' IDs and Custom are taken
		const std::string id = name.toStdString();
		if (id != kCustomStyle && !find_style(builtin_styles(), id))
			return id;
		QMessageBox::warning(parent, text("Beautify.SaveStyle.Title"),
				     text("Beautify.SaveStyle.Taken").arg(name));
	}
}

bool confirm_delete_style(const std::string &name)
{
	return QMessageBox::question(dialog_parent(), text("Beautify.DeleteStyle"),
				     text("Beautify.DeleteStyle.Question").arg(QString::fromStdString(name))) ==
	       QMessageBox::Yes;
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

	set_beautify_actions({ask_style_name, confirm_delete_style});

	auto add_camera =
		static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(obs_module_text("Tools.AddCamera")));
	QObject::connect(add_camera, &QAction::triggered, [] {
		auto wizard = new AddCameraWizard(static_cast<QMainWindow *>(obs_frontend_get_main_window()));
		wizard->setAttribute(Qt::WA_DeleteOnClose);
		wizard->show();
	});
}

} // namespace bmagicam::ui
