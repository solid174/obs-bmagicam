// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "remote-api.hpp"

#include "../camera/background-work.hpp"
#include "../camera/camera-controls.hpp"
#include "../camera/camera-routines.hpp"
#include "../camera/control-map.hpp"
#include "../camera/look-library.hpp"
#include "../camera/phone-snapshot.hpp"
#include "../camera/stream-presets.hpp"
#include "../camera/values.hpp"
#include "../filter/beautify-filter.hpp"
#include "../filter/style-library.hpp"
#include "../source/camera-source.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <thread>

namespace bmagicam::remote {

namespace {

std::mutex theme_mutex;
nlohmann::json theme_palette = nlohmann::json::object();

std::string text(const std::string &key)
{
	return obs_module_text(key.c_str());
}

const char *kind_name(ControlKind kind)
{
	switch (kind) {
	case ControlKind::Number:
		return "number";
	case ControlKind::Stops:
		return "stops";
	case ControlKind::Choice:
		return "choice";
	case ControlKind::Switch:
		return "switch";
	case ControlKind::Action:
		return "action";
	case ControlKind::Text:
		return "text";
	}
	return "text";
}

const char *state_name(CameraSession::State state)
{
	switch (state) {
	case CameraSession::State::NoPhone:
		return "noPhone";
	case CameraSession::State::Searching:
		return "searching";
	case CameraSession::State::Connecting:
		return "connecting";
	case CameraSession::State::Starting:
		return "starting";
	case CameraSession::State::Live:
		return "live";
	case CameraSession::State::Paused:
		return "paused";
	case CameraSession::State::Error:
		return "error";
	}
	return "error";
}

std::string control_label(const Control &control)
{
	std::string label;
	for (const std::string &key : control.label_keys) {
		std::string part = text(key);
		if (label.empty() && !control.label_argument.empty()) {
			if (const size_t at = part.find("%1"); at != std::string::npos)
				part.replace(at, 2, control.label_argument);
		}
		label += (label.empty() ? "" : " · ") + part;
	}
	return label;
}

// Parses a locale file into the object, over what it already holds
void read_locale(const std::string &file, nlohmann::json &strings)
{
	char *path = obs_module_file(("locale/" + file).c_str());
	char *content = path ? os_quick_read_utf8_file(path) : nullptr;
	bfree(path);
	if (!content)
		return;
	std::string all = content;
	bfree(content);
	size_t start = 0;
	while (start < all.size()) {
		size_t end = all.find('\n', start);
		if (end == std::string::npos)
			end = all.size();
		const std::string line = all.substr(start, end - start);
		start = end + 1;
		const size_t equals = line.find('=');
		if (equals == std::string::npos || line.empty() || line[0] == '#' || line[0] == ';')
			continue;
		std::string value = line.substr(equals + 1);
		if (!value.empty() && value.back() == '\r')
			value.pop_back();
		if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
			value = value.substr(1, value.size() - 2);
		strings[line.substr(0, equals)] = value;
	}
}

// The camera with the ID, with a new reference; null when there is none
OBSSourceAutoRelease find_camera(const std::string &id)
{
	OBSSourceAutoRelease source = obs_get_source_by_uuid(id.c_str());
	if (!is_camera_source(source))
		return nullptr;
	return source;
}

// A camera's controls, or the reply that says why there are none
std::shared_ptr<CameraControls> connected_controls(const std::string &camera, Reply &reply)
{
	OBSSourceAutoRelease source = find_camera(camera);
	if (!source) {
		reply = error(404, "not_found", "No iPhone Camera with this ID");
		return nullptr;
	}
	std::shared_ptr<CameraControls> controls = camera_controls(source);
	if (!controls || !controls->connected()) {
		reply = error(503, "offline", "The camera is not connected");
		reply.body["camera"] = camera_summary(source);
		return nullptr;
	}
	return controls;
}

bool same_value(const nlohmann::json &a, const nlohmann::json &b, double tolerance)
{
	if (a.is_number() && b.is_number())
		return std::abs(a.get<double>() - b.get<double>()) <= tolerance;
	return a == b;
}

// The settings key under which a source keeps its look
constexpr const char *kLookSetting = "look";
constexpr const char *kPresetSetting = "preset";

} // namespace

Reply error(int status, const std::string &code, const std::string &message)
{
	return {status, {{"error", {{"code", code}, {"message", message}}}}};
}

void set_theme(nlohmann::json palette)
{
	std::lock_guard lock(theme_mutex);
	theme_palette = std::move(palette);
}

nlohmann::json theme()
{
	std::lock_guard lock(theme_mutex);
	return theme_palette;
}

nlohmann::json info()
{
	return {{"plugin", PLUGIN_VERSION},
		{"obs", obs_get_version_string()},
		{"api", 1},
		{"language", obs_get_locale()}};
}

nlohmann::json locale()
{
	nlohmann::json strings = nlohmann::json::object();
	read_locale("en-US.ini", strings);
	const std::string language = obs_get_locale();
	if (language != "en-US")
		read_locale(language + ".ini", strings);
	return strings;
}

nlohmann::json controls()
{
	nlohmann::json tabs = nlohmann::json::array();
	for (const auto &[id, key] : {std::pair<const char *, const char *>{"camera", "Control.Tab.Camera"},
				      {"color", "Dock.Tab.Color"},
				      {"focus", "Dock.Tab.Focus"},
				      {"beauty", "Dock.Tab.Beauty"},
				      {"audio", "Dock.Tab.Audio"},
				      {"phone", "Dock.Tab.Phone"}})
		tabs.push_back({{"id", id}, {"label", text(key)}});
	nlohmann::json list = nlohmann::json::array();
	for (const Control &control : control_map()) {
		nlohmann::json item = {{"id", control.id},
				       {"kind", kind_name(control.kind)},
				       {"tab", control.tab},
				       {"label", control_label(control)},
				       {"tooltip", text(control.tooltip_key)},
				       {"unit", control.unit},
				       {"decimals", control.decimals},
				       {"stream", control.stream},
				       {"simple", !control.simple_label_key.empty()}};
		if (!control.simple_label_key.empty())
			item["simpleLabel"] = text(control.simple_label_key);
		if (control.simple_ends.size() == 2)
			item["simpleEnds"] = {text(control.simple_ends[0]), text(control.simple_ends[1])};
		if (!control.companion.empty())
			item["companion"] = control.companion;
		list.push_back(item);
	}
	return {{"tabs", tabs}, {"controls", list}};
}

nlohmann::json stream_presets()
{
	nlohmann::json list = nlohmann::json::array();
	for (const StreamPreset &preset : bmagicam::stream_presets())
		list.push_back({{"id", preset.id},
				{"name", preset.profile},
				{"width", preset.width},
				{"height", preset.height},
				{"fps", preset.fps},
				{"bitrateKbps", preset.bitrate / 1000},
				{"description", text(preset.description_key)}});
	return list;
}

std::vector<OBSSource> camera_sources()
{
	std::vector<OBSSource> found;
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			if (is_camera_source(source))
				static_cast<std::vector<OBSSource> *>(param)->emplace_back(source);
			return true;
		},
		&found);
	return found;
}

