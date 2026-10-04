// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "panels.hpp"

#include "beauty-panel.hpp"
#include "microphone-row.hpp"
#include "widgets/buttons.hpp"
#include "widgets/ruler.hpp"
#include "widgets/segmented.hpp"
#include "../camera/background-work.hpp"
#include "../camera/camera-controls.hpp"
#include "../camera/camera-routines.hpp"
#include "../camera/look-library.hpp"
#include "../camera/values.hpp"

#include <obs.hpp>

#include <QApplication>
#include <QHBoxLayout>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

#include <cmath>

namespace bmagicam::ui {

std::vector<LensChoice> lens_choices(const nlohmann::json &cameras)
{
	std::vector<LensChoice> choices;
	if (!cameras.is_object() || !cameras.contains("cameras") || !cameras["cameras"].is_array())
		return choices;
	const nlohmann::json &list = cameras["cameras"];

	// One Front button, for the active front lens or the first one
	const nlohmann::json *front = nullptr;
	for (const nlohmann::json &lens : list) {
		if (string_at(lens, "facing") == "front" && (!front || bool_at(lens, "isActive")))
			front = &lens;
	}
	if (front)
		choices.push_back(
			{string_at(*front, "id"), text("Dock.Lens.Front"), bool_at(*front, "isAvailable", true)});

	for (const nlohmann::json &lens : list) {
		if (string_at(lens, "facing") != "back")
			continue;
		QString label = QString::fromStdString(string_at(lens, "zoomFactor"));
		label.replace(QLatin1Char('x'), QString::fromUtf8("×"));
		if (label.isEmpty())
			label = QStringLiteral("%1 mm").arg(number_at(lens, "focalLength"));
		choices.push_back({string_at(lens, "id"), label, bool_at(lens, "isAvailable", true)});
	}
	return choices;
}

int lens_choice_index(const std::vector<LensChoice> &choices, const nlohmann::json &cameras,
		      const nlohmann::json &active)
{
	const std::string id = string_at(active, "id");
	for (size_t index = 0; index < choices.size(); index++) {
		if (choices[index].id == id)
			return static_cast<int>(index);
	}
	// Another front lens is still the Front button
	if (cameras.is_object() && cameras.contains("cameras") && cameras["cameras"].is_array()) {
		for (const nlohmann::json &lens : cameras["cameras"]) {
			if (string_at(lens, "id") == id && string_at(lens, "facing") == "front" && !choices.empty() &&
			    choices.front().label == text("Dock.Lens.Front"))
				return 0;
		}
	}
	return -1;
}

QString look_label(const std::string &id, bool builtin)
{
	return builtin ? text(("Look." + id).c_str()) : QString::fromStdString(id);
}

std::string source_look(obs_source_t *source)
{
	OBSDataAutoRelease settings = obs_source_get_settings(source);
	return obs_data_get_string(settings, "look");
}

void set_source_look(obs_source_t *source, const std::string &id)
{
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "look", id.c_str());
	obs_source_update(source, settings);
}

void run_set_up_for_streaming(const PanelContext &context, QPushButton *button)
{
	if (button) {
		button->setEnabled(false);
		button->setText(text("Dock.SetUp.Running"));
	}
	QPointer<QPushButton> guard(button);
	run_in_background([controls = context.controls, notify = context.notify, guard] {
		const bool complete = set_up_for_streaming(*controls);
		QMetaObject::invokeMethod(
			qApp,
			[notify, guard, complete] {
				if (guard) {
					guard->setEnabled(true);
					guard->setText(text("Dock.SetUp"));
				}
				if (notify)
					notify(text(complete ? "Dock.SetUp.Done" : "Dock.SetUp.Failed"));
			},
			Qt::QueuedConnection);
	});
}

