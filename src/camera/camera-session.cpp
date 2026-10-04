// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "camera-session.hpp"

#include "background-work.hpp"
#include "network.hpp"
#include "stream-presets.hpp"
#include "streaming-xml.hpp"
#include "../discovery/phone-browser.hpp"
#include "../phone/camera-client.hpp"
#include "../stream/ffmpeg-check.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <condition_variable>
#include <iterator>
#include <mutex>
#include <thread>

namespace bmagicam {

namespace {

using namespace std::chrono_literals;

// The phone renames the destination after its service name; the file name only matters for the upload
constexpr const char *kDestinationFile = "obs-bmagicam.xml";
// The stream keeps running this long after the source is hidden, so that switching between scenes that share the
// source does not restart it (CAM-7)
constexpr auto kHideGrace = 2s;
// Time the phone gets to connect to the receiver after it was told to start
constexpr auto kConnectWait = 5s;
// The phone's server pauses for a few seconds after a video format change
constexpr auto kFormatChangePause = 10s;
constexpr std::chrono::milliseconds kRetryPauses[] = {500ms, 1s, 2s, 4s, 5s};

std::string string_at(const nlohmann::json &object, const char *key)
{
	if (object.is_object() && object.contains(key) && object[key].is_string())
		return object[key].get<std::string>();
	return {};
}

std::string url_path_segment(const std::string &text)
{
	static const char *hex = "0123456789ABCDEF";
	std::string encoded;
	for (const unsigned char c : text) {
		if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			encoded += static_cast<char>(c);
		} else {
			encoded += '%';
			encoded += hex[c >> 4];
			encoded += hex[c & 15];
		}
	}
	return encoded;
}

// Uploads the destination unless the phone has it up to date. An upload under a name the phone already has fails,
// with an alert on the phone's screen even though the answer is 204, so an outdated copy is deleted first.
bool upload_destination(const CameraClient &client, const std::string &service, const std::string &platform,
			const std::string &url)
{
	const std::string path = "/livestreams/customPlatforms/" + url_path_segment(platform);
	const ApiReply stored = client.get(path);
	if (stored.ok()) {
		if (streaming_xml_matches(stored.text, url))
			return true;
		client.remove(path);
	}
	return client
		.put_xml(std::string("/livestreams/customPlatforms/") + kDestinationFile, streaming_xml(service, url))
		.ok();
}

// The phone's own livestream destinations, saved before the session chooses its own, so that they can be given back
// even after a crash. Kept in the module's config folder, by phone.
class SavedDestinations {
public:
	static void save(const std::string &phone, const nlohmann::json &destination)
	{
		std::lock_guard lock(mutex());
		nlohmann::json all = load();
		all[phone] = {
			{"platform", string_at(destination, "platform")},
			{"server", string_at(destination, "server")},
			{"quality", string_at(destination, "quality")},
		};
		store(all);
	}

	static nlohmann::json get(const std::string &phone)
	{
		std::lock_guard lock(mutex());
		const nlohmann::json all = load();
		return all.contains(phone) ? all[phone] : nlohmann::json();
	}

	static void forget(const std::string &phone)
	{
		std::lock_guard lock(mutex());
		nlohmann::json all = load();
		all.erase(phone);
		store(all);
	}

private:
	static std::mutex &mutex()
	{
		static std::mutex instance;
		return instance;
	}

	static std::string path()
	{
		char *path = obs_module_config_path("destinations.json");
		std::string result = path ? path : "";
		bfree(path);
		return result;
	}

	static nlohmann::json load()
	{
		char *text = os_quick_read_utf8_file(path().c_str());
		nlohmann::json all = text ? nlohmann::json::parse(text, nullptr, false) : nlohmann::json::object();
		bfree(text);
		return all.is_object() ? all : nlohmann::json::object();
	}