nlohmann::json camera_summary(obs_source_t *source)
{
	const CameraSession::Status status = camera_status(source);
	const std::shared_ptr<CameraControls> controls = camera_controls(source);
	OBSDataAutoRelease settings = obs_source_get_settings(source);
	nlohmann::json phone = {{"name", status.phone}};
	if (controls)
		phone["version"] = string_at(controls->get("/system/product"), "softwareVersion");
	return {{"id", obs_source_get_uuid(source)},
		{"name", obs_source_get_name(source)},
		{"phone", phone},
		{"state", state_name(status.state)},
		{"text", camera_status_text(status)},
		{"connected", controls && controls->connected()},
		{"look", obs_data_get_string(settings, kLookSetting)},
		{"preset", stream_preset(obs_data_get_string(settings, kPresetSetting)).id}};
}

nlohmann::json control_state(const Control &control, const CameraControls &controls)
{
	const ControlState state = control.read(controls);
	nlohmann::json item = {{"available", state.available}, {"value", state.value}};
	if (control.kind == ControlKind::Action)
		item.erase("value");
	item["locked"] = !state.locked_by.empty();
	if (!state.locked_by.empty())
		item["lockReason"] = text(state.locked_by);
	switch (control.kind) {
	case ControlKind::Number:
		item["min"] = state.minimum;
		item["max"] = state.maximum;
		item["step"] = state.step;
		break;
	case ControlKind::Stops:
		item["stops"] = state.stops;
		if (!state.marks.empty())
			item["marks"] = state.marks;
		break;
	case ControlKind::Choice: {
		nlohmann::json options = nlohmann::json::array();
		for (const ControlState::Option &option : state.options)
			options.push_back(
				{{"id", option.id},
				 {"label", option.label_key.empty() ? option.label : text(option.label_key)}});
		item["options"] = options;
		break;
	}
	default:
		break;
	}
	return item;
}

