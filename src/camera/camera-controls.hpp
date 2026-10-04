// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include "../phone/camera-client.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace bmagicam {

// The control connection to the phone of one iPhone Camera source (docs/architecture.md, "Control path"). It finds the
// phone, follows the properties the dock shows through the event WebSocket, reads the ranges and lists they depend on,
// and sends changes through a WriteQueue. It runs whether or not the source streams. Calls may come from any thread;
// listeners hear on the connection's own threads.
class CameraControls {
public:
	// Hears the path of a property whose value changed, or an empty path when the connection opened or closed.
	using Listener = std::function<void(const std::string &path)>;

	CameraControls();
	// Stops talking to the phone; requests in flight end in the background.
	~CameraControls();

	CameraControls(const CameraControls &) = delete;
	CameraControls &operator=(const CameraControls &) = delete;

	// The phone to control: found on the network by its ID, or at an address entered by hand.
	void set_phone(const std::string &phone_id, const std::string &address);

	bool connected() const;
	// The property's last known value: null when it is not known (yet), or the phone does not have it.
	nlohmann::json get(const std::string &path) const;

	// Changes fields of a property. The change shows in get() at once; when the phone refuses it, the phone's value
	// comes back.
	void set(const std::string &path, const nlohmann::json &fields);
	// Asks the phone to do something, such as refocus. Also through the write queue, so it waits for the writes it
	// depends on.
	void act(const std::string &path, const nlohmann::json &body = nullptr);
	// The phone's answer to the last write to the property: 403 when another setting locks it, 0 when it did not answer.
	int write_status(const std::string &path) const;

	// For routines, such as Set up for streaming, that need each answer before the next step. They block, and the
	// property's value follows a successful write.
	ApiReply put_now(const std::string &path, const nlohmann::json &body);
	ApiReply get_now(const std::string &path);
	ApiReply remove_now(const std::string &path);
	// Waits until the property's value satisfies the test, or the time is up.
	bool wait_for(const std::string &path, const std::function<bool(const nlohmann::json &)> &test,
		      std::chrono::milliseconds limit);

	int listen(Listener listener);
	void unlisten(int id);

private:
	struct Core;
	std::shared_ptr<Core> core_;
};

} // namespace bmagicam
