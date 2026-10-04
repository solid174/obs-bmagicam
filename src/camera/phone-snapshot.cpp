// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "phone-snapshot.hpp"

#include "camera-controls.hpp"
#include "values.hpp"
#include "../phone/camera-client.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <nlohmann/json.hpp>

#include <cctype>
#include <chrono>
#include <thread>
#include <vector>

namespace bmagicam {

namespace {

using namespace std::chrono_literals;

constexpr const char *kPresetName = "Before obs-bmagicam";
constexpr int kAttempts = 5;
constexpr auto kPause = 1s;
// The phone's server pauses after a video format change
constexpr auto kFormatPause = 10s;

// What a snapshot keeps, in the order a restore writes it back: the format and the lens first, since ranges follow
// them; auto exposure before ISO and shutter; the autofocus mode before the focus distance
struct Setting {
	const char *path;
	// Fields written back; the others are read-only or follow from these
	std::vector<const char *> fields;
};
const std::vector<Setting> &settings()
{
	static const std::vector<Setting> list = {
		{"/system/videoFormat", {"name"}},
		{"/system/codecFormat", {"codec", "container"}},
		{"/system/dynamicRange", {"dynamicRange"}},
		{"/lens/cameras/active", {"id"}},
		{"/lens/zoom", {"focalLength"}},
		{"/video/autoExposure", {"mode"}},
		{"/video/iso", {"iso"}},
		{"/video/shutter", {"shutterSpeed", "shutterAngle"}},
		{"/video/whiteBalance", {"whiteBalance"}},
		{"/video/whiteBalanceTint", {"whiteBalanceTint"}},
		{"/lens/focus/autoFocus", {"enabled", "mode"}},
		{"/lens/focus", {"normalised"}},
		{"/lens/opticalImageStabilization", {"enabled"}},
		{"/colorCorrection/lift", {"red", "green", "blue", "luma"}},
		{"/colorCorrection/gamma", {"red", "green", "blue", "luma"}},
		{"/colorCorrection/gain", {"red", "green", "blue", "luma"}},
		{"/colorCorrection/offset", {"red", "green", "blue", "luma"}},
		{"/colorCorrection/contrast", {"pivot", "adjust"}},
		{"/colorCorrection/color", {"hue", "saturation"}},
		{"/colorCorrection/lumaContribution", {"lumaContribution"}},
		{"/monitoring/Device/brightness", {"brightness"}},
		{"/monitoring/Device/zebra", {"enabled"}},
		{"/monitoring/Device/focusAssist", {"enabled"}},
		{"/monitoring/Device/falseColor", {"enabled"}},
		{"/monitoring/Device/frameGuide", {"enabled"}},
		{"/monitoring/Device/frameGrids", {"enabled"}},
		{"/monitoring/Device/safeArea", {"enabled"}},
		{"/monitoring/Device/displayLUT", {"enabled"}},
		{"/monitoring/zebra", {"highlight", "skinTone"}},
		{"/monitoring/focusAssist", {"mode", "color", "intensity"}},
		{"/monitoring/frameGuideRatio", {"ratio"}},
		{"/monitoring/frameGrids", {"frameGrids"}},
		{"/monitoring/safeAreaPercent", {"percent"}},
		{"/audio/channel/0/input", {"input"}},
		{"/audio/channel/1/input", {"input"}},
		{"/transports/0/proxyRecording", {"enabled"}},
		{"/camera/id", {"id"}},
	};
	return list;
}

std::string snapshot_path(const std::string &phone_key)
{
	std::string name;
	for (const char c : phone_key)
		name += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.') ? c : '_';
	char *path = obs_module_config_path(("snapshots/" + name + ".json").c_str());
	std::string result = path ? path : "";
	bfree(path);
	return result;
}

std::string url_segment(const std::string &text)
{
	static const char *hex = "0123456789ABCDEF";
	std::string encoded;
	for (const unsigned char c : text) {
		if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			encoded += static_cast<char>(c);
		} else {
			encoded += '%';
			encoded += hex[c >> 4];
			encoded += hex[c & 15];
		}
	}
	return encoded;
}

// The fields of a value the phone takes back
nlohmann::json writable(const Setting &setting, const nlohmann::json &value)
{
	nlohmann::json body = nlohmann::json::object();
	for (const char *field : setting.fields) {
		if (value.is_object() && value.contains(field))
			body[field] = value[field];
	}
	return body;
}

} // namespace

