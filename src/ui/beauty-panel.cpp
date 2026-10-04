// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "beauty-panel.hpp"

#include "beauty-link.hpp"
#include "widgets/buttons.hpp"
#include "widgets/flow-layout.hpp"
#include "widgets/ruler.hpp"
#include "widgets/segmented.hpp"
#include "../filter/beautify-filter.hpp"
#include "../filter/style-library.hpp"

#include <obs.hpp>

#include <QInputDialog>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include <cmath>

namespace bmagicam::ui {

namespace {

// A magic wand, the sign of the Beauty slider (BEA-2), painted in the text color
class WandIcon : public QWidget {
public:
	explicit WandIcon(QWidget *parent) : QWidget(parent)
	{
		const int size = fontMetrics().height();
		setFixedSize(size, size);
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);
		const QColor color =
			palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::WindowText);
		const double size = width();
		painter.setPen(QPen(color, size / 7.0, Qt::SolidLine, Qt::RoundCap));
		painter.drawLine(QPointF(size * 0.15, size * 0.85), QPointF(size * 0.6, size * 0.4));
		painter.setPen(QPen(color, size / 12.0, Qt::SolidLine, Qt::RoundCap));
		const auto sparkle = [&](QPointF center, double radius) {
			painter.drawLine(center - QPointF(radius, 0), center + QPointF(radius, 0));
			painter.drawLine(center - QPointF(0, radius), center + QPointF(0, radius));
		};
		sparkle(QPointF(size * 0.78, size * 0.22), size * 0.17);
		sparkle(QPointF(size * 0.38, size * 0.16), size * 0.08);
		sparkle(QPointF(size * 0.86, size * 0.6), size * 0.07);
	}
};

QString style_label(const BeautyStyle &style)
{
	return style.builtin ? text(("Beautify.Style." + style.id).c_str()) : QString::fromStdString(style.id);
}

} // namespace

