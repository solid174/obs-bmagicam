// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "panels.hpp"

#include "microphone-row.hpp"
#include "widgets/buttons.hpp"
#include "widgets/flow-layout.hpp"
#include "widgets/histogram.hpp"
#include "widgets/ruler.hpp"
#include "widgets/segmented.hpp"
#include "../camera/camera-controls.hpp"
#include "../camera/look-library.hpp"
#include "../camera/stream-presets.hpp"
#include "../camera/values.hpp"
#include "../source/camera-source.hpp"

#include <obs-module.h>

#include <QButtonGroup>
#include <QComboBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace bmagicam::ui {

namespace {

// White balance presets, as quick values under the ruler (docs/ui.md, "Controls")
struct WhiteBalancePreset {
	int kelvin;
	const char *key;
};
constexpr WhiteBalancePreset kWhiteBalancePresets[] = {
	{3200, "Dock.WB.Tungsten"}, {4000, "Dock.WB.Fluorescent"}, {5600, "Dock.WB.Daylight"},
	{6500, "Dock.WB.Cloudy"},   {7500, "Dock.WB.Shade"},
};

// Lays its tiles out in as many columns as fit, in rows of equal tiles
class TileStrip : public QWidget {
public:
	explicit TileStrip(QWidget *parent) : QWidget(parent)
	{
		QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
		policy.setHeightForWidth(true);
		setSizePolicy(policy);
	}

	void add(Tile *tile)
	{
		tile->setParent(this);
		tiles_.push_back(tile);
	}

	bool hasHeightForWidth() const override { return true; }

	int heightForWidth(int width) const override
	{
		const int rows = rows_for(width);
		return rows * tile_height() + (rows - 1) * gap();
	}

	QSize sizeHint() const override
	{
		const int width = static_cast<int>(tiles_.size()) * tile_width();
		return {width, heightForWidth(width)};
	}

	QSize minimumSizeHint() const override { return {tile_width(), heightForWidth(tile_width())}; }

protected:
	void resizeEvent(QResizeEvent *) override
	{
		const int rows = rows_for(width());
		const int count = static_cast<int>(tiles_.size());
		const int columns = (count + rows - 1) / rows;
		for (int index = 0; index < count; index++) {
			const int row = index / columns;
			const int in_row = std::min(columns, count - row * columns);
			const int column = index % columns;
			const int item_width = (width() - (in_row - 1) * gap()) / in_row;
			tiles_[static_cast<size_t>(index)]->setGeometry(column * (item_width + gap()),
									row * (tile_height() + gap()), item_width,
									tile_height());
		}
	}

private:
	int tile_width() const { return fontMetrics().height() * 9 / 2; }
	int tile_height() const { return tiles_.empty() ? 0 : tiles_.front()->sizeHint().height(); }
	int gap() const { return fontMetrics().height() / 4; }
	int rows_for(int width) const
	{
		const int count = std::max(1, static_cast<int>(tiles_.size()));
		const int columns = std::clamp((width + gap()) / (tile_width() + gap()), 1, count);
		return (count + columns - 1) / columns;
	}

	std::vector<Tile *> tiles_;
};

// Stacks and tabs take the height of the page shown, not of the tallest one
void fit_to_current(QStackedWidget *stack)
{
	for (int index = 0; index < stack->count(); index++)
		stack->widget(index)->setSizePolicy(QSizePolicy::Preferred, index == stack->currentIndex()
										    ? QSizePolicy::Preferred
										    : QSizePolicy::Ignored);
	stack->updateGeometry();
}

void fit_to_current(QTabWidget *tabs)
{
	for (int index = 0; index < tabs->count(); index++)
		tabs->widget(index)->setSizePolicy(QSizePolicy::Preferred, index == tabs->currentIndex()
										   ? QSizePolicy::Preferred
										   : QSizePolicy::Ignored);
	tabs->updateGeometry();
}

QVBoxLayout *page_layout(QWidget *page)
{
	auto layout = new QVBoxLayout(page);
	const int fh = page->fontMetrics().height();
	layout->setContentsMargins(fh / 2, fh / 2, fh / 2, fh / 2);
	layout->setSpacing(fh / 2);
	return layout;
}

QLabel *muted_label(const QString &content, QWidget *parent)
{
	auto label = new QLabel(content, parent);
	label->setWordWrap(true);
	set_theme_class(label, "text-muted");
	return label;
}

// The shutter as a speed (1/x), also when the phone reports it as an angle
double shutter_speed(const nlohmann::json &shutter, double fps)
{
	const double speed = number_at(shutter, "shutterSpeed");
	const double angle = number_at(shutter, "shutterAngle");
	if (speed <= 0 && angle > 0 && fps > 0)
		return fps * 360 / angle;
	return speed;
}

double frame_rate(const nlohmann::json &format)
{
	return std::strtod(string_at(format, "frameRate").c_str(), nullptr);
}

QString duration_text(double seconds)
{
	const int minutes = static_cast<int>(seconds / 60);
	if (minutes >= 60)
		return text("Dock.Duration.Hours").arg(minutes / 60).arg(minutes % 60);
	return text("Dock.Duration.Minutes").arg(minutes);
}

} // namespace

AdvancedPanel::AdvancedPanel(PanelContext context, QWidget *parent)
	: QWidget(parent),
	  context_(std::move(context)),
	  looks_list_(all_looks())
{
	auto layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(fontMetrics().height() / 2);
	add_top(layout);
	add_tiles(layout);

	tabs_ = new QTabWidget(this);
	tabs_->setObjectName("advancedTabs");
	tabs_->addTab(color_tab(), text("Dock.Tab.Color"));
	tabs_->addTab(focus_tab(), text("Dock.Tab.Focus"));
	tabs_->addTab(audio_tab(), text("Dock.Tab.Audio"));
	tabs_->addTab(phone_tab(), text("Dock.Tab.Phone"));
	connect(tabs_, &QTabWidget::currentChanged, this, [this] { fit_to_current(tabs_); });
	fit_to_current(tabs_);
	layout->addWidget(tabs_);
	layout->addStretch();

	refresh({});
	tick();
}

void AdvancedPanel::refresh(const std::set<std::string> &paths)
{
	watches_.refresh(paths);
}

void AdvancedPanel::tick()
{
	OBSSourceAutoRelease source = obs_weak_source_get_source(context_.source);
	if (!source)
		return;
	histogram_->set_histogram(camera_histogram(source));

	const nlohmann::json live = context_.controls->get("/livestreams/0");
	const QString format = QString::fromStdString(string_at(live, "effectiveVideoFormat"));
	const double bitrate = number_at(live, "bitrate") / 1e6;
	const CameraSession::Status status = camera_status(source);
	stream_->setText(status.state == CameraSession::State::Live && bitrate > 0
				 ? text("Dock.Stream.Live").arg(format).arg(bitrate, 0, 'f', 1)
				 : QString::fromStdString(camera_status_text(status)));
}

