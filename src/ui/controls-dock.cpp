// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "controls-dock.hpp"

#include "camera-link.hpp"
#include "dock-kit.hpp"
#include "panels.hpp"
#include "setup-guide.hpp"
#include "../camera/background-work.hpp"
#include "../camera/camera-controls.hpp"
#include "../camera/camera-routines.hpp"
#include "../camera/phone-snapshot.hpp"
#include "../camera/values.hpp"
#include "../source/camera-source.hpp"
#include "../sync/microphone-sync.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <QApplication>
#include <QCheckBox>
#include <QInputDialog>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <vector>

namespace bmagicam::ui {

namespace {

ControlsDock *dock_instance = nullptr;

constexpr int kStatePollMs = 500;
constexpr int kMessageMs = 6000;
// The phone's battery level below which the status line asks for a charger
constexpr double kLowBattery = 20;
constexpr const char *kModeFile = "ui.json";

std::vector<OBSSource> camera_sources()
{
	std::vector<OBSSource> sources;
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			if (is_camera_source(source))
				static_cast<std::vector<OBSSource> *>(param)->emplace_back(source);
			return true;
		},
		&sources);
	return sources;
}

// "iPhone 17 Pro (A) · iPhone Camera": the phone, then the source's name
QString source_label(obs_source_t *source)
{
	std::string phone = camera_status(source).phone;
	if (phone.empty()) {
		OBSDataAutoRelease settings = obs_source_get_settings(source);
		phone = obs_data_get_string(settings, "phone_name");
		if (phone.empty())
			phone = obs_data_get_string(settings, "address");
	}
	const QString name = QString::fromUtf8(obs_source_get_name(source));
	return phone.empty() ? name : QString::fromStdString(phone) + QString::fromUtf8(" · ") + name;
}

bool load_advanced_mode()
{
	char *path = obs_module_config_path(kModeFile);
	OBSDataAutoRelease data = path ? obs_data_create_from_json_file_safe(path, "bak") : nullptr;
	bfree(path);
	return data && obs_data_get_bool(data, "advanced");
}

void save_advanced_mode(bool advanced)
{
	char *folder = obs_module_config_path("");
	if (folder)
		os_mkdirs(folder);
	bfree(folder);
	char *path = obs_module_config_path(kModeFile);
	if (path) {
		OBSDataAutoRelease data = obs_data_create();
		obs_data_set_bool(data, "advanced", advanced);
		obs_data_save_json_safe(data, path, "tmp", "bak");
	}
	bfree(path);
}

} // namespace

ControlsDock *ControlsDock::instance()
{
	return dock_instance;
}

