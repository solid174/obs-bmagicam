// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "beauty-styles.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace bmagicam {

bool BeautyValues::operator==(const BeautyValues &other) const
{
	for (size_t index = 0; index < kBeautyKeys.size(); index++)
		if (beauty_value(*this, index) != beauty_value(other, index))
			return false;
	return true;
}

int &beauty_value(BeautyValues &values, size_t index)
{
	int *const fields[] = {&values.smoothing, &values.texture,       &values.evening,    &values.sharpen,
			       &values.glow,      &values.mask_softness, &values.detail_size};
	return *fields[index];
}

int beauty_value(const BeautyValues &values, size_t index)
{
	return beauty_value(const_cast<BeautyValues &>(values), index);
}

std::vector<BeautyStyle> parse_styles(const std::string &text, bool builtin)
{
	std::vector<BeautyStyle> styles;
	const nlohmann::json file = nlohmann::json::parse(text, nullptr, false);
	if (!file.is_object() || !file.contains("styles") || !file["styles"].is_array())
		return styles;
	for (const nlohmann::json &item : file["styles"]) {
		const char *key = builtin ? "id" : "name";
		if (!item.is_object() || !item.contains(key) || !item[key].is_string() || !item.contains("values") ||
		    !item["values"].is_object())
			continue;
		BeautyStyle style;
		style.id = item[key].get<std::string>();
		style.builtin = builtin;
		for (size_t index = 0; index < kBeautyKeys.size(); index++) {
			const nlohmann::json &value = item["values"].value(kBeautyKeys[index], nlohmann::json());
			if (value.is_number())
				beauty_value(style.values, index) = std::clamp(value.get<int>(), 0, 100);
		}
		if (!style.id.empty())
			styles.push_back(std::move(style));
	}
	return styles;
}

std::string write_styles(const std::vector<BeautyStyle> &styles)
{
	nlohmann::json list = nlohmann::json::array();
	for (const BeautyStyle &style : styles) {
		nlohmann::json values = nlohmann::json::object();
		for (size_t index = 0; index < kBeautyKeys.size(); index++)
			values[kBeautyKeys[index]] = beauty_value(style.values, index);
		list.push_back({{"name", style.id}, {"values", values}});
	}
	return nlohmann::json{{"styles", list}}.dump(1, '\t');
}

BeautyAmounts beauty_amounts(const BeautyValues &values, int strength)
{
	const float s = static_cast<float>(std::clamp(strength, 0, 100)) / 100.0f;
	const auto share = [](int value) {
		return static_cast<float>(std::clamp(value, 0, 100)) / 100.0f;
	};
	BeautyAmounts amounts;
	amounts.smoothing = share(values.smoothing) * std::pow(s, 0.8f);
	// Texture stays higher at low strength, so light smoothing never looks plastic
	amounts.texture = 1.0f - (1.0f - share(values.texture)) * std::pow(s, 0.6f);
	amounts.evening = share(values.evening) * s;
	amounts.sharpen = share(values.sharpen) * s;
	amounts.glow = share(values.glow) * s * s;
	amounts.mask_softness = share(values.mask_softness);
	amounts.detail_size = share(values.detail_size);
	return amounts;
}

} // namespace bmagicam
