// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include "beauty-styles.hpp"

#include <obs.h>

#include <functional>
#include <string>

namespace bmagicam {

inline constexpr const char *kBeautifyFilterId = "bmagicam_beautify";

// Settings keys besides the advanced values (kBeautyKeys), the same names as in the API
inline constexpr const char *kBeautyStyle = "style";
inline constexpr const char *kBeautyStrength = "strength";
inline constexpr const char *kBeautyShowMask = "showMask";
// The style of values that are no saved style
inline constexpr const char *kCustomStyle = "custom";
inline constexpr int kDefaultBeautyStrength = 50;

// The Beautify filter: skin retouching on any video source (BEA-1 to BEA-8)
void register_beautify_filter();

// What the buttons in the filter's properties open; the user interface provides them.
struct BeautifyActions {
	// Asks for a new style's name; empty when cancelled
	std::function<std::string()> ask_style_name;
	// Asks before deleting one of the user's styles
	std::function<bool(const std::string &name)> confirm_delete_style;
};
void set_beautify_actions(BeautifyActions actions);

bool is_beautify_filter(obs_source_t *source);
// The source's first Beautify filter with a new reference; null when it has none
obs_source_t *find_beautify_filter(obs_source_t *source);
// Adds a Beautify filter with the style at the strength, and returns it with a new reference
obs_source_t *add_beautify_filter(obs_source_t *source, const std::string &style, int strength);

// The advanced values in a filter's settings
BeautyValues beauty_values(obs_data_t *settings);
void set_beauty_values(obs_data_t *settings, const BeautyValues &values);
// Chooses a style: its ID and its values
void select_style(obs_data_t *settings, const BeautyStyle &style);

} // namespace bmagicam
