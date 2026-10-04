// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <nlohmann/json.hpp>
#include <obs.hpp>

#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace bmagicam {

class CameraControls;

namespace remote {

// Pushes changes to the clients connected over the WebSocket (docs/remote-api.md, "Events"): a control of a camera,
// a camera's state, the Beautify filters, the looks, and the end of a long action.
class Events {
public:
	// One connected client's events, which its connection's thread takes and sends
	class Session {
	public:
		std::vector<std::string> take();

	private:
		friend class Events;
		void push(const nlohmann::json &event);
		// Keeps only the newest state of each control, so a dragged slider does not flood the client
		void push_control(const std::string &key, const nlohmann::json &event);

		std::mutex mutex_;
		std::vector<nlohmann::json> events_;
		std::map<std::string, nlohmann::json> controls_;
	};

	Events();
	~Events();
	Events(const Events &) = delete;
	Events &operator=(const Events &) = delete;

	std::shared_ptr<Session> join();
	void leave(const std::shared_ptr<Session> &session);
	void broadcast(const nlohmann::json &event);

private:
	// What the controls' listeners reach, which may still run while the events stop
	struct Changes {
		std::mutex mutex;
		std::condition_variable wake;
		bool stopping = false;
		// Camera and property
		std::set<std::pair<std::string, std::string>> changed;
	};

	struct Camera {
		OBSWeakSource source;
		std::shared_ptr<CameraControls> controls;
		int listener = 0;
		std::string summary;
	};

	void watch();
	void follow_cameras();
	void send_controls(const std::set<std::pair<std::string, std::string>> &changed);
	std::vector<std::shared_ptr<Session>> sessions();

	std::shared_ptr<Changes> changes_ = std::make_shared<Changes>();
	std::mutex sessions_mutex_;
	std::vector<std::weak_ptr<Session>> sessions_;

	// Only used on the watch thread
	std::map<std::string, Camera> cameras_;
	std::string beautify_;
	std::thread thread_;
};

} // namespace remote
} // namespace bmagicam
