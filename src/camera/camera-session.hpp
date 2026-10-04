// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include "../stream/stream-receiver.hpp"

#include <cstdint>
#include <memory>
#include <string>

struct AVFrame;

namespace bmagicam {

// One iPhone Camera source's phone (docs/architecture.md, "Camera session"): connects to Blackmagic Camera, points its
// livestream at this computer, starts the stream while the source is shown and stops it two seconds after it is
// hidden, receives it, and reconnects after any failure. When the session ends it stops the stream and gives the phone
// back its own livestream destination, in the background, so removing a source never waits for the network.
class CameraSession {
public:
	enum class State {
		// No phone chosen
		NoPhone,
		// The chosen phone is not on the network
		Searching,
		Connecting,
		Starting,
		Live,
		// The source is hidden everywhere
		Paused,
		Error,
	};

	enum class Problem {
		None,
		// The address does not answer: the app is not on screen, or the phone is elsewhere
		NotAnswering,
		// Camera Available for is set to monitoring only
		MonitorOnly,
		// The phone streams, but nothing arrives
		CannotReach,
		// The phone cannot stream now; the reason is in Status::detail
		Unavailable,
		// The phone refused a step of the setup; the step is in Status::detail
		Refused,
		// Blackmagic Camera is older than 3.4, which brought remote control
		Outdated,
		// The FFmpeg in OBS cannot run the receiver
		FFmpeg,
	};

	struct Status {
		State state = State::NoPhone;
		Problem problem = Problem::None;
		// "iPhone 17 Pro (A)" once known
		std::string phone;
		std::string detail;
	};

	struct Settings {
		// The phone's device_id as Bonjour announces it; empty to use the address instead
		std::string phone_id;
		// Shown while the phone is looked for
		std::string phone_name;
		// Entered by hand, when there is no phone_id
		std::string address;
		std::string preset;
		StreamReceiver::Settings receiver;
	};

	// Receives the session's video, audio and status changes. Calls come from the session's threads.
	class Output {
	public:
		virtual ~Output() = default;
		virtual void session_video(const AVFrame &frame, uint64_t timestamp) = 0;
		virtual void session_audio(const float *const planes[2], uint32_t frames, uint64_t timestamp) = 0;
		// The stream ended: the picture should be cleared
		virtual void session_cleared() = 0;
		virtual void session_status_changed() = 0;
	};

	CameraSession(Output &output, StreamReceiver::Clock clock);
	// Stops calling the output at once; the phone is put back in the background
	~CameraSession();

	CameraSession(const CameraSession &) = delete;
	CameraSession &operator=(const CameraSession &) = delete;

	void update(const Settings &settings);
	// Whether the source is shown anywhere
	void set_active(bool active);
	Status status() const;

private:
	struct Core;
	std::shared_ptr<Core> core_;
	int discovery_listener_ = 0;
};

} // namespace bmagicam
