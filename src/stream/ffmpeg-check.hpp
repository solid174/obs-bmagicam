// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

namespace bmagicam {

// Whether the FFmpeg libraries that OBS loaded can run the stream receiver: they must have the major versions this
// plugin was built against, and receive SRT. On macOS, OBS's FFmpeg libraries carry no version in their names, so a
// plugin built for another FFmpeg major still loads. The first call logs what is wrong.
bool ffmpeg_usable();

} // namespace bmagicam
