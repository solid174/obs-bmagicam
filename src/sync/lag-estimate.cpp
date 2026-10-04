// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "lag-estimate.hpp"

extern "C" {
#include <libavutil/tx.h>
}

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdlib>

namespace bmagicam {

namespace {

// Peaks this close to the main one belong to it
constexpr int kPeakWidth = 16;

size_t fft_size(size_t length)
{
	size_t size = 1;
	while (size < length)
		size <<= 1;
	return size;
}

} // namespace

LagEstimate estimate_lag(const std::vector<float> &earlier, const std::vector<float> &later, size_t begin, size_t end,
			 int max_lag)
{
	LagEstimate estimate;
	end = std::min(end, earlier.size());
	if (begin >= end || later.empty() || max_lag <= 0)
		return estimate;

	// Room for the later signal and the largest lag either way, so the circular correlation does not wrap
	const size_t size = fft_size(std::max(earlier.size(), later.size()) + 2 * static_cast<size_t>(max_lag));
	std::vector<AVComplexFloat> first(size, AVComplexFloat{0, 0});
	std::vector<AVComplexFloat> second(size, AVComplexFloat{0, 0});
	for (size_t i = begin; i < end; i++)
		first[i].re = earlier[i];
	for (size_t i = 0; i < later.size(); i++)
		second[i].re = later[i];

	AVTXContext *forward = nullptr;
	AVTXContext *inverse = nullptr;
	av_tx_fn forward_fn = nullptr;
	av_tx_fn inverse_fn = nullptr;
	// The buffers are plain vectors, not aligned for SIMD
	const float scale = 1.0f;
	if (av_tx_init(&forward, &forward_fn, AV_TX_FLOAT_FFT, 0, static_cast<int>(size), &scale, AV_TX_UNALIGNED) <
		    0 ||
	    av_tx_init(&inverse, &inverse_fn, AV_TX_FLOAT_FFT, 1, static_cast<int>(size), &scale, AV_TX_UNALIGNED) <
		    0) {
		av_tx_uninit(&forward);
		av_tx_uninit(&inverse);
		return estimate;
	}

	std::vector<AVComplexFloat> first_spectrum(size);
	std::vector<AVComplexFloat> second_spectrum(size);
	forward_fn(forward, first_spectrum.data(), first.data(), sizeof(AVComplexFloat));
	forward_fn(forward, second_spectrum.data(), second.data(), sizeof(AVComplexFloat));

	// Phase transform: every frequency counts by its phase alone; below 100 Hz only hum is left
	const size_t lowest = std::max<size_t>(1, size * 100 / 8000);
	std::vector<AVComplexFloat> product(size, AVComplexFloat{0, 0});
	for (size_t bin = 0; bin < size; bin++) {
		const size_t frequency = std::min(bin, size - bin);
		if (frequency < lowest)
			continue;
		const std::complex<float> a(first_spectrum[bin].re, first_spectrum[bin].im);
		const std::complex<float> b(second_spectrum[bin].re, second_spectrum[bin].im);
		const std::complex<float> cross = b * std::conj(a);
		const float magnitude = std::abs(cross);
		if (magnitude > 1e-12f)
			product[bin] = {cross.real() / magnitude, cross.imag() / magnitude};
	}
	std::vector<AVComplexFloat> correlation(size);
	inverse_fn(inverse, correlation.data(), product.data(), sizeof(AVComplexFloat));
	av_tx_uninit(&forward);
	av_tx_uninit(&inverse);

	const auto at = [&](int lag) {
		return correlation[static_cast<size_t>((lag % static_cast<int>(size) + static_cast<int>(size)) %
						       static_cast<int>(size))]
			.re;
	};
	int best = 0;
	for (int lag = -max_lag; lag <= max_lag; lag++) {
		if (at(lag) > at(best))
			best = lag;
	}
	float runner_up = 0;
	for (int lag = -max_lag; lag <= max_lag; lag++) {
		if (std::abs(lag - best) > kPeakWidth)
			runner_up = std::max(runner_up, at(lag));
	}

	// Between samples, from a parabola through the peak and its neighbours
	const double left = at(best - 1);
	const double center = at(best);
	const double right = at(best + 1);
	const double curve = left - 2 * center + right;
	const double offset = std::abs(curve) > 1e-12 ? 0.5 * (left - right) / curve : 0;
	estimate.lag = best + std::clamp(offset, -0.5, 0.5);
	estimate.clarity = runner_up > 0 ? center / runner_up : 0;
	return estimate;
}

double loudness(const std::vector<float> &signal, size_t begin, size_t end)
{
	end = std::min(end, signal.size());
	if (begin >= end)
		return 0;
	double sum = 0;
	for (size_t i = begin; i < end; i++)
		sum += static_cast<double>(signal[i]) * signal[i];
	return std::sqrt(sum / static_cast<double>(end - begin));
}

} // namespace bmagicam
