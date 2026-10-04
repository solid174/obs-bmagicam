// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <vector>

namespace bmagicam {

class CameraControls;

// How a control shows and changes (docs/ui.md, "Controls")
enum class ControlKind {
	// A range in even steps
	Number,
	// A list of values, such as the ISOs the phone supports
	Stops,
	// One of several options
	Choice,
	// On or off
	Switch,
	// Something the phone does once, such as refocus
	Action,
	// Read only
	Text,
};

// One control's state on one phone
struct ControlState {
	// The phone answers for it; controls it does not have are not shown (CTL-1)
	bool available = false;
	nlohmann::json value;
	// The locale key that says what locks it and how to unlock it (CTL-4); empty when it can be changed
	std::string locked_by;
	// Number
	double minimum = 0;
	double maximum = 0;
	double step = 0;
	// Stops, and those marked, such as the flicker-free shutters
	std::vector<double> stops;
	std::vector<double> marks;
	// Choice: each option's ID with its name, or the locale key of its name
	struct Option {
		std::string id;
		std::string label;
		std::string label_key;
	};
	std::vector<Option> options;
};

// A control of the phone as the dock, the web panel and the API offer it (docs/ui.md, "Control map")
struct Control {
	// Its ID in the API, such as "wb.temperature"
	std::string id;
	ControlKind kind = ControlKind::Number;
	// Where it shows in Advanced mode
	std::string tab;
	// The locale keys of its name, joined with " · "; a %1 in the first takes label_argument
	std::vector<std::string> label_keys;
	std::string label_argument;
	std::string tooltip_key;
	// Simple mode shows it under this name, with words under the ruler's two ends instead of numbers; empty when it is
	// in Advanced mode only (UI-4)
	std::string simple_label_key;
	std::vector<std::string> simple_ends;
	// Another control shown in the same row, such as Auto beside Brightness
	std::string companion;
	// How values read: "K", "mm", "%", "dB", "1/" before a shutter speed, "f/" before an aperture, "s" for a duration,
	// or a locale key prefix that names each value
	std::string unit;
	int decimals = 0;
	// The stream changes with it; some change the phone's screen or its recordings only (CTL-7)
	bool stream = true;
	// The properties it shows, so a change to one of them changes the control
	std::vector<std::string> paths;
	// The property whose answer confirms a write; the first of paths when empty, none for actions
	std::string confirm_path;
	std::function<ControlState(const CameraControls &)> read;
	// Writes a value; false when the value does not fit the control
	std::function<bool(CameraControls &, const nlohmann::json &)> write;
};

// Every control, in the order the panels show them
const std::vector<Control> &control_map();
// The control with the ID; null when there is none
const Control *find_control(const std::string &id);

enum class ControlWrite { Written, Invalid, Unavailable, Locked };
// Checks a value against the control's state on the phone and writes it: numbers within the range, stops snapped to
// the nearest stop, options by their ID, switches as booleans
ControlWrite write_control(const Control &control, CameraControls &controls, const nlohmann::json &value);

} // namespace bmagicam
