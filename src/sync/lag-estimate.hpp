// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <vector>

namespace bmagicam {

// The lag at which one signal matches another (docs/architecture.md, "Microphone sync").
struct LagEstimate {
	// How many samples later the later signal carries the same sound; fractional
	double lag = 0;
	// The peak against the correlation's root mean square: about 4 to 7 for unrelated sound, well above 8 for the
	// same sound heard by two microphones
	double clarity = 0;
};

// Cross-correlates two signals with phase transform (GCC-PHAT), which gives a sharp peak for the same sound even when
// two microphones color it differently. Both signals share one timeline; the earlier one counts only within
// [begin, end), the later one where it can match within ±max_lag samples. Returns the correlation at each lag, from
// -max_lag to max_lag.
std::vector<float> correlate(const std::vector<float> &earlier, const std::vector<float> &later, size_t begin,
			     size_t end, int max_lag);

// The highest peak of a correlation from correlate(), refined between samples.
LagEstimate peak_of(const std::vector<float> &correlation, int max_lag);

// The root mean square of the signal within [begin, end)
double loudness(const std::vector<float> &signal, size_t begin, size_t end);

} // namespace bmagicam