nlohmann::json camera_detail(obs_source_t *source)
{
	nlohmann::json camera = camera_summary(source);
	nlohmann::json states = nlohmann::json::object();
	if (const std::shared_ptr<CameraControls> controls = camera_controls(source)) {
		for (const Control &control : control_map())
			states[control.id] = control_state(control, *controls);
	}
	camera["controls"] = states;
	return camera;
}

Reply put_control(const std::string &camera, const std::string &control_id, const nlohmann::json &body, bool wait)
{
	Reply reply;
	const std::shared_ptr<CameraControls> controls = connected_controls(camera, reply);
	if (!controls)
		return reply;
	const Control *control = find_control(control_id);
	if (!control)
		return error(404, "not_found", "No control with this ID");
	if (!body.is_object() || (!body.contains("value") && control->kind != ControlKind::Action))
		return error(400, "invalid", "The body must be {\"value\": ...}");
	const nlohmann::json value = body.value("value", nlohmann::json());
	switch (write_control(*control, *controls, value)) {
	case ControlWrite::Invalid:
		return error(400, "invalid", "The value does not fit this control");
	case ControlWrite::Unavailable:
		return error(422, "unsupported", "The phone does not offer this control now");
	case ControlWrite::Locked:
		return error(409, "locked", text(control->read(*controls).locked_by));
	case ControlWrite::Written:
		break;
	}
	const std::string confirm = control->confirm_path.empty() ? control->paths.front() : control->confirm_path;
	if (!wait || control->kind == ControlKind::Action || confirm == "-")
		return {202, {{"accepted", true}}};
	// Until the phone confirms the value, or 3 s
	const ControlState requested = control->read(*controls);
	const double tolerance = std::max(requested.step / 2, 1e-6);
	const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
	while (std::chrono::steady_clock::now() < until) {
		const int status = controls->write_status(confirm);
		if (status != 0 && status != 200 && status != 204)
			return error(status == 403 ? 409 : 400, status == 403 ? "locked" : "invalid",
				     "The phone refused the value");
		const ControlState now = control->read(*controls);
		if (same_value(now.value, value, tolerance) && status != 0)
			return {200, {{"value", now.value}}};
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	return error(504, "timeout", "The phone did not confirm the value within 3 s");
}

Reply run_action(const std::string &camera, const std::string &action, const nlohmann::json &body,
		 const std::function<void(const nlohmann::json &event)> &report)
{
	Reply reply;
	const std::shared_ptr<CameraControls> controls = connected_controls(camera, reply);
	if (!controls)
		return reply;
	const auto in_background = [&](std::function<bool(CameraControls &)> work) {
		run_in_background([controls, work = std::move(work), report, camera, action] {
			const bool done = work(*controls);
			if (report)
				report({{"op", "action"}, {"camera", camera}, {"action", action}, {"done", done}});
		});
		return Reply{202, {{"accepted", true}}};
	};
	if (action == "wb-auto") {
		controls->act("/video/whiteBalance/doAuto");
	} else if (action == "refocus") {
		controls->act("/lens/focus/autoFocus/retrigger");
	} else if (action == "focus-point") {
		const double x = body.is_object() ? body.value("x", -1.0) : -1.0;
		const double y = body.is_object() ? body.value("y", -1.0) : -1.0;
		if (x < 0 || x > 1 || y < 0 || y > 1)
			return error(400, "invalid", "x and y must be between 0 and 1");
		controls->act("/lens/focus/doAutoFocus", {{"position", {{"x", x}, {"y", y}}}});
	} else if (action == "record-start") {
		controls->act("/transports/0/record");
	} else if (action == "record-stop") {
		controls->act("/transports/0/stop");
	} else if (action == "set-up-for-streaming") {
		return in_background([](CameraControls &phone) { return set_up_for_streaming(phone); });
	} else if (action == "reset-defaults") {
		return in_background([](CameraControls &phone) { return reset_to_defaults(phone); });
	} else if (action == "restore-settings") {
		OBSSourceAutoRelease source = find_camera(camera);
		OBSDataAutoRelease settings = obs_source_get_settings(source);
		const std::string phone = obs_data_get_string(settings, "phone");
		const std::string key = phone == "manual" ? std::string(obs_data_get_string(settings, "address"))
							  : phone;
		if (!has_snapshot(key))
			return error(404, "not_found", "No settings saved from before obs-bmagicam for this phone");
		return in_background([key](CameraControls &phone) { return restore_snapshot(phone, key); });
	} else if (action == "reset-group") {
		const std::string group = body.is_object() ? body.value("group", std::string()) : std::string();
		if (group == "color") {
			apply_color(*controls, neutral_color());
		} else if (group == "focus") {
			controls->set("/lens/focus/autoFocus", {{"enabled", true}, {"mode", "Continuous"}});
			controls->set("/lens/focus/autoFocus/target", {{"x", 0.5}, {"y", 0.5}});
		} else if (group == "audio" || group == "screen") {
			std::vector<std::string> paths;
			if (group == "audio") {
				for (int channel = 0; channel < 2; channel++)
					for (const char *name : {"/lowCutFilter", "/padding", "/phantomPower"})
						paths.push_back("/audio/channel/" + std::to_string(channel) + name);
			} else {
				for (const char *tool :
				     {"zebra", "focusAssist", "falseColor", "frameGuide", "frameGrids", "safeArea"})
					paths.push_back(std::string("/monitoring/Device/") + tool);
			}
			for (const std::string &path : paths) {
				if (controls->get(path).is_object())
					controls->set(path, {{"enabled", false}});
			}
		} else {
			return error(400, "invalid", "group must be color, focus, audio or screen");
		}
	} else {
		return error(404, "not_found", "No action with this name");
	}
	return {202, {{"accepted", true}}};
}

Reply put_look(const std::string &camera, const nlohmann::json &body)
{
	Reply reply;
	const std::shared_ptr<CameraControls> controls = connected_controls(camera, reply);
	if (!controls)
		return reply;
	const std::string id = body.is_object() ? body.value("look", std::string()) : std::string();
	const std::vector<Look> looks_list = all_looks();
	const Look *look = find_look(looks_list, id);
	if (!look)
		return error(404, "not_found", "No look with this ID");
	apply_color(*controls, look->values);
	OBSSourceAutoRelease source = find_camera(camera);
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, kLookSetting, look->id.c_str());
	obs_source_update(source, settings);
	return {200, {{"look", look->id}}};
}

