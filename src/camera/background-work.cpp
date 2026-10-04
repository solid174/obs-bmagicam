// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "background-work.hpp"

#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

namespace bmagicam {

namespace {

std::mutex mutex;
std::condition_variable cv;
int running = 0;

} // namespace

void run_in_background(std::function<void()> work)
{
	{
		std::lock_guard lock(mutex);
		running++;
	}
	std::thread([work = std::move(work)] {
		work();
		{
			std::lock_guard lock(mutex);
			running--;
		}
		cv.notify_all();
	}).detach();
}

void wait_for_background_work(std::chrono::seconds limit)
{
	std::unique_lock lock(mutex);
	cv.wait_for(lock, limit, [] { return running == 0; });
}

} // namespace bmagicam
