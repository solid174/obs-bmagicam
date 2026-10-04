// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <nlohmann/json.hpp>

#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace bmagicam {

// Sends property writes to one phone (docs/architecture.md, "Control path"). At most one write per property is in
// flight. While it is, newer values merge into one pending write, sent when the first completes, so a dragged slider
// sends a steady stream of writes, each with the latest value. A write waits for the writes it depends on that were
// asked for before it, for example auto exposure before ISO. A few writes run in parallel, and a write the phone did
// not answer is tried again.
class WriteQueue {
public:
	// Sends one write and returns the phone's HTTP status, 0 when it did not answer.
	using Send = std::function<int(const std::string &path, const nlohmann::json &body)>;
	// Hears how each write ended, on a worker thread.
	using Done = std::function<void(const std::string &path, int status)>;

	// retry_pauses_ms: the pauses before each new try of a write that went unanswered
	WriteQueue(Send send, Done done, int workers = 4, std::vector<int> retry_pauses_ms = {500, 1000, 2000});
	// Drops the writes not sent yet and waits for those in flight.
	~WriteQueue();

	WriteQueue(const WriteQueue &) = delete;
	WriteQueue &operator=(const WriteQueue &) = delete;

	// The fields of the body replace those of a write to the same path that has not been sent yet.
	void write(const std::string &path, const nlohmann::json &body);
	// Waits until nothing is pending or in flight.
	void wait_idle();

	// The paths whose pending writes must go out before a write to the given path
	static std::vector<std::string> prerequisites(const std::string &path);

private:
	void work();
	// The next path whose write can start now; empty when none can
	std::string next_startable() const;

	const Send send_;
	const Done done_;
	const std::vector<int> retry_pauses_ms_;

	mutable std::mutex mutex_;
	std::condition_variable cv_;
	std::condition_variable idle_cv_;
	// Paths with a write to send, in the order they were first asked for
	std::deque<std::string> queue_;
	std::map<std::string, nlohmann::json> pending_;
	std::set<std::string> in_flight_;
	bool stopping_ = false;
	std::vector<std::thread> workers_;
};

} // namespace bmagicam
