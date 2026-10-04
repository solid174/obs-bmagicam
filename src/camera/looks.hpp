// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace bmagicam {

// A look: color correction that the phone applies before compression (LOOK-2, LOOK-3). Its values are the seven
// /colorCorrection properties, keyed by the last part of their paths: "lift", "gamma", "gain", "offset",
// "contrast", "color" and "lumaContribution".
struct Look {
	// "studio" for a built-in look, the user's name for one of theirs
	std::string id;
	bool builtin = false;
	nlohmann::json values;
};

// The seven properties, by key
const std::vector<std::string> &color_keys();
// The path of a color property: "/colorCorrection/lift" for "lift"
std::string color_path(const std::string &key);
// Neutral color correction, which is also the Natural look
const nlohmann::json &neutral_color();
// The values with every field, missing ones taken from neutral
nlohmann::json complete_color(const nlohmann::json &values);
// Whether two colors are the same, within what the phone rounds to
bool same_color(const nlohmann::json &a, const nlohmann::json &b);

// Reads a looks file: {"looks": [{"id": ..., "values": {...}}]}, the user's with "name" instead of "id"
std::vector<Look> parse_looks(const std::string &text, bool builtin);
std::string write_looks(const std::vector<Look> &looks);

} // namespace bmagicam
