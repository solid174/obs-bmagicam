// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <chrono>
#include <functional>

namespace bmagicam {

// Runs work on a thread of its own that may outlive whatever started it, such as a session putting its phone back
// after the source was removed, so that nothing in OBS waits for the network.
void run_in_background(std::function<void()> work);

// Waits until all background work has ended, or the time is up; for module unload, since the work runs the module's
// code.
void wait_for_background_work(std::chrono::seconds limit);

} // namespace bmagicam