void AdvancedPanel::add_top(QVBoxLayout *layout)
{
	histogram_ = new HistogramView(this);
	histogram_->setToolTip(text("Dock.Histogram.Tooltip"));
	layout->addWidget(histogram_);

	auto look_row = new QHBoxLayout();
	auto look_label = new QLabel(text("Dock.Look"), this);
	look_label->setToolTip(text("Dock.Look.Tooltip"));
	look_ = new QComboBox(this);
	look_->setObjectName("lookChoice");
	look_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	look_->setMinimumContentsLength(6);
	look_->setToolTip(text("Dock.Look.Tooltip"));
	auto save = new QPushButton(this);
	save->setObjectName("saveLook");
	save->setToolTip(text("Dock.Look.Save.Tooltip"));
	set_theme_class(save, "icon-plus");
	delete_look_ = new QPushButton(this);
	delete_look_->setObjectName("deleteLook");
	delete_look_->setToolTip(text("Dock.Look.Delete.Tooltip"));
	set_theme_class(delete_look_, "icon-trash");
	look_row->addWidget(look_label);
	look_row->addWidget(look_, 1);
	look_row->addWidget(save);
	look_row->addWidget(delete_look_);
	layout->addLayout(look_row);

	connect(look_, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
		const std::string id = look_->itemData(index).toString().toStdString();
		const Look *look = find_look(looks_list_, id);
		if (!look)
			return;
		apply_color(*context_.controls, look->values);
		if (OBSSourceAutoRelease source = obs_weak_source_get_source(context_.source))
			set_source_look(source, look->id);
	});
	connect(save, &QPushButton::clicked, this, [this] { save_look(); });
	connect(delete_look_, &QPushButton::clicked, this, [this] { delete_look(); });
	std::vector<std::string> color_watch;
	for (const std::string &key : color_keys())
		color_watch.push_back(color_path(key));
	watches_.add(color_watch, [this] { refresh_look_choice(); });

	stream_ = muted_label({}, this);
	battery_ = muted_label({}, this);
	layout->addWidget(stream_);
	layout->addWidget(battery_);
	watches_.add({"/camera/power", "/system/product"}, [this] {
		const nlohmann::json power = context_.controls->get("/camera/power");
		const nlohmann::json batteries = power.is_object() ? power.value("batteries", nlohmann::json())
								   : nullptr;
		QString line;
		if (batteries.is_array() && !batteries.empty())
			line = text("Dock.Battery")
				       .arg(static_cast<int>(number_at(batteries[0], "chargeRemainingPercent")));
		const std::string version = string_at(context_.controls->get("/system/product"), "softwareVersion");
		if (!version.empty())
			line += (line.isEmpty() ? QString() : QStringLiteral(" · ")) +
				text("Dock.AppVersion").arg(QString::fromStdString(version));
		battery_->setText(line);
		battery_->setVisible(!line.isEmpty());
	});
}

void AdvancedPanel::refresh_look_choice()
{
	const nlohmann::json color = current_color(*context_.controls);
	std::string chosen;
	if (OBSSourceAutoRelease source = obs_weak_source_get_source(context_.source))
		chosen = source_look(source);
	const Look *chosen_look = find_look(looks_list_, chosen);

	// The look the source chose, marked when the color was changed since
	look_->clear();
	int selected = -1;
	for (const Look &look : looks_list_) {
		QString label = look_label(look.id, look.builtin);
		const bool matches = !color.is_null() && same_color(look.values, color);
		if (look.id == chosen && !matches)
			label = text("Dock.Look.Changed").arg(label);
		if ((look.id == chosen) || (matches && (selected < 0 || !chosen_look)))
			selected = look_->count();
		look_->addItem(label, QString::fromStdString(look.id));
	}
	look_->setCurrentIndex(selected);
	const Look *current = selected >= 0 ? &looks_list_[static_cast<size_t>(selected)] : nullptr;
	delete_look_->setEnabled(current && !current->builtin);
}

void AdvancedPanel::save_look()
{
	const nlohmann::json color = current_color(*context_.controls);
	if (color.is_null())
		return;
	bool ok = false;
	const QString name = QInputDialog::getText(this, text("Dock.Look.Save.Title"), text("Dock.Look.Save.Name"),
						   QLineEdit::Normal, QString(), &ok)
				     .trimmed();
	if (!ok || name.isEmpty())
		return;
	if (save_user_look(name.toStdString(), color)) {
		looks_list_ = all_looks();
		if (OBSSourceAutoRelease source = obs_weak_source_get_source(context_.source))
			set_source_look(source, name.toStdString());
		refresh_look_choice();
		context_.notify(text("Dock.Look.Saved").arg(name));
	}
}

void AdvancedPanel::delete_look()
{
	const std::string id = look_->currentData().toString().toStdString();
	const Look *look = find_look(looks_list_, id);
	if (!look || look->builtin)
		return;
	const QString name = QString::fromStdString(id);
	if (QMessageBox::question(this, text("Dock.Look.Delete.Title"), text("Dock.Look.Delete.Question").arg(name)) !=
	    QMessageBox::Yes)
		return;
	if (delete_user_look(id)) {
		looks_list_ = all_looks();
		refresh_look_choice();
	}
}

void AdvancedPanel::add_tiles(QVBoxLayout *layout)
{
	const char *labels[kTileCount] = {"Dock.Tile.Lens", "Dock.Tile.FPS", "Dock.Tile.Shutter", "Dock.Tile.Iris",
					  "Dock.Tile.ISO",  "Dock.Tile.WB",  "Dock.Tile.Tint"};
	const char *tooltips[kTileCount] = {"Dock.Lens.Tooltip", "Dock.Preset.Tooltip", "Dock.Shutter.Tooltip",
					    "Dock.Iris.Tooltip", "Dock.ISO.Tooltip",    "Dock.WB.Tooltip",
					    "Dock.Tint.Tooltip"};
	auto strip = new TileStrip(this);
	auto group = new QButtonGroup(this);
	group->setExclusive(true);
	for (int index = 0; index < kTileCount; index++) {
		auto tile = new Tile(text(labels[index]), strip);
		tile->setObjectName(QStringLiteral("tile%1").arg(index));
		tile->setToolTip(text(tooltips[index]));
		strip->add(tile);
		group->addButton(tile, index);
		tiles_.push_back(tile);
	}
	layout->addWidget(strip);

	adjusters_ = new QStackedWidget(this);
	adjusters_->addWidget(lens_adjuster());
	adjusters_->addWidget(preset_adjuster());
	adjusters_->addWidget(shutter_adjuster());
	adjusters_->addWidget(iris_adjuster());
	adjusters_->addWidget(iso_adjuster());
	adjusters_->addWidget(white_balance_adjuster());
	adjusters_->addWidget(tint_adjuster());
	layout->addWidget(adjusters_);

	connect(group, &QButtonGroup::idClicked, this, [this](int index) { select_tile(index); });
	watches_.add({"/lens/zoom", "/system/videoFormat", "/video/shutter", "/video/autoExposure", "/lens/iris",
		      "/video/iso", "/video/whiteBalance", "/video/whiteBalanceTint"},
		     [this] { refresh_tiles(); });
	select_tile(kLensTile);
}

