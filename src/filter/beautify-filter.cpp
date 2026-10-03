// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "beautify-filter.hpp"

#include <obs-module.h>

namespace bmagicam {

namespace {

struct BeautifyFilter {
	obs_source_t *context;
};

const char *beautify_get_name(void *)
{
	return obs_module_text("Beautify.Name");
}

void *beautify_create(obs_data_t *, obs_source_t *context)
{
	return new BeautifyFilter{context};
}

void beautify_destroy(void *data)
{
	delete static_cast<BeautifyFilter *>(data);
}

void beautify_video_render(void *data, gs_effect_t *)
{
	obs_source_skip_video_filter(static_cast<BeautifyFilter *>(data)->context);
}

} // namespace

void register_beautify_filter()
{
	obs_source_info info = {};
	info.id = kBeautifyFilterId;
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = beautify_get_name;
	info.create = beautify_create;
	info.destroy = beautify_destroy;
	info.video_render = beautify_video_render;
	obs_register_source(&info);
}

} // namespace bmagicam
