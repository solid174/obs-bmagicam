// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include "../camera/camera-session.hpp"

#include <obs.h>

#include <memory>

namespace bmagicam {

class CameraControls;

inline constexpr const char *kCameraSourceId = "bmagicam_camera";

// The iPhone Camera source: video and audio from one phone.
void register_camera_source();

// Whether the source is an iPhone Camera.
bool is_camera_source(obs_source_t *source);
// The control connection of an iPhone Camera source's phone; null for other sources.
std::shared_ptr<CameraControls> camera_controls(obs_source_t *source);
// The state of an iPhone Camera source's phone and stream.
CameraSession::Status camera_status(obs_source_t *source);

} // namespace bmagicam