void AdvancedPanel::select_tile(int index)
{
	tiles_[static_cast<size_t>(index)]->setChecked(true);
	adjusters_->setCurrentIndex(index);
	fit_to_current(adjusters_);
}

void AdvancedPanel::refresh_tiles()
{
	const CameraControls &controls = *context_.controls;
	const nlohmann::json zoom = controls.get("/lens/zoom");
	tiles_[kLensTile]->set_value(zoom.is_object()
					     ? QStringLiteral("%1 mm").arg(std::lround(number_at(zoom, "focalLength")))
					     : QStringLiteral("—"));
	const double fps = frame_rate(controls.get("/system/videoFormat"));
	tiles_[kFpsTile]->set_value(fps > 0 ? QString::number(fps, 'g', 4) : QStringLiteral("—"));

	const bool automatic = string_at(controls.get("/video/autoExposure"), "mode") == "Continuous";
	const nlohmann::json shutter = controls.get("/video/shutter");
	const double speed = shutter_speed(shutter, fps);
	tiles_[kShutterTile]->set_value(speed > 0 ? QStringLiteral("1/%1").arg(std::lround(speed))
						  : QStringLiteral("—"));
	tiles_[kShutterTile]->set_automatic(automatic || bool_at(shutter, "continuousShutterAutoExposure"));

	const nlohmann::json iris = controls.get("/lens/iris");
	tiles_[kIrisTile]->set_value(iris.is_object()
					     ? QStringLiteral("f/%1").arg(number_at(iris, "apertureStop"), 0, 'f', 1)
					     : QStringLiteral("—"));
	const nlohmann::json iso = controls.get("/video/iso");
	tiles_[kIsoTile]->set_value(iso.is_object() ? QString::number(std::lround(number_at(iso, "iso")))
						    : QStringLiteral("—"));
	tiles_[kIsoTile]->set_automatic(automatic);
	const nlohmann::json white_balance = controls.get("/video/whiteBalance");
	tiles_[kWhiteBalanceTile]->set_value(
		white_balance.is_object()
			? QStringLiteral("%1 K").arg(std::lround(number_at(white_balance, "whiteBalance")))
			: QStringLiteral("—"));
	const nlohmann::json tint = controls.get("/video/whiteBalanceTint");
	const long tint_value = std::lround(number_at(tint, "whiteBalanceTint"));
	tiles_[kTintTile]->set_value(tint.is_object() ? (tint_value > 0 ? QStringLiteral("+%1").arg(tint_value)
									: QString::number(tint_value))
						      : QStringLiteral("—"));
}

Ruler *AdvancedPanel::number_ruler(const std::string &path, const std::string &field, int decimals,
				   std::function<QString(double)> format)
{
	auto ruler = new Ruler();
	if (!format)
		format = [decimals](double value) {
			return QString::number(value, 'f', decimals);
		};
	ruler->set_format(std::move(format));
	ruler->set_spacing(0.4);
	guards_.push_back(std::make_unique<EditGuard>());
	EditGuard *guard = guards_.back().get();
	ruler->changed = [this, guard, path, field](double value, bool) {
		guard->touched();
		context_.controls->set(path, {{field, value}});
	};
	watches_.add({path}, [this, ruler, guard, path, field] { follow_number(ruler, guard, path, field); });
	return ruler;
}

void AdvancedPanel::follow_number(Ruler *ruler, EditGuard *guard, const std::string &path, const std::string &field)
{
	const nlohmann::json value = context_.controls->get(path);
	if (!value.is_object())
		return;
	if (guard->settled(this, [this, ruler, guard, path, field] { follow_number(ruler, guard, path, field); }))
		ruler->set_value(number_at(value, field.c_str()));
}

Chip *AdvancedPanel::enabled_chip(const char *label_key, const char *tooltip_key, const std::string &path)
{
	auto chip = new Chip(text(label_key));
	chip->setToolTip(text(tooltip_key));
	connect(chip, &QAbstractButton::clicked, this,
		[this, path](bool on) { context_.controls->set(path, {{"enabled", on}}); });
	watches_.add({path}, [this, chip, path] {
		const nlohmann::json value = context_.controls->get(path);
		chip->setVisible(value.is_object());
		chip->setChecked(bool_at(value, "enabled"));
	});
	return chip;
}

QPushButton *AdvancedPanel::reset_button(const char *tooltip_key, std::function<void()> reset)
{
	auto button = new QPushButton();
	button->setToolTip(text(tooltip_key));
	set_theme_class(button, "icon-revert");
	connect(button, &QPushButton::clicked, this, [reset = std::move(reset)] { reset(); });
	return button;
}

QWidget *AdvancedPanel::lens_adjuster()
{
	auto page = new QWidget();
	auto layout = page_layout(page);
	auto lenses = new Segmented(page);
	lenses->setObjectName("advancedLenses");
	lenses->setToolTip(text("Dock.Lens.Tooltip"));
	layout->addWidget(lenses);

	auto zoom_row = new Row(text("Dock.Zoom"), text("Dock.Zoom.Tooltip"), page);
	auto base = new Chip(QStringLiteral("1×"), zoom_row);
	base->setCheckable(false);
	base->setToolTip(text("Dock.Zoom.Base.Tooltip"));
	zoom_row->add_extra(base);
	Ruler *zoom = number_ruler("/lens/zoom", "focalLength", 0,
				   [](double value) { return QStringLiteral("%1 mm").arg(std::lround(value)); });
	zoom->setObjectName("zoom");
	zoom->set_spacing(0.25);
	zoom_row->set_control(zoom);
	layout->addWidget(zoom_row);

	auto choices = std::make_shared<std::vector<LensChoice>>();
	lenses->activated = [this, choices](int index) {
		if (index >= 0 && index < static_cast<int>(choices->size()))
			context_.controls->set("/lens/cameras/active",
					       {{"id", (*choices)[static_cast<size_t>(index)].id}});
	};
	connect(base, &QAbstractButton::clicked, this, [this] {
		const nlohmann::json range = context_.controls->get("/lens/zoom/description");
		const nlohmann::json limits = range.is_object() ? range.value("focalLength", nlohmann::json())
								: nullptr;
		context_.controls->set("/lens/zoom", {{"focalLength", std::lround(number_at(limits, "min", 0))}});
	});
	watches_.add({"/lens/cameras", "/lens/cameras/active"}, [this, lenses, choices] {
		const nlohmann::json cameras = context_.controls->get("/lens/cameras");
		*choices = lens_choices(cameras);
		QStringList labels;
		for (const LensChoice &choice : *choices)
			labels.push_back(choice.label);
		lenses->set_items(labels);
		lenses->setVisible(choices->size() > 1);
		lenses->set_selected(
			lens_choice_index(*choices, cameras, context_.controls->get("/lens/cameras/active")));
	});
	watches_.add({"/lens/zoom/description"}, [this, zoom_row, zoom] {
		const nlohmann::json range = context_.controls->get("/lens/zoom/description");
		const nlohmann::json limits = range.is_object() ? range.value("focalLength", nlohmann::json())
								: nullptr;
		const bool controllable = bool_at(range, "controllable", true) && limits.is_object();
		zoom_row->setVisible(controllable);
		if (controllable)
			zoom->set_range(number_at(limits, "min"), number_at(limits, "max"), 1);
	});
	return page;
}

