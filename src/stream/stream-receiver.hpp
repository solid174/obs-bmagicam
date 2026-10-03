// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

struct AVFrame;

namespace bmagicam {

// Receives the phone's livestream: listens for SRT on a UDP port, demuxes the MPEG-TS, decodes the video (in
// hardware where possible) and the AAC audio, and hands both to the sink on one timeline (see MediaClock). Audio is
// handed over as soon as it is decoded, with its timestamps. Video waits as compressed packets, is decoded shortly
// before its presentation time and handed over at that time.
class StreamReceiver {
public:
	class Sink {
	public:
		virtual ~Sink() = default;
		// The phone connected.
		virtual void stream_started() = 0;
		// A video frame in system memory, at its presentation time (OBS time).
		virtual void stream_video(const AVFrame &frame, uint64_t timestamp) = 0;
		// 48 kHz stereo audio, planar float, starting at the timestamp (OBS time).
		virtual void stream_audio(const float *const planes[2], uint32_t frames, uint64_t timestamp) = 0;
		// The stream ended.
		virtual void stream_ended() = 0;
	};

	struct Settings {
		int port = 9710;
		int latency_ms = 120;
		bool hardware_decoding = true;
	};

	// Returns OBS time in nanoseconds.
	using Clock = uint64_t (*)();

	StreamReceiver(Sink &sink, Clock clock);
	~StreamReceiver();

	StreamReceiver(const StreamReceiver &) = delete;
	StreamReceiver &operator=(const StreamReceiver &) = delete;

	void start(const Settings &settings);
	void stop();

private:
	class Connection;

	void receive_loop();

	Sink &sink_;
	const Clock clock_;
	Settings settings_;
	std::atomic<bool> stopping_ = false;
	std::thread receive_thread_;
};

} // namespace bmagicam