	static void store(const nlohmann::json &all)
	{
		char *folder = obs_module_config_path("");
		if (folder)
			os_mkdirs(folder);
		bfree(folder);
		const std::string text = all.dump(1, '\t');
		os_quick_write_utf8_file_safe(path().c_str(), text.c_str(), text.size(), false, "tmp", nullptr);
	}
};

bool operator==(const StreamReceiver::Settings &a, const StreamReceiver::Settings &b)
{
	return a.port == b.port && a.latency_ms == b.latency_ms && a.hardware_decoding == b.hardware_decoding;
}

bool operator==(const CameraSession::Settings &a, const CameraSession::Settings &b)
{
	return a.phone_id == b.phone_id && a.phone_name == b.phone_name && a.address == b.address &&
	       a.preset == b.preset && a.receiver == b.receiver;
}

// Remote control arrived in Blackmagic Camera 3.4
bool supports_remote_control(const std::string &version)
{
	int major = 0;
	int minor = 0;
	if (std::sscanf(version.c_str(), "%d.%d", &major, &minor) < 2)
		return true;
	return major > 3 || (major == 3 && minor >= 4);
}

// For the log, which is not translated
const char *problem_text(CameraSession::Problem problem)
{
	switch (problem) {
	case CameraSession::Problem::NotAnswering:
		return "the phone does not answer";
	case CameraSession::Problem::MonitorOnly:
		return "the phone allows monitoring only";
	case CameraSession::Problem::CannotReach:
		return "the phone streams, but nothing arrives";
	case CameraSession::Problem::Unavailable:
		return "the phone cannot stream now";
	case CameraSession::Problem::Refused:
		return "the phone refused a step of the setup";
	case CameraSession::Problem::Outdated:
		return "Blackmagic Camera is older than 3.4";
	case CameraSession::Problem::FFmpeg:
		return "the FFmpeg in OBS cannot run the receiver";
	case CameraSession::Problem::None:
		break;
	}
	return "";
}

} // namespace

struct CameraSession::Core final : StreamReceiver::Sink {
	Core(Output &output, StreamReceiver::Clock clock) : output(&output), receiver(*this, clock) {}

	// The output; cleared when the session ends
	std::mutex output_mutex;
	Output *output;

	StreamReceiver receiver;

	mutable std::mutex mutex;
	std::condition_variable cv;
	Settings settings;
	uint64_t generation = 0;
	bool active = false;
	bool ending = false;
	// A stream from the phone is arriving
	bool phone_connected = false;
	bool phones_changed = false;
	Status status;

	// Only used by the session's thread: what it changed on which phone, to undo
	std::string changed_phone;
	std::string changed_phone_key;
	std::string changed_platform;
	bool phone_streaming = false;
	bool receiving = false;
	StreamReceiver::Settings receiving_settings;
	// Logged once rather than on every retry
	Problem logged_problem = Problem::None;
	std::string refused_format;

	void run();
	// Connects, sets up and runs the stream until it ends. Returns false on a failure, which is tried again.
	bool stream(const Settings &settings, uint64_t generation);
	void stop_phone_stream(const CameraClient &client);
	void put_phone_back();
	void stop_receiving();
	bool wait_for_answer(const CameraClient &client);

	void set_status(State state, Problem problem = Problem::None, const std::string &detail = {});
	void set_phone(const std::string &phone);
	void notify_output();

	void stream_started() override
	{
		{
			std::lock_guard lock(mutex);
			phone_connected = true;
		}
		cv.notify_all();
	}

	void stream_ended() override
	{
		{
			std::lock_guard lock(mutex);
			phone_connected = false;
		}
		cv.notify_all();
		std::lock_guard lock(output_mutex);
		if (output)
			output->session_cleared();
	}

	void stream_video(const AVFrame &frame, uint64_t timestamp) override
	{
		std::lock_guard lock(output_mutex);
		if (output)
			output->session_video(frame, timestamp);
	}

	void stream_audio(const float *const planes[2], uint32_t frames, uint64_t timestamp) override
	{
		std::lock_guard lock(output_mutex);
		if (output)
			output->session_audio(planes, frames, timestamp);
	}
};

void CameraSession::Core::notify_output()
{
	std::lock_guard lock(output_mutex);
	if (output)
		output->session_status_changed();
}

void CameraSession::Core::set_status(State state, Problem problem, const std::string &detail)
{
	{
		std::lock_guard lock(mutex);
		if (status.state == state && status.problem == problem && status.detail == detail)
			return;
		status.state = state;
		status.problem = problem;
		status.detail = detail;
	}
	// Each problem once until the camera is live again
	if (problem != Problem::None && problem != logged_problem)
		obs_log(LOG_WARNING, "%s%s%s%s", problem_text(problem), detail.empty() ? "" : " (", detail.c_str(),
			detail.empty() ? "" : ")");
	if (problem != Problem::None || state == State::Live)
		logged_problem = problem;
	notify_output();
}

void CameraSession::Core::set_phone(const std::string &phone)
{
	{
		std::lock_guard lock(mutex);
		if (status.phone == phone)
			return;
		status.phone = phone;
	}
	notify_output();
}

void CameraSession::Core::stop_receiving()
{
	if (receiving) {
		receiver.stop();
		receiving = false;
	}
}

