// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "control-map.hpp"

#include "camera-controls.hpp"
#include "looks.hpp"
#include "values.hpp"

#include <algorithm>
#include <cmath>

namespace bmagicam {

namespace {

using Option = ControlState::Option;

constexpr const char *kExposureLocked = "Dock.Exposure.Locked";
constexpr const char *kPhoneLocked = "Control.Locked.Phone";

bool continuous_exposure(const CameraControls &controls)
{
	return string_at(controls.get("/video/autoExposure"), "mode") == "Continuous";
}

// The range of a field in a description endpoint, such as /video/whiteBalance/description
bool description_range(const CameraControls &controls, const std::string &path, const char *field, double step,
		       ControlState &state)
{
	const nlohmann::json description = controls.get(path);
	const nlohmann::json limits = description.is_object() ? description.value(field, nlohmann::json()) : nullptr;
	if (!limits.is_object())
		return false;
	state.minimum = number_at(limits, "min");
	state.maximum = number_at(limits, "max");
	state.step = step;
	return state.maximum > state.minimum;
}

Control make(std::string id, ControlKind kind, std::string tab, std::vector<std::string> labels, std::string tooltip,
	     std::vector<std::string> paths)
{
	Control control;
	control.id = std::move(id);
	control.kind = kind;
	control.tab = std::move(tab);
	control.label_keys = std::move(labels);
	control.tooltip_key = std::move(tooltip);
	control.paths = std::move(paths);
	return control;
}

// A number field of a property, in a fixed range
Control number(std::string id, std::string tab, std::vector<std::string> labels, std::string tooltip,
	       const std::string &path, const std::string &field, double minimum, double maximum, double step,
	       int decimals, std::string unit = {})
{
	Control control =
		make(std::move(id), ControlKind::Number, std::move(tab), std::move(labels), std::move(tooltip), {path});
	control.decimals = decimals;
	control.unit = std::move(unit);
	control.read = [path, field, minimum, maximum, step](const CameraControls &controls) {
		ControlState state;
		const nlohmann::json value = controls.get(path);
		state.available = value.is_object();
		state.value = number_at(value, field.c_str());
		state.minimum = minimum;
		state.maximum = maximum;
		state.step = step;
		return state;
	};
	control.write = [path, field](CameraControls &controls, const nlohmann::json &value) {
		controls.set(path, {{field, value}});
		return true;
	};
	return control;
}

// The "enabled" field of a property
Control enabled_switch(std::string id, std::string tab, std::vector<std::string> labels, std::string tooltip,
		       const std::string &path, bool stream = true)
{
	Control control =
		make(std::move(id), ControlKind::Switch, std::move(tab), std::move(labels), std::move(tooltip), {path});
	control.stream = stream;
	control.read = [path](const CameraControls &controls) {
		ControlState state;
		const nlohmann::json value = controls.get(path);
		state.available = value.is_object();
		state.value = bool_at(value, "enabled");
		return state;
	};
	control.write = [path](CameraControls &controls, const nlohmann::json &value) {
		controls.set(path, {{"enabled", value.get<bool>()}});
		return true;
	};
	return control;
}

// Something the phone does once; there while the property it acts on is known
Control action(std::string id, std::string tab, std::vector<std::string> labels, std::string tooltip,
	       const std::string &path, const std::string &needs, nlohmann::json body = nullptr)
{
	Control control = make(std::move(id), ControlKind::Action, std::move(tab), std::move(labels),
			       std::move(tooltip), {needs});
	control.read = [needs](const CameraControls &controls) {
		ControlState state;
		state.available = controls.get(needs).is_object();
		return state;
	};
	control.write = [path, body](CameraControls &controls, const nlohmann::json &) {
		controls.act(path, body);
		return true;
	};
	return control;
}

// The phone's lenses: Front, then one per back lens named like the iPhone camera names them (0.5×, 1×, 2×...)
std::vector<Option> lens_options(const nlohmann::json &cameras)
{
	std::vector<Option> options;
	if (!cameras.is_object() || !cameras.contains("cameras") || !cameras["cameras"].is_array())
		return options;
	const nlohmann::json &list = cameras["cameras"];
	const nlohmann::json *front = nullptr;
	for (const nlohmann::json &lens : list) {
		if (string_at(lens, "facing") == "front" && (!front || bool_at(lens, "isActive")))
			front = &lens;
	}
	if (front && bool_at(*front, "isAvailable", true))
		options.push_back({string_at(*front, "id"), {}, "Dock.Lens.Front"});
	for (const nlohmann::json &lens : list) {
		if (string_at(lens, "facing") != "back" || !bool_at(lens, "isAvailable", true))
			continue;
		std::string label = string_at(lens, "zoomFactor");
		for (size_t at = label.find('x'); at != std::string::npos; at = label.find('x'))
			label.replace(at, 1, "×");
		if (label.empty())
			label = std::to_string(std::lround(number_at(lens, "focalLength"))) + " mm";
		options.push_back({string_at(lens, "id"), label, {}});
	}
	return options;
}

std::vector<Control> build()
{
	std::vector<Control> map;

	// Camera: exposure, white balance and the lens, the tiles of Advanced mode
	Control exposure_auto = make("exposure.auto", ControlKind::Switch, "camera", {"Control.ExposureAuto"},
				     "Dock.Exposure.Auto.Tooltip", {"/video/autoExposure"});
	exposure_auto.read = [](const CameraControls &controls) {
		ControlState state;
		const nlohmann::json value = controls.get("/video/autoExposure");
		state.available = value.is_object();
		state.value = string_at(value, "mode") == "Continuous";
		return state;
	};
	exposure_auto.write = [](CameraControls &controls, const nlohmann::json &value) {
		controls.set("/video/autoExposure", {{"mode", value.get<bool>() ? "Continuous" : "Off"}});
		return true;
	};
	map.push_back(exposure_auto);

	Control iso = make("exposure.iso", ControlKind::Stops, "camera", {"Dock.ISO"}, "Dock.ISO.Tooltip",
			   {"/video/iso", "/video/supportedISOs", "/video/autoExposure"});
	iso.simple_label_key = "Dock.Brightness";
	iso.simple_ends = {"Dock.Brightness.Darker", "Dock.Brightness.Brighter"};
	iso.companion = "exposure.auto";
	iso.read = [](const CameraControls &controls) {
		ControlState state;
		state.stops = numbers_at(controls.get("/video/supportedISOs"), "supportedISOs");
		const nlohmann::json value = controls.get("/video/iso");
		state.available = !state.stops.empty() && value.is_object();
		state.value = number_at(value, "iso");
		if (continuous_exposure(controls))
			state.locked_by = kExposureLocked;
		return state;
	};
	iso.write = [](CameraControls &controls, const nlohmann::json &value) {
		controls.set("/video/iso", {{"iso", std::lround(value.get<double>())}});
		return true;
	};
	map.push_back(iso);

	Control shutter = make("exposure.shutter", ControlKind::Stops, "camera", {"Dock.Shutter"},
			       "Dock.Shutter.Tooltip",
			       {"/video/shutter", "/video/supportedShutters", "/video/flickerFreeShutters",
				"/video/autoExposure", "/system/videoFormat"});
	shutter.companion = "exposure.auto";
	shutter.unit = "1/";
	shutter.read = [](const CameraControls &controls) {
		ControlState state;
		state.stops = numbers_at(controls.get("/video/supportedShutters"), "shutterSpeeds");
		std::sort(state.stops.begin(), state.stops.end());
		state.marks = numbers_at(controls.get("/video/flickerFreeShutters"), "shutterSpeeds");
		const nlohmann::json value = controls.get("/video/shutter");
		state.available = !state.stops.empty() && value.is_object();
		state.value = shutter_speed(value, frame_rate(controls.get("/system/videoFormat")));
		if (continuous_exposure(controls))
			state.locked_by = kExposureLocked;
		return state;
	};
	shutter.write = [](CameraControls &controls, const nlohmann::json &value) {
		controls.set("/video/shutter", {{"shutterSpeed", std::lround(value.get<double>())}});
		return true;
	};
	map.push_back(shutter);

	Control iris = make("exposure.iris", ControlKind::Number, "camera", {"Dock.Iris"}, "Dock.Iris.Tooltip",
			    {"/lens/iris", "/lens/iris/description"});
	iris.unit = "f/";
	iris.decimals = 1;
	iris.read = [](const CameraControls &controls) {
		ControlState state;
		const nlohmann::json value = controls.get("/lens/iris");
		state.available = value.is_object() &&
				  bool_at(controls.get("/lens/iris/description"), "controllable") &&
				  description_range(controls, "/lens/iris/description", "apertureStop", 0.1, state);
		state.value = number_at(value, "apertureStop");
		return state;
	};
	iris.write = [](CameraControls &controls, const nlohmann::json &value) {
		controls.set("/lens/iris", {{"apertureStop", value}});
		return true;
	};
	map.push_back(iris);

	Control temperature = make("wb.temperature", ControlKind::Number, "camera", {"Dock.WB"}, "Dock.WB.Tooltip",
				   {"/video/whiteBalance", "/video/whiteBalance/description"});
	temperature.simple_label_key = "Dock.Warmth";
	temperature.simple_ends = {"Dock.Warmth.Cool", "Dock.Warmth.Warm"};
	temperature.companion = "wb.auto";
	temperature.unit = "K";
	temperature.read = [](const CameraControls &controls) {
		ControlState state;
		const nlohmann::json value = controls.get("/video/whiteBalance");
		state.available = value.is_object();
		state.value = number_at(value, "whiteBalance");
		if (!description_range(controls, "/video/whiteBalance/description", "whiteBalance", 50, state)) {
			state.minimum = 2500;
			state.maximum = 10000;
			state.step = 50;
		}
		return state;
	};
	temperature.write = [](CameraControls &controls, const nlohmann::json &value) {
		controls.set("/video/whiteBalance", {{"whiteBalance", std::lround(value.get<double>())}});
		return true;
	};
	map.push_back(temperature);

	Control tint = make("wb.tint", ControlKind::Number, "camera", {"Dock.Tint"}, "Dock.Tint.Tooltip",
			    {"/video/whiteBalanceTint", "/video/whiteBalanceTint/description"});
	tint.read = [](const CameraControls &controls) {
		ControlState state;
		const nlohmann::json value = controls.get("/video/whiteBalanceTint");
		state.available = value.is_object();
		state.value = number_at(value, "whiteBalanceTint");
		if (!description_range(controls, "/video/whiteBalanceTint/description", "whiteBalanceTint", 1, state)) {
			state.minimum = -50;
			state.maximum = 50;
			state.step = 1;
		}
		return state;
	};
	tint.write = [](CameraControls &controls, const nlohmann::json &value) {
		controls.set("/video/whiteBalanceTint", {{"whiteBalanceTint", std::lround(value.get<double>())}});
		return true;
	};
	map.push_back(tint);

	map.push_back(action("wb.auto", "camera", {"Control.WhiteBalanceAuto"}, "Dock.Warmth.Auto.Tooltip",
			     "/video/whiteBalance/doAuto", "/video/whiteBalance"));

	Control lens = make("lens.camera", ControlKind::Choice, "camera", {"Dock.Lens"}, "Dock.Lens.Tooltip",
			    {"/lens/cameras", "/lens/cameras/active"});
	lens.simple_label_key = "Dock.Lens";
	lens.confirm_path = "/lens/cameras/active";
	lens.read = [](const CameraControls &controls) {
		ControlState state;
		const nlohmann::json cameras = controls.get("/lens/cameras");
		state.options = lens_options(cameras);
		state.available = state.options.size() > 1;
		const std::string active = string_at(controls.get("/lens/cameras/active"), "id");
		state.value = active;
		// Another front lens is still the Front option
		if (cameras.is_object() && cameras.contains("cameras") && cameras["cameras"].is_array()) {
			for (const nlohmann::json &item : cameras["cameras"]) {
				if (string_at(item, "id") == active && string_at(item, "facing") == "front" &&
				    !state.options.empty() && state.options.front().label_key == "Dock.Lens.Front")
					state.value = state.options.front().id;
			}
		}
		return state;
	};
	lens.write = [](CameraControls &controls, const nlohmann::json &value) {
		controls.set("/lens/cameras/active", {{"id", value.get<std::string>()}});
		return true;
	};
	map.push_back(lens);

	Control lens_auto = enabled_switch("lens.auto", "camera", {"Dock.Lens.Auto"}, "Dock.Lens.Auto.Tooltip",
					   "/lens/cameras/auto");
	lens_auto.read = [](const CameraControls &controls) {
		ControlState state;
		const nlohmann::json value = controls.get("/lens/cameras/auto");
		state.available = bool_at(value, "supported");
		state.value = bool_at(value, "enabled");
		return state;
	};
	map.push_back(lens_auto);

	Control zoom = make("lens.zoom", ControlKind::Number, "camera", {"Dock.Zoom"}, "Dock.Zoom.Tooltip",
			    {"/lens/zoom", "/lens/zoom/description"});
	zoom.unit = "mm";
	zoom.read = [](const CameraControls &controls) {
		ControlState state;
		const nlohmann::json value = controls.get("/lens/zoom");
		state.available = value.is_object() &&
				  bool_at(controls.get("/lens/zoom/description"), "controllable", true) &&
				  description_range(controls, "/lens/zoom/description", "focalLength", 1, state);
		state.value = number_at(value, "focalLength");
		return state;
	};
	zoom.write = [](CameraControls &controls, const nlohmann::json &value) {
		controls.set("/lens/zoom", {{"focalLength", std::lround(value.get<double>())}});
		return true;
	};
	map.push_back(zoom);

	// On from OBS is Standard; Cinematic and Extreme are chosen on the phone and read as on (STB-2)
	Control stabilization = enabled_switch("lens.stabilization", "camera", {"Dock.Stabilize"},
					       "Dock.Stabilize.Tooltip", "/lens/opticalImageStabilization");
	stabilization.simple_label_key = "Dock.Stabilize";
	stabilization.read = [](const CameraControls &controls) {
		ControlState state;
		const nlohmann::json value = controls.get("/lens/opticalImageStabilization");
		state.available = value.is_object();
		state.value = bool_at(value, "enabled");
		if (!bool_at(value, "controlAvailable", true))
			state.locked_by = kPhoneLocked;
		return state;
	};
	map.push_back(stabilization);

	// Focus
	Control focus_auto = make("focus.auto", ControlKind::Switch, "focus", {"Control.FocusAuto"},
				  "Dock.Focus.Auto.Tooltip", {"/lens/focus/autoFocus"});
	focus_auto.simple_label_key = "Dock.Focus";
	focus_auto.companion = "focus.refocus";
	focus_auto.read = [](const CameraControls &controls) {
		ControlState state;
		const nlohmann::json value = controls.get("/lens/focus/autoFocus");
		const std::string mode = string_at(value, "mode");
		state.available = value.is_object();
		state.value = bool_at(value, "enabled", true) && (mode == "Continuous" || mode.rfind("Track", 0) == 0);
		return state;
	};
	focus_auto.write = [](CameraControls &controls, const nlohmann::json &value) {
		controls.set("/lens/focus/autoFocus",
			     {{"enabled", true}, {"mode", value.get<bool>() ? "Continuous" : "OneShot"}});
		return true;
	};
	map.push_back(focus_auto);

	Control focus_mode = make("focus.mode", ControlKind::Choice, "focus", {"Dock.Focus.Mode"},
				  "Dock.Focus.Mode.Tooltip",
				  {"/lens/focus/autoFocus", "/lens/focus/autoFocus/description"});
	focus_mode.read = [](const CameraControls &controls) {
		ControlState state;
		const nlohmann::json value = controls.get("/lens/focus/autoFocus");
		state.available = value.is_object();
		state.options.push_back({"manual", {}, "Dock.Focus.Manual"});
		for (const std::string &mode :
		     strings_at(controls.get("/lens/focus/autoFocus/description"), "supportedModes"))
			state.options.push_back({mode, {}, "Dock.Focus.Mode." + mode});
		state.value = bool_at(value, "enabled", true) ? string_at(value, "mode") : "manual";
		return state;
	};
	focus_mode.write = [](CameraControls &controls, const nlohmann::json &value) {
		const std::string mode = value.get<std::string>();
		if (mode == "manual")
			controls.set("/lens/focus/autoFocus", {{"enabled", false}});
		else
			controls.set("/lens/focus/autoFocus", {{"enabled", true}, {"mode", mode}});
		return true;
	};
	map.push_back(focus_mode);

	// Near to far in percent; the phone takes "normalised" only (docs/camera-api.md, V-10)
	Control position = make("focus.position", ControlKind::Number, "focus", {"Dock.Focus.Position"},
				"Dock.Focus.Position.Tooltip", {"/lens/focus", "/lens/focus/autoFocus"});
	position.unit = "%";
	position.confirm_path = "/lens/focus";
	position.read = [](const CameraControls &controls) {
		ControlState state;
		const nlohmann::json value = controls.get("/lens/focus");
		state.available = value.is_object();
		state.value = normalized_at(value) * 100;
		state.minimum = 0;
		state.maximum = 100;
		state.step = 0.5;
		const nlohmann::json focus = controls.get("/lens/focus/autoFocus");
		if (bool_at(focus, "enabled", true) && string_at(focus, "mode") != "OneShot")
			state.locked_by = "Dock.Focus.Position.Locked";
		return state;
	};
	position.write = [](CameraControls &controls, const nlohmann::json &value) {
		controls.set("/lens/focus", {{"normalised", value.get<double>() / 100}});
		return true;
	};
	map.push_back(position);

	map.push_back(action("focus.refocus", "focus", {"Dock.Focus.Refocus"}, "Dock.Focus.Refocus.Tooltip",
			     "/lens/focus/autoFocus/retrigger", "/lens/focus/autoFocus"));
	map.push_back(action("focus.center", "focus", {"Dock.Focus.Center"}, "Dock.Focus.Center.Tooltip",
			     "/lens/focus/doAutoFocus", "/lens/focus/autoFocus",
			     {{"position", {{"x", 0.5}, {"y", 0.5}}}}));

	Control focus_state = make("focus.state", ControlKind::Text, "focus", {"Control.FocusState"},
				   "Control.FocusState.Tooltip", {"/lens/focus/autoFocus"});
	focus_state.unit = "Dock.Focus.State.";
	focus_state.read = [](const CameraControls &controls) {
		ControlState state;
		const std::string value = string_at(controls.get("/lens/focus/autoFocus"), "state");
		state.available = !value.empty();
		state.value = value;
		return state;
	};
	map.push_back(focus_state);

	// Color
	struct Basic {
		const char *id;
		const char *label;
		const char *key;
		const char *field;
		double minimum;
		double maximum;
	};
	const Basic basics[] = {
		{"color.saturation", "Dock.Color.Saturation", "color", "saturation", 0, 2},
		{"color.contrast", "Dock.Color.Contrast", "contrast", "adjust", 0, 2},
		{"color.pivot", "Dock.Color.Pivot", "contrast", "pivot", 0, 1},
		{"color.hue", "Dock.Color.Hue", "color", "hue", -1, 1},
		{"color.lumaMix", "Dock.Color.LumaMix", "lumaContribution", "lumaContribution", 0, 1},
	};
	for (const Basic &basic : basics)
		map.push_back(number(basic.id, "color", {basic.label}, std::string(basic.label) + ".Tooltip",
				     color_path(basic.key), basic.field, basic.minimum, basic.maximum, 0.01, 2));
	struct Wheel {
		const char *key;
		const char *label;
		double minimum;
		double maximum;
		double step;
	};
	const Wheel wheels[] = {
		{"lift", "Dock.Color.Lift", -2, 2, 0.005},
		{"gamma", "Dock.Color.Gamma", -4, 4, 0.01},
		{"gain", "Dock.Color.Gain", 0, 16, 0.01},
		{"offset", "Dock.Color.Offset", -8, 8, 0.01},
	};
	const char *channels[][3] = {{"r", "red", "Dock.Color.Red"},
				     {"g", "green", "Dock.Color.Green"},
				     {"b", "blue", "Dock.Color.Blue"},
				     {"y", "luma", "Dock.Color.Luma"}};
	for (const Wheel &wheel : wheels) {
		for (const auto &channel : channels)
			map.push_back(number(std::string("color.") + wheel.key + "." + channel[0], "color",
					     {wheel.label, channel[2]}, std::string(wheel.label) + ".Tooltip",
					     color_path(wheel.key), channel[1], wheel.minimum, wheel.maximum,
					     wheel.step, 3));
	}

	// Audio, per channel
	for (int channel = 0; channel < 2; channel++) {
		const std::string base = "/audio/channel/" + std::to_string(channel);
		const std::string prefix = "audio." + std::to_string(channel + 1) + ".";
		const std::string number_text = std::to_string(channel + 1);

		Control input = make(prefix + "input", ControlKind::Choice, "audio", {"Dock.Audio.Channel"},
				     "Dock.Audio.Input.Tooltip",
				     {base + "/input", base + "/supportedInputs", "/audio/channels"});
		input.label_argument = number_text;
		input.read = [base, channel](const CameraControls &controls) {
			ControlState state;
			const nlohmann::json value = controls.get(base + "/input");
			const int count = static_cast<int>(number_at(controls.get("/audio/channels"), "channels", 2));
			state.available = value.is_object() && channel < count;
			const nlohmann::json supported = controls.get(base + "/supportedInputs");
			if (supported.is_array()) {
				for (const nlohmann::json &item : supported) {
					const std::string name = item.is_string() ? item.get<std::string>()
										  : string_at(item, "input");
					if (!name.empty() && bool_at(item, "available", true))
						state.options.push_back({name, name, {}});
				}
			}
			state.value = string_at(value, "input");
			return state;
		};
		input.write = [base](CameraControls &controls, const nlohmann::json &value) {
			controls.set(base + "/input", {{"input", value.get<std::string>()}});
			return true;
		};
		map.push_back(input);

		Control level = make(prefix + "level", ControlKind::Number, "audio",
				     {"Dock.Audio.Channel", "Control.AudioLevel"}, "Dock.Audio.Level.Tooltip",
				     {base + "/level", base + "/input/description"});
		level.label_argument = number_text;
		level.unit = "dB";
		level.decimals = 1;
		level.read = [base](const CameraControls &controls) {
			ControlState state;
			const nlohmann::json value = controls.get(base + "/level");
			// A level only where the input's gain can change
			const nlohmann::json description = controls.get(base + "/input/description");
			const nlohmann::json range = description.is_object()
							     ? description.value("description", nlohmann::json())
								       .value("gainRange", nlohmann::json())
							     : nullptr;
			state.minimum = number_at(range, "Min");
			state.maximum = number_at(range, "Max");
			state.step = 0.5;
			state.available = value.is_object() && state.maximum > state.minimum;
			state.value = number_at(value, "gain");
			return state;
		};
		level.write = [base](CameraControls &controls, const nlohmann::json &value) {
			controls.set(base + "/level", {{"gain", value}});
			return true;
		};
		map.push_back(level);

		const char *switches[][3] = {{"lowCut", "/lowCutFilter", "Dock.Audio.LowCut"},
					     {"pad", "/padding", "Dock.Audio.Pad"},
					     {"phantom", "/phantomPower", "Dock.Audio.Phantom"}};
		for (const auto &item : switches) {
			Control control = enabled_switch(prefix + item[0], "audio", {"Dock.Audio.Channel", item[2]},
							 std::string(item[2]) + ".Tooltip", base + item[1]);
			control.label_argument = number_text;
			map.push_back(control);
		}
	}

	// The phone: format, recording, its own screen, its number
	Control format = make("format.video", ControlKind::Text, "phone", {"Control.Format"}, "Control.Format.Tooltip",
			      {"/system/videoFormat"});
	format.read = [](const CameraControls &controls) {
		ControlState state;
		const nlohmann::json value = controls.get("/system/videoFormat");
		state.available = value.is_object();
		state.value = std::to_string(static_cast<int>(number_at(value, "width"))) + " × " +
			      std::to_string(static_cast<int>(number_at(value, "height"))) + ", " +
			      string_at(value, "frameRate") + " fps";
		return state;
	};
	map.push_back(format);

	Control range = make("format.dynamicRange", ControlKind::Choice, "phone", {"Dock.Format.DynamicRange"},
			     "Dock.Format.DynamicRange.Tooltip",
			     {"/system/dynamicRange", "/system/supportedDynamicRanges"});
	range.read = [](const CameraControls &controls) {
		ControlState state;
		std::vector<std::string> ids =
			strings_at(controls.get("/system/supportedDynamicRanges"), "supportedDynamicRanges");
		// Video first: the range the looks are made for
		std::stable_sort(ids.begin(), ids.end(), [](const std::string &a, const std::string &b) {
			return a == "Video" && b != "Video";
		});
		for (const std::string &id : ids)
			state.options.push_back({id, id, {}});
		state.available = !ids.empty();
		state.value = string_at(controls.get("/system/dynamicRange"), "dynamicRange");
		return state;
	};
	range.write = [](CameraControls &controls, const nlohmann::json &value) {
		controls.set("/system/dynamicRange", {{"dynamicRange", value.get<std::string>()}});
		return true;
	};
	map.push_back(range);

	Control codec = make("format.codec", ControlKind::Choice, "phone", {"Dock.Format.Codec"},
			     "Dock.Format.Codec.Tooltip",
			     {"/system/format", "/system/supportedCodecFormats", "/system/codecFormat"});
	codec.stream = false;
	codec.read = [](const CameraControls &controls) {
		ControlState state;
		const nlohmann::json supported = controls.get("/system/supportedCodecFormats");
		if (supported.is_object() && supported.contains("codecFormats") &&
		    supported["codecFormats"].is_array()) {
			for (const nlohmann::json &item : supported["codecFormats"]) {
				const std::string name = string_at(item, "codec");
				if (!name.empty() &&
				    std::none_of(state.options.begin(), state.options.end(),
						 [&](const Option &option) { return option.id == name; }))
					state.options.push_back({name, name, {}});
			}
		}
		state.available = !state.options.empty();
		state.value = string_at(controls.get("/system/format"), "codec");
		return state;
	};
	codec.write = [](CameraControls &controls, const nlohmann::json &value) {
		controls.set("/system/codecFormat", {{"codec", value.get<std::string>()}, {"container", "mov"}});
		return true;
	};
	map.push_back(codec);

	Control record = make("record.active", ControlKind::Switch, "phone", {"Dock.Record"}, "Dock.Record.Tooltip",
			      {"/transports/0/record"});
	record.stream = false;
	// Recording starts and stops as actions, which the phone confirms as a new state, not as an answer
	record.confirm_path = "-";
	record.read = [](const CameraControls &controls) {
		ControlState state;
		const nlohmann::json value = controls.get("/transports/0/record");
		state.available = value.is_object();
		state.value = bool_at(value, "recording");
		return state;
	};
	record.write = [](CameraControls &controls, const nlohmann::json &value) {
		controls.act(value.get<bool>() ? "/transports/0/record" : "/transports/0/stop");
		return true;
	};
	map.push_back(record);

	map.push_back(enabled_switch("record.proxy", "phone", {"Dock.Record.Proxy"}, "Dock.Record.Proxy.Tooltip",
				     "/transports/0/proxyRecording", false));

	Control storage = make("record.storage", ControlKind::Text, "phone", {"Control.Storage"},
			       "Control.Storage.Tooltip", {"/media/workingset"});
	storage.stream = false;
	storage.unit = "s";
	storage.read = [](const CameraControls &controls) {
		ControlState state;
		const nlohmann::json media = controls.get("/media/workingset");
		double seconds = -1;
		if (media.is_object() && media.contains("workingset") && media["workingset"].is_array()) {
			for (const nlohmann::json &disk : media["workingset"]) {
				if (bool_at(disk, "activeDisk", true))
					seconds = number_at(disk, "remainingRecordTime", -1);
			}
		}
		state.available = seconds >= 0;
		state.value = seconds;
		return state;
	};
	map.push_back(storage);

	Control brightness = number("screen.brightness", "phone", {"Dock.Screen.Brightness"},
				    "Dock.Screen.Brightness.Tooltip", "/monitoring/Device/brightness", "brightness", 0,
				    100, 1, 0, "%");
	brightness.stream = false;
	brightness.read = [](const CameraControls &controls) {
		ControlState state;
		const nlohmann::json value = controls.get("/monitoring/Device/brightness");
		state.available = value.is_object();
		state.value = number_at(value, "brightness");
		state.minimum = 0;
		state.maximum = 100;
		state.step = 1;
		if (!bool_at(value, "adjustable", true))
			state.locked_by = kPhoneLocked;
		return state;
	};
	map.push_back(brightness);

	const char *tools[][3] = {
		{"screen.zebra", "zebra", "Dock.Screen.Zebra"},
		{"screen.focusAssist", "focusAssist", "Dock.Screen.FocusAssist"},
		{"screen.falseColor", "falseColor", "Dock.Screen.FalseColor"},
		{"screen.frameGuide", "frameGuide", "Dock.Screen.FrameGuide"},
		{"screen.grids", "frameGrids", "Dock.Screen.Grids"},
		{"screen.safeArea", "safeArea", "Dock.Screen.SafeArea"},
		{"screen.displayLut", "displayLUT", "Dock.Screen.DisplayLUT"},
	};
	for (const auto &tool : tools)
		map.push_back(enabled_switch(tool[0], "phone", {tool[2]}, std::string(tool[2]) + ".Tooltip",
					     std::string("/monitoring/Device/") + tool[1], false));

	Control ratio = make("screen.frameGuideRatio", ControlKind::Choice, "phone", {"Dock.Screen.GuideRatio"},
			     "Dock.Screen.GuideRatio.Tooltip",
			     {"/monitoring/frameGuideRatio", "/monitoring/frameGuideRatio/presets",
			      "/monitoring/Device/frameGuide"});
	ratio.stream = false;
	ratio.read = [](const CameraControls &controls) {
		ControlState state;
		for (const std::string &item :
		     strings_at(controls.get("/monitoring/frameGuideRatio/presets"), "presets"))
			state.options.push_back({item, item, {}});
		state.available = !state.options.empty() &&
				  bool_at(controls.get("/monitoring/Device/frameGuide"), "enabled");
		state.value = string_at(controls.get("/monitoring/frameGuideRatio"), "ratio");
		return state;
	};
	ratio.write = [](CameraControls &controls, const nlohmann::json &value) {
		controls.set("/monitoring/frameGuideRatio", {{"ratio", value.get<std::string>()}});
		return true;
	};
	map.push_back(ratio);

	Control safe = number("screen.safeAreaPercent", "phone", {"Dock.Screen.SafeAreaSize"},
			      "Dock.Screen.SafeAreaSize.Tooltip", "/monitoring/safeAreaPercent", "percent", 50, 100, 1,
			      0, "%");
	safe.stream = false;
	safe.paths.push_back("/monitoring/Device/safeArea");
	const auto safe_read = safe.read;
	safe.read = [safe_read](const CameraControls &controls) {
		ControlState state = safe_read(controls);
		state.available = state.available && bool_at(controls.get("/monitoring/Device/safeArea"), "enabled");
		return state;
	};
	map.push_back(safe);

	Control camera_id = number("camera.id", "phone", {"Dock.CameraNumber"}, "Dock.CameraNumber.Tooltip",
				   "/camera/id", "id", 0, 255, 1, 0);
	camera_id.stream = false;
	camera_id.write = [](CameraControls &controls, const nlohmann::json &value) {
		controls.set("/camera/id", {{"id", std::lround(value.get<double>())}});
		return true;
	};
	map.push_back(camera_id);

	Control battery = make("phone.battery", ControlKind::Text, "phone", {"Control.Battery"},
			       "Control.Battery.Tooltip", {"/camera/power"});
	battery.stream = false;
	battery.unit = "%";
	battery.read = [](const CameraControls &controls) {
		ControlState state;
		const nlohmann::json power = controls.get("/camera/power");
		const nlohmann::json batteries = power.is_object() ? power.value("batteries", nlohmann::json())
								   : nullptr;
		state.available = batteries.is_array() && !batteries.empty();
		if (state.available)
			state.value = std::lround(number_at(batteries[0], "chargeRemainingPercent"));
		return state;
	};
	map.push_back(battery);

	Control version = make("phone.version", ControlKind::Text, "phone", {"Control.Version"},
			       "Control.Version.Tooltip", {"/system/product"});
	version.stream = false;
	version.read = [](const CameraControls &controls) {
		ControlState state;
		const std::string value = string_at(controls.get("/system/product"), "softwareVersion");
		state.available = !value.empty();
		state.value = value;
		return state;
	};
	map.push_back(version);

	return map;
}

} // namespace

const std::vector<Control> &control_map()
{
	static const std::vector<Control> map = build();
	return map;
}

const Control *find_control(const std::string &id)
{
	const std::vector<Control> &map = control_map();
	const auto found =
		std::find_if(map.begin(), map.end(), [&](const Control &control) { return control.id == id; });
	return found != map.end() ? &*found : nullptr;
}

ControlWrite write_control(const Control &control, CameraControls &controls, const nlohmann::json &value)
{
	if (!control.read || !control.write || control.kind == ControlKind::Text)
		return ControlWrite::Invalid;
	const ControlState state = control.read(controls);
	if (!state.available)
		return ControlWrite::Unavailable;
	if (!state.locked_by.empty())
		return ControlWrite::Locked;
	nlohmann::json checked = value;
	switch (control.kind) {
	case ControlKind::Number: {
		if (!value.is_number())
			return ControlWrite::Invalid;
		const double number = value.get<double>();
		const double slack = (state.maximum - state.minimum) * 1e-6;
		if (number < state.minimum - slack || number > state.maximum + slack)
			return ControlWrite::Invalid;
		checked = std::clamp(number, state.minimum, state.maximum);
		break;
	}
	case ControlKind::Stops: {
		if (!value.is_number() || state.stops.empty())
			return ControlWrite::Invalid;
		const double number = value.get<double>();
		if (number < state.stops.front() / 2 || number > state.stops.back() * 2)
			return ControlWrite::Invalid;
		// The nearest stop
		const auto nearest = std::min_element(state.stops.begin(), state.stops.end(), [&](double a, double b) {
			return std::abs(a - number) < std::abs(b - number);
		});
		checked = *nearest;
		break;
	}
	case ControlKind::Choice:
		if (!value.is_string() ||
		    std::none_of(state.options.begin(), state.options.end(), [&](const ControlState::Option &option) {
			    return option.id == value.get<std::string>();
		    }))
			return ControlWrite::Invalid;
		break;
	case ControlKind::Switch:
		if (!value.is_boolean())
			return ControlWrite::Invalid;
		break;
	case ControlKind::Action:
	case ControlKind::Text:
		break;
	}
	return control.write(controls, checked) ? ControlWrite::Written : ControlWrite::Invalid;
}

} // namespace bmagicam
