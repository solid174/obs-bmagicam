// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <obs.h>

#include <chrono>
#include <cstdint>
#include <memory>

namespace bmagicam {

// Sync microphone (SYN-1 to SYN-3, docs/architecture.md, "Microphone sync"): listens to the iPhone Camera and a
// microphone of the computer's while the user talks or claps, and finds how much later the iPhone carries the same
// sound. Both sources are heard with their own timestamps, before any Sync Offset, so measuring again gives the same
// value instead of adding up.
class MicrophoneSync {
public:
	// How long to listen
	static constexpr std::chrono::seconds kListenTime{12};

	enum class Result {
		// The lag is in lag_ns: set it as the microphone's Sync Offset
		Measured,
		// One of the two heard nothing
		Silence,
		// No lag stood out, or the parts of the recording disagreed
		Unclear,
	};

	struct Outcome {
		Result result = Result::Unclear;
		int64_t lag_ns = 0;
	};

	// Starts listening at once
	MicrophoneSync(obs_source_t *camera, obs_source_t *microphone);
	// Stops listening
	~MicrophoneSync();

	MicrophoneSync(const MicrophoneSync &) = delete;
	MicrophoneSync &operator=(const MicrophoneSync &) = delete;

	// Stops listening and measures what was heard
	Outcome finish();

private:
	struct Capture;

	static void captured(void *param, obs_source_t *source, const struct audio_data *audio, bool muted);
	void stop();

	obs_weak_source_t *camera_;
	obs_weak_source_t *microphone_;
	std::unique_ptr<Capture> camera_capture_;
	std::unique_ptr<Capture> microphone_capture_;
	bool listening_ = false;
};

} // namespace bmagicam
