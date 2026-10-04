// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

struct AVFrame;

namespace bmagicam {

// Luma and RGB histograms of a picture, for judging exposure in the dock.
struct Histogram {
	static constexpr size_t kBins = 64;
	std::array<uint32_t, kBins> luma{};
	std::array<uint32_t, kBins> red{};
	std::array<uint32_t, kBins> green{};
	std::array<uint32_t, kBins> blue{};
	uint32_t samples = 0;
};

// Samples a decoded frame (NV12, P010, I420 or I010) on a grid of about 240 points across.
Histogram histogram_of(const AVFrame &frame);

} // namespace bmagicam