BeautyPanel::BeautyPanel(PanelContext context, bool advanced, QWidget *parent)
	: QWidget(parent),
	  context_(std::move(context)),
	  advanced_(advanced),
	  styles_(all_styles())
{
	auto layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(fontMetrics().height() * 3 / 4);

	auto row = new Row(text("Dock.Beauty"), text("Dock.Beauty.Tooltip"), this);
	row->add_leading(new WandIcon(row));
	enabled_ = new Chip(text("Dock.On"), row);
	enabled_->setObjectName("beautyEnabled");
	enabled_->setToolTip(text("Dock.Beauty.On.Tooltip"));
	row->add_extra(enabled_);

	auto body = new QWidget(row);
	auto body_layout = new QVBoxLayout(body);
	body_layout->setContentsMargins(0, 0, 0, 0);
	body_layout->setSpacing(fontMetrics().height() / 4);
	add_ = new QPushButton(text("Dock.Beauty.Add"), body);
	add_->setObjectName("addBeauty");
	add_->setToolTip(text("Dock.Beauty.Add.Tooltip"));
	body_layout->addWidget(add_);
	controls_ = new QWidget(body);
	auto controls_layout = new QVBoxLayout(controls_);
	controls_layout->setContentsMargins(0, 0, 0, 0);
	controls_layout->setSpacing(fontMetrics().height() / 4);
	style_choice_ = new Segmented(controls_);
	style_choice_->setObjectName("beautyStyles");
	style_choice_->setToolTip(text("Beautify.Style.Tooltip"));
	strength_ = new Ruler(controls_);
	strength_->setObjectName("beautyStrength");
	strength_->setToolTip(text("Beautify.Strength.Tooltip"));
	strength_->set_range(0, 100, 1);
	strength_->set_spacing(0.3);
	if (advanced_)
		strength_->set_format([](double value) { return QString::number(std::lround(value)); });
	else
		strength_->set_plain(text("Dock.Beauty.Off"), text("Dock.Beauty.Full"));
	controls_layout->addWidget(style_choice_);
	controls_layout->addWidget(strength_);
	body_layout->addWidget(controls_);
	row->set_control(body);
	layout->addWidget(row);

	if (advanced_) {
		for (size_t index = 0; index < kBeautyKeys.size(); index++) {
			const std::string name = kBeautyValueNames[index];
			auto value_row = new Row(text(name.c_str()), text((name + ".Tooltip").c_str()), this);
			auto ruler = new Ruler(value_row);
			ruler->set_range(0, 100, 1);
			ruler->set_spacing(0.3);
			ruler->set_format([](double value) { return QString::number(std::lround(value)); });
			value_row->set_control(ruler);
			ruler->changed = [this, index](double value, bool) {
				value_guards_[index].touched();
				change_value(index, static_cast<int>(std::lround(value)));
			};
			values_[index] = ruler;
			layout->addWidget(value_row);
			advanced_widgets_.push_back(value_row);
		}
		auto buttons = new QWidget(this);
		auto buttons_layout = new FlowLayout(buttons);
		show_mask_ = new Chip(text("Beautify.ShowMask"), buttons);
		show_mask_->setObjectName("beautyShowMask");
		show_mask_->setToolTip(text("Beautify.ShowMask.Tooltip"));
		auto save = new QPushButton(text("Beautify.SaveStyle"), buttons);
		save->setToolTip(text("Beautify.SaveStyle.Tooltip"));
		delete_ = new QPushButton(text("Beautify.DeleteStyle"), buttons);
		delete_->setToolTip(text("Beautify.DeleteStyle.Tooltip"));
		buttons_layout->addWidget(show_mask_);
		buttons_layout->addWidget(save);
		buttons_layout->addWidget(delete_);
		layout->addWidget(buttons);
		advanced_widgets_.push_back(buttons);
		connect(show_mask_, &QAbstractButton::clicked, this, [this](bool on) {
			OBSDataAutoRelease changes = obs_data_create();
			obs_data_set_bool(changes, kBeautyShowMask, on);
			link_->update(changes);
		});
		connect(save, &QPushButton::clicked, this, [this] { save_style(); });
		connect(delete_, &QPushButton::clicked, this, [this] { delete_style(); });
	}

	link_ = new BeautyLink(context_.source, this);
	link_->changed = [this] {
		refresh();
	};
	connect(add_, &QPushButton::clicked, this, [this] {
		link_->add();
		if (OBSSourceAutoRelease source = obs_weak_source_get_source(context_.source);
		    source && context_.notify)
			context_.notify(text("Dock.Beauty.Added").arg(QString::fromUtf8(obs_source_get_name(source))));
	});
	connect(enabled_, &QAbstractButton::clicked, this, [this](bool on) { link_->set_enabled(on); });
	style_choice_->activated = [this](int index) {
		choose_style(index);
	};
	strength_->changed = [this](double value, bool) {
		strength_guard_.touched();
		OBSDataAutoRelease changes = obs_data_create();
		obs_data_set_int(changes, kBeautyStrength, std::lround(value));
		link_->update(changes);
	};
	refresh();
}

void BeautyPanel::refresh()
{
	obs_source_t *filter = link_->filter();
	add_->setVisible(!filter);
	controls_->setVisible(filter);
	enabled_->setVisible(filter);
	for (QWidget *widget : advanced_widgets_)
		widget->setVisible(filter);
	if (!filter)
		return;

	OBSDataAutoRelease settings = obs_source_get_settings(filter);
	enabled_->setChecked(obs_source_enabled(filter));
	// A style saved elsewhere, such as in the filter's properties, appears once it is chosen
	const std::string style = obs_data_get_string(settings, kBeautyStyle);
	if (style != kCustomStyle && !find_style(styles_, style))
		styles_ = all_styles();
	QStringList labels;
	for (const BeautyStyle &item : styles_)
		labels.push_back(style_label(item));
	if (labels != style_labels_) {
		style_labels_ = labels;
		style_choice_->set_items(labels);
	}
	int selected = -1;
	for (size_t index = 0; index < styles_.size(); index++) {
		if (styles_[index].id == style)
			selected = static_cast<int>(index);
	}
	style_choice_->set_selected(selected);
	if (strength_guard_.settled(this, [this] { refresh(); }))
		strength_->set_value(static_cast<double>(obs_data_get_int(settings, kBeautyStrength)));

	if (!advanced_)
		return;
	for (size_t index = 0; index < kBeautyKeys.size(); index++) {
		if (value_guards_[index].settled(this, [this] { refresh(); }))
			values_[index]->set_value(static_cast<double>(obs_data_get_int(settings, kBeautyKeys[index])));
	}
	show_mask_->setChecked(obs_data_get_bool(settings, kBeautyShowMask));
	delete_->setVisible(selected >= 0 && !styles_[static_cast<size_t>(selected)].builtin);
}

