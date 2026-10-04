// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace bmagicam {

// Fields of the phone's JSON values, with a fallback when a field is missing or of another type.
double number_at(const nlohmann::json &object, const char *key, double fallback = 0);
std::string string_at(const nlohmann::json &object, const char *key);
bool bool_at(const nlohmann::json &object, const char *key, bool fallback = false);
std::vector<double> numbers_at(const nlohmann::json &object, const char *key);
std::vector<std::string> strings_at(const nlohmann::json &object, const char *key);
// The "normalised" field, which some answers also carry as "normalized"; writes must use "normalised"
// (docs/camera-api.md, Quirks)
double normalized_at(const nlohmann::json &object, double fallback = 0);
// The shutter as a speed (1/x), also when the phone reports it as an angle
double shutter_speed(const nlohmann::json &shutter, double fps);
// The frame rate of a /system/videoFormat value
double frame_rate(const nlohmann::json &format);

} // namespace bmagicam