void CameraSession::Core::run()
{
	if (!ffmpeg_usable()) {
		set_status(State::Error, Problem::FFmpeg);
		std::unique_lock lock(mutex);
		cv.wait(lock, [&] { return ending; });
		return;
	}

	size_t failures = 0;
	std::unique_lock lock(mutex);
	while (!ending) {
		const Settings current = settings;
		const uint64_t current_generation = generation;
		const bool shown = active;
		lock.unlock();

		const bool no_phone = current.phone_id.empty() && current.address.empty();
		if (no_phone || !shown) {
			if (!changed_phone.empty() && phone_streaming)
				stop_phone_stream(CameraClient(changed_phone));
			stop_receiving();
			set_status(no_phone ? State::NoPhone : State::Paused);
			lock.lock();
			cv.wait(lock, [&] { return ending || generation != current_generation || active != shown; });
			continue;
		}

		const bool ended_well = stream(current, current_generation);
		lock.lock();
		if (ended_well) {
			failures = 0;
			continue;
		}
		// Retry after a pause, or as soon as Bonjour sees the phone again
		const auto pause = kRetryPauses[std::min(failures++, std::size(kRetryPauses) - 1)];
		phones_changed = false;
		cv.wait_for(lock, pause,
			    [&] { return ending || generation != current_generation || !active || phones_changed; });
	}
	lock.unlock();

	put_phone_back();
	stop_receiving();
}

bool CameraSession::Core::stream(const Settings &settings, uint64_t current_generation)
{
	const auto changed = [&] {
		return ending || generation != current_generation || !active;
	};

	// A phone chosen from the list is found by its ID, wherever its address moved to
	std::string address = settings.address;
	if (!settings.phone_id.empty()) {
		FoundPhone found;
		if (!PhoneBrowser::instance().find(settings.phone_id, found)) {
			set_phone(settings.phone_name);
			set_status(State::Searching);
			return false;
		}
		address = found.address;
	}
	const std::string phone_key = settings.phone_id.empty() ? address : settings.phone_id;

	// A different phone than before gets the previous one put back first
	if (!changed_phone.empty() && changed_phone_key != phone_key)
		put_phone_back();

	const CameraClient client(address);
	set_status(State::Connecting);

	const ApiReply product = client.get("/system/product");
	if (!product.answered()) {
		set_status(State::Error, Problem::NotAnswering);
		return false;
	}
	const std::string name = string_at(product.body, "productName");
	const std::string letter = string_at(product.body, "deviceName");
	set_phone(letter.empty() ? name : name + " (" + letter + ")");
	if (!supports_remote_control(string_at(product.body, "softwareVersion"))) {
		set_status(State::Error, Problem::Outdated);
		return false;
	}

	const std::string availability = string_at(client.get("/access/status").body, "availability");
	if (!availability.empty() && availability != "control-and-monitor") {
		set_status(State::Error, Problem::MonitorOnly);
		return false;
	}

	set_status(State::Starting);
	const std::string local_address = local_address_toward(address);
	if (local_address.empty()) {
		set_status(State::Error, Problem::NotAnswering);
		return false;
	}

	if (!receiving || !(receiving_settings == settings.receiver)) {
		receiver.start(settings.receiver);
		receiving = true;
		receiving_settings = settings.receiver;
	}

	const std::string service = "OBS on " + computer_name();
	const std::string platform = streaming_platform(service);
	const std::string url = "srt://" + local_address + ":" + std::to_string(settings.receiver.port);

	// Keep the phone's own destination before anything here changes it. Only on the first setup: replacing an
	// outdated destination can leave the phone on another one.
	if (changed_phone_key != phone_key) {
		const ApiReply current = client.get("/livestreams/0/activePlatform");
		if (current.ok() && string_at(current.body, "platform") != platform)
			SavedDestinations::save(phone_key, current.body);
	}
	changed_phone = address;
	changed_phone_key = phone_key;
	changed_platform = platform;

	// Point the phone at the receiver
	if (!upload_destination(client, service, platform, url)) {
		set_status(State::Error, Problem::Refused, "destination");
		return false;
	}

	// The stream follows the camera's video format, so the preset sets it
	const StreamPreset &preset = stream_preset(settings.preset);
	if (string_at(client.get("/system/videoFormat").body, "name") != preset.video_format) {
		const ApiReply format = client.put("/system/videoFormat", {{"name", preset.video_format}});
		if (format.ok()) {
			wait_for_answer(client);
		} else if (refused_format != preset.video_format) {
			obs_log(LOG_WARNING, "the phone kept its video format instead of %s (%d)", preset.video_format,
				format.status);
			refused_format = preset.video_format;
		}
	}

	const ApiReply available = client.get("/livestreams/0/available");
	if (available.ok() && available.body.is_object() && available.body.value("available", true) == false) {
		std::string reasons;
		if (available.body.contains("reasons") && available.body["reasons"].is_array()) {
			for (const nlohmann::json &reason : available.body["reasons"]) {
				if (reason.is_string())
					reasons += (reasons.empty() ? "" : ", ") + reason.get<std::string>();
			}
		}
		set_status(State::Error, Problem::Unavailable, reasons);
		return false;
	}

	const nlohmann::json destination = {{"platform", platform},
					    {"server", kStreamingServer},
					    {"quality", preset.profile}};
	if (!client.put("/livestreams/0/activePlatform", destination).ok()) {
		set_status(State::Error, Problem::Refused, "destination");
		return false;
	}

	{
		std::lock_guard lock(mutex);
		phone_connected = false;
	}
	if (!client.put("/livestreams/0/start").ok()) {
		set_status(State::Error, Problem::Refused, "start");
		return false;
	}
	phone_streaming = true;

	std::unique_lock lock(mutex);
	if (!cv.wait_for(lock, kConnectWait, [&] { return phone_connected || changed(); })) {
		lock.unlock();
		stop_phone_stream(client);
		set_status(State::Error, Problem::CannotReach);
		return false;
	}

	// Live until the settings change, the source is hidden, the phone does not come back after a break or the session
	// ends
	while (!ending) {
		if (generation != current_generation)
			break;

		if (!active) {
			if (cv.wait_for(lock, kHideGrace,
					[&] { return ending || active || generation != current_generation; }))
				continue;
			break;
		}

		if (!phone_connected) {
			// The phone reconnects by itself, so it is only started again if it stopped. Setting it up again
			// while it reconnects restarts its stream once more and can leave it connected without sending.
			lock.unlock();
			obs_log(LOG_INFO, "waiting for the phone to reconnect");
			set_status(State::Connecting);
			if (string_at(client.get("/livestreams/0").body, "status") == "Idle")
				client.put("/livestreams/0/start");
			lock.lock();
			if (!cv.wait_for(lock, kConnectWait, [&] { return phone_connected || changed(); })) {
				lock.unlock();
				obs_log(LOG_INFO, "the phone did not reconnect; setting it up again");
				stop_phone_stream(client);
				return false;
			}
			continue;
		}

		lock.unlock();
		set_status(State::Live, Problem::None, preset.profile);
		lock.lock();
		cv.wait(lock, [&] { return changed() || !phone_connected; });
	}
	lock.unlock();

	stop_phone_stream(client);
	return true;
}

