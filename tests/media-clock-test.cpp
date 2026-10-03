// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "stream/media-clock.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

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

struct Arrivals {
	int64_t delay = 350 * kMs;
	double drift = 0;
	int64_t audio_late = 0;
	// Audio frames that the phone sends together; each arrives with the last of its group
	int64_t audio_group = 1;
	bool audio = true;
};

struct Packet {
	Stream stream;
	int64_t pts;
	int64_t arrival;
};

// Plays video and audio packets into the clock, in the order they arrive. A packet with timestamp pts arrives at
// pts * (1 + drift) + delay, audio another audio_late later. Returns the range of the slack, the time between arrival
// and presentation, of the packets presented from settle_ns on.
Range play(MediaClock &clock, int64_t start_ns, int64_t duration_ns, const Arrivals &arrivals = {},
	   int64_t settle_ns = 0)
{
	const auto arrival_of = [&](int64_t pts) {
		return std::llround(static_cast<double>(pts) * (1 + arrivals.drift)) + arrivals.delay;
	};

	std::vector<Packet> packets;
	for (int64_t pts = start_ns; pts < start_ns + duration_ns; pts += kVideoFrame)
		packets.push_back({Stream::Video, pts, arrival_of(pts)});
	for (int64_t i = 0; arrivals.audio && start_ns + i * kAudioFrame < start_ns + duration_ns; i++) {
		const int64_t last_of_group = (i / arrivals.audio_group + 1) * arrivals.audio_group - 1;
		packets.push_back({Stream::Audio, start_ns + i * kAudioFrame,
				   arrival_of(start_ns + last_of_group * kAudioFrame) + arrivals.audio_late});
	}
	std::stable_sort(packets.begin(), packets.end(),
			 [](const Packet &a, const Packet &b) { return a.arrival < b.arrival; });

	Range slack;
	for (const Packet &packet : packets) {
		clock.on_packet(packet.stream, packet.pts, packet.arrival);
		if (clock.settled() && packet.pts - start_ns >= settle_ns)
			slack.add(clock.to_obs(packet.pts) - packet.arrival);
	}
	return slack;
}

} // namespace

TEST_CASE("A constant delay maps each timestamp to its arrival plus the buffer")
{
	MediaClock clock;
	const Range slack = play(clock, 0, 10 * kSecond);
	CHECK(slack.min == kConfig.buffer_ns);
	CHECK(slack.max == kConfig.buffer_ns);
}

