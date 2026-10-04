// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "write-queue.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace bmagicam {

WriteQueue::WriteQueue(Send send, Done done, int workers, std::vector<int> retry_pauses_ms)
	: send_(std::move(send)),
	  done_(std::move(done)),
	  retry_pauses_ms_(std::move(retry_pauses_ms))
{
	for (int i = 0; i < workers; i++)
		workers_.emplace_back(&WriteQueue::work, this);
}

WriteQueue::~WriteQueue()
{
	{
		std::lock_guard lock(mutex_);
		stopping_ = true;
		queue_.clear();
		pending_.clear();
	}
	cv_.notify_all();
	for (std::thread &worker : workers_)
		worker.join();
}

std::vector<std::string> WriteQueue::prerequisites(const std::string &path)
{
	// ISO and shutter are refused under auto exposure; the shutters on offer follow the frame rate; zoom and focus
	// ranges follow the lens; manual focus is refused under continuous autofocus
	if (path == "/video/iso")
		return {"/video/autoExposure"};
	if (path == "/video/shutter")
		return {"/video/autoExposure", "/system/videoFormat"};
	if (path == "/lens/zoom")
		return {"/lens/cameras/active"};
	if (path == "/lens/focus")
		return {"/lens/cameras/active", "/lens/focus/autoFocus"};
	return {};
}

void WriteQueue::write(const std::string &path, const nlohmann::json &body)
{
	{
		std::lock_guard lock(mutex_);
		if (stopping_)
			return;
		auto [entry, added] = pending_.try_emplace(path, body);
		if (!added) {
			if (entry->second.is_object() && body.is_object())
				entry->second.update(body);
			else
				entry->second = body;
		}
		if (std::find(queue_.begin(), queue_.end(), path) == queue_.end())
			queue_.push_back(path);
	}
	cv_.notify_all();
}

void WriteQueue::wait_idle()
{
	std::unique_lock lock(mutex_);
	idle_cv_.wait(lock, [&] { return queue_.empty() && in_flight_.empty(); });
}

std::string WriteQueue::next_startable() const
{
	for (auto path = queue_.begin(); path != queue_.end(); ++path) {
		if (in_flight_.count(*path))
			continue;
		bool blocked = false;
		for (const std::string &before : prerequisites(*path)) {
			blocked = in_flight_.count(before) || std::find(queue_.begin(), path, before) != path;
			if (blocked)
				break;
		}
		if (!blocked)
			return *path;
	}
	return {};
}

void WriteQueue::work()
{
	std::unique_lock lock(mutex_);
	while (true) {
		std::string path;
		cv_.wait(lock, [&] { return stopping_ || !(path = next_startable()).empty(); });
		if (stopping_)
			return;

		queue_.erase(std::find(queue_.begin(), queue_.end(), path));
		const nlohmann::json body = std::move(pending_[path]);
		pending_.erase(path);
		in_flight_.insert(path);
		lock.unlock();

		int status = send_(path, body);
		bool report = true;
		for (size_t attempt = 0; status == 0 && attempt < retry_pauses_ms_.size(); attempt++) {
			lock.lock();
			// A newer value for the path takes over from this one, and stopping ends the tries
			const bool interrupted = cv_.wait_for(lock,
							      std::chrono::milliseconds(retry_pauses_ms_[attempt]),
							      [&] { return stopping_ || pending_.count(path) != 0; });
			lock.unlock();
			if (interrupted) {
				report = false;
				break;
			}
			status = send_(path, body);
		}
		if (report)
			done_(path, status);

		lock.lock();
		in_flight_.erase(path);
		if (queue_.empty() && in_flight_.empty())
			idle_cv_.notify_all();
		cv_.notify_all();
	}
}

} // namespace bmagicam