Reply put_stream(const std::string &camera, const nlohmann::json &body)
{
	OBSSourceAutoRelease source = find_camera(camera);
	if (!source)
		return error(404, "not_found", "No iPhone Camera with this ID");
	const std::string id = body.is_object() ? body.value("preset", std::string()) : std::string();
	const auto &all = bmagicam::stream_presets();
	if (std::none_of(all.begin(), all.end(), [&](const StreamPreset &preset) { return id == preset.id; }))
		return error(400, "invalid", "No stream preset with this ID");
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, kPresetSetting, id.c_str());
	obs_source_update(source, settings);
	return {200, {{"preset", id}}};
}

nlohmann::json looks()
{
	nlohmann::json list = nlohmann::json::array();
	for (const Look &look : all_looks())
		list.push_back({{"id", look.id},
				{"name", look.builtin ? text("Look." + look.id) : look.id},
				{"builtin", look.builtin},
				{"values", look.values}});
	return list;
}

Reply post_look(const nlohmann::json &body)
{
	const std::string name = body.is_object() ? body.value("name", std::string()) : std::string();
	const std::string camera = body.is_object() ? body.value("camera", std::string()) : std::string();
	if (name.empty() || find_look(builtin_looks(), name))
		return error(400, "invalid", "The look needs a name that is not a built-in look's");
	Reply reply;
	const std::shared_ptr<CameraControls> controls = connected_controls(camera, reply);
	if (!controls)
		return reply;
	const nlohmann::json color = current_color(*controls);
	if (color.is_null())
		return error(503, "offline", "The camera's color is not known yet");
	if (!save_user_look(name, color))
		return error(500, "failed", "The look could not be saved");
	return {201, {{"id", name}, {"name", name}, {"builtin", false}, {"values", color}}};
}

