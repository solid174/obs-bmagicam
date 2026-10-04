// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "remote-server.hpp"

#include "access.hpp"
#include "remote-api.hpp"
#include "remote-events.hpp"
#include "../source/camera-source.hpp"

#include <httplib.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace bmagicam::remote {

namespace {

constexpr auto kHelloTimeout = std::chrono::seconds(10);
// How long a connection waits for a client message before it sends the events queued meanwhile
constexpr auto kEventInterval = std::chrono::milliseconds(50);

void respond(httplib::Response &response, const Reply &reply)
{
	response.status = reply.status;
	response.set_content(reply.body.dump(), "application/json; charset=utf-8");
}

nlohmann::json parse(const std::string &text, bool &ok)
{
	if (text.empty()) {
		ok = true;
		return nlohmann::json::object();
	}
	nlohmann::json value = nlohmann::json::parse(text, nullptr, false);
	ok = !value.is_discarded();
	return value;
}

} // namespace

struct Server::Running {
	RemoteSettings settings;
	httplib::Server http;
	Events events;
	PasswordGate gate;
	std::atomic<bool> stopping = false;
	std::thread thread;

	explicit Running(const RemoteSettings &chosen) : settings(chosen) { routes(); }

	~Running()
	{
		stopping = true;
		http.stop();
		if (thread.joinable())
			thread.join();
	}

	// Whether the request carries the password; answers it otherwise (WEB-4)
	bool authorized(const httplib::Request &request, httplib::Response &response)
	{
		if (!settings.authentication)
			return true;
		const auto now = PasswordGate::Clock::now();
		if (gate.blocked(request.remote_addr, now)) {
			respond(response,
				remote::error(429, "blocked", "Too many wrong passwords; try again in a minute"));
			return false;
		}
		const std::string header = request.get_header_value("Authorization");
		const std::string given = header.rfind("Bearer ", 0) == 0 ? header.substr(7) : std::string();
		if (same_password(given, settings.password)) {
			gate.succeeded(request.remote_addr);
			return true;
		}
		gate.failed(request.remote_addr, now);
		std::this_thread::sleep_for(std::chrono::seconds(1));
		respond(response, remote::error(401, "unauthorized", "Missing or wrong password"));
		return false;
	}

	using Handler = std::function<Reply(const httplib::Request &, const nlohmann::json &)>;

	// An API route: the password, a JSON body, a JSON answer
	httplib::Server::Handler api(Handler handler)
	{
		return [this, handler = std::move(handler)](const httplib::Request &request,
							    httplib::Response &response) {
			if (!authorized(request, response))
				return;
			bool ok = false;
			const nlohmann::json body = parse(request.body, ok);
			if (!ok) {
				respond(response, remote::error(400, "invalid", "The body is not JSON"));
				return;
			}
			respond(response, handler(request, body));
		};
	}

	void report(const nlohmann::json &event) { events.broadcast(event); }