ControlsDock::ControlsDock(QWidget *parent) : QWidget(parent)
{
	dock_instance = this;
	setMinimumWidth(280);
	const int fh = fontMetrics().height();

	link_ = new CameraLink(this);
	link_->changed = [this](const std::set<std::string> &paths) {
		if (paths.empty() || paths.count("/camera/power"))
			refresh_state();
		if (simple_)
			simple_->refresh(paths);
		if (advanced_panel_)
			advanced_panel_->refresh(paths);
	};

	picker_ = new QComboBox(this);
	picker_->setObjectName("phonePicker");
	picker_->setToolTip(text("Dock.Picker.Tooltip"));
	picker_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	picker_->setMinimumContentsLength(8);
	menu_ = new QToolButton(this);
	menu_->setObjectName("dockMenu");
	menu_->setToolTip(text("Dock.Menu"));
	set_theme_class(menu_, "icon-dots-vert");
	state_ = new QLabel(this);
	state_->setObjectName("phoneState");
	advanced_ = new QCheckBox(text("Dock.Advanced"), this);
	advanced_->setObjectName("advancedMode");
	advanced_->setToolTip(text("Dock.Advanced.Tooltip"));

	auto picker_row = new QHBoxLayout();
	picker_row->addWidget(picker_, 1);
	picker_row->addWidget(menu_);
	auto state_row = new QHBoxLayout();
	state_row->addWidget(state_, 1);
	state_row->addWidget(advanced_);

	empty_ = new QLabel(text("Dock.Empty"), this);
	empty_->setAlignment(Qt::AlignCenter);
	empty_->setWordWrap(true);
	set_theme_class(empty_, "text-muted");
	waiting_ = new QLabel(this);
	waiting_->setAlignment(Qt::AlignCenter);
	waiting_->setWordWrap(true);
	set_theme_class(waiting_, "text-muted");
	scroll_ = new QScrollArea(this);
	scroll_->setWidgetResizable(true);
	scroll_->setFrameShape(QFrame::NoFrame);
	scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	// The rows sit on the dock's own background
	scroll_->viewport()->setAutoFillBackground(false);
	pages_ = new QStackedWidget(this);
	pages_->addWidget(empty_);
	pages_->addWidget(waiting_);
	pages_->addWidget(scroll_);

	status_line_ = new QLabel(this);
	status_line_->setObjectName("statusLine");
	status_line_->setWordWrap(true);
	status_line_->hide();

	auto layout = new QVBoxLayout(this);
	layout->setSpacing(fh / 3);
	layout->addLayout(picker_row);
	layout->addLayout(state_row);
	layout->addWidget(pages_, 1);
	layout->addWidget(status_line_);

	connect(picker_, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
		OBSSourceAutoRelease source =
			obs_get_source_by_uuid(picker_->itemData(index).toString().toUtf8().constData());
		choose_source(OBSGetWeakRef(source));
	});
	connect(advanced_, &QCheckBox::toggled, this, [this](bool on) { set_advanced(on); });
	connect(menu_, &QToolButton::clicked, this, [this] { open_menu(); });
	connect(&state_timer_, &QTimer::timeout, this, [this] { refresh_state(); });
	state_timer_.start(kStatePollMs);
	message_timer_.setSingleShot(true);
	connect(&message_timer_, &QTimer::timeout, this, [this] {
		message_.clear();
		refresh_state();
	});

	signal_handler_t *handler = obs_get_signal_handler();
	for (const char *signal : {"source_create", "source_destroy", "source_rename"})
		signal_handler_connect(handler, signal, source_list_changed, this);
	obs_frontend_add_event_callback(frontend_event, this);

	advanced_->setChecked(load_advanced_mode());
	refresh_sources();
}

ControlsDock::~ControlsDock()
{
	signal_handler_t *handler = obs_get_signal_handler();
	for (const char *signal : {"source_create", "source_destroy", "source_rename"})
		signal_handler_disconnect(handler, signal, source_list_changed, this);
	if (frontend_callback_)
		obs_frontend_remove_event_callback(frontend_event, this);
	if (OBSSourceAutoRelease scene = obs_weak_source_get_source(watched_scene_))
		signal_handler_disconnect(obs_source_get_signal_handler(scene), "item_select", item_selected, this);
	if (dock_instance == this)
		dock_instance = nullptr;
}

void ControlsDock::source_list_changed(void *data, calldata_t *)
{
	// Sources come and go on any thread; the list is read again on the Qt thread
	auto dock = static_cast<ControlsDock *>(data);
	QMetaObject::invokeMethod(dock, [dock] { dock->refresh_sources(); }, Qt::QueuedConnection);
}

void ControlsDock::frontend_event(enum obs_frontend_event event, void *data)
{
	auto dock = static_cast<ControlsDock *>(data);
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		dock->refresh_sources();
		dock->follow_scene_selection();
		break;
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
		dock->follow_scene_selection();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		dock->choose_source({});
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		// OBS drops its callbacks before it destroys the docks
		obs_frontend_remove_event_callback(frontend_event, dock);
		dock->frontend_callback_ = false;
		break;
	case OBS_FRONTEND_EVENT_THEME_CHANGED:
		// The dock's own controls paint from the palette; the new one shows on the next paint
		for (QWidget *widget : dock->findChildren<QWidget *>())
			widget->update();
		break;
	default:
		break;
	}
}

void ControlsDock::item_selected(void *data, calldata_t *calldata)
{
	auto item = static_cast<obs_sceneitem_t *>(calldata_ptr(calldata, "item"));
	obs_source_t *source = item ? obs_sceneitem_get_source(item) : nullptr;
	if (!is_camera_source(source))
		return;
	const QString uuid = QString::fromUtf8(obs_source_get_uuid(source));
	auto dock = static_cast<ControlsDock *>(data);
	QMetaObject::invokeMethod(
		dock,
		[dock, uuid] {
			OBSSourceAutoRelease chosen = obs_get_source_by_uuid(uuid.toUtf8().constData());
			if (chosen)
				dock->show_source(chosen);
		},
		Qt::QueuedConnection);
}

