// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <cstdint>

namespace bmagicam {

// Maps the phone's presentation timestamps to OBS time, the same way for audio and video, so that they stay in sync.
//
// A timestamp maps to itself plus the transport delay plus a fixed buffer. The transport delay is the greatest delay
// of the packets relative to their timestamps, over both streams: every packet has to arrive before it is presented,
// including the last one of a burst. It settles right after the phone connects, from the packets that arrive once both
// streams are there and the burst after connecting is over, and then follows the greatest delay window by window, which
// also absorbs the drift between
// the phone's clock and the computer's. It follows by small steps, which the audio absorbs by stretching, except that
// a large excess that lasts is dropped at once. A packet that would arrive with less than the minimum slack before its
// presentation time moves the map later at once.
class MediaClock {
public:
	enum class Stream { Audio, Video };

	struct Config {
		// Time from a packet's arrival to its presentation, for the packet that arrives latest
		int64_t buffer_ns = 50'000'000;
		// Least time any packet gets between its arrival and its presentation
		int64_t min_slack_ns = 10'000'000;
		// Packets that arrive this soon after the first one come in a burst and are not measured
		int64_t burst_ns = 200'000'000;
		// The map settles once both streams have arrived, after the burst, for this long...
		int64_t settle_ns = 300'000'000;
		// ...or this long after the first packet, for a stream without audio
		int64_t max_settle_ns = 1'000'000'000;
		// Length of the windows over which the delay is followed
		int64_t window_ns = 1'000'000'000;
		// Time constant with which the map follows the transport delay
		int64_t follow_ns = 30'000'000'000;
		// Largest step by which the map follows, per window
		int64_t max_step_ns = 3'000'000;
		// An excess delay larger than this, for this many windows in a row, is dropped at once
		int64_t drop_excess_ns = 100'000'000;
		int drop_excess_windows = 3;
		// A change of the transport delay larger than this starts over
		int64_t discontinuity_ns = 1'000'000'000;
	};

	MediaClock() = default;
	explicit MediaClock(const Config &config) : config_(config) {}

	void reset();

	// Records that a packet with the given timestamp (stream time) arrived at the given time (OBS time).
	void on_packet(Stream stream, int64_t pts_ns, int64_t arrival_ns);

	// Whether the map has settled. Until then nothing should be presented.
	bool settled() const { return settled_; }

	// OBS time at which the given timestamp is presented.
	int64_t to_obs(int64_t pts_ns) const { return pts_ns + delay_ + config_.buffer_ns; }

private:
	static constexpr int64_t kNone = INT64_MIN;

	// The greatest delay of any packet in the current window
	int64_t window_max_delay() const;
	void start_window(int64_t arrival_ns);

	Config config_;
	bool started_ = false;
	bool settled_ = false;
	int64_t delay_ = 0;
	int64_t start_ = 0;
	int64_t first_arrival_[2] = {kNone, kNone};
	int64_t window_start_ = 0;
	int64_t window_max_delay_[2] = {kNone, kNone};
	int excess_windows_ = 0;
};

} // namespace bmagicam