	void routes()
	{
		// Only the local network may connect (WEB-4)
		http.set_pre_routing_handler([](const httplib::Request &request, httplib::Response &response) {
			if (is_local_address(request.remote_addr))
				return httplib::Server::HandlerResponse::Unhandled;
			respond(response, remote::error(403, "forbidden", "Only the local network may connect"));
			return httplib::Server::HandlerResponse::Handled;
		});

		// The web panel, without a password: it asks for one
		if (char *panel = obs_module_file("panel")) {
			http.set_mount_point("/", panel);
			bfree(panel);
		}

		const auto plain = [this](std::function<nlohmann::json()> make) {
			return api([make = std::move(make)](const httplib::Request &, const nlohmann::json &) {
				return Reply{200, make()};
			});
		};
		http.Get("/api/v1/info", plain(info));
		http.Get("/api/v1/theme", plain(theme));
		http.Get("/api/v1/locale", plain(locale));
		http.Get("/api/v1/controls", plain(controls));
		http.Get("/api/v1/stream-presets", plain(stream_presets));
		http.Get("/api/v1/cameras", plain([] {
				 nlohmann::json list = nlohmann::json::array();
				 for (const OBSSource &source : camera_sources())
					 list.push_back(camera_summary(source));
				 return list;
			 }));
		http.Get(R"(/api/v1/cameras/([^/]+))", api([](const httplib::Request &request, const nlohmann::json &) {
				 OBSSourceAutoRelease source = obs_get_source_by_uuid(request.matches[1].str().c_str());
				 if (!is_camera_source(source))
					 return remote::error(404, "not_found", "No iPhone Camera with this ID");
				 return Reply{200, camera_detail(source)};
			 }));
		http.Put(R"(/api/v1/cameras/([^/]+)/controls/([^/]+))",
			 api([](const httplib::Request &request, const nlohmann::json &body) {
				 return put_control(request.matches[1], request.matches[2], body,
						    request.get_param_value("wait") == "1");
			 }));
		http.Post(R"(/api/v1/cameras/([^/]+)/actions/([^/]+))",
			  api([this](const httplib::Request &request, const nlohmann::json &body) {
				  return run_action(request.matches[1], request.matches[2], body,
						    [this](const nlohmann::json &event) { report(event); });
			  }));
		http.Put(R"(/api/v1/cameras/([^/]+)/look)",
			 api([](const httplib::Request &request, const nlohmann::json &body) {
				 return put_look(request.matches[1], body);
			 }));
		http.Put(R"(/api/v1/cameras/([^/]+)/stream)",
			 api([](const httplib::Request &request, const nlohmann::json &body) {
				 return put_stream(request.matches[1], body);
			 }));

		http.Get("/api/v1/looks", plain(looks));
		http.Post("/api/v1/looks", api([this](const httplib::Request &, const nlohmann::json &body) {
				  return looks_changed(post_look(body));
			  }));
		http.Patch(R"(/api/v1/looks/(.+))",
			   api([this](const httplib::Request &request, const nlohmann::json &body) {
				   return looks_changed(patch_look(request.matches[1], body));
			   }));
		http.Delete(R"(/api/v1/looks/(.+))",
			    api([this](const httplib::Request &request, const nlohmann::json &) {
				    return looks_changed(delete_look(request.matches[1]));
			    }));

		http.Get("/api/v1/beautify", plain(beautify));
		http.Get("/api/v1/beautify/styles", plain(styles));
		http.Post("/api/v1/beautify",
			  api([](const httplib::Request &, const nlohmann::json &body) { return add_beautify(body); }));
		http.Put(R"(/api/v1/beautify/([^/]+)/(.+))",
			 api([](const httplib::Request &request, const nlohmann::json &body) {
				 return put_beautify(request.matches[1], request.matches[2], body);
			 }));

		http.WebSocket("/api/v1/events",
			       [this](const httplib::Request &request, httplib::ws::WebSocket &socket) {
				       connection(request, socket);
			       });
	}

	// The looks changed: every client hears it
	Reply looks_changed(Reply reply)
	{
		if (reply.status < 300)
			events.broadcast({{"op", "looks"}, {"looks", looks()}});
		return reply;
	}

	void send_error(httplib::ws::WebSocket &socket, const std::string &op, const Reply &reply)
	{
		nlohmann::json event = reply.body.value("error", nlohmann::json::object());
		event["op"] = "error";
		event["request"] = op;
		event["status"] = reply.status;
		socket.send(event.dump());
	}