void ControlsDock::follow_scene_selection()
{
	if (OBSSourceAutoRelease old_scene = obs_weak_source_get_source(watched_scene_))
		signal_handler_disconnect(obs_source_get_signal_handler(old_scene), "item_select", item_selected, this);
	OBSSourceAutoRelease scene = obs_frontend_preview_program_mode_active()
					     ? obs_frontend_get_current_preview_scene()
					     : obs_frontend_get_current_scene();
	watched_scene_ = OBSGetWeakRef(scene);
	if (scene)
		signal_handler_connect(obs_source_get_signal_handler(scene), "item_select", item_selected, this);
}

void ControlsDock::refresh_sources()
{
	const std::vector<OBSSource> sources = camera_sources();
	OBSSourceAutoRelease current = obs_weak_source_get_source(source_);

	picker_->clear();
	int current_index = -1;
	for (const OBSSource &source : sources) {
		if (source == current.Get())
			current_index = picker_->count();
		picker_->addItem(source_label(source), QString::fromUtf8(obs_source_get_uuid(source)));
	}
	picker_->setVisible(!sources.empty());

	if (current_index >= 0) {
		picker_->setCurrentIndex(current_index);
		refresh_state();
		return;
	}
	// The chosen source is gone: the first one takes over
	choose_source(sources.empty() ? OBSWeakSource() : OBSGetWeakRef(sources.front().Get()));
}

void ControlsDock::show_source(obs_source_t *source)
{
	if (!is_camera_source(source))
		return;
	const int index = picker_->findData(QString::fromUtf8(obs_source_get_uuid(source)));
	if (index < 0)
		refresh_sources();
	OBSSourceAutoRelease current = obs_weak_source_get_source(source_);
	if (current.Get() != source)
		choose_source(OBSGetWeakRef(source));
}

void ControlsDock::choose_source(const OBSWeakSource &source)
{
	source_ = source;
	OBSSourceAutoRelease chosen = obs_weak_source_get_source(source_);
	const int index = chosen ? picker_->findData(QString::fromUtf8(obs_source_get_uuid(chosen))) : -1;
	if (index >= 0)
		picker_->setCurrentIndex(index);
	link_->set_controls(camera_controls(chosen));
	rebuild();
}

void ControlsDock::set_advanced(bool advanced)
{
	save_advanced_mode(advanced);
	rebuild();
}

void ControlsDock::rebuild()
{
	if (QWidget *old = scroll_->takeWidget())
		old->deleteLater();
	simple_ = nullptr;
	advanced_panel_ = nullptr;

	OBSSourceAutoRelease source = obs_weak_source_get_source(source_);
	if (!source) {
		refresh_state();
		return;
	}

	PanelContext context;
	context.controls = camera_controls(source);
	context.source = source_;
	context.notify = [guard = QPointer<ControlsDock>(this)](const QString &message) {
		if (guard)
			guard->show_message(message);
	};
	if (advanced_->isChecked()) {
		advanced_panel_ = new AdvancedPanel(context);
		scroll_->setWidget(advanced_panel_);
	} else {
		simple_ = new SimplePanel(context);
		scroll_->setWidget(simple_);
	}
	scroll_->widget()->setAutoFillBackground(false);
	refresh_state();
}

