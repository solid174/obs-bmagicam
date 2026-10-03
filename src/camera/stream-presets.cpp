// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "stream-presets.hpp"

namespace bmagicam {

const std::vector<StreamPreset> &stream_presets()
{
	// The first is the default. Bitrates from docs/ui.md: 12 Mb/s at 1080p60 measured clean on the test hotspot.
	static const std::vector<StreamPreset> presets = {
		{"1080p60-high", "1080p60 High", "1920x1080p60", 1920, 1080, 60, 12'000'000, "Preset.1080p60High"},
		{"1080p60-balanced", "1080p60 Balanced", "1920x1080p60", 1920, 1080, 60, 8'000'000,
		 "Preset.1080p60Balanced"},
		{"1080p30", "1080p30", "1920x1080p30", 1920, 1080, 30, 6'000'000, "Preset.1080p30"},
		{"720p60", "720p60", "1280x720p60", 1280, 720, 60, 5'000'000, "Preset.720p60"},
		{"4k30", "4K30", "3840x2160p30", 3840, 2160, 30, 20'000'000, "Preset.4K30"},
		{"4k60", "4K60", "3840x2160p60", 3840, 2160, 60, 30'000'000, "Preset.4K60"},
	};
	return presets;
}

const StreamPreset &stream_preset(const std::string &id)
{
	for (const StreamPreset &preset : stream_presets()) {
		if (id == preset.id)
			return preset;
	}
	return stream_presets().front();
}

} // namespace bmagicam
