// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <string>

namespace bmagicam {

// The server name inside the destination, which the phone needs when the destination is selected.
inline constexpr const char *kStreamingServer = "OBS";

// A livestream destination in Blackmagic Streaming XML that points the phone at an SRT listener, with one profile
// per stream preset (docs/camera-api.md, "Livestream to OBS"). The phone stores it as "<service> SRT".
std::string streaming_xml(const std::string &service, const std::string &srt_url);

// What the phone calls a destination uploaded with the given service name.
std::string streaming_platform(const std::string &service);

} // namespace bmagicam
