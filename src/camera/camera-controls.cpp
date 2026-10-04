// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "camera-controls.hpp"

#include "background-work.hpp"
#include "../discovery/phone-browser.hpp"
#include "../phone/event-socket.hpp"
#include "../phone/write-queue.hpp"

#include <algorithm>
#include <condition_variable>
#include <future>
#include <map>
#include <mutex>
#include <set>
#include <vector>

namespace bmagicam {

namespace {

using namespace std::chrono_literals;

// How often the phone's address is checked against Bonjour while connected
constexpr auto kAddressCheck = 5s;
// Time the event socket gets to report the values before the rest are read one by one
constexpr auto kSubscribeWait = 3s;
constexpr size_t kParallelReads = 4;

// The values the dock shows, followed live. The recording timecode is left out: it changes with every frame.
const std::vector<std::string> &value_paths()
{
	static const std::vector<std::string> paths = {
		"/system/product",
		"/access/status",
		"/camera/power",
		"/camera/id",
		"/video/autoExposure",
		"/video/iso",
		"/video/shutter",
		"/video/shutter/measurement",
		"/video/whiteBalance",
		"/video/whiteBalanceTint",
		"/lens/cameras/active",
		"/lens/zoom",
		"/lens/focus",
		"/lens/focus/autoFocus",
		"/lens/focus/autoFocus/target",
		"/lens/opticalImageStabilization",
		"/lens/iris",
		"/colorCorrection/lift",
		"/colorCorrection/gamma",
		"/colorCorrection/gain",
		"/colorCorrection/offset",
		"/colorCorrection/contrast",
		"/colorCorrection/color",
		"/colorCorrection/lumaContribution",
		"/system/videoFormat",
		"/system/format",
		"/system/codecFormat",
		"/system/dynamicRange",
		"/monitoring/Device/brightness",
		"/monitoring/Device/zebra",
		"/monitoring/Device/focusAssist",
		"/monitoring/Device/falseColor",
		"/monitoring/Device/frameGuide",
		"/monitoring/Device/frameGrids",
		"/monitoring/Device/safeArea",
		"/monitoring/Device/displayLUT",
		"/monitoring/zebra",
		"/monitoring/focusAssist",
		"/monitoring/frameGuideRatio",
		"/monitoring/frameGrids",
		"/monitoring/safeAreaPercent",
		"/audio/channel/0/input",
		"/audio/channel/0/level",
		"/audio/channel/0/phantomPower",
		"/audio/channel/0/padding",
		"/audio/channel/0/lowCutFilter",
		"/audio/channel/1/input",
		"/audio/channel/1/level",
		"/audio/channel/1/phantomPower",
		"/audio/channel/1/padding",
		"/audio/channel/1/lowCutFilter",
		"/transports/0/record",
		"/transports/0/proxyRecording",
		"/media/workingset",
		"/presets",
		"/presets/active",
		"/livestreams/0",
	};
	return paths;
}

// Ranges and lists, read when the connection opens and again when what they depend on changes
const std::vector<std::string> &description_paths()
{
	static const std::vector<std::string> paths = {
		"/video/supportedISOs",
		"/video/supportedShutters",
		"/video/flickerFreeShutters",
		"/video/whiteBalance/description",
		"/video/whiteBalanceTint/description",
		"/lens/cameras",
		"/lens/cameras/auto",
		"/lens/zoom/description",
		"/lens/focus/description",
		"/lens/focus/autoFocus/description",
		"/lens/iris/description",
		"/system/supportedVideoFormats",
		"/system/supportedDynamicRanges",
		"/system/supportedCodecFormats",
		"/monitoring/frameGuideRatio/presets",
		"/audio/channels",
		"/audio/channel/0/supportedInputs",
		"/audio/channel/1/supportedInputs",
		"/audio/channel/0/input/description",
		"/audio/channel/1/input/description",
	};
	return paths;
}

bool followed(const std::string &path)
{
	const auto &values = value_paths();
	const auto &descriptions = description_paths();
	return std::find(values.begin(), values.end(), path) != values.end() ||
	       std::find(descriptions.begin(), descriptions.end(), path) != descriptions.end();
}

// What to read again when a property changes, because it depends on it: the shutters and ISOs on offer follow the
// frame rate and the lens, and the zoom, focus and iris ranges follow the lens
std::vector<std::string> dependents(const std::string &path)
{
	if (path == "/system/videoFormat")
		return {"/system/format", "/system/supportedVideoFormats", "/video/supportedShutters",
			"/video/flickerFreeShutters", "/video/supportedISOs"};
	if (path == "/lens/cameras/active")
		return {"/lens/cameras",           "/lens/zoom/description",
			"/lens/focus/description", "/lens/focus/autoFocus/description",
			"/lens/iris/description",  "/lens/iris",
			"/video/supportedISOs",    "/lens/opticalImageStabilization"};
	if (path == "/audio/channel/0/input")
		return {"/audio/channel/0/input/description"};
	if (path == "/audio/channel/1/input")
		return {"/audio/channel/1/input/description"};
	return {};
}

// The event socket names the phone screen's properties after its LCD, the REST API after "Device"
constexpr const char *kScreenRest = "/monitoring/Device/";
constexpr const char *kScreenEvents = "/monitoring/LCD/";

std::string event_property(const std::string &path)
{
	if (path.rfind(kScreenRest, 0) == 0)
		return kScreenEvents + path.substr(std::char_traits<char>::length(kScreenRest));
	return path;
}

std::string rest_path(const std::string &property)
{
	if (property.rfind(kScreenEvents, 0) == 0)
		return kScreenRest + property.substr(std::char_traits<char>::length(kScreenEvents));
	return property;
}

// The writes the API takes as POST
bool posted(const std::string &path)
{
	return path == "/transports/0/record" || path == "/transports/0/stop";
}

bool success(int status)
{
	return status >= 200 && status < 300;
}

} // namespace

struct CameraControls::Core : std::enable_shared_from_this<Core> {
	mutable std::mutex mutex;
	std::condition_variable cv;
	std::string phone_id;
	std::string manual_address;
	uint64_t generation = 0;
	bool closing = false;
	bool phones_changed = false;