bool has_snapshot(const std::string &phone_key)
{
	return os_file_exists(snapshot_path(phone_key).c_str());
}

void take_snapshot_if_new(const CameraClient &client, const std::string &phone_key)
{
	if (has_snapshot(phone_key))
		return;

	nlohmann::json values = nlohmann::json::object();
	for (const Setting &setting : settings()) {
		const ApiReply reply = client.get(setting.path);
		if (!reply.answered())
			return;
		if (reply.ok() && reply.body.is_object())
			values[setting.path] = reply.body;
	}
	// A phone preset as well, which the user can load in the app; where the phone has presets at all. The livestream
	// destination is kept with the session's own record, which also gives it back after a crash.
	const bool preset = client.put("/presets/" + url_segment(kPresetName)).ok();

	const nlohmann::json snapshot = {
		{"preset", preset ? kPresetName : ""},
		{"values", values},
	};
	char *folder = obs_module_config_path("snapshots");
	if (folder)
		os_mkdirs(folder);
	bfree(folder);
	const std::string text = snapshot.dump(1, '\t');
	if (os_quick_write_utf8_file_safe(snapshot_path(phone_key).c_str(), text.c_str(), text.size(), false, "tmp",
					  nullptr))
		obs_log(LOG_INFO, "saved the phone's settings before changing them");
}

bool restore_snapshot(CameraControls &controls, const std::string &phone_key)
{
	char *text = os_quick_read_utf8_file(snapshot_path(phone_key).c_str());
	const nlohmann::json snapshot = text ? nlohmann::json::parse(text, nullptr, false) : nlohmann::json();
	bfree(text);
	if (!snapshot.is_object())
		return false;

	const auto put_confirmed = [&](const std::string &path, const nlohmann::json &body) {
		for (int attempt = 0; attempt < kAttempts; attempt++) {
			const ApiReply reply = controls.put_now(path, body);
			if (reply.ok())
				return true;
			if (reply.answered())
				return false;
			std::this_thread::sleep_for(kPause);
		}
		return false;
	};

	bool complete = true;
	// The phone preset first: it covers what the app keeps in presets; the snapshot covers the rest
	const std::string preset = string_at(snapshot, "preset");
	if (!preset.empty()) {
		for (const std::string &name : strings_at(controls.get_now("/presets").body, "presets")) {
			if (name.rfind(preset, 0) == 0)
				complete = put_confirmed("/presets/active", {{"preset", name}}) && complete;
		}
	}

	const nlohmann::json values = snapshot.value("values", nlohmann::json::object());
	const std::string exposure = string_at(values.value("/video/autoExposure", nlohmann::json()), "mode");
	const nlohmann::json focus = values.value("/lens/focus/autoFocus", nlohmann::json());
	const bool focus_automatic = bool_at(focus, "enabled", true) && string_at(focus, "mode") != "OneShot";
	for (const Setting &setting : settings()) {
		if (!values.contains(setting.path))
			continue;
		const std::string path = setting.path;
		// Under auto exposure the camera sets ISO and shutter itself, and continuous focus the distance
		if ((path == "/video/iso" || path == "/video/shutter") && exposure == "Continuous")
			continue;
		if (path == "/lens/focus" && focus_automatic)
			continue;
		const nlohmann::json body = writable(setting, values[path]);
		if (body.empty())
			continue;
		const nlohmann::json now = controls.get_now(path).body;
		if (now.is_object() && writable(setting, now) == body)
			continue;
		const bool written = put_confirmed(path, body);
		complete = written && complete;
		if (written && path == "/system/videoFormat") {
			const auto until = std::chrono::steady_clock::now() + kFormatPause;
			while (std::chrono::steady_clock::now() < until && !controls.get_now(path).ok())
				std::this_thread::sleep_for(500ms);
		}
	}
	return complete;
}

} // namespace bmagicam
