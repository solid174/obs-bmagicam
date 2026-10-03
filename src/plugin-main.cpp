// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include <obs-module.h>
#include <plugin-support.h>

#include "filter/beautify-filter.hpp"
#include "source/camera-source.hpp"
#include "stream/ffmpeg-check.hpp"
#include "ui/frontend.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")
OBS_MODULE_AUTHOR("solid174")

const char *obs_module_name(void)
{
	return obs_module_text("Plugin.Name");
}

const char *obs_module_description(void)
{
	return obs_module_text("Plugin.Description");
}

bool obs_module_load(void)
{
	// Logs now if the FFmpeg in OBS cannot run the stream receiver
	bmagicam::ffmpeg_usable();

	bmagicam::register_camera_source();
	bmagicam::register_beautify_filter();
	bmagicam::ui::load();

	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "plugin unloaded");
}
