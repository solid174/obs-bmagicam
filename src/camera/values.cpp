// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "values.hpp"

namespace bmagicam {

double number_at(const nlohmann::json &object, const char *key, double fallback)
{
	if (object.is_object() && object.contains(key) && object[key].is_number())
		return object[key].get<double>();
	return fallback;
}

std::string string_at(const nlohmann::json &object, const char *key)
{
	if (object.is_object() && object.contains(key) && object[key].is_string())
		return object[key].get<std::string>();
	return {};
}

bool bool_at(const nlohmann::json &object, const char *key, bool fallback)
{
	if (object.is_object() && object.contains(key) && object[key].is_boolean())
		return object[key].get<bool>();
	return fallback;
}

std::vector<double> numbers_at(const nlohmann::json &object, const char *key)
{
	std::vector<double> numbers;
	if (object.is_object() && object.contains(key) && object[key].is_array()) {
		for (const nlohmann::json &item : object[key]) {
			if (item.is_number())
				numbers.push_back(item.get<double>());
		}
	}
	return numbers;
}

std::vector<std::string> strings_at(const nlohmann::json &object, const char *key)
{
	std::vector<std::string> strings;
	if (object.is_object() && object.contains(key) && object[key].is_array()) {
		for (const nlohmann::json &item : object[key]) {
			if (item.is_string())
				strings.push_back(item.get<std::string>());
		}
	}
	return strings;
}

double normalized_at(const nlohmann::json &object, double fallback)
{
	return number_at(object, "normalized", number_at(object, "normalised", fallback));
}

} // namespace bmagicam