TEST_CASE("Nothing is presented until both streams have arrived for a while")
{
	MediaClock clock;
	for (int64_t pts = 0; pts < 500 * kMs; pts += kVideoFrame)
		clock.on_packet(Stream::Video, pts, pts + 350 * kMs);
	CHECK_FALSE(clock.settled());

	clock.on_packet(Stream::Audio, 500 * kMs, 850 * kMs);
	clock.on_packet(Stream::Video, 790 * kMs, 1'140 * kMs);
	CHECK_FALSE(clock.settled());
	clock.on_packet(Stream::Video, 810 * kMs, 1'160 * kMs);
	CHECK(clock.settled());
}

TEST_CASE("A stream without audio settles after a second")
{
	MediaClock clock;
	Arrivals video_only;
	video_only.audio = false;
	play(clock, 0, 990 * kMs, video_only);
	CHECK_FALSE(clock.settled());
	play(clock, 990 * kMs, 30 * kMs, video_only);
	CHECK(clock.settled());
}

TEST_CASE("The stream that arrives later relative to its timestamps sets the delay")
{
	for (int64_t audio_late : {30 * kMs, 600 * kMs}) {
		CAPTURE(audio_late);
		MediaClock clock;
		Arrivals stream;
		stream.audio_late = audio_late;
		const Range slack = play(clock, 0, 5 * kSecond, stream);

		CHECK(slack.min == kConfig.buffer_ns);
		CHECK(slack.max == kConfig.buffer_ns + audio_late);
	}
}

TEST_CASE("Audio that comes in groups keeps the map steady")
{
	MediaClock clock;
	Arrivals arrivals;
	arrivals.audio_group = 14;
	play(clock, 0, 5 * kSecond, arrivals);

	const int64_t map = clock.to_obs(0);
	const Range slack = play(clock, 5 * kSecond, 60 * kSecond, arrivals);
	CHECK(slack.min >= kConfig.buffer_ns - 1 * kMs);
	CHECK(std::llabs(clock.to_obs(0) - map) <= 1 * kMs);
}

TEST_CASE("A large excess delay from the start is dropped after a few windows")
{
	MediaClock clock;
	// The first audio packet comes 600 ms late, the rest on time
	clock.on_packet(Stream::Video, 0, 350 * kMs);
	clock.on_packet(Stream::Audio, 0, 950 * kMs);
	play(clock, kAudioFrame, 3'500 * kMs);
	CHECK(clock.settled());

	// Two windows after settling, the excess is still there, untouched
	const int64_t with_excess = 950 * kMs + kConfig.buffer_ns;
	CHECK(clock.to_obs(0) == with_excess);

	// The third drops it at once
	const Range slack = play(clock, 3'500 * kMs, 5 * kSecond, {}, 1 * kSecond);
	CHECK(clock.to_obs(0) == 350 * kMs + kConfig.buffer_ns);
	CHECK(slack.min == kConfig.buffer_ns);
	CHECK(slack.max == kConfig.buffer_ns);
}

TEST_CASE("Clock drift is followed, so the slack stays near the buffer")
{
	for (double drift : {100e-6, -100e-6}) {
		CAPTURE(drift);
		MediaClock clock;
		Arrivals stream;
		stream.drift = drift;
		const Range slack = play(clock, 0, 20 * 60 * kSecond, stream, 2 * 60 * kSecond);
		CHECK(slack.min >= kConfig.buffer_ns - 5 * kMs);
		CHECK(slack.max <= kConfig.buffer_ns + 5 * kMs);
	}
}

TEST_CASE("A late packet moves the map later at once, and the map comes back slowly")
{
	MediaClock clock;
	play(clock, 0, 10 * kSecond);

	const int64_t pts = 10 * kSecond;
	const int64_t arrival = pts + 450 * kMs;
	clock.on_packet(Stream::Video, pts, arrival);
	CHECK(clock.to_obs(pts) - arrival == kConfig.min_slack_ns);

	const Range later = play(clock, pts + kVideoFrame, 5 * 60 * kSecond, {}, 4 * 60 * kSecond);
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

	const Range slack = play(clock, 20 * kVideoFrame, 10 * kSecond);
	CHECK(slack.min == kConfig.buffer_ns);
	CHECK(slack.max == kConfig.buffer_ns);
}

TEST_CASE("The burst of both streams right after connecting does not add delay")
{
	MediaClock clock;
	// The first 400 ms of both streams arrive within a few milliseconds, the last of them on time
	const int64_t burst_end = 400 * kMs + 350 * kMs;
	for (int64_t pts = 0; pts < 400 * kMs; pts += kAudioFrame)
		clock.on_packet(Stream::Audio, pts, burst_end);
	for (int64_t pts = 0; pts < 400 * kMs; pts += kVideoFrame)
		clock.on_packet(Stream::Video, pts, burst_end);

	play(clock, 400 * kMs, 1 * kSecond);
	CHECK(clock.settled());
	const int64_t map = clock.to_obs(0);
	const Range slack = play(clock, 1'400 * kMs, 10 * kSecond);
	CHECK(slack.min == kConfig.buffer_ns);
	CHECK(slack.max == kConfig.buffer_ns);
	CHECK(clock.to_obs(0) == map);
}

TEST_CASE("A jump in the timestamps starts over")
{
	MediaClock clock;
	play(clock, 0, 5 * kSecond);
	CHECK(clock.settled());

	const int64_t pts = 3'600 * kSecond;
	const int64_t arrival = 5 * kSecond + 350 * kMs;
	clock.on_packet(Stream::Video, pts, arrival);
	CHECK_FALSE(clock.settled());
	CHECK(clock.to_obs(pts) - arrival == kConfig.buffer_ns);
}
