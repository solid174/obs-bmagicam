// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "media-clock.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>

namespace bmagicam {

void MediaClock::reset()
{
	started_ = false;
	settled_ = false;
	delay_ = 0;
	start_ = 0;
	excess_windows_ = 0;
	std::fill(std::begin(first_arrival_), std::end(first_arrival_), kNone);
	start_window(0);
}

int64_t MediaClock::window_max_delay() const
{
	return std::max(window_max_delay_[0], window_max_delay_[1]);
}

void MediaClock::start_window(int64_t arrival_ns)
{
	window_start_ = arrival_ns;
	std::fill(std::begin(window_max_delay_), std::end(window_max_delay_), kNone);
}

void MediaClock::on_packet(Stream stream, int64_t pts_ns, int64_t arrival_ns)
{
	const auto index = static_cast<size_t>(stream);
	const int64_t delay = arrival_ns - pts_ns;

	if (started_ && std::llabs(delay - delay_) > config_.discontinuity_ns)
		reset();

	if (!started_) {
		started_ = true;
		start_ = arrival_ns;
		start_window(arrival_ns);
	}
	if (first_arrival_[index] == kNone)
		first_arrival_[index] = arrival_ns;

	window_max_delay_[index] = std::max(window_max_delay_[index], delay);

	if (!settled_) {
		// Measure from when both streams are there and the burst after connecting is over
		const bool both_arrived = first_arrival_[0] != kNone && first_arrival_[1] != kNone;
		const int64_t measure_from =
			std::max(start_ + config_.burst_ns,
				 both_arrived ? std::max(first_arrival_[0], first_arrival_[1]) : start_);
		if (window_start_ < measure_from && arrival_ns >= measure_from) {
			start_window(arrival_ns);
			window_max_delay_[index] = delay;
		}
		delay_ = window_max_delay();

		const bool measured = window_start_ >= measure_from && arrival_ns - window_start_ >= config_.settle_ns;
		if ((both_arrived && measured) || arrival_ns - start_ >= config_.max_settle_ns) {
			settled_ = true;
			start_window(arrival_ns);
		}
		return;
	}

	const int64_t elapsed = arrival_ns - window_start_;
	if (elapsed >= config_.window_ns) {
		const int64_t difference = window_max_delay() - delay_;
		if (-difference > config_.drop_excess_ns) {
			if (++excess_windows_ >= config_.drop_excess_windows) {
				delay_ += difference;
				excess_windows_ = 0;
			}
		} else {
			excess_windows_ = 0;
			const double share =
				std::min(1.0, static_cast<double>(elapsed) / static_cast<double>(config_.follow_ns));
			const int64_t step = std::llround(static_cast<double>(difference) * share);
			delay_ += std::clamp(step, -config_.max_step_ns, config_.max_step_ns);
		}
		start_window(arrival_ns);
	}

	const int64_t slack = delay_ + config_.buffer_ns - delay;
	if (slack < config_.min_slack_ns)
		delay_ = delay - config_.buffer_ns + config_.min_slack_ns;
}

} // namespace bmagicam