	// One WebSocket client: the password, then everything it needs, then changes both ways
	void connection(const httplib::Request &request, httplib::ws::WebSocket &socket)
	{
		socket.set_read_timeout(kHelloTimeout);
		std::string message;
		if (socket.read(message) != httplib::ws::Text) {
			socket.close(httplib::ws::CloseStatus::PolicyViolation, "hello expected");
			return;
		}
		bool ok = false;
		const nlohmann::json hello = parse(message, ok);
		if (!ok || !hello.is_object() || hello.value("op", std::string()) != "hello") {
			socket.close(httplib::ws::CloseStatus::PolicyViolation, "hello expected");
			return;
		}
		if (settings.authentication) {
			const auto now = PasswordGate::Clock::now();
			if (gate.blocked(request.remote_addr, now)) {
				send_error(socket, "hello",
					   remote::error(429, "blocked",
							 "Too many wrong passwords; try again in a minute"));
				socket.close(httplib::ws::CloseStatus::PolicyViolation, "blocked");
				return;
			}
			if (!same_password(hello.value("password", std::string()), settings.password)) {
				gate.failed(request.remote_addr, now);
				std::this_thread::sleep_for(std::chrono::seconds(1));
				send_error(socket, "hello",
					   remote::error(401, "unauthorized", "Missing or wrong password"));
				socket.close(httplib::ws::CloseStatus::PolicyViolation, "unauthorized");
				return;
			}
			gate.succeeded(request.remote_addr);
		}

		const std::shared_ptr<Events::Session> session = events.join();
		nlohmann::json cameras = nlohmann::json::array();
		for (const OBSSource &source : camera_sources())
			cameras.push_back(camera_detail(source));
		socket.send(nlohmann::json{
			{"op", "ready"},
			{"info", info()},
			{"cameras", cameras},
			{"looks", looks()},
			{"beautify", beautify()},
			{"styles",
			 styles()}}.dump());

		socket.set_read_timeout(kEventInterval);
		while (!stopping && socket.is_open()) {
			const httplib::ws::ReadResult result = socket.read(message);
			if (result == httplib::ws::Fail)
				break;
			if (result == httplib::ws::Text)
				handle(socket, message);
			for (const std::string &event : session->take()) {
				if (!socket.send(event))
					break;
			}
		}
		events.leave(session);
	}

	// A change a client sends; it behaves like the matching request (docs/remote-api.md, "Events")
	void handle(httplib::ws::WebSocket &socket, const std::string &message)
	{
		bool ok = false;
		const nlohmann::json request = parse(message, ok);
		const std::string op = ok && request.is_object() ? request.value("op", std::string()) : std::string();
		const std::string camera = ok && request.is_object() ? request.value("camera", std::string())
								     : std::string();
		Reply reply;
		if (op == "set")
			reply = put_control(camera, request.value("control", std::string()),
					    {{"value", request.value("value", nlohmann::json())}}, false);
		else if (op == "action")
			reply = run_action(camera, request.value("action", std::string()), request,
					   [this](const nlohmann::json &event) { report(event); });
		else if (op == "look")
			reply = put_look(camera, request);
		else if (op == "stream")
			reply = put_stream(camera, request);
		else if (op == "addBeauty")
			reply = add_beautify(request);
		else if (op == "beautify")
			reply = put_beautify(request.value("source", std::string()),
					     request.value("filter", std::string()),
					     request.value("settings", nlohmann::json::object()));
		else
			reply = remote::error(400, "invalid", "Unknown op");
		if (reply.status >= 400)
			send_error(socket, op, reply);
	}
};

Server &Server::instance()
{
	static Server server;
	return server;
}

bool Server::apply(const RemoteSettings &settings)
{
	std::lock_guard lock(mutex_);
	running_.reset();
	error_.clear();
	if (!settings.enabled)
		return true;
	auto running = std::make_unique<Running>(settings);
	if (!running->http.bind_to_port("0.0.0.0", settings.port)) {
		error_ = "port " + std::to_string(settings.port) + " is in use";
		obs_log(LOG_WARNING, "Remote Control cannot listen: %s", error_.c_str());
		return false;
	}
	Running *server = running.get();
	running->thread = std::thread([server] { server->http.listen_after_bind(); });
	running_ = std::move(running);
	obs_log(LOG_INFO, "Remote Control listening on port %d", settings.port);
	return true;
}

void Server::stop()
{
	std::lock_guard lock(mutex_);
	if (running_)
		obs_log(LOG_INFO, "Remote Control stopped");
	running_.reset();
}

bool Server::running() const
{
	std::lock_guard lock(mutex_);
	return running_ != nullptr;
}

std::string Server::error() const
{
	std::lock_guard lock(mutex_);
	return error_;
}

} // namespace bmagicam::remote