void ControlsDock::refresh_state()
{
	OBSSourceAutoRelease source = obs_weak_source_get_source(source_);
	if (!source) {
		state_->clear();
		state_->hide();
		advanced_->hide();
		menu_->hide();
		pages_->setCurrentWidget(empty_);
		status_line_->hide();
		return;
	}
	state_->show();
	advanced_->show();
	menu_->show();

	const CameraSession::Status status = camera_status(source);
	const char *key = "Dock.State.Connecting";
	const char *theme_class = "text-warning";
	switch (status.state) {
	case CameraSession::State::Live:
		key = "Dock.State.Live";
		theme_class = "text-success";
		break;
	case CameraSession::State::Searching:
		key = "Dock.State.Searching";
		break;
	case CameraSession::State::Starting:
		key = "Dock.State.Starting";
		break;
	case CameraSession::State::Paused:
		key = "Dock.State.Paused";
		theme_class = "text-muted";
		break;
	case CameraSession::State::Error:
		key = "Dock.State.Error";
		theme_class = "text-danger";
		break;
	case CameraSession::State::NoPhone:
		key = "Dock.State.NoPhone";
		break;
	case CameraSession::State::Connecting:
		break;
	}
	state_->setText(text(key));
	set_theme_class(state_, theme_class);
	const QString detail = QString::fromStdString(camera_status_text(status));
	state_->setToolTip(detail);

	const int index = picker_->currentIndex();
	if (index >= 0) {
		const QString label = source_label(source);
		if (picker_->itemText(index) != label)
			picker_->setItemText(index, label);
	}

	const std::shared_ptr<CameraControls> controls = camera_controls(source);
	if (controls && controls->connected()) {
		pages_->setCurrentWidget(scroll_);
	} else {
		waiting_->setText(status.state == CameraSession::State::Error ? detail : text("Dock.Waiting"));
		pages_->setCurrentWidget(waiting_);
	}
	if (advanced_panel_ && pages_->currentWidget() == scroll_)
		advanced_panel_->tick();

	// A message for a moment, otherwise a warning when something needs attention
	QString line = message_;
	const char *line_class = "text-muted";
	if (line.isEmpty() && controls && controls->connected()) {
		const nlohmann::json power = controls->get("/camera/power");
		const nlohmann::json batteries = power.is_object() ? power.value("batteries", nlohmann::json())
								   : nullptr;
		if (string_at(power, "source") == "Battery" && batteries.is_array() && !batteries.empty()) {
			const double level = number_at(batteries[0], "chargeRemainingPercent", 100);
			if (level <= kLowBattery) {
				line = text("Dock.LowBattery").arg(static_cast<int>(level));
				line_class = "text-warning";
			}
		}
	}
	// Doubled sound: the iPhone's own sound and a computer microphone that is not synced both reach the mix, so
	// viewers hear the voice twice, the microphone ahead (SYN-1)
	if (line.isEmpty() && is_audible(source)) {
		QString microphone;
		obs_enum_sources(
			[](void *param, obs_source_t *candidate) {
				auto name = static_cast<QString *>(param);
				if (name->isEmpty() && is_computer_microphone(candidate) && is_audible(candidate) &&
				    obs_source_get_sync_offset(candidate) == 0)
					*name = QString::fromUtf8(obs_source_get_name(candidate));
				return true;
			},
			&microphone);
		if (!microphone.isEmpty()) {
			line = text("Dock.DoubleSound").arg(microphone);
			line_class = "text-warning";
		}
	}
	status_line_->setText(line);
	set_theme_class(status_line_, line_class);
	status_line_->setVisible(!line.isEmpty());
}

void ControlsDock::show_message(const QString &message)
{
	message_ = message;
	message_timer_.start(kMessageMs);
	refresh_state();
}

void ControlsDock::open_menu()
{
	OBSSourceAutoRelease source = obs_weak_source_get_source(source_);
	const std::shared_ptr<CameraControls> controls = camera_controls(source);
	const bool ready = controls && controls->connected();

	QMenu menu(this);
	QAction *set_up = menu.addAction(text("Dock.SetUp"));
	set_up->setEnabled(ready);
	connect(set_up, &QAction::triggered, this, [this, controls] {
		PanelContext context;
		context.controls = controls;
		context.notify = [guard = QPointer<ControlsDock>(this)](const QString &message) {
			if (guard)
				guard->show_message(message);
		};
		show_message(text("Dock.SetUp.Running"));
		run_set_up_for_streaming(context, nullptr);
	});
	// The phone's own presets (docs/ui.md, "Header, menu and status line")
	QMenu *presets = menu.addMenu(text("Dock.Presets"));
	presets->setEnabled(ready);
	const std::vector<std::string> names = controls ? strings_at(controls->get("/presets"), "presets")
							: std::vector<std::string>();
	QMenu *load = presets->addMenu(text("Dock.Presets.Load"));
	QMenu *remove = presets->addMenu(text("Dock.Presets.Delete"));
	load->setEnabled(!names.empty());
	remove->setEnabled(!names.empty());
	for (const std::string &name : names) {
		const QString label = QString::fromStdString(name);
		connect(load->addAction(label), &QAction::triggered, this, [this, controls, name] {
			run_on_phone(
				text("Dock.Presets.Loading"),
				[controls, name] {
					return controls->put_now("/presets/active", {{"preset", name}}).ok();
				},
				"Dock.Presets.Loaded", "Dock.Presets.Failed");
		});
		connect(remove->addAction(label), &QAction::triggered, this, [this, controls, name, label] {
			if (QMessageBox::question(this, text("Dock.Presets.Delete.Title"),
						  text("Dock.Presets.Delete.Question").arg(label)) != QMessageBox::Yes)
				return;
			run_on_phone(
				text("Dock.Presets.Deleting"),
				[controls, name] {
					const bool done =
						controls->remove_now("/presets/" + QUrl::toPercentEncoding(
											   QString::fromStdString(name))
											   .toStdString())
							.ok();
					controls->get_now("/presets");
					return done;
				},
				"Dock.Presets.Deleted", "Dock.Presets.Failed");
		});
	}
	connect(presets->addAction(text("Dock.Presets.Save")), &QAction::triggered, this, [this] { save_preset(); });

	menu.addSeparator();
	QAction *reset = menu.addAction(text("Dock.Reset"));
	reset->setEnabled(ready);
	connect(reset, &QAction::triggered, this, [this] { reset_to_defaults(); });
	QAction *restore = menu.addAction(text("Dock.Restore"));
	OBSDataAutoRelease settings = source ? obs_source_get_settings(source) : nullptr;
	const std::string phone = settings ? obs_data_get_string(settings, "phone") : "";
	const std::string phone_key = phone == "manual" ? std::string(obs_data_get_string(settings, "address")) : phone;
	restore->setEnabled(ready && has_snapshot(phone_key));
	connect(restore, &QAction::triggered, this, [this] { restore_settings(); });
	menu.addSeparator();
	connect(menu.addAction(text("Camera.SetupGuide")), &QAction::triggered, this, [this] {
		auto guide = new SetupGuideDialog(this);
		guide->setAttribute(Qt::WA_DeleteOnClose);
		guide->show();
	});
	menu.exec(menu_->mapToGlobal(QPoint(0, menu_->height())));
}

