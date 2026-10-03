// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "frontend.hpp"

#include "controls-dock.hpp"
#include "../source/camera-source.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <obs.hpp>

#include <QAction>

#include <string>

namespace bmagicam::ui {

namespace {

std::string unique_source_name(const char *base)
{
	std::string name = base;
	for (int i = 2;; i++) {
		OBSSourceAutoRelease existing = obs_get_source_by_name(name.c_str());
		if (!existing)
			return name;
		name = std::string(base) + " " + std::to_string(i);
	}
}

// Adds an iPhone Camera source to the scene being edited: the preview scene in Studio Mode, otherwise the
// current scene.
void add_camera()
{
	OBSSourceAutoRelease scene_source = obs_frontend_preview_program_mode_active()
						    ? obs_frontend_get_current_preview_scene()
						    : obs_frontend_get_current_scene();
	obs_scene_t *scene = obs_scene_from_source(scene_source);
	if (!scene)
		return;

	std::string name = unique_source_name(obs_module_text("Camera.Name"));
	OBSSourceAutoRelease camera = obs_source_create(kCameraSourceId, name.c_str(), nullptr, nullptr);
	obs_scene_add(scene, camera);
}

} // namespace

void load()
{
	auto dock = new ControlsDock();
	if (!obs_frontend_add_dock_by_id(kControlsDockId, obs_module_text("Dock.Title"), dock))
		delete dock;

	auto add_camera_action =
		static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(obs_module_text("Tools.AddCamera")));
	QObject::connect(add_camera_action, &QAction::triggered, add_camera);
}

} // namespace bmagicam::ui
