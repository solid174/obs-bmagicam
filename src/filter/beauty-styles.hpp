// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <array>
#include <string>
#include <vector>

namespace bmagicam {

// The advanced Beautify values, 0–100 each (BEA-2). The names are the JSON and settings keys, as in the API.
struct BeautyValues {
	int smoothing = 0;
	int texture = 0;
	int evening = 0;
	int sharpen = 0;
	int glow = 0;
	int mask_softness = 0;
	int detail_size = 0;

	bool operator==(const BeautyValues &other) const;
	bool operator!=(const BeautyValues &other) const { return !(*this == other); }
};

// Each value's key, in the order of the advanced sliders
inline constexpr std::array<const char *, 7> kBeautyKeys = {"smoothing", "texture",      "evening",   "sharpen",
							    "glow",      "maskSoftness", "detailSize"};
int &beauty_value(BeautyValues &values, size_t index);
int beauty_value(const BeautyValues &values, size_t index);

// A style: a full set of values with a name (BEA-3)
struct BeautyStyle {
	// Built-in styles use a fixed ID and a translated name; the user's are named by the user
	std::string id;
	bool builtin = false;
	BeautyValues values;
};

// Styles from JSON: {"styles": [{"id": "...", "values": {...}}]}. Broken entries are skipped.
std::vector<BeautyStyle> parse_styles(const std::string &text, bool builtin);
std::string write_styles(const std::vector<BeautyStyle> &styles);

// What the filter applies at a Beauty strength, 0–1 each. The Beauty slider sends each value through its own
// response curve, so the first half of the slider stays natural and the top reaches the full style (BEA-2).
struct BeautyAmounts {
	float smoothing = 0;
	// The share of the finest grain kept on smoothed skin
	float texture = 1;
	float evening = 0;
	float sharpen = 0;
	float glow = 0;
	// Shape, not strength: the same at every Beauty strength
	float mask_softness = 0;
	float detail_size = 0;
};
BeautyAmounts beauty_amounts(const BeautyValues &values, int strength);

} // namespace bmagicam