void ControlsDock::run_on_phone(const QString &running, std::function<bool()> step, const char *done_key,
				const char *failed_key)
{
	show_message(running);
	run_in_background([step = std::move(step), guard = QPointer<ControlsDock>(this), done_key, failed_key] {
		const bool done = step();
		QMetaObject::invokeMethod(
			qApp,
			[guard, done, done_key, failed_key] {
				if (guard)
					guard->show_message(text(done ? done_key : failed_key));
			},
			Qt::QueuedConnection);
	});
}

void ControlsDock::save_preset()
{
	OBSSourceAutoRelease source = obs_weak_source_get_source(source_);
	const std::shared_ptr<CameraControls> controls = camera_controls(source);
	if (!controls)
		return;
	bool ok = false;
	const QString name = QInputDialog::getText(this, text("Dock.Presets.Save.Title"),
						   text("Dock.Presets.Save.Name"), QLineEdit::Normal, QString(), &ok)
				     .trimmed();
	if (!ok || name.isEmpty())
		return;
	const std::string path = "/presets/" + QUrl::toPercentEncoding(name).toStdString();
	run_on_phone(
		text("Dock.Presets.Saving"),
		[controls, path] {
			const bool done = controls->put_now(path, nullptr).ok();
			controls->get_now("/presets");
			return done;
		},
		"Dock.Presets.Saved", "Dock.Presets.Failed");
}

void ControlsDock::restore_settings()
{
	OBSSourceAutoRelease source = obs_weak_source_get_source(source_);
	const std::shared_ptr<CameraControls> controls = camera_controls(source);
	if (!controls)
		return;
	if (QMessageBox::question(this, text("Dock.Restore.Title"), text("Dock.Restore.Question")) != QMessageBox::Yes)
		return;
	OBSDataAutoRelease settings = obs_source_get_settings(source);
	const std::string phone = obs_data_get_string(settings, "phone");
	const std::string phone_key = phone == "manual" ? std::string(obs_data_get_string(settings, "address")) : phone;
	run_on_phone(
		text("Dock.Restore.Running"), [controls, phone_key] { return restore_snapshot(*controls, phone_key); },
		"Dock.Restore.Done", "Dock.Restore.Failed");
}

void ControlsDock::reset_to_defaults()
{
	OBSSourceAutoRelease source = obs_weak_source_get_source(source_);
	const std::shared_ptr<CameraControls> controls = camera_controls(source);
	if (!controls)
		return;
	if (QMessageBox::question(this, text("Dock.Reset.Title"), text("Dock.Reset.Question")) != QMessageBox::Yes)
		return;
	run_on_phone(
		text("Dock.Reset.Running"), [controls] { return bmagicam::reset_to_defaults(*controls); },
		"Dock.Reset.Done", "Dock.Reset.Failed");
}

} // namespace bmagicam::ui
