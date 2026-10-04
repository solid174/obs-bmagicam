// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "remote-events.hpp"

#include "remote-api.hpp"
#include "../camera/camera-controls.hpp"
#include "../camera/control-map.hpp"
#include "../source/camera-source.hpp"

#include <algorithm>
#include <chrono>

namespace bmagicam::remote {

namespace {

// Cameras, their states and the Beautify filters are compared this often
constexpr auto kPollInterval = std::chrono::milliseconds(500);
// Control changes are gathered for this long, so a burst goes out once
constexpr auto kGatherInterval = std::chrono::milliseconds(40);

} // namespace

std::vector<std::string> Events::Session::take()
{
	std::lock_guard lock(mutex_);
	std::vector<std::string> out;
	out.reserve(events_.size() + controls_.size());
	for (const nlohmann::json &event : events_)
		out.push_back(event.dump());
	for (const auto &[key, event] : controls_)
		out.push_back(event.dump());
	events_.clear();
	controls_.clear();
	return out;
}

void Events::Session::push(const nlohmann::json &event)
{
	std::lock_guard lock(mutex_);
	events_.push_back(event);
}

void Events::Session::push_control(const std::string &key, const nlohmann::json &event)
{
	std::lock_guard lock(mutex_);
	controls_[key] = event;
}

Events::Events() : thread_(&Events::watch, this) {}

Events::~Events()
{
	{
		std::lock_guard lock(changes_->mutex);
		changes_->stopping = true;
	}
	changes_->wake.notify_all();
	thread_.join();
	for (auto &[id, camera] : cameras_) {
		if (camera.controls)
			camera.controls->unlisten(camera.listener);
	}
}

std::shared_ptr<Events::Session> Events::join()
{
	auto session = std::make_shared<Session>();
	std::lock_guard lock(sessions_mutex_);
	sessions_.push_back(session);
	return session;
}

void Events::leave(const std::shared_ptr<Session> &session)
{
	std::lock_guard lock(sessions_mutex_);
	sessions_.erase(std::remove_if(sessions_.begin(), sessions_.end(),
				       [&](const std::weak_ptr<Session> &item) {
					       const auto locked = item.lock();
					       return !locked || locked == session;
				       }),
			sessions_.end());
}

std::vector<std::shared_ptr<Events::Session>> Events::sessions()
{
	std::lock_guard lock(sessions_mutex_);
	std::vector<std::shared_ptr<Session>> out;
	for (const std::weak_ptr<Session> &item : sessions_) {
		if (auto session = item.lock())
			out.push_back(std::move(session));
	}
	return out;
}

void Events::broadcast(const nlohmann::json &event)
{
	for (const std::shared_ptr<Session> &session : sessions())
		session->push(event);
}

void Events::watch()
{
	auto next_poll = std::chrono::steady_clock::now();
	for (;;) {
		std::set<std::pair<std::string, std::string>> changed;
		{
			Changes &changes = *changes_;
			std::unique_lock lock(changes.mutex);
			changes.wake.wait_until(lock, next_poll,
						[&changes] { return changes.stopping || !changes.changed.empty(); });
			if (changes.stopping)
				return;
			if (!changes.changed.empty()) {
				// A moment for the rest of a burst
				lock.unlock();
				std::this_thread::sleep_for(kGatherInterval);
				lock.lock();
				changed.swap(changes.changed);
			}
		}
		// The controls are read without the lock: their listeners take it
		if (!changed.empty())
			send_controls(changed);
		if (std::chrono::steady_clock::now() >= next_poll) {
			follow_cameras();
			const nlohmann::json filters = beautify();
			const std::string snapshot = filters.dump();
			if (snapshot != beautify_) {
				beautify_ = snapshot;
				broadcast({{"op", "beautify"}, {"beautify", filters}});
			}
			next_poll = std::chrono::steady_clock::now() + kPollInterval;
		}
	}
}

void Events::follow_cameras()
{
	std::set<std::string> present;
	for (const OBSSource &source : camera_sources()) {
		const std::string id = obs_source_get_uuid(source);
		present.insert(id);
		Camera &camera = cameras_[id];
		camera.source = OBSGetWeakRef(source);
		const std::shared_ptr<CameraControls> controls = camera_controls(source);
		if (controls != camera.controls) {
			if (camera.controls)
				camera.controls->unlisten(camera.listener);
			camera.controls = controls;
			if (controls)
				camera.listener = controls->listen([changes = changes_, id](const std::string &path) {
					{
						std::lock_guard lock(changes->mutex);
						changes->changed.insert({id, path});
					}
					changes->wake.notify_all();
				});
		}
		const nlohmann::json summary = camera_summary(source);
		const std::string snapshot = summary.dump();
		if (snapshot != camera.summary) {
			const bool was_connected = camera.summary.find("\"connected\":true") != std::string::npos;
			camera.summary = snapshot;
			// A camera that connects brings every control's state
			if (summary.value("connected", false) && !was_connected)
				broadcast({{"op", "camera"}, {"camera", camera_detail(source)}});
			else
				broadcast({{"op", "camera"}, {"camera", summary}});
		}
	}
	for (auto camera = cameras_.begin(); camera != cameras_.end();) {
		if (present.count(camera->first)) {
			++camera;
			continue;
		}
		if (camera->second.controls)
			camera->second.controls->unlisten(camera->second.listener);
		broadcast({{"op", "cameraRemoved"}, {"camera", camera->first}});
		camera = cameras_.erase(camera);
	}
}

void Events::send_controls(const std::set<std::pair<std::string, std::string>> &changed)
{
	const std::vector<std::shared_ptr<Session>> targets = sessions();
	std::map<std::string, std::set<std::string>> paths;
	for (const auto &[camera, path] : changed)
		paths[camera].insert(path);
	for (const auto &entry : paths) {
		const std::string &id = entry.first;
		const std::set<std::string> &camera_paths = entry.second;
		const auto camera = cameras_.find(id);
		if (camera == cameras_.end() || !camera->second.controls)
			continue;
		const CameraControls &controls = *camera->second.controls;
		// An empty path means anything may have changed
		const bool everything = camera_paths.count(std::string());
		for (const Control &control : control_map()) {
			const bool affected = everything || std::any_of(control.paths.begin(), control.paths.end(),
									[&](const std::string &path) {
										return camera_paths.count(path);
									});
			if (!affected)
				continue;
			nlohmann::json event = control_state(control, controls);
			event["op"] = "control";
			event["camera"] = id;
			event["control"] = control.id;
			for (const std::shared_ptr<Session> &session : targets)
				session->push_control(id + "\n" + control.id, event);
		}
	}
}

} // namespace bmagicam::remote