void BeautyPanel::choose_style(int index)
{
	if (index < 0 || index >= static_cast<int>(styles_.size()))
		return;
	OBSDataAutoRelease changes = obs_data_create();
	select_style(changes, styles_[static_cast<size_t>(index)]);
	link_->update(changes);
}

// Moving a value away from the style's makes the style Custom, as in the filter's properties
void BeautyPanel::change_value(size_t index, int value)
{
	if (!link_->filter())
		return;
	OBSDataAutoRelease settings = obs_source_get_settings(link_->filter());
	BeautyValues values = beauty_values(settings);
	beauty_value(values, index) = value;
	OBSDataAutoRelease changes = obs_data_create();
	obs_data_set_int(changes, kBeautyKeys[index], value);
	const BeautyStyle *style = find_style(styles_, obs_data_get_string(settings, kBeautyStyle));
	if (style && style->values != values)
		obs_data_set_string(changes, kBeautyStyle, kCustomStyle);
	link_->update(changes);
}

void BeautyPanel::save_style()
{
	if (!link_->filter())
		return;
	const std::string name = ask_beauty_style_name(this);
	if (name.empty())
		return;
	OBSDataAutoRelease settings = obs_source_get_settings(link_->filter());
	if (!save_user_style(name, beauty_values(settings)))
		return;
	styles_ = all_styles();
	OBSDataAutoRelease changes = obs_data_create();
	obs_data_set_string(changes, kBeautyStyle, name.c_str());
	link_->update(changes);
	if (context_.notify)
		context_.notify(text("Dock.Beauty.Saved").arg(QString::fromStdString(name)));
}

void BeautyPanel::delete_style()
{
	if (!link_->filter())
		return;
	OBSDataAutoRelease settings = obs_source_get_settings(link_->filter());
	const std::string name = obs_data_get_string(settings, kBeautyStyle);
	const BeautyStyle *style = find_style(styles_, name);
	if (!style || style->builtin || !confirm_delete_beauty_style(this, name))
		return;
	delete_user_style(name);
	styles_ = all_styles();
	// The filter keeps the values
	OBSDataAutoRelease changes = obs_data_create();
	obs_data_set_string(changes, kBeautyStyle, kCustomStyle);
	link_->update(changes);
	if (context_.notify)
		context_.notify(text("Dock.Beauty.Deleted").arg(QString::fromStdString(name)));
}

std::string ask_beauty_style_name(QWidget *parent)
{
	for (;;) {
		bool ok = false;
		const QString name = QInputDialog::getText(parent, text("Beautify.SaveStyle.Title"),
							   text("Beautify.SaveStyle.Name"), QLineEdit::Normal,
							   QString(), &ok)
					     .trimmed();
		if (!ok || name.isEmpty())
			return {};
		// The built-in styles' IDs and Custom are taken
		const std::string id = name.toStdString();
		if (id != kCustomStyle && !find_style(builtin_styles(), id))
			return id;
		QMessageBox::warning(parent, text("Beautify.SaveStyle.Title"),
				     text("Beautify.SaveStyle.Taken").arg(name));
	}
}

bool confirm_delete_beauty_style(QWidget *parent, const std::string &name)
{
	return QMessageBox::question(parent, text("Beautify.DeleteStyle"),
				     text("Beautify.DeleteStyle.Question").arg(QString::fromStdString(name))) ==
	       QMessageBox::Yes;
}

} // namespace bmagicam::ui