SimplePanel::SimplePanel(PanelContext context, QWidget *parent)
	: QWidget(parent),
	  context_(std::move(context)),
	  looks_list_(all_looks())
{
	auto layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(fontMetrics().height() * 3 / 4);
	CameraControls &controls = *context_.controls;

	set_up_ = new QPushButton(text("Dock.SetUp"), this);
	set_up_->setObjectName("setUpForStreaming");
	set_up_->setToolTip(text("Dock.SetUp.Tooltip"));
	set_up_->setMinimumHeight(fontMetrics().height() * 2);
	connect(set_up_, &QPushButton::clicked, this, [this] { run_set_up_for_streaming(context_, set_up_); });
	layout->addWidget(set_up_);

	look_row_ = add_row("Dock.Look", "Dock.Look.Tooltip");
	looks_ = new Segmented(look_row_);
	looks_->setObjectName("looks");
	look_row_->set_control(looks_);
	looks_->activated = [this](int index) {
		if (index < 0 || index >= static_cast<int>(looks_list_.size()))
			return;
		const Look &look = looks_list_[static_cast<size_t>(index)];
		apply_color(*context_.controls, look.values);
		if (OBSSourceAutoRelease source = obs_weak_source_get_source(context_.source))
			set_source_look(source, look.id);
	};
	std::vector<std::string> color_watch;
	for (const std::string &key : color_keys())
		color_watch.push_back(color_path(key));
	watches_.add(color_watch, [this] { refresh_looks(); });

	brightness_row_ = add_row("Dock.Brightness", "Dock.Brightness.Tooltip");
	brightness_auto_ = new Chip(text("Dock.Auto"), brightness_row_);
	brightness_auto_->setObjectName("brightnessAuto");
	brightness_row_->add_extra(brightness_auto_);
	brightness_ = new Ruler(brightness_row_);
	brightness_->setObjectName("brightness");
	brightness_->set_plain(text("Dock.Brightness.Darker"), text("Dock.Brightness.Brighter"));
	brightness_->set_spacing(1.2);
	brightness_row_->set_control(brightness_);
	brightness_->changed = [this, &controls](double value, bool) {
		brightness_guard_.touched();
		controls.set("/video/iso", {{"iso", std::lround(value)}});
	};
	connect(brightness_auto_, &QAbstractButton::clicked, this,
		[&controls](bool on) { controls.set("/video/autoExposure", {{"mode", on ? "Continuous" : "Off"}}); });
	watches_.add({"/video/supportedISOs", "/video/iso", "/video/autoExposure"}, [this] { refresh_brightness(); });

	warmth_row_ = add_row("Dock.Warmth", "Dock.Warmth.Tooltip");
	// Auto measures once, so it is a button in the shape of the other Auto chips
	auto warmth_auto = new Chip(text("Dock.Auto"), warmth_row_);
	warmth_auto->setCheckable(false);
	warmth_auto->setObjectName("warmthAuto");
	warmth_auto->setToolTip(text("Dock.Warmth.Auto.Tooltip"));
	warmth_row_->add_extra(warmth_auto);
	warmth_ = new Ruler(warmth_row_);
	warmth_->setObjectName("warmth");
	warmth_->set_plain(text("Dock.Warmth.Cool"), text("Dock.Warmth.Warm"));
	warmth_->set_spacing(0.3);
	warmth_row_->set_control(warmth_);
	warmth_->changed = [this, &controls](double value, bool) {
		warmth_guard_.touched();
		controls.set("/video/whiteBalance", {{"whiteBalance", std::lround(value)}});
	};
	connect(warmth_auto, &QAbstractButton::clicked, this,
		[&controls] { controls.act("/video/whiteBalance/doAuto"); });
	watches_.add({"/video/whiteBalance", "/video/whiteBalance/description"}, [this] { refresh_warmth(); });

	lens_row_ = add_row("Dock.Lens", "Dock.Lens.Tooltip");
	lenses_ = new Segmented(lens_row_);
	lenses_->setObjectName("lenses");
	lens_row_->set_control(lenses_);
	lenses_->activated = [this, &controls](int index) {
		if (index >= 0 && index < static_cast<int>(lens_choices_.size()))
			controls.set("/lens/cameras/active", {{"id", lens_choices_[static_cast<size_t>(index)].id}});
	};
	watches_.add({"/lens/cameras", "/lens/cameras/active"}, [this] { refresh_lenses(); });

	focus_row_ = add_row("Dock.Focus", "Dock.Focus.Tooltip");
	auto focus_controls = new QWidget(focus_row_);
	auto focus_layout = new QHBoxLayout(focus_controls);
	focus_layout->setContentsMargins(0, 0, 0, 0);
	focus_auto_ = new Chip(text("Dock.Auto"), focus_controls);
	focus_auto_->setObjectName("focusAuto");
	focus_auto_->setToolTip(text("Dock.Focus.Auto.Tooltip"));
	auto refocus = new QPushButton(text("Dock.Focus.Refocus"), focus_controls);
	refocus->setObjectName("refocus");
	refocus->setToolTip(text("Dock.Focus.Refocus.Tooltip"));
	focus_layout->addWidget(focus_auto_);
	focus_layout->addWidget(refocus);
	focus_layout->addStretch();
	focus_row_->set_control(focus_controls);
	connect(focus_auto_, &QAbstractButton::clicked, this, [&controls](bool on) {
		controls.set("/lens/focus/autoFocus", {{"enabled", true}, {"mode", on ? "Continuous" : "OneShot"}});
	});
	connect(refocus, &QPushButton::clicked, this, [&controls] { controls.act("/lens/focus/autoFocus/retrigger"); });
	watches_.add({"/lens/focus/autoFocus"}, [this] { refresh_focus(); });

	stabilization_row_ = add_row("Dock.Stabilize", "Dock.Stabilize.Tooltip");
	stabilization_ = new Segmented(stabilization_row_);
	stabilization_->setObjectName("stabilization");
	stabilization_->set_items({text("Dock.Off"), text("Dock.On")});
	stabilization_row_->set_control(stabilization_);
	stabilization_->activated = [&controls](int index) {
		controls.set("/lens/opticalImageStabilization", {{"enabled", index == 1}});
	};
	watches_.add({"/lens/opticalImageStabilization"}, [this] { refresh_stabilization(); });

	layout->addWidget(new BeautyPanel(context_, false, this));
	layout->addWidget(new MicrophoneRow(context_, this));
	layout->addStretch();
	refresh({});
}

