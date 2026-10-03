// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace bmagicam {

// Follows property changes on one phone through Blackmagic Camera's event WebSocket (docs/camera-api.md). It
// subscribes to the given properties, hands their current values over first and then every change, and reconnects on
// its own after the connection drops.
class EventSocket {
public:
	// Called on the socket's thread with a property path such as "/livestreams/0" and its value.
	using Handler = std::function<void(const std::string &property, const nlohmann::json &value)>;
	// Called on the socket's thread when the connection opens (true) or drops (false).
	using ConnectionHandler = std::function<void(bool connected)>;

	EventSocket(std::string host, int port, std::vector<std::string> properties, Handler handler,
		    ConnectionHandler on_connection);
	~EventSocket();

	EventSocket(const EventSocket &) = delete;
	EventSocket &operator=(const EventSocket &) = delete;

private:
	void run();
	void handle_message(const std::string &text);
	// Sleeps for the given time, or less when the socket stops
	void pause(int milliseconds);

	const std::string host_;
	const int port_;
	const std::vector<std::string> properties_;
	const Handler handler_;
	const ConnectionHandler on_connection_;
	std::atomic<bool> stopping_ = false;
	std::thread thread_;
};

} // namespace bmagicam