QWidget *AdvancedPanel::preset_adjuster()
{
	auto page = new QWidget();
	auto layout = page_layout(page);
	auto presets = new Segmented(page);
	presets->setObjectName("presets");
	QStringList labels;
	QStringList tooltips;
	for (const StreamPreset &preset : stream_presets()) {
		labels.push_back(QString::fromUtf8(preset.profile));
		tooltips.push_back(text(preset.description_key));
	}
	presets->set_items(labels, tooltips);
	layout->addWidget(presets);
	layout->addWidget(muted_label(text("Dock.Preset.Note"), page));

	const auto show_current = [this, presets] {
		OBSSourceAutoRelease source = obs_weak_source_get_source(context_.source);
		if (!source)
			return;
		OBSDataAutoRelease settings = obs_source_get_settings(source);
		const std::string id = stream_preset(obs_data_get_string(settings, "preset")).id;
		const auto &all = stream_presets();
		for (size_t index = 0; index < all.size(); index++) {
			if (id == all[index].id)
				presets->set_selected(static_cast<int>(index));
		}
	};
	presets->activated = [this](int index) {
		OBSSourceAutoRelease source = obs_weak_source_get_source(context_.source);
		if (!source || index < 0)
			return;
		// The stream preset belongs to the source (docs/architecture.md); the source sets the phone up again
		OBSDataAutoRelease settings = obs_data_create();
		obs_data_set_string(settings, "preset", stream_presets()[static_cast<size_t>(index)].id);
		obs_source_update(source, settings);
	};
	watches_.add({"/system/videoFormat"}, show_current);
	return page;
}

QWidget *AdvancedPanel::shutter_adjuster()
{
	auto page = new QWidget();
	auto layout = page_layout(page);
	auto row = new Row(text("Dock.Shutter"), text("Dock.Shutter.Tooltip"), page);
	auto automatic = new Chip(text("Dock.Auto"), row);
	automatic->setObjectName("shutterAuto");
	automatic->setToolTip(text("Dock.Exposure.Auto.Tooltip"));
	row->add_extra(automatic);
	auto shutter = new Ruler(row);
	shutter->setObjectName("shutter");
	shutter->set_format([](double value) { return QStringLiteral("1/%1").arg(std::lround(value)); });
	row->set_control(shutter);
	layout->addWidget(row);

	auto quick = new QWidget(page);
	auto quick_layout = new FlowLayout(quick);
	layout->addWidget(quick);

	guards_.push_back(std::make_unique<EditGuard>());
	EditGuard *guard = guards_.back().get();
	shutter->changed = [this, guard](double value, bool) {
		guard->touched();
		context_.controls->set("/video/shutter", {{"shutterSpeed", std::lround(value)}});
	};
	connect(automatic, &QAbstractButton::clicked, this, [this](bool on) {
		context_.controls->set("/video/autoExposure", {{"mode", on ? "Continuous" : "Off"}});
	});

	auto quick_values = std::make_shared<std::vector<double>>();
	auto follow = std::make_shared<std::function<void()>>();
	*follow = [this, row, automatic, shutter, quick, quick_layout, quick_values, guard,
		   weak = std::weak_ptr<std::function<void()>>(follow)] {
		const CameraControls &controls = *context_.controls;
		const nlohmann::json supported = controls.get("/video/supportedShutters");
		const std::vector<double> speeds = numbers_at(supported, "shutterSpeeds");
		const nlohmann::json value = controls.get("/video/shutter");
		row->setVisible(!speeds.empty() && value.is_object());
		if (speeds.empty())
			return;
		std::vector<double> sorted = speeds;
		std::sort(sorted.begin(), sorted.end());
		shutter->set_stops(sorted);
		const std::vector<double> flicker_free =
			numbers_at(controls.get("/video/flickerFreeShutters"), "shutterSpeeds");
		shutter->set_marks(flicker_free);

		const bool is_automatic = string_at(controls.get("/video/autoExposure"), "mode") == "Continuous";
		automatic->setChecked(is_automatic);
		shutter->setEnabled(!is_automatic);
		shutter->setToolTip(text(is_automatic ? "Dock.Exposure.Locked" : "Dock.Shutter.Tooltip"));
		quick->setEnabled(!is_automatic);

		// The flicker-free shutters as quick values, marked like on the ruler
		if (*quick_values != flicker_free) {
			*quick_values = flicker_free;
			while (QLayoutItem *item = quick_layout->takeAt(0)) {
				delete item->widget();
				delete item;
			}
			for (double speed : flicker_free) {
				auto button = new QPushButton(QStringLiteral("1/%1 ✓").arg(std::lround(speed)), quick);
				button->setToolTip(text("Dock.Shutter.FlickerFree.Tooltip"));
				connect(button, &QPushButton::clicked, this, [this, speed] {
					context_.controls->set("/video/shutter",
							       {{"shutterSpeed", std::lround(speed)}});
				});
				quick_layout->addWidget(button);
			}
		}
		const auto again = weak.lock();
		if (again && guard->settled(this, *again))
			shutter->set_value(shutter_speed(value, frame_rate(controls.get("/system/videoFormat"))));
	};
	watches_.add({"/video/supportedShutters", "/video/flickerFreeShutters", "/video/shutter", "/video/autoExposure",
		      "/system/videoFormat"},
		     [follow] { (*follow)(); });
	return page;
}

QWidget *AdvancedPanel::iris_adjuster()
{
	auto page = new QWidget();
	auto layout = page_layout(page);
	auto row = new Row(text("Dock.Iris"), text("Dock.Iris.Tooltip"), page);
	Ruler *iris = number_ruler("/lens/iris", "apertureStop", 1,
				   [](double value) { return QStringLiteral("f/%1").arg(value, 0, 'f', 1); });
	row->set_control(iris);
	layout->addWidget(row);
	auto fixed = muted_label({}, page);
	layout->addWidget(fixed);
	watches_.add({"/lens/iris/description", "/lens/iris"}, [this, row, iris, fixed] {
		const nlohmann::json description = context_.controls->get("/lens/iris/description");
		const nlohmann::json limits =
			description.is_object() ? description.value("apertureStop", nlohmann::json()) : nullptr;
		const bool controllable = bool_at(description, "controllable") && limits.is_object() &&
					  number_at(limits, "max") > number_at(limits, "min");
		row->setVisible(controllable);
		if (controllable)
			iris->set_range(number_at(limits, "min"), number_at(limits, "max"), 0.1);
		const double stop = number_at(context_.controls->get("/lens/iris"), "apertureStop");
		fixed->setText(text("Dock.Iris.Fixed").arg(stop, 0, 'f', 1));
		fixed->setVisible(!controllable);
	});
	return page;
}

