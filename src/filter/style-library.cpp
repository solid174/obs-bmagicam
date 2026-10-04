// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "style-library.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <algorithm>
#include <mutex>

namespace bmagicam {

namespace {

constexpr const char *kUserStylesFile = "beauty-styles.json";

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

bool write_user_styles(const std::vector<BeautyStyle> &styles)
{
	char *folder = obs_module_config_path("");
	if (folder)
		os_mkdirs(folder);
	bfree(folder);
	char *path = obs_module_config_path(kUserStylesFile);
	const std::string text = write_styles(styles);
	const bool written = path &&
			     os_quick_write_utf8_file_safe(path, text.c_str(), text.size(), false, "tmp", nullptr);
	bfree(path);
	return written;
}

} // namespace

const std::vector<BeautyStyle> &builtin_styles()
{
	static const std::vector<BeautyStyle> styles =
		parse_styles(read_file(obs_module_file("beauty/styles.json")), true);
	return styles;
}

std::vector<BeautyStyle> user_styles()
{
	std::lock_guard lock(files_mutex);
	return parse_styles(read_file(obs_module_config_path(kUserStylesFile)), false);
}

std::vector<BeautyStyle> all_styles()
{
	std::vector<BeautyStyle> styles = builtin_styles();
	for (BeautyStyle &style : user_styles())
		styles.push_back(std::move(style));
	return styles;
}

const BeautyStyle *find_style(const std::vector<BeautyStyle> &styles, const std::string &id)
{
	const auto style =
		std::find_if(styles.begin(), styles.end(), [&](const BeautyStyle &item) { return item.id == id; });
	return style != styles.end() ? &*style : nullptr;
}

bool save_user_style(const std::string &name, const BeautyValues &values)
{
	std::vector<BeautyStyle> styles = user_styles();
	std::lock_guard lock(files_mutex);
	const BeautyStyle style{name, false, values};
	const auto existing =
		std::find_if(styles.begin(), styles.end(), [&](const BeautyStyle &item) { return item.id == name; });
	if (existing != styles.end())
		*existing = style;
	else
		styles.push_back(style);
	return write_user_styles(styles);
}

bool delete_user_style(const std::string &name)
{
	std::vector<BeautyStyle> styles = user_styles();
	std::lock_guard lock(files_mutex);
	styles.erase(std::remove_if(styles.begin(), styles.end(),
				    [&](const BeautyStyle &item) { return item.id == name; }),
		     styles.end());
	return write_user_styles(styles);
}

} // namespace bmagicam
