// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace bmagicam {

// A reply from Blackmagic Camera's REST API.
struct ApiReply {
	// HTTP status, or 0 when the phone did not answer
	int status = 0;
	// The body as JSON, null when there is none or it is not JSON
	nlohmann::json body;
	// The body as it came, for the replies that are not JSON
	std::string text;

	bool ok() const { return status >= 200 && status < 300; }
	bool answered() const { return status != 0; }
};

// Talks to one phone's REST API: HTTPS on port 4444 with the app's self-signed certificate, under /control/api/v1
// (see docs/camera-api.md). The phone closes every connection, so each call opens its own. Calls block and may be made
// from any thread.
class CameraClient {
public:
	explicit CameraClient(std::string host, int port = 4444);

	const std::string &host() const { return host_; }
	int port() const { return port_; }

	ApiReply get(const std::string &path) const;
	ApiReply put(const std::string &path, const nlohmann::json &body = nullptr) const;
	ApiReply put_xml(const std::string &path, const std::string &xml) const;
	ApiReply remove(const std::string &path) const;

private:
	enum class Method { Get, Put, Delete };

	ApiReply request(Method method, const std::string &path, const std::string &body,
			 const std::string &content_type) const;

	std::string host_;
	int port_;
};

} // namespace bmagicam
