// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <QObject>

#include <functional>
#include <memory>
#include <set>
#include <string>

namespace bmagicam {
class CameraControls;
}

namespace bmagicam::ui {

// Brings the changes of one source's camera controls to the Qt thread, gathered per pass of the event loop, so a
// burst of events, such as ISO under auto exposure, updates the dock once.
class CameraLink : public QObject {
public:
	explicit CameraLink(QObject *parent = nullptr);
	~CameraLink() override;

	void set_controls(std::shared_ptr<CameraControls> controls);
	const std::shared_ptr<CameraControls> &controls() const { return controls_; }

	// Hears the paths that changed since the last call; an empty set when anything may have changed, such as when
	// the connection opened, closed or the controls were replaced
	std::function<void(const std::set<std::string> &paths)> changed;

private:
	struct Relay;

	void flush();

	std::shared_ptr<Relay> relay_;
	std::shared_ptr<CameraControls> controls_;
	int listener_ = 0;
};

} // namespace bmagicam::ui
