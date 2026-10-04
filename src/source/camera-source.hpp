// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include "histogram.hpp"
#include "../camera/camera-session.hpp"

#include <obs.h>

#include <functional>
#include <memory>
#include <string>

namespace bmagicam {

class CameraControls;

inline constexpr const char *kCameraSourceId = "bmagicam_camera";

// The iPhone Camera source: video and audio from one phone.
void register_camera_source();

// What the buttons in the source's properties open; the user interface provides them.
struct CameraSourceActions {
	std::function<void(obs_source_t *source)> open_controls;
	std::function<void()> open_setup_guide;
};
void set_camera_source_actions(CameraSourceActions actions);

// Whether the source is an iPhone Camera.
bool is_camera_source(obs_source_t *source);
// The control connection of an iPhone Camera source's phone; null for other sources.
std::shared_ptr<CameraControls> camera_controls(obs_source_t *source);
// The state of an iPhone Camera source's phone and stream.
CameraSession::Status camera_status(obs_source_t *source);
// What the state means for the user, in the source's properties and the dock (CAM-5).
std::string camera_status_text(const CameraSession::Status &status);
// The histogram of an iPhone Camera source's latest pictures. Asking for it keeps it computed for two seconds.
Histogram camera_histogram(obs_source_t *source);

} // namespace bmagicam
