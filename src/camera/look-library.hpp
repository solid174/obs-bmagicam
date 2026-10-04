// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include "looks.hpp"

namespace bmagicam {

class CameraControls;

// The built-in looks, from the plugin's data folder (docs/architecture.md, "Looks")
const std::vector<Look> &builtin_looks();
// The user's looks, from looks.json in the plugin's config folder (LOOK-4)
std::vector<Look> user_looks();
// Built-in looks first, then the user's
std::vector<Look> all_looks();
// The look with the ID; null when there is none
const Look *find_look(const std::vector<Look> &looks, const std::string &id);

// Saves the values as the user's look, replacing one with the same name
bool save_user_look(const std::string &name, const nlohmann::json &values);
bool rename_user_look(const std::string &from, const std::string &to);
bool delete_user_look(const std::string &name);

// The phone's color correction now, complete, or null while part of it is not known
nlohmann::json current_color(const CameraControls &controls);
// Writes color values to the phone
void apply_color(CameraControls &controls, const nlohmann::json &values);

} // namespace bmagicam