	// The phone in use; empty host while there is none
	std::string host;
	int port = 4444;
	bool connected = false;
	std::map<std::string, nlohmann::json> values;
	// Properties the event socket reports
	std::set<std::string> pushed;
	std::map<std::string, int> statuses;
	std::set<std::string> rereads;
	std::unique_ptr<WriteQueue> writes;

	std::mutex listeners_mutex;
	std::map<int, Listener> listeners;
	int next_listener = 1;

	void run();
	// Follows the phone at the address until it moves, another phone is chosen or the controls close
	void follow(const std::string &address, int port_number, uint64_t current);
	bool resolve(std::string &address, int &port_number) const;
	void read_all(const CameraClient &client, const std::vector<std::string> &paths);
	void store(const std::string &path, const ApiReply &reply);

	void on_event(const std::string &property, const nlohmann::json &value);
	void on_connection(bool open);
	void on_written(const std::string &path, int status);
	void notify(const std::string &path);
};

void CameraControls::Core::notify(const std::string &path)
{
	cv.notify_all();
	std::vector<Listener> targets;
	{
		std::lock_guard lock(listeners_mutex);
		for (const auto &[id, listener] : listeners)
			targets.push_back(listener);
	}
	for (const Listener &listener : targets)
		listener(path);
}

bool CameraControls::Core::resolve(std::string &address, int &port_number) const
{
	std::string id;
	{
		std::lock_guard lock(mutex);
		id = phone_id;
		address = manual_address;
	}
	port_number = 4444;
	if (id.empty())
		return !address.empty();

	FoundPhone found;
	if (!PhoneBrowser::instance().find(id, found))
		return false;
	address = found.address;
	port_number = found.port;
	return true;
}

void CameraControls::Core::run()
{
	const int discovery = PhoneBrowser::instance().listen([weak = weak_from_this()] {
		if (const std::shared_ptr<Core> core = weak.lock()) {
			{
				std::lock_guard lock(core->mutex);
				core->phones_changed = true;
			}
			core->cv.notify_all();
		}
	});

	std::unique_lock lock(mutex);
	while (!closing) {
		const uint64_t current = generation;
		phones_changed = false;
		lock.unlock();

		std::string address;
		int port_number = 4444;
		const bool found = resolve(address, port_number);
		if (found)
			follow(address, port_number, current);

		lock.lock();
		if (!found)
			cv.wait_for(lock, kAddressCheck,
				    [&] { return closing || generation != current || phones_changed; });
	}
	lock.unlock();

	PhoneBrowser::instance().unlisten(discovery);
}

void CameraControls::Core::follow(const std::string &address, int port_number, uint64_t current)
{
	const CameraClient client(address, port_number);
	{
		std::lock_guard lock(mutex);
		host = address;
		port = port_number;
		writes = std::make_unique<WriteQueue>(
			[client](const std::string &path, const nlohmann::json &body) {
				return (posted(path) ? client.post(path, body) : client.put(path, body)).status;
			},
			[this](const std::string &path, int status) { on_written(path, status); });
	}

	std::vector<std::string> subscriptions;
	for (const std::string &path : value_paths())
		subscriptions.push_back(event_property(path));
	for (const std::string &path : description_paths())
		subscriptions.push_back(path);
	auto socket = std::make_unique<EventSocket>(
		address, port_number, subscriptions,
		[this](const std::string &property, const nlohmann::json &value) { on_event(property, value); },
		[this](bool open) { on_connection(open); });

	const auto changed = [&] {
		return closing || generation != current;
	};

	// Ranges and lists first; then the values the event socket did not report
	read_all(client, description_paths());
	{
		std::unique_lock lock(mutex);
		cv.wait_for(lock, kSubscribeWait,
			    [&] { return changed() || pushed.size() >= value_paths().size() / 2; });
	}
	std::vector<std::string> missing;
	{
		std::lock_guard lock(mutex);
		for (const std::string &path : value_paths()) {
			if (!pushed.count(path))
				missing.push_back(path);
		}
	}
	read_all(client, missing);

	std::unique_lock lock(mutex);
	auto next_check = std::chrono::steady_clock::now() + kAddressCheck;
	while (!changed()) {
		if (!rereads.empty()) {
			const std::vector<std::string> paths(rereads.begin(), rereads.end());
			rereads.clear();
			lock.unlock();
			read_all(client, paths);
			lock.lock();
			continue;
		}
		if (phones_changed || std::chrono::steady_clock::now() >= next_check) {
			// A phone that left Bonjour keeps its socket, which reconnects by itself; one that moved is followed
			phones_changed = false;
			lock.unlock();
			std::string now_address;
			int now_port = 4444;
			const bool found = resolve(now_address, now_port);
			lock.lock();
			if (found && (now_address != address || now_port != port_number))
				break;
			next_check = std::chrono::steady_clock::now() + kAddressCheck;
			continue;
		}
		cv.wait_until(lock, next_check, [&] { return changed() || !rereads.empty() || phones_changed; });
	}
	lock.unlock();

	socket.reset();
	std::unique_ptr<WriteQueue> old_writes;
	{
		std::lock_guard lock(mutex);
		old_writes = std::move(writes);
		host.clear();
		connected = false;
		values.clear();
		pushed.clear();
		statuses.clear();
		rereads.clear();
	}
	old_writes.reset();
	notify({});
}

void CameraControls::Core::read_all(const CameraClient &client, const std::vector<std::string> &paths)
{
	for (size_t first = 0; first < paths.size(); first += kParallelReads) {
		{
			std::lock_guard lock(mutex);
			if (closing)
				return;
		}
		const size_t end = std::min(paths.size(), first + kParallelReads);
		std::vector<std::future<ApiReply>> replies;
		for (size_t i = first; i < end; i++)
			replies.push_back(std::async(std::launch::async,
						     [&client, &path = paths[i]] { return client.get(path); }));
		for (size_t i = first; i < end; i++)
			store(paths[i], replies[i - first].get());
	}
}

void CameraControls::Core::store(const std::string &path, const ApiReply &reply)
{
	if (!reply.answered())
		return;

	bool changed = false;
	{
		std::lock_guard lock(mutex);
		if (reply.ok() && !reply.body.is_null()) {
			nlohmann::json &value = values[path];
			changed = value != reply.body;
			value = reply.body;
		} else if (reply.status == 404 || reply.status == 501) {
			// The phone does not have it
			changed = values.erase(path) > 0;
		}
	}
	if (changed)
		notify(path);
}

void CameraControls::Core::on_event(const std::string &property, const nlohmann::json &value)
{
	const std::string path = rest_path(property);
	bool changed = false;
	{
		std::lock_guard lock(mutex);
		pushed.insert(path);
		nlohmann::json &stored = values[path];
		changed = stored != value;
		// What depends on the property is read when it changes, not when it first arrives
		if (changed && !stored.is_null()) {
			for (const std::string &dependent : dependents(path))
				rereads.insert(dependent);
		}
		stored = value;
	}
	if (changed)
		notify(path);
}

void CameraControls::Core::on_connection(bool open)
{
	{
		std::lock_guard lock(mutex);
		connected = open;
	}
	notify({});
}

void CameraControls::Core::on_written(const std::string &path, int status)
{
	{
		std::lock_guard lock(mutex);
		statuses[path] = status;
		// The phone's value replaces the assumed one when the write failed, or when the socket does not report it
		if (followed(path) && (!success(status) || !pushed.count(path)))
			rereads.insert(path);
	}
	notify(path);
}

CameraControls::CameraControls() : core_(std::make_shared<Core>())
{
	run_in_background([core = core_] { core->run(); });
}

CameraControls::~CameraControls()
{
	{
		std::lock_guard lock(core_->mutex);
		core_->closing = true;
	}
	core_->cv.notify_all();
	std::lock_guard lock(core_->listeners_mutex);
	core_->listeners.clear();
}

void CameraControls::set_phone(const std::string &phone_id, const std::string &address)
{
	{
		std::lock_guard lock(core_->mutex);
		if (core_->phone_id == phone_id && core_->manual_address == address)
			return;
		core_->phone_id = phone_id;
		core_->manual_address = phone_id.empty() ? address : std::string();
		core_->generation++;
	}
	core_->cv.notify_all();
}

bool CameraControls::connected() const
{
	std::lock_guard lock(core_->mutex);
	return core_->connected;
}

nlohmann::json CameraControls::get(const std::string &path) const
{
	std::lock_guard lock(core_->mutex);
	const auto value = core_->values.find(path);
	return value != core_->values.end() ? value->second : nlohmann::json();
}

void CameraControls::set(const std::string &path, const nlohmann::json &fields)
{
	{
		std::lock_guard lock(core_->mutex);
		nlohmann::json &value = core_->values[path];
		if (value.is_object() && fields.is_object())
			value.update(fields);
		else
			value = fields;
		if (core_->writes)
			core_->writes->write(path, fields);
	}
	core_->notify(path);
}

void CameraControls::act(const std::string &path, const nlohmann::json &body)
{
	std::lock_guard lock(core_->mutex);
	if (core_->writes)
		core_->writes->write(path, body);
}

int CameraControls::write_status(const std::string &path) const
{
	std::lock_guard lock(core_->mutex);
	const auto status = core_->statuses.find(path);
	return status != core_->statuses.end() ? status->second : 204;
}

ApiReply CameraControls::put_now(const std::string &path, const nlohmann::json &body)
{
	std::string host;
	int port = 4444;
	{
		std::lock_guard lock(core_->mutex);
		host = core_->host;
		port = core_->port;
	}
	if (host.empty())
		return {};

	const CameraClient client(host, port);
	const ApiReply reply = posted(path) ? client.post(path, body) : client.put(path, body);
	if (reply.ok() && followed(path)) {
		{
			std::lock_guard lock(core_->mutex);
			nlohmann::json &value = core_->values[path];
			if (value.is_object() && body.is_object())
				value.update(body);
			if (!core_->pushed.count(path))
				core_->rereads.insert(path);
		}
		core_->notify(path);
	}
	return reply;
}

ApiReply CameraControls::get_now(const std::string &path)
{
	std::string host;
	int port = 4444;
	{
		std::lock_guard lock(core_->mutex);
		host = core_->host;
		port = core_->port;
	}
	if (host.empty())
		return {};
	const ApiReply reply = CameraClient(host, port).get(path);
	if (followed(path))
		core_->store(path, reply);
	return reply;
}

ApiReply CameraControls::remove_now(const std::string &path)
{
	std::string host;
	int port = 4444;
	{
		std::lock_guard lock(core_->mutex);
		host = core_->host;
		port = core_->port;
	}
	return host.empty() ? ApiReply() : CameraClient(host, port).remove(path);
}

bool CameraControls::wait_for(const std::string &path, const std::function<bool(const nlohmann::json &)> &test,
			      std::chrono::milliseconds limit)
{
	std::unique_lock lock(core_->mutex);
	return core_->cv.wait_for(lock, limit, [&] {
		if (core_->closing)
			return true;
		const auto value = core_->values.find(path);
		return value != core_->values.end() && test(value->second);
	}) && !core_->closing;
}

int CameraControls::listen(Listener listener)
{
	std::lock_guard lock(core_->listeners_mutex);
	const int id = core_->next_listener++;
	core_->listeners[id] = std::move(listener);
	return id;
}

void CameraControls::unlisten(int id)
{
	std::lock_guard lock(core_->listeners_mutex);
	core_->listeners.erase(id);
}

} // namespace bmagicam
