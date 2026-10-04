// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "phone/write-queue.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

using bmagicam::WriteQueue;
using nlohmann::json;

namespace {

// A phone whose answers the test releases one by one
struct FakePhone {
	std::mutex mutex;
	std::condition_variable cv;
	std::vector<std::pair<std::string, json>> sent;
	std::vector<std::pair<std::string, int>> done;
	// Paths whose writes wait for release() before they are answered
	std::vector<std::string> held;
	int releases = 0;
	// Statuses to answer, in order; 204 once they run out
	std::vector<int> statuses;

	int send(const std::string &path, const json &body)
	{
		std::unique_lock lock(mutex);
		sent.emplace_back(path, body);
		cv.notify_all();
		if (std::find(held.begin(), held.end(), path) != held.end()) {
			cv.wait(lock, [&] { return releases > 0; });
			releases--;
		}
		if (statuses.empty())
			return 204;
		const int status = statuses.front();
		statuses.erase(statuses.begin());
		return status;
	}

	void finished(const std::string &path, int status)
	{
		std::lock_guard lock(mutex);
		done.emplace_back(path, status);
		cv.notify_all();
	}

	void release()
	{
		std::lock_guard lock(mutex);
		releases++;
		cv.notify_all();
	}

	bool wait_sent(size_t count)
	{
		std::unique_lock lock(mutex);
		return cv.wait_for(lock, std::chrono::seconds(2), [&] { return sent.size() >= count; });
	}

	WriteQueue::Send sender()
	{
		return [this](const std::string &path, const json &body) {
			return send(path, body);
		};
	}

	WriteQueue::Done listener()
	{
		return [this](const std::string &path, int status) {
			finished(path, status);
		};
	}
};

} // namespace

TEST_CASE("a write in flight collects newer values into one pending write")
{
	FakePhone phone;
	phone.held = {"/lens/zoom"};
	{
		WriteQueue queue(phone.sender(), phone.listener());
		queue.write("/lens/zoom", {{"focalLength", 30}});
		REQUIRE(phone.wait_sent(1));
		queue.write("/lens/zoom", {{"focalLength", 40}});
		queue.write("/lens/zoom", {{"focalLength", 50}});
		phone.release();
		phone.release();
		queue.wait_idle();
	}
	REQUIRE(phone.sent.size() == 2);
	CHECK(phone.sent[0].second == json{{"focalLength", 30}});
	CHECK(phone.sent[1].second == json{{"focalLength", 50}});
	CHECK(phone.done.size() == 2);
}

TEST_CASE("pending writes to one path merge their fields")
{
	FakePhone phone;
	phone.held = {"/colorCorrection/lift"};
	{
		WriteQueue queue(phone.sender(), phone.listener());
		queue.write("/colorCorrection/lift", {{"red", 0.1}});
		REQUIRE(phone.wait_sent(1));
		queue.write("/colorCorrection/lift", {{"blue", 0.2}});
		queue.write("/colorCorrection/lift", {{"red", 0.3}});
		phone.release();
		phone.release();
		queue.wait_idle();
	}
	REQUIRE(phone.sent.size() == 2);
	CHECK(phone.sent[1].second == json{{"blue", 0.2}, {"red", 0.3}});
}

TEST_CASE("a write waits for the writes it depends on")
{
	FakePhone phone;
	phone.held = {"/video/autoExposure"};
	{
		WriteQueue queue(phone.sender(), phone.listener());
		queue.write("/video/autoExposure", {{"mode", "Off"}});
		REQUIRE(phone.wait_sent(1));
		queue.write("/video/iso", {{"iso", 400}});
		queue.write("/video/whiteBalance", {{"whiteBalance", 5600}});
		// White balance does not depend on auto exposure and goes out while ISO waits
		REQUIRE(phone.wait_sent(2));
		CHECK(phone.sent[1].first == "/video/whiteBalance");
		phone.release();
		queue.wait_idle();
	}
	REQUIRE(phone.sent.size() == 3);
	CHECK(phone.sent[2].first == "/video/iso");
}

TEST_CASE("an unanswered write is tried again")
{
	FakePhone phone;
	phone.statuses = {0, 0, 204};
	{
		WriteQueue queue(phone.sender(), phone.listener(), 1, {10, 10, 10});
		queue.write("/video/whiteBalance", {{"whiteBalance", 5600}});
		queue.wait_idle();
	}
	CHECK(phone.sent.size() == 3);
	REQUIRE(phone.done.size() == 1);
	CHECK(phone.done[0].second == 204);
}

TEST_CASE("a write the phone keeps refusing is reported once")
{
	FakePhone phone;
	phone.statuses = {403};
	{
		WriteQueue queue(phone.sender(), phone.listener());
		queue.write("/video/iso", {{"iso", 400}});
		queue.wait_idle();
	}
	CHECK(phone.sent.size() == 1);
	REQUIRE(phone.done.size() == 1);
	CHECK(phone.done[0] == std::pair<std::string, int>("/video/iso", 403));
}