Reply patch_look(const std::string &look, const nlohmann::json &body)
{
	const std::vector<Look> list = user_looks();
	if (!find_look(list, look))
		return error(404, "not_found", "No look of yours with this ID");
	const std::string name = body.is_object() ? body.value("name", std::string()) : std::string();
	if (name.empty() || find_look(builtin_looks(), name))
		return error(400, "invalid", "The look needs a name that is not a built-in look's");
	if (find_look(list, name) && name != look)
		return error(409, "taken", "A look with this name exists");
	if (!rename_user_look(look, name))
		return error(500, "failed", "The look could not be renamed");
	return {200, {{"id", name}}};
}

Reply delete_look(const std::string &look)
{
	if (!find_look(user_looks(), look))
		return error(404, "not_found", "No look of yours with this ID");
	if (!delete_user_look(look))
		return error(500, "failed", "The look could not be deleted");
	return {200, {{"deleted", look}}};
}

nlohmann::json beautify_filter(obs_source_t *source, obs_source_t *filter)
{
	OBSDataAutoRelease settings = obs_source_get_settings(filter);
	nlohmann::json item = {{"source", obs_source_get_uuid(source)},
			       {"sourceName", obs_source_get_name(source)},
			       {"filter", obs_source_get_name(filter)},
			       {"enabled", obs_source_enabled(filter)},
			       {"style", obs_data_get_string(settings, kBeautyStyle)},
			       {"strength", obs_data_get_int(settings, kBeautyStrength)},
			       {"showMask", obs_data_get_bool(settings, kBeautyShowMask)}};
	for (const char *key : kBeautyKeys)
		item[key] = obs_data_get_int(settings, key);
	return item;
}

nlohmann::json beautify()
{
	nlohmann::json list = nlohmann::json::array();
	const auto collect = [](void *param, obs_source_t *source) {
		obs_source_enum_filters(
			source,
			[](obs_source_t *parent, obs_source_t *filter, void *data) {
				if (is_beautify_filter(filter))
					static_cast<nlohmann::json *>(data)->push_back(beautify_filter(parent, filter));
			},
			param);
		return true;
	};
	obs_enum_sources(collect, &list);
	obs_enum_scenes(collect, &list);
	return list;
}

nlohmann::json styles()
{
	nlohmann::json list = nlohmann::json::array();
	for (const BeautyStyle &style : all_styles()) {
		nlohmann::json values = nlohmann::json::object();
		for (size_t index = 0; index < kBeautyKeys.size(); index++)
			values[kBeautyKeys[index]] = beauty_value(style.values, index);
		list.push_back({{"id", style.id},
				{"name", style.builtin ? text("Beautify.Style." + style.id) : style.id},
				{"builtin", style.builtin},
				{"values", values}});
	}
	return list;
}

