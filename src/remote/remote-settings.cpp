// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "remote-settings.hpp"

#include <nlohmann/json.hpp>
#include <obs-module.h>
#include <util/platform.h>

#include <mutex>
#include <random>

namespace bmagicam {

namespace {

constexpr const char *kFile = "remote.json";

std::mutex file_mutex;

} // namespace

std::string generate_password()
{
	static constexpr char kCharacters[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
	std::random_device device;
	std::uniform_int_distribution<size_t> pick(0, sizeof(kCharacters) - 2);
	std::string password;
	for (int index = 0; index < 16; index++)
		password.push_back(kCharacters[pick(device)]);
	return password;
}

RemoteSettings load_remote_settings()
{
	RemoteSettings settings;
	{
		std::lock_guard lock(file_mutex);
		char *path = obs_module_config_path(kFile);
		char *content = path ? os_quick_read_utf8_file(path) : nullptr;
		const nlohmann::json file = nlohmann::json::parse(content ? content : "", nullptr, false);
		bfree(content);
		bfree(path);
		if (file.is_object()) {
			settings.enabled = file.value("enabled", false);
			settings.port = file.value("port", kDefaultRemotePort);
			settings.authentication = file.value("authentication", true);
			settings.password = file.value("password", std::string());
		}
	}
	if (settings.port < 1024 || settings.port > 65535)
		settings.port = kDefaultRemotePort;
	if (settings.password.empty()) {
		settings.password = generate_password();
		save_remote_settings(settings);
	}
	return settings;
}

bool save_remote_settings(const RemoteSettings &settings)
{
	std::lock_guard lock(file_mutex);
	char *folder = obs_module_config_path("");
	if (folder)
		os_mkdirs(folder);
	bfree(folder);
	const nlohmann::json file = {{"enabled", settings.enabled},
				     {"port", settings.port},
				     {"authentication", settings.authentication},
				     {"password", settings.password}};
	const std::string text = file.dump(1, '\t');
	char *path = obs_module_config_path(kFile);
	const bool written = path &&
			     os_quick_write_utf8_file_safe(path, text.c_str(), text.size(), false, "tmp", nullptr);
	bfree(path);
	return written;
}

} // namespace bmagicam
