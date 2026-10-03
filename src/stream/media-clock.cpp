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
	anchor_pts_ = 0;
	rate_ = 0;
	start_ = 0;
	excess_windows_ = 0;
	std::fill(std::begin(first_arrival_), std::end(first_arrival_), kNone);
	start_window(0);
}

int64_t MediaClock::delay_at(int64_t pts_ns) const
{
	return delay_ + std::llround(rate_ * static_cast<double>(pts_ns - anchor_pts_));
}

void MediaClock::anchor(int64_t pts_ns)
{
	delay_ = delay_at(pts_ns);
	anchor_pts_ = pts_ns;
}

void MediaClock::start_window(int64_t arrival_ns)
{
	window_start_ = arrival_ns;
	window_max_ = kNone;
}

void MediaClock::on_packet(Stream stream, int64_t pts_ns, int64_t arrival_ns)
{
	const auto index = static_cast<size_t>(stream);
	const int64_t delay = arrival_ns - pts_ns;

	if (started_ && std::llabs(delay - delay_at(pts_ns)) > config_.discontinuity_ns)
		reset();

	if (!started_) {
		started_ = true;
		start_ = arrival_ns;
		start_window(arrival_ns);
	}
	if (first_arrival_[index] == kNone)
		first_arrival_[index] = arrival_ns;

	if (!settled_) {
		// Measure from when both streams are there and the burst after connecting is over
		const bool both_arrived = first_arrival_[0] != kNone && first_arrival_[1] != kNone;
		const int64_t measure_from =
			std::max(start_ + config_.burst_ns,
				 both_arrived ? std::max(first_arrival_[0], first_arrival_[1]) : start_);
		if (window_start_ < measure_from && arrival_ns >= measure_from)
			start_window(arrival_ns);

		window_max_ = std::max(window_max_, delay);
		delay_ = window_max_;
		anchor_pts_ = pts_ns;

		const bool measured = window_start_ >= measure_from && arrival_ns - window_start_ >= config_.settle_ns;
		if ((both_arrived && measured) || arrival_ns - start_ >= config_.max_settle_ns) {
			settled_ = true;
			start_window(arrival_ns);
		}
		return;
	}

	window_max_ = std::max(window_max_, delay - delay_at(pts_ns));

	const int64_t elapsed = arrival_ns - window_start_;
	if (elapsed >= config_.window_ns) {
		// How far the latest packet of the window came beyond the map: zero when the map is right
		const int64_t error = window_max_;
		anchor(pts_ns);
		if (-error > config_.drop_excess_ns) {
			if (++excess_windows_ >= config_.drop_excess_windows) {
				delay_ += error;
				excess_windows_ = 0;
			}
		} else {
			excess_windows_ = 0;
			const double seconds = static_cast<double>(elapsed) / 1e9;
			const int64_t step = std::llround(config_.delay_gain * static_cast<double>(error) * seconds);
			delay_ += std::clamp(step, -config_.max_step_ns, config_.max_step_ns);
			rate_ = std::clamp(rate_ + config_.rate_gain * static_cast<double>(error) / 1e9 * seconds,
					   -config_.max_rate, config_.max_rate);
		}
		start_window(arrival_ns);
	}

	const int64_t slack = delay_at(pts_ns) + config_.buffer_ns - delay;
	if (slack < config_.min_slack_ns) {
		anchor(pts_ns);
		delay_ = delay - config_.buffer_ns + config_.min_slack_ns;
	}
}

} // namespace bmagicam
