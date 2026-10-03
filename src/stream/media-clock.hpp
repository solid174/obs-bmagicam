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
// streams are there and the burst after connecting is over.
//
// Afterwards the delay changes at a rate of its own: the phone's timestamps run slower or faster than the computer's
// clock, by as much as 0.1 %. A loop measures per window how far the latest packet came beyond the map and corrects
// both the delay and its rate, so that a steady drift is followed without lag. A packet that would arrive with less
// than the minimum slack before its presentation time moves the map later at once; a large excess that lasts is
// dropped at once.
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
		// Length of the windows over which the map is corrected
		int64_t window_ns = 1'000'000'000;
		// Loop gains: share of a window's error per second that corrects the delay, and that corrects its rate
		double delay_gain = 0.2;
		double rate_gain = 0.005;
		// Largest rate at which the delay may change: 0.3 %
		double max_rate = 0.003;
		// Largest correction of the delay per window
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
	int64_t to_obs(int64_t pts_ns) const { return pts_ns + delay_at(pts_ns) + config_.buffer_ns; }

	// Rate at which the transport delay changes, in OBS time per stream time.
	double rate() const { return rate_; }

private:
	static constexpr int64_t kNone = INT64_MIN;

	// The transport delay the map assumes for a timestamp
	int64_t delay_at(int64_t pts_ns) const;
	// Moves the anchor of the map to the timestamp, without moving the map
	void anchor(int64_t pts_ns);
	void start_window(int64_t arrival_ns);

	Config config_;
	bool started_ = false;
	bool settled_ = false;
	int64_t delay_ = 0;
	int64_t anchor_pts_ = 0;
	double rate_ = 0;
	int64_t start_ = 0;
	int64_t first_arrival_[2] = {kNone, kNone};
	int64_t window_start_ = 0;
	// The greatest delay of the window's packets (while settling), and the furthest any came beyond the map (after)
	int64_t window_max_ = kNone;
	int excess_windows_ = 0;
};

} // namespace bmagicam
