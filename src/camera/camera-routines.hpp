// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

namespace bmagicam {

class CameraControls;

// Set up for streaming (CTL-8, docs/architecture.md): a flicker-free shutter for the local mains frequency, exposure
// measured once and then held, white balance measured once, continuous focus, and the Rec.709 range the looks are made
// for. Stabilization stays as it is. Blocks for a few seconds; returns whether the phone took every step.
bool set_up_for_streaming(CameraControls &controls);

// Reset to camera defaults (RST-1): the app's default settings, then neutral color correction. Each step is tried
// again until the phone confirms it (RST-4). Returns whether it did.
bool reset_to_defaults(CameraControls &controls);

} // namespace bmagicam
