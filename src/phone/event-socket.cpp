// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "event-socket.hpp"

#include <httplib.h>

#include <algorithm>
#include <chrono>
#include <utility>

namespace bmagicam {

namespace {

constexpr const char *kPath = "/control/api/v1/event/websocket";
constexpr int kConnectTimeoutSeconds = 3;
// How often a waiting read returns, so that stopping takes effect quickly
constexpr auto kReadTimeout = std::chrono::milliseconds(250);
constexpr int kFirstRetryMs = 500;
constexpr int kLongestRetryMs = 5000;

} // namespace

EventSocket::EventSocket(std::string host, int port, std::vector<std::string> properties, Handler handler,
			 ConnectionHandler on_connection)
	: host_(std::move(host)),
	  port_(port),
	  properties_(std::move(properties)),
	  handler_(std::move(handler)),
	  on_connection_(std::move(on_connection))
{
	thread_ = std::thread(&EventSocket::run, this);
}

EventSocket::~EventSocket()
{
	stopping_ = true;
	thread_.join();
}

void EventSocket::pause(int milliseconds)
{
	const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
	while (!stopping_ && std::chrono::steady_clock::now() < until)
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

void EventSocket::run()
{
	int retry_ms = kFirstRetryMs;
	while (!stopping_) {
		httplib::ws::WebSocketClient socket("wss://" + host_ + ":" + std::to_string(port_) + kPath);
		socket.enable_server_certificate_verification(false);
		socket.set_connection_timeout(kConnectTimeoutSeconds);
		socket.set_read_timeout(kReadTimeout);

		if (socket.connect()) {
			retry_ms = kFirstRetryMs;
			on_connection_(true);

			const nlohmann::json subscribe = {
				{"type", "request"},
				{"id", 1},
				{"data", {{"action", "subscribe"}, {"properties", properties_}}},
			};
			socket.send(subscribe.dump());

			std::string message;
			while (!stopping_) {
				const httplib::ws::ReadResult result = socket.read(message);
				if (result == httplib::ws::Fail)
					break;
				if (result != httplib::ws::Timeout)
					handle_message(message);
			}
			socket.close();
			on_connection_(false);
		}

		pause(retry_ms);
		retry_ms = std::min(retry_ms * 2, kLongestRetryMs);
	}
}

void EventSocket::handle_message(const std::string &text)
{
	const nlohmann::json message = nlohmann::json::parse(text, nullptr, false);
	if (!message.is_object() || !message.contains("data") || !message["data"].is_object())
		return;

	const nlohmann::json &data = message["data"];
	const std::string action = data.value("action", "");
	if (action == "subscribe" && data.contains("values") && data["values"].is_object()) {
		for (const auto &[property, value] : data["values"].items())
			handler_(property, value);
	} else if (action == "propertyValueChanged" && data.contains("property") && data["property"].is_string()) {
		handler_(data["property"].get<std::string>(), data.value("value", nlohmann::json()));
	}
}

} // namespace bmagicam
