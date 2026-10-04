// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <nlohmann/json.hpp>
#include <obs.hpp>

#include <functional>
#include <string>
#include <vector>

namespace bmagicam {

class CameraControls;
struct Control;

namespace remote {

// The answer to an API request: an HTTP status and a JSON body (docs/remote-api.md)
struct Reply {
	int status = 200;
	nlohmann::json body;
};
Reply error(int status, const std::string &code, const std::string &message);

// The OBS theme's palette, which the user interface sets on the Qt thread whenever the theme changes
void set_theme(nlohmann::json palette);
nlohmann::json theme();

nlohmann::json info();
// Every UI string in OBS's language, with English for the ones not translated
nlohmann::json locale();
// The tabs and the descriptors of every control (docs/ui.md, "Control map")
nlohmann::json controls();
nlohmann::json stream_presets();

// The iPhone Camera sources
std::vector<OBSSource> camera_sources();
// A camera without its controls: ID, name, phone, state, look and preset
nlohmann::json camera_summary(obs_source_t *source);
// A camera with every control's state
nlohmann::json camera_detail(obs_source_t *source);
nlohmann::json control_state(const Control &control, const CameraControls &controls);

Reply put_control(const std::string &camera, const std::string &control, const nlohmann::json &body, bool wait);
// Runs an action; the long ones, such as the resets, answer at once and report when they are done
Reply run_action(const std::string &camera, const std::string &action, const nlohmann::json &body,
		 const std::function<void(const nlohmann::json &event)> &report);
Reply put_look(const std::string &camera, const nlohmann::json &body);
Reply put_stream(const std::string &camera, const nlohmann::json &body);

nlohmann::json looks();
Reply post_look(const nlohmann::json &body);
Reply patch_look(const std::string &look, const nlohmann::json &body);
Reply delete_look(const std::string &look);

// Every Beautify filter on any source
nlohmann::json beautify();
nlohmann::json beautify_filter(obs_source_t *source, obs_source_t *filter);
nlohmann::json styles();
Reply put_beautify(const std::string &source, const std::string &filter, const nlohmann::json &body);
// Adds a Beautify filter with the default style to a source that has none, as the dock's Add Beauty does
Reply add_beautify(const nlohmann::json &body);

} // namespace remote
} // namespace bmagicam