QWidget *AdvancedPanel::iso_adjuster()
{
	auto page = new QWidget();
	auto layout = page_layout(page);
	auto row = new Row(text("Dock.ISO"), text("Dock.ISO.Tooltip"), page);
	auto automatic = new Chip(text("Dock.Auto"), row);
	automatic->setObjectName("isoAuto");
	automatic->setToolTip(text("Dock.Exposure.Auto.Tooltip"));
	row->add_extra(automatic);
	auto iso = new Ruler(row);
	iso->setObjectName("iso");
	iso->set_format([](double value) { return QString::number(std::lround(value)); });
	row->set_control(iso);
	layout->addWidget(row);

	guards_.push_back(std::make_unique<EditGuard>());
	EditGuard *guard = guards_.back().get();
	iso->changed = [this, guard](double value, bool) {
		guard->touched();
		context_.controls->set("/video/iso", {{"iso", std::lround(value)}});
	};
	connect(automatic, &QAbstractButton::clicked, this, [this](bool on) {
		context_.controls->set("/video/autoExposure", {{"mode", on ? "Continuous" : "Off"}});
	});
	auto follow = std::make_shared<std::function<void()>>();
	*follow = [this, row, automatic, iso, guard, weak = std::weak_ptr<std::function<void()>>(follow)] {
		const CameraControls &controls = *context_.controls;
		const std::vector<double> isos = numbers_at(controls.get("/video/supportedISOs"), "supportedISOs");
		const nlohmann::json value = controls.get("/video/iso");
		row->setVisible(!isos.empty() && value.is_object());
		if (isos.empty())
			return;
		iso->set_stops(isos);
		const bool is_automatic = string_at(controls.get("/video/autoExposure"), "mode") == "Continuous";
		automatic->setChecked(is_automatic);
		iso->setEnabled(!is_automatic);
		iso->setToolTip(text(is_automatic ? "Dock.Exposure.Locked" : "Dock.ISO.Tooltip"));
		const auto again = weak.lock();
		if (again && guard->settled(this, *again))
			iso->set_value(number_at(value, "iso"));
	};
	watches_.add({"/video/supportedISOs", "/video/iso", "/video/autoExposure"}, [follow] { (*follow)(); });
	return page;
}

QWidget *AdvancedPanel::white_balance_adjuster()
{
	auto page = new QWidget();
	auto layout = page_layout(page);
	auto row = new Row(text("Dock.WB"), text("Dock.WB.Tooltip"), page);
	auto automatic = new Chip(text("Dock.Auto"), row);
	automatic->setCheckable(false);
	automatic->setToolTip(text("Dock.Warmth.Auto.Tooltip"));
	row->add_extra(automatic);
	Ruler *kelvin = number_ruler("/video/whiteBalance", "whiteBalance", 0,
				     [](double value) { return QStringLiteral("%1 K").arg(std::lround(value)); });
	kelvin->setObjectName("whiteBalance");
	kelvin->set_spacing(0.3);
	row->set_control(kelvin);
	layout->addWidget(row);
	connect(automatic, &QAbstractButton::clicked, this,
		[this] { context_.controls->act("/video/whiteBalance/doAuto"); });

	auto quick = new QWidget(page);
	auto quick_layout = new FlowLayout(quick);
	for (const WhiteBalancePreset &preset : kWhiteBalancePresets) {
		auto button = new QPushButton(QString::number(preset.kelvin), quick);
		button->setToolTip(text(preset.key));
		connect(button, &QPushButton::clicked, this, [this, kelvin = preset.kelvin] {
			context_.controls->set("/video/whiteBalance", {{"whiteBalance", kelvin}});
		});
		quick_layout->addWidget(button);
	}
	layout->addWidget(quick);
	watches_.add({"/video/whiteBalance/description"}, [this, kelvin] {
		const nlohmann::json range = context_.controls->get("/video/whiteBalance/description");
		const nlohmann::json limits = range.is_object() ? range.value("whiteBalance", nlohmann::json())
								: nullptr;
		kelvin->set_range(number_at(limits, "min", 2500), number_at(limits, "max", 10000), 50);
	});
	return page;
}

QWidget *AdvancedPanel::tint_adjuster()
{
	auto page = new QWidget();
	auto layout = page_layout(page);
	auto row = new Row(text("Dock.Tint"), text("Dock.Tint.Tooltip"), page);
	Ruler *tint = number_ruler("/video/whiteBalanceTint", "whiteBalanceTint", 0, [](double value) {
		const long number = std::lround(value);
		return number > 0 ? QStringLiteral("+%1").arg(number) : QString::number(number);
	});
	tint->setObjectName("tint");
	row->set_control(tint);
	layout->addWidget(row);
	watches_.add({"/video/whiteBalanceTint/description"}, [this, tint] {
		const nlohmann::json range = context_.controls->get("/video/whiteBalanceTint/description");
		const nlohmann::json limits = range.is_object() ? range.value("whiteBalanceTint", nlohmann::json())
								: nullptr;
		tint->set_range(number_at(limits, "min", -50), number_at(limits, "max", 50), 1);
	});
	return page;
}

QWidget *AdvancedPanel::color_tab()
{
	auto page = new QWidget();
	auto layout = page_layout(page);
	auto header = new QHBoxLayout();
	header->addWidget(muted_label(text("Dock.Color.Note"), page), 1);
	header->addWidget(
		reset_button("Dock.Color.Reset", [this] { apply_color(*context_.controls, neutral_color()); }));
	layout->addLayout(header);

	struct Slider {
		const char *label;
		const char *tooltip;
		const char *key;
		const char *field;
		double minimum;
		double maximum;
		double step;
	};
	const Slider basic[] = {
		{"Dock.Color.Saturation", "Dock.Color.Saturation.Tooltip", "color", "saturation", 0, 2, 0.01},
		{"Dock.Color.Contrast", "Dock.Color.Contrast.Tooltip", "contrast", "adjust", 0, 2, 0.01},
		{"Dock.Color.Pivot", "Dock.Color.Pivot.Tooltip", "contrast", "pivot", 0, 1, 0.01},
		{"Dock.Color.Hue", "Dock.Color.Hue.Tooltip", "color", "hue", -1, 1, 0.01},
		{"Dock.Color.LumaMix", "Dock.Color.LumaMix.Tooltip", "lumaContribution", "lumaContribution", 0, 1,
		 0.01},
	};
	for (const Slider &slider : basic) {
		auto row = new Row(text(slider.label), text(slider.tooltip), page);
		Ruler *ruler = number_ruler(color_path(slider.key), slider.field, 2);
		ruler->set_range(slider.minimum, slider.maximum, slider.step);
		row->set_control(ruler);
		layout->addWidget(row);
	}

	auto wheels_toggle = new Chip(text("Dock.Color.Wheels"), page);
	wheels_toggle->setObjectName("showWheels");
	wheels_toggle->setToolTip(text("Dock.Color.Wheels.Tooltip"));
	layout->addWidget(wheels_toggle, 0, Qt::AlignLeft);
	auto wheels = new QWidget(page);
	auto wheels_layout = new QVBoxLayout(wheels);
	wheels_layout->setContentsMargins(0, 0, 0, 0);
	struct Wheel {
		const char *key;
		const char *label;
		double minimum;
		double maximum;
		double step;
	};
	const Wheel groups[] = {
		{"lift", "Dock.Color.Lift", -2, 2, 0.005},
		{"gamma", "Dock.Color.Gamma", -4, 4, 0.01},
		{"gain", "Dock.Color.Gain", 0, 16, 0.01},
		{"offset", "Dock.Color.Offset", -8, 8, 0.01},
	};
	const char *channels[][2] = {{"red", "Dock.Color.Red"},
				     {"green", "Dock.Color.Green"},
				     {"blue", "Dock.Color.Blue"},
				     {"luma", "Dock.Color.Luma"}};
	for (const Wheel &group : groups) {
		for (const auto &channel : channels) {
			auto row = new Row(text(group.label) + QStringLiteral(" · ") + text(channel[1]),
					   text((std::string(group.label) + ".Tooltip").c_str()), wheels);
			Ruler *ruler = number_ruler(color_path(group.key), channel[0], 3);
			ruler->set_range(group.minimum, group.maximum, group.step);
			ruler->set_spacing(0.3);
			row->set_control(ruler);
			wheels_layout->addWidget(row);
		}
	}
	wheels->hide();
	layout->addWidget(wheels);
	connect(wheels_toggle, &QAbstractButton::toggled, wheels, &QWidget::setVisible);
	return page;
}

