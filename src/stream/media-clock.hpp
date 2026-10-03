// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <cstdint>

namespace bmagicam {

// Maps the phone's presentation timestamps to OBS time, the same way for audio and video, so that they stay in sync.
//
// A timestamp maps to itself plus the transport delay plus a fixed buffer. The transport delay follows the arrival
// times of the packets: per window, the stream that arrives latest relative to its timestamps sets it, and the map
// moves towards it slowly, which also absorbs the drift between the phone's clock and the computer's. A packet that
// would arrive with less than the minimum slack before its presentation time moves the map later at once.
class MediaClock {
public:
	enum class Stream { Audio, Video };

	struct Config {
		// Time from a packet's arrival to its presentation, for the stream that arrives latest
		int64_t buffer_ns = 50'000'000;
		// Least time any packet gets between its arrival and its presentation
		int64_t min_slack_ns = 10'000'000;
		// Length of the windows over which arrival times are compared
		int64_t window_ns = 1'000'000'000;
		// Time constant with which the map follows the transport delay
		int64_t follow_ns = 30'000'000'000;
		// A change of the transport delay larger than this starts over
		int64_t discontinuity_ns = 1'000'000'000;
	};

	MediaClock() = default;
	explicit MediaClock(const Config &config) : config_(config) {}

	void reset();

	// Records that a packet with the given timestamp (stream time) arrived at the given time (OBS time).
	void on_packet(Stream stream, int64_t pts_ns, int64_t arrival_ns);

	bool started() const { return started_; }

	// OBS time at which the given timestamp is presented.
	int64_t to_obs(int64_t pts_ns) const { return pts_ns + delay_ + config_.buffer_ns; }

private:
	static constexpr int64_t kNoPacket = INT64_MAX;

	// The least delay in the current window of the stream that arrives latest
	int64_t latest_min_delay() const;

	Config config_;
	bool started_ = false;
	bool first_window_ = true;
	int64_t delay_ = 0;
	int64_t window_start_ = 0;
	int64_t window_min_delay_[2] = {kNoPacket, kNoPacket};
};

} // namespace bmagicam
