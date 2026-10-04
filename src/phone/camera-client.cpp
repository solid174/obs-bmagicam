// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "camera-client.hpp"

#include <httplib.h>

#include <utility>

namespace bmagicam {

namespace {

constexpr const char *kBasePath = "/control/api/v1";
// The first request after the phone was idle sometimes goes unanswered; the next one answers
constexpr int kAttempts = 2;
constexpr int kConnectTimeoutSeconds = 3;
constexpr int kReadTimeoutSeconds = 5;

} // namespace

CameraClient::CameraClient(std::string host, int port) : host_(std::move(host)), port_(port) {}

ApiReply CameraClient::get(const std::string &path) const
{
	return request(Method::Get, path, {}, {});
}

ApiReply CameraClient::put(const std::string &path, const nlohmann::json &body) const
{
	return request(Method::Put, path, body.is_null() ? std::string() : body.dump(), "application/json");
}

ApiReply CameraClient::put_xml(const std::string &path, const std::string &xml) const
{
	return request(Method::Put, path, xml, "application/xml");
}

ApiReply CameraClient::remove(const std::string &path) const
{
	return request(Method::Delete, path, {}, {});
}

ApiReply CameraClient::request(Method method, const std::string &path, const std::string &body,
			       const std::string &content_type) const
{
	const std::string target = kBasePath + path;
	for (int attempt = 0; attempt < kAttempts; attempt++) {
		// The certificate is self-signed and nothing secret travels on this connection (architecture.md, D8)
		httplib::SSLClient client(host_, port_);
		client.enable_server_certificate_verification(false);
		client.set_connection_timeout(kConnectTimeoutSeconds);
		client.set_read_timeout(kReadTimeoutSeconds);
		client.set_write_timeout(kReadTimeoutSeconds);

		httplib::Result result;
		switch (method) {
		case Method::Get:
			result = client.Get(target);
			break;
		case Method::Put:
			result = client.Put(target, body, content_type);
			break;
		case Method::Delete:
			result = client.Delete(target);
			break;
		}
		if (!result)
			continue;

		ApiReply reply;
		reply.status = result->status;
		reply.text = result->body;
		if (!reply.text.empty()) {
			reply.body = nlohmann::json::parse(reply.text, nullptr, false);
			if (reply.body.is_discarded())
				reply.body = nullptr;
		}
		return reply;
	}
	return {};
}

} // namespace bmagicam
