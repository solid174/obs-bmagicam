// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "looks.hpp"

#include <cmath>

namespace bmagicam {

namespace {

// The phone keeps color values as 32-bit floats
constexpr double kTolerance = 0.002;

} // namespace

const std::vector<std::string> &color_keys()
{
	static const std::vector<std::string> keys = {"lift",  "gamma",           "gain", "offset", "contrast",
						      "color", "lumaContribution"};
	return keys;
}

std::string color_path(const std::string &key)
{
	return "/colorCorrection/" + key;
}

const nlohmann::json &neutral_color()
{
	static const nlohmann::json neutral = {
		{"lift", {{"red", 0.0}, {"green", 0.0}, {"blue", 0.0}, {"luma", 0.0}}},
		{"gamma", {{"red", 0.0}, {"green", 0.0}, {"blue", 0.0}, {"luma", 0.0}}},
		{"gain", {{"red", 1.0}, {"green", 1.0}, {"blue", 1.0}, {"luma", 1.0}}},
		{"offset", {{"red", 0.0}, {"green", 0.0}, {"blue", 0.0}, {"luma", 0.0}}},
		{"contrast", {{"pivot", 0.5}, {"adjust", 1.0}}},
		{"color", {{"hue", 0.0}, {"saturation", 1.0}}},
		{"lumaContribution", {{"lumaContribution", 1.0}}},
	};
	return neutral;
}

nlohmann::json complete_color(const nlohmann::json &values)
{
	nlohmann::json complete = neutral_color();
	if (!values.is_object())
		return complete;
	for (const std::string &key : color_keys()) {
		if (!values.contains(key) || !values[key].is_object())
			continue;
		for (const auto &[field, value] : values[key].items()) {
			if (complete[key].contains(field) && value.is_number())
				complete[key][field] = value.get<double>();
		}
	}
	return complete;
}

bool same_color(const nlohmann::json &a, const nlohmann::json &b)
{
	const nlohmann::json first = complete_color(a);
	const nlohmann::json second = complete_color(b);
	for (const std::string &key : color_keys()) {
		for (const auto &[field, value] : first[key].items()) {
			if (std::abs(value.get<double>() - second[key][field].get<double>()) > kTolerance)
				return false;
		}
	}
	return true;
}

std::vector<Look> parse_looks(const std::string &text, bool builtin)
{
	std::vector<Look> looks;
	const nlohmann::json file = nlohmann::json::parse(text, nullptr, false);
	if (!file.is_object() || !file.contains("looks") || !file["looks"].is_array())
		return looks;
	for (const nlohmann::json &item : file["looks"]) {
		const char *key = builtin ? "id" : "name";
		if (!item.is_object() || !item.contains(key) || !item[key].is_string())
			continue;
		Look look;
		look.id = item[key].get<std::string>();
		look.builtin = builtin;
		look.values = complete_color(item.value("values", nlohmann::json::object()));
		if (!look.id.empty())
			looks.push_back(std::move(look));
	}
	return looks;
}

std::string write_looks(const std::vector<Look> &looks)
{
	nlohmann::json list = nlohmann::json::array();
	for (const Look &look : looks)
		list.push_back({{"name", look.id}, {"values", look.values}});
	return nlohmann::json{{"looks", list}}.dump(1, '\t');
}

} // namespace bmagicam
