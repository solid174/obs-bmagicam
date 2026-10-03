// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "stream/media-clock.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

using bmagicam::MediaClock;
using Stream = MediaClock::Stream;

namespace {

constexpr int64_t kMs = 1'000'000;
constexpr int64_t kSecond = 1'000 * kMs;
constexpr int64_t kVideoFrame = 16'666'667; // 60 fps
constexpr int64_t kAudioFrame = 21'333'333; // 1024 samples at 48 kHz

const MediaClock::Config kConfig;

struct Range {
	int64_t min = INT64_MAX;
	int64_t max = INT64_MIN;

	void add(int64_t value)
	{
		min = std::min(min, value);
		max = std::max(max, value);
	}
};

// Plays interleaved video and audio packets into the clock. A packet with timestamp pts arrives at
// pts * (1 + drift) + delay, audio another audio_late later. Returns the range of the slack the packets got, that is
// the time between their arrival and their presentation, from settle_ns on.
Range play(MediaClock &clock, int64_t start_ns, int64_t duration_ns, int64_t delay, double drift = 0,
	   int64_t audio_late = 0, int64_t settle_ns = 0)
{
	Range slack;
	int64_t video_pts = start_ns;
	int64_t audio_pts = start_ns;
	while (std::min(video_pts, audio_pts) < start_ns + duration_ns) {
		const bool video = video_pts <= audio_pts;
		const int64_t pts = video ? video_pts : audio_pts;
		const int64_t arrival =
			std::llround(static_cast<double>(pts) * (1 + drift)) + delay + (video ? 0 : audio_late);

		clock.on_packet(video ? Stream::Video : Stream::Audio, pts, arrival);
		if (pts - start_ns >= settle_ns)
			slack.add(clock.to_obs(pts) - arrival);

		(video ? video_pts : audio_pts) += video ? kVideoFrame : kAudioFrame;
	}
	return slack;
}

} // namespace

TEST_CASE("A constant delay maps each timestamp to its arrival plus the buffer")
{
	MediaClock clock;
	const Range slack = play(clock, 0, 10 * kSecond, 350 * kMs);
	CHECK(slack.min == kConfig.buffer_ns);
	CHECK(slack.max == kConfig.buffer_ns);
}

TEST_CASE("The stream that arrives later relative to its timestamps sets the delay")
{
	MediaClock clock;
	play(clock, 0, 5 * kSecond, 350 * kMs, 0, 30 * kMs);

	const int64_t pts = 5 * kSecond;
	CHECK(clock.to_obs(pts) - (pts + 380 * kMs) == kConfig.buffer_ns);
	CHECK(clock.to_obs(pts) - (pts + 350 * kMs) == kConfig.buffer_ns + 30 * kMs);
}

TEST_CASE("Clock drift is followed, so the slack stays near the buffer")
{
	for (double drift : {100e-6, -100e-6}) {
		CAPTURE(drift);
		MediaClock clock;
		const Range slack = play(clock, 0, 20 * 60 * kSecond, 350 * kMs, drift, 0, 2 * 60 * kSecond);
		CHECK(slack.min >= kConfig.buffer_ns - 5 * kMs);
		CHECK(slack.max <= kConfig.buffer_ns + 5 * kMs);
	}
}

TEST_CASE("A late packet moves the map later at once, and the map comes back slowly")
{
	MediaClock clock;
	play(clock, 0, 10 * kSecond, 350 * kMs);

	const int64_t pts = 10 * kSecond;
	const int64_t arrival = pts + 450 * kMs;
	clock.on_packet(Stream::Video, pts, arrival);
	CHECK(clock.to_obs(pts) - arrival == kConfig.min_slack_ns);

	const Range later = play(clock, pts + kVideoFrame, 5 * 60 * kSecond, 350 * kMs, 0, 0, 4 * 60 * kSecond);
	CHECK(later.min >= kConfig.buffer_ns);
	CHECK(later.max <= kConfig.buffer_ns + 1 * kMs);
}

TEST_CASE("The burst of packets while the connection is set up does not add delay")
{
	MediaClock clock;
	// The first 20 packets arrive together, the last of them on time
	const int64_t burst_end = 19 * kVideoFrame + 350 * kMs;
	for (int i = 0; i < 20; i++)
		clock.on_packet(Stream::Video, i * kVideoFrame, burst_end);

	const Range slack = play(clock, 20 * kVideoFrame, 10 * kSecond, 350 * kMs, 0, 0, 2 * kSecond);
	CHECK(slack.min == kConfig.buffer_ns);
	CHECK(slack.max == kConfig.buffer_ns);
}

TEST_CASE("A jump in the timestamps starts over")
{
	MediaClock clock;
	play(clock, 0, 5 * kSecond, 350 * kMs);

	const int64_t pts = 3'600 * kSecond;
	const int64_t arrival = 5 * kSecond + 350 * kMs;
	clock.on_packet(Stream::Video, pts, arrival);
	CHECK(clock.to_obs(pts) - arrival == kConfig.buffer_ns);
}
