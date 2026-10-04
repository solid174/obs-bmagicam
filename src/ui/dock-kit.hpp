// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <obs.hpp>

#include <QString>
#include <QWidget>

#include <chrono>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

class QHBoxLayout;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace bmagicam {
class CameraControls;
}

namespace bmagicam::ui {

// A translated text from the locale files
QString text(const char *key);
// Sets an OBS theme class on a widget (docs/ui.md, "Theme rules") and restyles it
void set_theme_class(QWidget *widget, const char *theme_class);

// What a panel of the dock works on: one iPhone Camera source and its phone
struct PanelContext {
	std::shared_ptr<CameraControls> controls;
	OBSWeakSource source;
	// Shows a short message in the dock's status line
	std::function<void(const QString &message)> notify;
};

// A labelled row of the dock: the label with extras such as an Auto chip on one line, the control below (docs/ui.md).
// The tooltip sits on the label and the control (UI-5).
class Row : public QWidget {
public:
	Row(const QString &title, const QString &tooltip, QWidget *parent = nullptr);

	// A widget before the title, such as an icon
	void add_leading(QWidget *widget);
	void add_extra(QWidget *widget);
	void set_control(QWidget *control);

private:
	QString tooltip_;
	QHBoxLayout *header_;
	QVBoxLayout *layout_;
};

// Keeps a control from following the phone while the user changes it and for a moment after, so the phone's echoes of
// older values do not make it jump back (docs/architecture.md, "Echo handling").
class EditGuard {
public:
	void touched();
	// Whether the control may follow the phone now. If not, the retry runs once the moment is over.
	bool settled(QObject *context, const std::function<void()> &retry);

private:
	std::chrono::steady_clock::time_point last_{};
	bool retry_scheduled_ = false;
};

// Refreshes parts of a panel when the properties they show change.
class Watches {
public:
	void add(std::vector<std::string> paths, std::function<void()> refresh);
	// Runs the refreshes that show one of the paths, or all of them for an empty set
	void refresh(const std::set<std::string> &paths) const;

private:
	struct Watch {
		std::vector<std::string> paths;
		std::function<void()> refresh;
	};
	std::vector<Watch> watches_;
};

} // namespace bmagicam::ui
