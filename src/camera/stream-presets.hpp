// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <string>
#include <vector>

namespace bmagicam {

// A stream preset (CAM-3): the camera's video format, which the stream follows, and the stream's bitrate.
struct StreamPreset {
	// Stored in the source's settings
	const char *id;
	// The profile's name on the phone; also what the user sees there
	const char *profile;
	// The camera's video format as the phone names it
	const char *video_format;
	int width;
	int height;
	int fps;
	int bitrate;
	// Locale key of the description shown with the preset
	const char *description_key;
};

const std::vector<StreamPreset> &stream_presets();

// The preset with the given ID, or the default one.
const StreamPreset &stream_preset(const std::string &id);

} // namespace bmagicam
