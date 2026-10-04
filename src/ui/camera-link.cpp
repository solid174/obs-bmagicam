// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "camera-link.hpp"

#include "../camera/camera-controls.hpp"

#include <mutex>
#include <utility>

namespace bmagicam::ui {

// Shared with the listener, which runs on the connection's threads and may outlive the link
struct CameraLink::Relay {
	std::mutex mutex;
	CameraLink *link = nullptr;
	std::set<std::string> pending;
	bool everything = false;
	bool scheduled = false;

	void add(const std::string &path)
	{
		std::lock_guard lock(mutex);
		if (!link)
			return;
		if (path.empty())
			everything = true;
		else
			pending.insert(path);
		if (!scheduled) {
			scheduled = true;
			// Posted while the lock is held, so the link cannot go away in between
			QMetaObject::invokeMethod(link, [target = link] { target->flush(); }, Qt::QueuedConnection);
		}
	}
};

CameraLink::CameraLink(QObject *parent) : QObject(parent), relay_(std::make_shared<Relay>())
{
	relay_->link = this;
}

CameraLink::~CameraLink()
{
	{
		std::lock_guard lock(relay_->mutex);
		relay_->link = nullptr;
	}
	if (controls_)
		controls_->unlisten(listener_);
}

void CameraLink::set_controls(std::shared_ptr<CameraControls> controls)
{
	if (controls == controls_)
		return;
	if (controls_)
		controls_->unlisten(listener_);
	controls_ = std::move(controls);
	if (controls_)
		listener_ = controls_->listen([relay = relay_](const std::string &path) { relay->add(path); });
	relay_->add({});
}

void CameraLink::flush()
{
	std::set<std::string> paths;
	bool everything = false;
	{
		std::lock_guard lock(relay_->mutex);
		paths.swap(relay_->pending);
		everything = relay_->everything;
		relay_->everything = false;
		relay_->scheduled = false;
	}
	if (changed)
		changed(everything ? std::set<std::string>() : paths);
}

} // namespace bmagicam::ui