QWidget *AdvancedPanel::focus_tab()
{
	auto page = new QWidget();
	auto layout = page_layout(page);
	auto header = new QHBoxLayout();
	auto state = muted_label({}, page);
	header->addWidget(state, 1);
	header->addWidget(reset_button("Dock.Focus.Reset", [this] {
		context_.controls->set("/lens/focus/autoFocus", {{"enabled", true}, {"mode", "Continuous"}});
		context_.controls->set("/lens/focus/autoFocus/target", {{"x", 0.5}, {"y", 0.5}});
	}));
	layout->addLayout(header);

	auto mode_row = new Row(text("Dock.Focus.Mode"), text("Dock.Focus.Mode.Tooltip"), page);
	auto modes = new Segmented(mode_row);
	modes->setObjectName("focusModes");
	mode_row->set_control(modes);
	layout->addWidget(mode_row);

	auto position_row = new Row(text("Dock.Focus.Position"), text("Dock.Focus.Position.Tooltip"), page);
	Ruler *position = number_ruler("/lens/focus", "normalized", 0, [](double value) {
		return QStringLiteral("%1 %").arg(std::lround(value * 100));
	});
	position->setObjectName("focusPosition");
	position->set_range(0, 1, 0.005);
	position->set_spacing(0.3);
	position_row->set_control(position);
	layout->addWidget(position_row);

	auto buttons = new QWidget(page);
	auto buttons_layout = new FlowLayout(buttons);
	auto refocus = new QPushButton(text("Dock.Focus.Refocus"), buttons);
	refocus->setToolTip(text("Dock.Focus.Refocus.Tooltip"));
	auto center = new QPushButton(text("Dock.Focus.Center"), buttons);
	center->setToolTip(text("Dock.Focus.Center.Tooltip"));
	buttons_layout->addWidget(refocus);
	buttons_layout->addWidget(center);
	layout->addWidget(buttons);
	connect(refocus, &QPushButton::clicked, this,
		[this] { context_.controls->act("/lens/focus/autoFocus/retrigger"); });
	connect(center, &QPushButton::clicked, this, [this] {
		context_.controls->act("/lens/focus/doAutoFocus", {{"position", {{"x", 0.5}, {"y", 0.5}}}});
	});

	// Manual focus, then the modes the phone offers
	auto mode_ids = std::make_shared<std::vector<std::string>>();
	modes->activated = [this, mode_ids](int index) {
		if (index < 0 || index >= static_cast<int>(mode_ids->size()))
			return;
		const std::string &mode = (*mode_ids)[static_cast<size_t>(index)];
		if (mode.empty())
			context_.controls->set("/lens/focus/autoFocus", {{"enabled", false}});
		else
			context_.controls->set("/lens/focus/autoFocus", {{"enabled", true}, {"mode", mode}});
	};
	watches_.add({"/lens/focus/autoFocus", "/lens/focus/autoFocus/description", "/lens/focus"},
		     [this, modes, mode_ids, mode_row, position, position_row, state] {
			     const CameraControls &controls = *context_.controls;
			     const nlohmann::json focus = controls.get("/lens/focus/autoFocus");
			     mode_row->setVisible(focus.is_object());
			     mode_ids->assign(1, std::string());
			     QStringList labels = {text("Dock.Focus.Manual")};
			     for (const std::string &mode :
				  strings_at(controls.get("/lens/focus/autoFocus/description"), "supportedModes")) {
				     mode_ids->push_back(mode);
				     labels.push_back(text(("Dock.Focus.Mode." + mode).c_str()));
			     }
			     modes->set_items(labels);
			     const bool enabled = bool_at(focus, "enabled", true);
			     const std::string mode = string_at(focus, "mode");
			     int selected = 0;
			     for (size_t index = 1; enabled && index < mode_ids->size(); index++) {
				     if ((*mode_ids)[index] == mode)
					     selected = static_cast<int>(index);
			     }
			     modes->set_selected(selected);
			     // Manual focus is refused while autofocus keeps focusing (CTL-4)
			     const bool locked = enabled && mode != "OneShot";
			     position_row->setVisible(controls.get("/lens/focus").is_object());
			     position->setEnabled(!locked);
			     position->setToolTip(
				     text(locked ? "Dock.Focus.Position.Locked" : "Dock.Focus.Position.Tooltip"));
			     const std::string focus_state = string_at(focus, "state");
			     state->setText(focus_state.empty() ? QString()
								: text(("Dock.Focus.State." + focus_state).c_str()));
		     });
	return page;
}

