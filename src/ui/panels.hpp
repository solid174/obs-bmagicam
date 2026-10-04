// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include "dock-kit.hpp"
#include "../camera/looks.hpp"

#include <nlohmann/json.hpp>

#include <QWidget>

#include <set>
#include <string>
#include <vector>

class QComboBox;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QVBoxLayout;

namespace bmagicam::ui {

class Chip;
class HistogramView;
class MicrophoneSync;
class Ruler;
class Segmented;
class Tile;

// The phone's lenses as the dock offers them: Front, then one button per back lens named like the iPhone camera does
// (0.5×, 1×, 2×...)
struct LensChoice {
	std::string id;
	QString label;
	bool available = true;
};
std::vector<LensChoice> lens_choices(const nlohmann::json &cameras);
// The choice for the active lens; -1 when none
int lens_choice_index(const std::vector<LensChoice> &choices, const nlohmann::json &cameras,
		      const nlohmann::json &active);

// Looks offered in the dock: built-in ones, then the user's (LOOK-1, LOOK-4)
QString look_label(const std::string &id, bool builtin);
// The look the source last chose, and sets it; applying it to the phone is up to the caller
std::string source_look(obs_source_t *source);
void set_source_look(obs_source_t *source, const std::string &id);

// Runs Set up for streaming (CTL-8) in the background and reports to the button, if any, and the status line.
void run_set_up_for_streaming(const PanelContext &context, QPushButton *button);

// Simple mode: everything a streamer needs on one page, in everyday words (docs/ui.md, "Simple mode", UI-4)
class SimplePanel : public QWidget {
public:
	SimplePanel(PanelContext context, QWidget *parent = nullptr);

	void refresh(const std::set<std::string> &paths);

private:
	Row *add_row(const char *title_key, const char *tooltip_key);
	void refresh_looks();
	void refresh_brightness();
	void refresh_warmth();
	void refresh_lenses();
	void refresh_focus();
	void refresh_stabilization();

	PanelContext context_;
	Watches watches_;
	std::vector<Look> looks_list_;
	QPushButton *set_up_ = nullptr;
	Row *look_row_ = nullptr;
	Segmented *looks_ = nullptr;
	Row *brightness_row_ = nullptr;
	Ruler *brightness_ = nullptr;
	Chip *brightness_auto_ = nullptr;
	EditGuard brightness_guard_;
	Row *warmth_row_ = nullptr;
	Ruler *warmth_ = nullptr;
	EditGuard warmth_guard_;
	Row *lens_row_ = nullptr;
	Segmented *lenses_ = nullptr;
	std::vector<LensChoice> lens_choices_;
	Row *focus_row_ = nullptr;
	Chip *focus_auto_ = nullptr;
	Row *stabilization_row_ = nullptr;
	Segmented *stabilization_ = nullptr;
};

// Advanced mode: every control, with the app's own layout on top: the histogram, the strip of parameter tiles and
// the adjuster of the selected tile, then the other groups in tabs (docs/ui.md, "Advanced mode")
class AdvancedPanel : public QWidget {
public:
	AdvancedPanel(PanelContext context, QWidget *parent = nullptr);

	void refresh(const std::set<std::string> &paths);
	// Updates the histogram and the stream details; called a few times a second while shown
	void tick();

private:
	enum TileId {
		kLensTile,
		kFpsTile,
		kShutterTile,
		kIrisTile,
		kIsoTile,
		kWhiteBalanceTile,
		kTintTile,
		kTileCount
	};

	void add_top(QVBoxLayout *layout);
	void add_tiles(QVBoxLayout *layout);
	QWidget *lens_adjuster();
	QWidget *preset_adjuster();
	QWidget *shutter_adjuster();
	QWidget *iris_adjuster();
	QWidget *iso_adjuster();
	QWidget *white_balance_adjuster();
	QWidget *tint_adjuster();
	QWidget *color_tab();
	QWidget *focus_tab();
	QWidget *audio_tab();
	QWidget *phone_tab();

	// A ruler for one number field of a property, which follows the phone unless the user is changing it
	Ruler *number_ruler(const std::string &path, const std::string &field, int decimals,
			    std::function<QString(double)> format = {});
	void follow_number(Ruler *ruler, EditGuard *guard, const std::string &path, const std::string &field);
	// A chip for a property's "enabled" field
	Chip *enabled_chip(const char *label_key, const char *tooltip_key, const std::string &path);
	QPushButton *reset_button(const char *tooltip_key, std::function<void()> reset);
	void select_tile(int index);
	void refresh_tiles();
	void refresh_look_choice();
	void save_look();
	void delete_look();

	PanelContext context_;
	Watches watches_;
	std::vector<std::unique_ptr<EditGuard>> guards_;
	std::vector<Look> looks_list_;
	HistogramView *histogram_ = nullptr;
	QComboBox *look_ = nullptr;
	QPushButton *delete_look_ = nullptr;
	QLabel *stream_ = nullptr;
	QLabel *battery_ = nullptr;
	std::vector<Tile *> tiles_;
	QStackedWidget *adjusters_ = nullptr;
	QTabWidget *tabs_ = nullptr;
};

} // namespace bmagicam::ui
