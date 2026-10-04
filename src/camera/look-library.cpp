// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "look-library.hpp"

#include "camera-controls.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <algorithm>
#include <mutex>

namespace bmagicam {

namespace {

std::mutex files_mutex;

std::string read_file(char *path)
{
	std::string text;
	if (path) {
		char *content = os_quick_read_utf8_file(path);
		if (content)
			text = content;
		bfree(content);
	}
	bfree(path);
	return text;
}

std::string user_looks_path()
{
	char *path = obs_module_config_path("looks.json");
	std::string result = path ? path : "";
	bfree(path);
	return result;
}

bool write_user_looks(const std::vector<Look> &looks)
{
	char *folder = obs_module_config_path("");
	if (folder)
		os_mkdirs(folder);
	bfree(folder);
	const std::string text = write_looks(looks);
	return os_quick_write_utf8_file_safe(user_looks_path().c_str(), text.c_str(), text.size(), false, "tmp",
					     nullptr);
}

} // namespace

const std::vector<Look> &builtin_looks()
{
	static const std::vector<Look> looks = parse_looks(read_file(obs_module_file("looks/builtin.json")), true);
	return looks;
}

std::vector<Look> user_looks()
{
	std::lock_guard lock(files_mutex);
	return parse_looks(read_file(obs_module_config_path("looks.json")), false);
}

std::vector<Look> all_looks()
{
	std::vector<Look> looks = builtin_looks();
	for (Look &look : user_looks())
		looks.push_back(std::move(look));
	return looks;
}

const Look *find_look(const std::vector<Look> &looks, const std::string &id)
{
	const auto look = std::find_if(looks.begin(), looks.end(), [&](const Look &item) { return item.id == id; });
	return look != looks.end() ? &*look : nullptr;
}

bool save_user_look(const std::string &name, const nlohmann::json &values)
{
	std::vector<Look> looks = user_looks();
	std::lock_guard lock(files_mutex);
	Look look{name, false, complete_color(values)};
	const auto existing =
		std::find_if(looks.begin(), looks.end(), [&](const Look &item) { return item.id == name; });
	if (existing != looks.end())
		*existing = look;
	else
		looks.push_back(look);
	return write_user_looks(looks);
}

bool rename_user_look(const std::string &from, const std::string &to)
{
	std::vector<Look> looks = user_looks();
	std::lock_guard lock(files_mutex);
	if (from == to || find_look(looks, to))
		return false;
	for (Look &look : looks) {
		if (look.id == from)
			look.id = to;
	}
	return write_user_looks(looks);
}

bool delete_user_look(const std::string &name)
{
	std::vector<Look> looks = user_looks();
	std::lock_guard lock(files_mutex);
	looks.erase(std::remove_if(looks.begin(), looks.end(), [&](const Look &item) { return item.id == name; }),
		    looks.end());
	return write_user_looks(looks);
}

nlohmann::json current_color(const CameraControls &controls)
{
	nlohmann::json color = nlohmann::json::object();
	for (const std::string &key : color_keys()) {
		const nlohmann::json value = controls.get(color_path(key));
		if (!value.is_object())
			return nullptr;
		color[key] = value;
	}
	return complete_color(color);
}

void apply_color(CameraControls &controls, const nlohmann::json &values)
{
	const nlohmann::json complete = complete_color(values);
	for (const std::string &key : color_keys())
		controls.set(color_path(key), complete[key]);
}

} // namespace bmagicam