Row *SimplePanel::add_row(const char *title_key, const char *tooltip_key)
{
	auto row = new Row(text(title_key), text(tooltip_key), this);
	layout()->addWidget(row);
	return row;
}

void SimplePanel::refresh(const std::set<std::string> &paths)
{
	set_up_->setVisible(true);
	watches_.refresh(paths);
}

void SimplePanel::refresh_looks()
{
	QStringList labels;
	for (const Look &look : looks_list_)
		labels.push_back(look_label(look.id, look.builtin));
	looks_->set_items(labels);

	const nlohmann::json color = current_color(*context_.controls);
	look_row_->setVisible(!color.is_null());
	if (color.is_null())
		return;

	// The look the source chose, if the phone still shows it, else the first that matches
	std::string chosen;
	if (OBSSourceAutoRelease source = obs_weak_source_get_source(context_.source))
		chosen = source_look(source);
	int selected = -1;
	for (size_t index = 0; index < looks_list_.size(); index++) {
		if (same_color(looks_list_[index].values, color) && (selected < 0 || looks_list_[index].id == chosen))
			selected = static_cast<int>(index);
	}
	looks_->set_selected(selected);
}

void SimplePanel::refresh_brightness()
{
	const CameraControls &controls = *context_.controls;
	const std::vector<double> isos = numbers_at(controls.get("/video/supportedISOs"), "supportedISOs");
	const nlohmann::json iso = controls.get("/video/iso");
	brightness_row_->setVisible(!isos.empty() && iso.is_object());
	if (isos.empty() || !iso.is_object())
		return;

	brightness_->set_stops(isos);
	const bool automatic = string_at(controls.get("/video/autoExposure"), "mode") == "Continuous";
	brightness_auto_->setChecked(automatic);
	// Under auto exposure the camera moves ISO; the ruler follows and says how to take over (CTL-4)
	brightness_->setEnabled(!automatic);
	brightness_->setToolTip(text(automatic ? "Dock.Brightness.Locked" : "Dock.Brightness.Tooltip"));
	if (brightness_guard_.settled(this, [this] { refresh_brightness(); }))
		brightness_->set_value(number_at(iso, "iso"));
}

void SimplePanel::refresh_warmth()
{
	const CameraControls &controls = *context_.controls;
	const nlohmann::json value = controls.get("/video/whiteBalance");
	const nlohmann::json range = controls.get("/video/whiteBalance/description");
	warmth_row_->setVisible(value.is_object());
	if (!value.is_object())
		return;

	const nlohmann::json limits = range.is_object() ? range.value("whiteBalance", nlohmann::json()) : nullptr;
	warmth_->set_range(number_at(limits, "min", 2500), number_at(limits, "max", 10000), 50);
	if (warmth_guard_.settled(this, [this] { refresh_warmth(); }))
		warmth_->set_value(number_at(value, "whiteBalance"));
}

void SimplePanel::refresh_lenses()
{
	const CameraControls &controls = *context_.controls;
	const nlohmann::json cameras = controls.get("/lens/cameras");
	lens_choices_ = lens_choices(cameras);
	lens_row_->setVisible(lens_choices_.size() > 1);
	QStringList labels;
	for (const LensChoice &choice : lens_choices_)
		labels.push_back(choice.label);
	lenses_->set_items(labels);
	for (size_t index = 0; index < lens_choices_.size(); index++)
		lenses_->set_item_enabled(static_cast<int>(index), lens_choices_[index].available);
	lenses_->set_selected(lens_choice_index(lens_choices_, cameras, controls.get("/lens/cameras/active")));
}

void SimplePanel::refresh_focus()
{
	const nlohmann::json focus = context_.controls->get("/lens/focus/autoFocus");
	focus_row_->setVisible(focus.is_object());
	const std::string mode = string_at(focus, "mode");
	focus_auto_->setChecked(bool_at(focus, "enabled", true) &&
				(mode == "Continuous" || mode.rfind("Track", 0) == 0));
}

void SimplePanel::refresh_stabilization()
{
	const nlohmann::json value = context_.controls->get("/lens/opticalImageStabilization");
	stabilization_row_->setVisible(value.is_object());
	stabilization_->set_selected(bool_at(value, "enabled") ? 1 : 0);
	// Some formats do not allow changing it (docs/architecture.md, "Stabilization")
	stabilization_->setEnabled(bool_at(value, "controlAvailable", true));
}

} // namespace bmagicam::ui
