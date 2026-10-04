// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <string>

namespace bmagicam {

class CameraClient;
class CameraControls;

// The phone as it was before obs-bmagicam first changed it (RST-2, docs/architecture.md, "Reset and restore"): a
// phone preset "Before obs-bmagicam", which the user can also load in the app without OBS, and a snapshot of every
// setting the dock can change, in snapshots/<phone>.json in the module's config folder. The livestream destination is
// the session's to give back (CameraSession).

// Saves the phone's state, unless this phone has a snapshot already. Called before the first change to a phone.
void take_snapshot_if_new(const CameraClient &client, const std::string &phone_key);

bool has_snapshot(const std::string &phone_key);

// Restores the phone's settings from its snapshot, each step tried again until the phone confirms it (RST-4). The
// livestream destination comes back when the iPhone Camera source is removed. Returns whether every step went through.
bool restore_snapshot(CameraControls &controls, const std::string &phone_key);

} // namespace bmagicam
