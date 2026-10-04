// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include "beauty-styles.hpp"

namespace bmagicam {

// The built-in styles, from the plugin's data folder
const std::vector<BeautyStyle> &builtin_styles();
// The user's styles, from beauty-styles.json in the plugin's config folder (BEA-3)
std::vector<BeautyStyle> user_styles();
// Built-in styles first, then the user's
std::vector<BeautyStyle> all_styles();
// The style with the ID; null when there is none
const BeautyStyle *find_style(const std::vector<BeautyStyle> &styles, const std::string &id);

// Saves the values as the user's style, replacing one with the same name
bool save_user_style(const std::string &name, const BeautyValues &values);
bool delete_user_style(const std::string &name);

} // namespace bmagicam
