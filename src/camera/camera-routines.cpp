// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "camera-routines.hpp"

#include "camera-controls.hpp"
#include "looks.hpp"
#include "values.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <thread>

namespace bmagicam {

namespace {

using namespace std::chrono_literals;

// Time auto exposure gets to measure once
constexpr auto kMeasureTime = 1500ms;
constexpr int kConfirmAttempts = 5;
constexpr auto kConfirmPause = 1s;

// The value nearest to the target on a logarithmic scale, as ISOs and shutters are; 0 for an empty list
double nearest_ratio(const std::vector<double> &values, double target)
{
	double best = 0;
	for (double value : values) {
		if (value > 0 && (best == 0 || std::abs(std::log(value / target)) < std::abs(std::log(best / target))))
			best = value;
	}
	return best;
}

bool contains(const std::vector<std::string> &list, const std::string &item)
{
	return std::find(list.begin(), list.end(), item) != list.end();
}

// A write tried again until the phone confirms it, for resets that must not stop halfway (RST-4)
bool put_confirmed(CameraControls &controls, const std::string &path, const nlohmann::json &body)
{
	for (int attempt = 0; attempt < kConfirmAttempts; attempt++) {
		const ApiReply reply = controls.put_now(path, body);
		if (reply.ok())
			return true;
		// Refused values are not tried again; only a phone that did not answer is
		if (reply.answered())
			return false;
		std::this_thread::sleep_for(kConfirmPause);
	}
	return false;
}

} // namespace

bool set_up_for_streaming(CameraControls &controls)
{
	bool complete = true;
	const auto put = [&](const std::string &path, const nlohmann::json &body) {
		const ApiReply reply = controls.put_now(path, body);
		complete = complete && reply.ok();
		return reply.ok();
	};

	// The flicker-free shutter nearest to 1/(2 × frame rate): the phone's list already follows the mains frequency
	const double fps =
		std::strtod(string_at(controls.get_now("/system/videoFormat").body, "frameRate").c_str(), nullptr);
	const double target_speed =
		fps > 0 ? nearest_ratio(numbers_at(controls.get_now("/video/flickerFreeShutters").body,
						   "shutterSpeeds"),
					2 * fps)
			: 0;

	// Exposure measured once
	if (string_at(controls.get_now("/video/autoExposure").body, "mode") != "Continuous") {
		put("/video/autoExposure", {{"mode", "OneShot"}});
		std::this_thread::sleep_for(kMeasureTime);
	}
	const double iso = number_at(controls.get_now("/video/iso").body, "iso");
	const nlohmann::json shutter = controls.get_now("/video/shutter").body;
	double speed = number_at(shutter, "shutterSpeed");
	const double angle = number_at(shutter, "shutterAngle");
	if (speed <= 0 && angle > 0 && fps > 0)
		speed = fps * 360 / angle;

	// ...and held: the flicker-free shutter, with ISO moved by as much as the shutter changed, so brightness stays
	put("/video/autoExposure", {{"mode", "Off"}});
	if (target_speed > 0) {
		put("/video/shutter", {{"shutterSpeed", std::lround(target_speed)}});
		if (iso > 0 && speed > 0) {
			const double wanted = iso * target_speed / speed;
			const double supported = nearest_ratio(
				numbers_at(controls.get_now("/video/supportedISOs").body, "supportedISOs"), wanted);
			if (supported > 0)
				put("/video/iso", {{"iso", std::lround(supported)}});
		}
	}

	put("/video/whiteBalance/doAuto", nullptr);

	// Continuous focus, on faces where the phone offers it
	const std::vector<std::string> modes =
		strings_at(controls.get_now("/lens/focus/autoFocus/description").body, "supportedModes");
	put("/lens/focus/autoFocus",
	    {{"enabled", true}, {"mode", contains(modes, "TrackFace") ? "TrackFace" : "Continuous"}});

	const std::vector<std::string> ranges =
		strings_at(controls.get_now("/system/supportedDynamicRanges").body, "supportedDynamicRanges");
	if (contains(ranges, "Video"))
		put("/system/dynamicRange", {{"dynamicRange", "Video"}});

	return complete;
}

bool reset_to_defaults(CameraControls &controls)
{
	bool complete = put_confirmed(controls, "/presets/active", {{"preset", "default"}});
	for (const std::string &key : color_keys())
		complete = put_confirmed(controls, color_path(key), neutral_color()[key]) && complete;
	return complete;
}

} // namespace bmagicam