QWidget *AdvancedPanel::audio_tab()
{
	auto page = new QWidget();
	auto layout = page_layout(page);
	layout->addWidget(new MicrophoneRow(context_, page));
	auto header = new QHBoxLayout();
	header->addWidget(muted_label(text("Dock.Audio.Note"), page), 1);
	header->addWidget(reset_button("Dock.Audio.Reset", [this] {
		for (int channel = 0; channel < 2; channel++) {
			const std::string base = "/audio/channel/" + std::to_string(channel);
			for (const char *name : {"/lowCutFilter", "/padding", "/phantomPower"}) {
				if (context_.controls->get(base + name).is_object())
					context_.controls->set(base + name, {{"enabled", false}});
			}
		}
	}));
	layout->addLayout(header);

	for (int channel = 0; channel < 2; channel++) {
		const std::string base = "/audio/channel/" + std::to_string(channel);
		auto row = new Row(text("Dock.Audio.Channel").arg(channel + 1), text("Dock.Audio.Input.Tooltip"), page);
		auto inputs = new QComboBox(row);
		inputs->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		inputs->setMinimumContentsLength(6);
		row->set_control(inputs);
		Ruler *level = number_ruler(base + "/level", "gain", 1,
					    [](double value) { return QStringLiteral("%1 dB").arg(value, 0, 'f', 1); });
		level->setToolTip(text("Dock.Audio.Level.Tooltip"));
		row->set_control(level);
		auto switches = new QWidget(row);
		auto switches_layout = new FlowLayout(switches);
		switches_layout->addWidget(
			enabled_chip("Dock.Audio.LowCut", "Dock.Audio.LowCut.Tooltip", base + "/lowCutFilter"));
		switches_layout->addWidget(enabled_chip("Dock.Audio.Pad", "Dock.Audio.Pad.Tooltip", base + "/padding"));
		switches_layout->addWidget(
			enabled_chip("Dock.Audio.Phantom", "Dock.Audio.Phantom.Tooltip", base + "/phantomPower"));
		row->set_control(switches);
		layout->addWidget(row);

		connect(inputs, QOverload<int>::of(&QComboBox::activated), this, [this, inputs, base](int index) {
			context_.controls->set(base + "/input",
					       {{"input", inputs->itemData(index).toString().toStdString()}});
		});
		watches_.add(
			{base + "/input", base + "/supportedInputs", base + "/input/description", "/audio/channels"},
			[this, row, inputs, level, base, channel] {
				const CameraControls &controls = *context_.controls;
				const nlohmann::json input = controls.get(base + "/input");
				const int channels =
					static_cast<int>(number_at(controls.get("/audio/channels"), "channels", 2));
				row->setVisible(input.is_object() && channel < channels);
				inputs->clear();
				const nlohmann::json supported = controls.get(base + "/supportedInputs");
				if (supported.is_array()) {
					for (const nlohmann::json &item : supported) {
						const std::string name = item.is_string() ? item.get<std::string>()
											  : string_at(item, "input");
						if (!name.empty() && bool_at(item, "available", true))
							inputs->addItem(QString::fromStdString(name),
									QString::fromStdString(name));
					}
				}
				inputs->setCurrentIndex(
					inputs->findData(QString::fromStdString(string_at(input, "input"))));
				// A level only where the input's gain can change
				const nlohmann::json description = controls.get(base + "/input/description");
				const nlohmann::json range =
					description.is_object() ? description.value("description", nlohmann::json())
									  .value("gainRange", nlohmann::json())
								: nullptr;
				const double minimum = number_at(range, "Min");
				const double maximum = number_at(range, "Max");
				level->setVisible(maximum > minimum);
				if (maximum > minimum)
					level->set_range(minimum, maximum, 0.5);
			});
	}
	return page;
}