Reply put_beautify(const std::string &source_id, const std::string &filter_name, const nlohmann::json &body)
{
	OBSSourceAutoRelease source = obs_get_source_by_uuid(source_id.c_str());
	OBSSourceAutoRelease filter = source ? obs_source_get_filter_by_name(source, filter_name.c_str()) : nullptr;
	if (!filter || !is_beautify_filter(filter))
		return error(404, "not_found", "No Beautify filter with this source and name");
	if (!body.is_object())
		return error(400, "invalid", "The body must be an object");

	OBSDataAutoRelease settings = obs_source_get_settings(filter);
	OBSDataAutoRelease changes = obs_data_create();
	const std::vector<BeautyStyle> all = all_styles();
	// A style given sets its values first; values given go on top of them
	const BeautyStyle *chosen = nullptr;
	if (body.contains("style")) {
		const std::string id = body["style"].is_string() ? body["style"].get<std::string>() : std::string();
		chosen = find_style(all, id);
		if (!chosen && id != kCustomStyle)
			return error(400, "invalid", "No style with this ID");
	}
	BeautyValues values = chosen ? chosen->values : beauty_values(settings);
	bool values_changed = false;
	for (size_t index = 0; index < kBeautyKeys.size(); index++) {
		if (!body.contains(kBeautyKeys[index]))
			continue;
		const nlohmann::json &value = body[kBeautyKeys[index]];
		if (!value.is_number() || value.get<double>() < 0 || value.get<double>() > 100)
			return error(400, "invalid", std::string(kBeautyKeys[index]) + " must be 0–100");
		beauty_value(values, index) = static_cast<int>(std::lround(value.get<double>()));
		values_changed = true;
	}
	if (body.contains("strength")) {
		const nlohmann::json &value = body["strength"];
		if (!value.is_number() || value.get<double>() < 0 || value.get<double>() > 100)
			return error(400, "invalid", "strength must be 0–100");
		obs_data_set_int(changes, kBeautyStrength, std::lround(value.get<double>()));
	}
	if (body.contains("showMask")) {
		if (!body["showMask"].is_boolean())
			return error(400, "invalid", "showMask must be true or false");
		obs_data_set_bool(changes, kBeautyShowMask, body["showMask"].get<bool>());
	}
	if (body.contains("enabled") && !body["enabled"].is_boolean())
		return error(400, "invalid", "enabled must be true or false");

	// Values away from the style make it Custom, as in the dock
	const BeautyStyle *style =
		body.contains("style") ? chosen : find_style(all, obs_data_get_string(settings, kBeautyStyle));
	if (body.contains("style") || values_changed) {
		set_beauty_values(changes, values);
		const bool matches = style && style->values == values;
		obs_data_set_string(changes, kBeautyStyle, matches ? style->id.c_str() : kCustomStyle);
	}
	obs_source_update(filter, changes);
	if (body.contains("enabled"))
		obs_source_set_enabled(filter, body["enabled"].get<bool>());
	return {200, beautify_filter(source, filter)};
}

Reply add_beautify(const nlohmann::json &body)
{
	const std::string id = body.is_object() ? body.value("source", std::string()) : std::string();
	OBSSourceAutoRelease source = obs_get_source_by_uuid(id.c_str());
	if (!source || !(obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO))
		return error(404, "not_found", "No video source with this ID");
	if (OBSSourceAutoRelease existing = find_beautify_filter(source))
		return error(409, "exists", "The source has a Beautify filter");
	OBSSourceAutoRelease filter = add_beautify_filter(source, "natural", kDefaultBeautyStrength);
	if (!filter)
		return error(500, "failed", "The filter could not be added");
	return {201, beautify_filter(source, filter)};
}

} // namespace bmagicam::remote
