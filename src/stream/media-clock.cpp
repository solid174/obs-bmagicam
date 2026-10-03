// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "media-clock.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace bmagicam {

void MediaClock::reset()
{
	started_ = false;
	first_window_ = true;
	delay_ = 0;
	window_start_ = 0;
	std::fill(std::begin(window_min_delay_), std::end(window_min_delay_), kNoPacket);
}

int64_t MediaClock::latest_min_delay() const
{
	int64_t latest = INT64_MIN;
	for (int64_t min_delay : window_min_delay_) {
		if (min_delay != kNoPacket)
			latest = std::max(latest, min_delay);
	}
	return latest;
}

void MediaClock::on_packet(Stream stream, int64_t pts_ns, int64_t arrival_ns)
{
	const int64_t delay = arrival_ns - pts_ns;

	if (started_ && std::llabs(delay - delay_) > config_.discontinuity_ns)
		reset();

	if (!started_) {
		started_ = true;
		window_start_ = arrival_ns;
	}

	int64_t &window_min = window_min_delay_[static_cast<int>(stream)];
	window_min = std::min(window_min, delay);

	// Packets read while the connection was set up come in a burst, later than the ones after them, so during the
	// first window the delay is simply the least one seen
	if (first_window_)
		delay_ = latest_min_delay();

	const int64_t elapsed = arrival_ns - window_start_;
	if (elapsed >= config_.window_ns) {
		if (!first_window_) {
			const double share =
				std::min(1.0, static_cast<double>(elapsed) / static_cast<double>(config_.follow_ns));
			delay_ += std::llround(static_cast<double>(latest_min_delay() - delay_) * share);
		}
		first_window_ = false;
		window_start_ = arrival_ns;
		std::fill(std::begin(window_min_delay_), std::end(window_min_delay_), kNoPacket);
	}

	const int64_t slack = delay_ + config_.buffer_ns - delay;
	if (slack < config_.min_slack_ns)
		delay_ = delay - config_.buffer_ns + config_.min_slack_ns;
}

} // namespace bmagicam
