// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "camera-source.hpp"

#include <obs-module.h>

namespace bmagicam {

namespace {

struct CameraSource {
	obs_source_t *source;
};

const char *camera_get_name(void *)
{
	return obs_module_text("Camera.Name");
}

void *camera_create(obs_data_t *, obs_source_t *source)
{
	return new CameraSource{source};
}

void camera_destroy(void *data)
{
	delete static_cast<CameraSource *>(data);
}

} // namespace

void register_camera_source()
{
	obs_source_info info = {};
	info.id = kCameraSourceId;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE;
	info.icon_type = OBS_ICON_TYPE_CAMERA;
	info.get_name = camera_get_name;
	info.create = camera_create;
	info.destroy = camera_destroy;
	obs_register_source(&info);
}

} // namespace bmagicam