bool CameraSession::Core::wait_for_answer(const CameraClient &client)
{
	const auto until = std::chrono::steady_clock::now() + kFormatChangePause;
	while (std::chrono::steady_clock::now() < until) {
		if (client.get("/system/videoFormat").ok())
			return true;
		std::this_thread::sleep_for(500ms);
	}
	return false;
}

void CameraSession::Core::stop_phone_stream(const CameraClient &client)
{
	if (phone_streaming) {
		client.put("/livestreams/0/stop");
		phone_streaming = false;
	}
}

void CameraSession::Core::put_phone_back()
{
	if (changed_phone.empty())
		return;

	const CameraClient client(changed_phone);
	stop_phone_stream(client);

	// A phone that is gone keeps the saved destination for the next time
	const nlohmann::json previous = SavedDestinations::get(changed_phone_key);
	if (previous.is_object() && client.put("/livestreams/0/activePlatform", previous).ok()) {
		SavedDestinations::forget(changed_phone_key);
		client.remove("/livestreams/customPlatforms/" + url_path_segment(changed_platform));
	}
	changed_phone.clear();
	changed_phone_key.clear();
	changed_platform.clear();
}

CameraSession::CameraSession(Output &output, StreamReceiver::Clock clock) : core_(std::make_shared<Core>(output, clock))
{
	discovery_listener_ = PhoneBrowser::instance().listen([weak = std::weak_ptr<Core>(core_)] {
		if (const std::shared_ptr<Core> core = weak.lock()) {
			{
				std::lock_guard lock(core->mutex);
				core->phones_changed = true;
			}
			core->cv.notify_all();
		}
	});
	run_in_background([core = core_] { core->run(); });
}

CameraSession::~CameraSession()
{
	PhoneBrowser::instance().unlisten(discovery_listener_);
	{
		std::lock_guard lock(core_->output_mutex);
		core_->output = nullptr;
	}
	{
		std::lock_guard lock(core_->mutex);
		core_->ending = true;
	}
	core_->cv.notify_all();
}

void CameraSession::update(const Settings &settings)
{
	{
		std::lock_guard lock(core_->mutex);
		if (core_->settings == settings)
			return;
		core_->settings = settings;
		core_->generation++;
	}
	core_->cv.notify_all();
}

void CameraSession::set_active(bool active)
{
	{
		std::lock_guard lock(core_->mutex);
		core_->active = active;
	}
	core_->cv.notify_all();
}

CameraSession::Status CameraSession::status() const
{
	std::lock_guard lock(core_->mutex);
	return core_->status;
}

} // namespace bmagicam