QWidget *AdvancedPanel::phone_tab()
{
	auto page = new QWidget();
	auto layout = page_layout(page);
	const std::shared_ptr<CameraControls> controls = context_.controls;

	// Format
	auto format = muted_label({}, page);
	layout->addWidget(format);
	watches_.add({"/system/videoFormat"}, [this, format] {
		const nlohmann::json value = context_.controls->get("/system/videoFormat");
		format->setText(text("Dock.Format.Current")
					.arg(static_cast<int>(number_at(value, "width")))
					.arg(static_cast<int>(number_at(value, "height")))
					.arg(QString::fromStdString(string_at(value, "frameRate"))));
	});

	auto range_row = new Row(text("Dock.Format.DynamicRange"), text("Dock.Format.DynamicRange.Tooltip"), page);
	auto ranges = new Segmented(range_row);
	range_row->set_control(ranges);
	layout->addWidget(range_row);
	auto range_ids = std::make_shared<std::vector<std::string>>();
	ranges->activated = [this, range_ids](int index) {
		if (index >= 0 && index < static_cast<int>(range_ids->size()))
			context_.controls->set("/system/dynamicRange",
					       {{"dynamicRange", (*range_ids)[static_cast<size_t>(index)]}});
	};
	watches_.add({"/system/dynamicRange", "/system/supportedDynamicRanges"}, [this, ranges, range_ids, range_row] {
		const CameraControls &state = *context_.controls;
		*range_ids = strings_at(state.get("/system/supportedDynamicRanges"), "supportedDynamicRanges");
		// Video first: the range the looks are made for
		std::stable_sort(range_ids->begin(), range_ids->end(), [](const std::string &a, const std::string &b) {
			return a == "Video" && b != "Video";
		});
		QStringList labels;
		for (const std::string &id : *range_ids)
			labels.push_back(QString::fromStdString(id));
		ranges->set_items(labels);
		range_row->setVisible(!range_ids->empty());
		const std::string current = string_at(state.get("/system/dynamicRange"), "dynamicRange");
		const auto found = std::find(range_ids->begin(), range_ids->end(), current);
		ranges->set_selected(found != range_ids->end() ? static_cast<int>(found - range_ids->begin()) : -1);
	});

	auto codec_row = new Row(text("Dock.Format.Codec"), text("Dock.Format.Codec.Tooltip"), page);
	auto codecs = new QComboBox(codec_row);
	codecs->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	codecs->setMinimumContentsLength(6);
	codec_row->set_control(codecs);
	layout->addWidget(codec_row);
	connect(codecs, QOverload<int>::of(&QComboBox::activated), this, [this, codecs](int index) {
		context_.controls->set("/system/codecFormat",
				       {{"codec", codecs->itemData(index).toString().toStdString()},
					{"container", "mov"}});
	});
	watches_.add({"/system/codecFormat", "/system/supportedCodecFormats", "/system/format"}, [this, codecs,
												  codec_row] {
		const CameraControls &state = *context_.controls;
		const nlohmann::json supported = state.get("/system/supportedCodecFormats");
		codecs->clear();
		if (supported.is_object() && supported.contains("codecFormats") &&
		    supported["codecFormats"].is_array()) {
			for (const nlohmann::json &item : supported["codecFormats"]) {
				const std::string codec = string_at(item, "codec");
				if (!codec.empty())
					codecs->addItem(QString::fromStdString(codec), QString::fromStdString(codec));
			}
		}
		codec_row->setVisible(codecs->count() > 0);
		codecs->setCurrentIndex(
			codecs->findData(QString::fromStdString(string_at(state.get("/system/format"), "codec"))));
	});

	// Recording on the phone
	auto record_row = new Row(text("Dock.Record"), text("Dock.Record.Tooltip"), page);
	auto record_controls = new QWidget(record_row);
	auto record_layout = new FlowLayout(record_controls);
	auto record = new QPushButton(record_controls);
	record->setObjectName("record");
	record_layout->addWidget(record);
	record_layout->addWidget(
		enabled_chip("Dock.Record.Proxy", "Dock.Record.Proxy.Tooltip", "/transports/0/proxyRecording"));
	record_row->set_control(record_controls);
	auto storage = muted_label({}, record_row);
	record_row->set_control(storage);
	layout->addWidget(record_row);
	connect(record, &QPushButton::clicked, this, [this] {
		const bool recording = bool_at(context_.controls->get("/transports/0/record"), "recording");
		context_.controls->act(recording ? "/transports/0/stop" : "/transports/0/record");
	});
	watches_.add({"/transports/0/record", "/media/workingset"}, [this, record, record_row, storage] {
		const nlohmann::json value = context_.controls->get("/transports/0/record");
		record_row->setVisible(value.is_object());
		const bool recording = bool_at(value, "recording");
		record->setText(text(recording ? "Dock.Record.Stop" : "Dock.Record.Start"));
		set_theme_class(record, recording ? "text-danger" : "");
		const nlohmann::json media = context_.controls->get("/media/workingset");
		double seconds = -1;
		if (media.is_object() && media.contains("workingset") && media["workingset"].is_array()) {
			for (const nlohmann::json &disk : media["workingset"]) {
				if (bool_at(disk, "activeDisk", true))
					seconds = number_at(disk, "remainingRecordTime", -1);
			}
		}
		storage->setText(seconds >= 0 ? text("Dock.Record.Left").arg(duration_text(seconds)) : QString());
	});

	// The phone's own screen (CTL-7)
	auto screen_title = new QLabel(text("Dock.Screen"), page);
	layout->addWidget(screen_title);
	layout->addWidget(muted_label(text("Dock.Screen.Note"), page));
	auto brightness_row = new Row(text("Dock.Screen.Brightness"), text("Dock.Screen.Brightness.Tooltip"), page);
	Ruler *brightness = number_ruler("/monitoring/Device/brightness", "brightness", 0,
					 [](double value) { return QStringLiteral("%1 %").arg(std::lround(value)); });
	brightness->set_range(0, 100, 1);
	brightness->set_spacing(0.5);
	brightness_row->set_control(brightness);
	layout->addWidget(brightness_row);
	watches_.add({"/monitoring/Device/brightness"}, [this, brightness_row, brightness] {
		const nlohmann::json value = context_.controls->get("/monitoring/Device/brightness");
		brightness_row->setVisible(value.is_object());
		brightness->setEnabled(bool_at(value, "adjustable", true));
	});

	auto tools = new QWidget(page);
	auto tools_layout = new FlowLayout(tools);
	struct Tool {
		const char *label;
		const char *tooltip;
		const char *path;
	};
	const Tool screen_tools[] = {
		{"Dock.Screen.Zebra", "Dock.Screen.Zebra.Tooltip", "/monitoring/Device/zebra"},
		{"Dock.Screen.FocusAssist", "Dock.Screen.FocusAssist.Tooltip", "/monitoring/Device/focusAssist"},
		{"Dock.Screen.FalseColor", "Dock.Screen.FalseColor.Tooltip", "/monitoring/Device/falseColor"},
		{"Dock.Screen.FrameGuide", "Dock.Screen.FrameGuide.Tooltip", "/monitoring/Device/frameGuide"},
		{"Dock.Screen.Grids", "Dock.Screen.Grids.Tooltip", "/monitoring/Device/frameGrids"},
		{"Dock.Screen.SafeArea", "Dock.Screen.SafeArea.Tooltip", "/monitoring/Device/safeArea"},
		{"Dock.Screen.DisplayLUT", "Dock.Screen.DisplayLUT.Tooltip", "/monitoring/Device/displayLUT"},
	};
	for (const Tool &tool : screen_tools)
		tools_layout->addWidget(enabled_chip(tool.label, tool.tooltip, tool.path));
	layout->addWidget(tools);

	auto guide_row = new Row(text("Dock.Screen.GuideRatio"), text("Dock.Screen.GuideRatio.Tooltip"), page);
	auto ratios = new QComboBox(guide_row);
	ratios->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	ratios->setMinimumContentsLength(6);
	guide_row->set_control(ratios);
	layout->addWidget(guide_row);
	connect(ratios, QOverload<int>::of(&QComboBox::activated), this, [this, ratios](int index) {
		context_.controls->set("/monitoring/frameGuideRatio",
				       {{"ratio", ratios->itemText(index).toStdString()}});
	});
	watches_.add(
		{"/monitoring/frameGuideRatio", "/monitoring/frameGuideRatio/presets", "/monitoring/Device/frameGuide"},
		[this, ratios, guide_row] {
			const CameraControls &state = *context_.controls;
			ratios->clear();
			for (const std::string &ratio :
			     strings_at(state.get("/monitoring/frameGuideRatio/presets"), "presets"))
				ratios->addItem(QString::fromStdString(ratio));
			guide_row->setVisible(ratios->count() > 0 &&
					      bool_at(state.get("/monitoring/Device/frameGuide"), "enabled"));
			ratios->setCurrentText(
				QString::fromStdString(string_at(state.get("/monitoring/frameGuideRatio"), "ratio")));
		});

	auto safe_row = new Row(text("Dock.Screen.SafeAreaSize"), text("Dock.Screen.SafeAreaSize.Tooltip"), page);
	Ruler *safe = number_ruler("/monitoring/safeAreaPercent", "percent", 0,
				   [](double value) { return QStringLiteral("%1 %").arg(std::lround(value)); });
	safe->set_range(50, 100, 1);
	safe->set_spacing(0.5);
	safe_row->set_control(safe);
	layout->addWidget(safe_row);
	watches_.add({"/monitoring/Device/safeArea", "/monitoring/safeAreaPercent"}, [this, safe_row] {
		safe_row->setVisible(bool_at(context_.controls->get("/monitoring/Device/safeArea"), "enabled") &&
				     context_.controls->get("/monitoring/safeAreaPercent").is_object());
	});

	auto screen_reset = reset_button("Dock.Screen.Reset", [this] {
		for (const char *tool :
		     {"zebra", "focusAssist", "falseColor", "frameGuide", "frameGrids", "safeArea"}) {
			const std::string path = std::string("/monitoring/Device/") + tool;
			if (context_.controls->get(path).is_object())
				context_.controls->set(path, {{"enabled", false}});
		}
	});
	layout->addWidget(screen_reset, 0, Qt::AlignLeft);

	// The phone itself
	auto number_row = new Row(text("Dock.CameraNumber"), text("Dock.CameraNumber.Tooltip"), page);
	auto number = new QSpinBox(number_row);
	number->setRange(0, 255);
	number_row->set_control(number);
	layout->addWidget(number_row);
	connect(number, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, number](int value) {
		if (number->hasFocus())
			context_.controls->set("/camera/id", {{"id", value}});
	});
	watches_.add({"/camera/id"}, [this, number, number_row] {
		const nlohmann::json value = context_.controls->get("/camera/id");
		number_row->setVisible(value.is_object());
		if (!number->hasFocus())
			number->setValue(static_cast<int>(number_at(value, "id")));
	});
	(void)controls;
	return page;
}

} // namespace bmagicam::ui
