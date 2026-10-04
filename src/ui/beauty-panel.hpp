// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include "dock-kit.hpp"
#include "../filter/beauty-styles.hpp"

#include <QStringList>
#include <QWidget>

#include <array>
#include <string>
#include <vector>

class QPushButton;

namespace bmagicam::ui {

class BeautyLink;
class Chip;
class Ruler;
class Segmented;

// The dock's Beauty controls for the Beautify filter on the panel's source (docs/ui.md, "Beauty"): the switch, the
// styles and the Beauty slider in Simple mode; in Advanced mode also every value, Show mask and the user's styles.
// Without a filter it offers to add one.
class BeautyPanel : public QWidget {
public:
	BeautyPanel(PanelContext context, bool advanced, QWidget *parent = nullptr);

private:
	void refresh();
	void choose_style(int index);
	void change_value(size_t index, int value);
	void save_style();
	void delete_style();

	PanelContext context_;
	bool advanced_;
	BeautyLink *link_ = nullptr;
	std::vector<BeautyStyle> styles_;
	QStringList style_labels_;
	Chip *enabled_ = nullptr;
	QPushButton *add_ = nullptr;
	QWidget *controls_ = nullptr;
	Segmented *style_choice_ = nullptr;
	Ruler *strength_ = nullptr;
	EditGuard strength_guard_;
	// Advanced mode only
	std::vector<QWidget *> advanced_widgets_;
	std::array<Ruler *, kBeautyKeys.size()> values_{};
	std::array<EditGuard, kBeautyKeys.size()> value_guards_;
	Chip *show_mask_ = nullptr;
	QPushButton *delete_ = nullptr;
};

// Asks for a new style's name; empty when cancelled. The built-in styles' names are taken.
std::string ask_beauty_style_name(QWidget *parent);
bool confirm_delete_beauty_style(QWidget *parent, const std::string &name);

} // namespace bmagicam::ui
