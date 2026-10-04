// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include "remote-settings.hpp"

#include <memory>
#include <mutex>
#include <string>

namespace bmagicam::remote {

// Remote Control (WEB-1 to WEB-5): serves the web panel, the API under /api/v1 and its events on the local network.
// Off until the settings turn it on.
class Server {
public:
	static Server &instance();

	// Starts, restarts or stops to match the settings; false when it could not listen, with the reason in error()
	bool apply(const RemoteSettings &settings);
	void stop();
	bool running() const;
	std::string error() const;

private:
	struct Running;

	Server() = default;

	mutable std::mutex mutex_;
	std::unique_ptr<Running> running_;
	std::string error_;
};

} // namespace bmagicam::remote
