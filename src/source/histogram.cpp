// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "histogram.hpp"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

#include <algorithm>
#include <cmath>

namespace bmagicam {

namespace {

size_t bin_of(double level)
{
	return static_cast<size_t>(
		std::clamp(std::lround(level * (Histogram::kBins - 1)), 0L, static_cast<long>(Histogram::kBins - 1)));
}

} // namespace

Histogram histogram_of(const AVFrame &frame)
{
	Histogram histogram;
	const auto format = static_cast<AVPixelFormat>(frame.format);
	const bool semi_planar = format == AV_PIX_FMT_NV12 || format == AV_PIX_FMT_P010LE;
	const bool planar = format == AV_PIX_FMT_YUV420P || format == AV_PIX_FMT_YUVJ420P ||
			    format == AV_PIX_FMT_YUV420P10LE;
	if ((!semi_planar && !planar) || frame.width <= 0 || frame.height <= 0)
		return histogram;

	// Samples as 8-bit values: P010 keeps its 10 bits at the top of 16, I010 at the bottom
	const auto sample = [&](int plane, int x, int y) -> double {
		const uint8_t *row = frame.data[plane] + static_cast<ptrdiff_t>(y) * frame.linesize[plane];
		if (format == AV_PIX_FMT_P010LE)
			return reinterpret_cast<const uint16_t *>(row)[x] / 256.0;
		if (format == AV_PIX_FMT_YUV420P10LE)
			return reinterpret_cast<const uint16_t *>(row)[x] / 4.0;
		return row[x];
	};

	const bool full = frame.color_range == AVCOL_RANGE_JPEG || format == AV_PIX_FMT_YUVJ420P;
	const bool bt601 = frame.colorspace == AVCOL_SPC_SMPTE170M || frame.colorspace == AVCOL_SPC_BT470BG;
	const double kr = bt601 ? 0.299 : 0.2126;
	const double kb = bt601 ? 0.114 : 0.0722;

	const int step = std::max(2, frame.width / 240) & ~1;
	for (int y = 0; y < frame.height; y += step) {
		for (int x = 0; x < frame.width; x += step) {
			const double luma = sample(0, x, y);
			double cb = 0;
			double cr = 0;
			if (semi_planar) {
				cb = sample(1, (x / 2) * 2, y / 2);
				cr = sample(1, (x / 2) * 2 + 1, y / 2);
			} else {
				cb = sample(1, x / 2, y / 2);
				cr = sample(2, x / 2, y / 2);
			}
			const double l = full ? luma / 255.0 : (luma - 16.0) / 219.0;
			const double u = (cb - 128.0) / (full ? 255.0 : 224.0);
			const double v = (cr - 128.0) / (full ? 255.0 : 224.0);
			const double red = l + 2.0 * (1.0 - kr) * v;
			const double blue = l + 2.0 * (1.0 - kb) * u;
			const double green = (l - kr * red - kb * blue) / (1.0 - kr - kb);
			histogram.luma[bin_of(l)]++;
			histogram.red[bin_of(red)]++;
			histogram.green[bin_of(green)]++;
			histogram.blue[bin_of(blue)]++;
			histogram.samples++;
		}
	}
	return histogram;
}

} // namespace bmagicam
