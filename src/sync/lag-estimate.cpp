// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "lag-estimate.hpp"

extern "C" {
#include <libavutil/tx.h>
}

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdlib>

namespace bmagicam {

namespace {

size_t fft_size(size_t length)
{
	size_t size = 1;
	while (size < length)
		size <<= 1;
	return size;
}

} // namespace

std::vector<float> correlate(const std::vector<float> &earlier, const std::vector<float> &later, size_t begin,
			     size_t end, int max_lag)
{
	std::vector<float> result;
	end = std::min(end, earlier.size());
	if (begin >= end || later.empty() || max_lag <= 0)
		return result;

	// The window of the earlier signal, and of the later one only what the window can match within the lags. That
	// keeps the transform short: FFmpeg's FFT is fast up to 2^17 points and falls back to a slow one beyond.
	const size_t window = end - begin;
	const size_t lags = 2 * static_cast<size_t>(max_lag);
	const size_t size = fft_size(window + lags);
	std::vector<AVComplexFloat> first(size, AVComplexFloat{0, 0});
	std::vector<AVComplexFloat> second(size, AVComplexFloat{0, 0});
	for (size_t i = 0; i < window; i++)
		first[i].re = earlier[begin + i];
	// second[j] holds later[begin - max_lag + j]
	for (size_t j = 0; j < window + lags; j++) {
		const auto index = static_cast<std::ptrdiff_t>(begin + j) - max_lag;
		if (index >= 0 && static_cast<size_t>(index) < later.size())
			second[j].re = later[static_cast<size_t>(index)];
	}

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
		return result;
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

	// correlation[lag + max_lag] compares earlier[t] with later[t + lag]
	result.resize(lags + 1);
	for (size_t i = 0; i <= lags; i++)
		result[i] = correlation[i].re;
	return result;
}

LagEstimate peak_of(const std::vector<float> &correlation, int max_lag)
{
	LagEstimate estimate;
	if (correlation.size() != 2 * static_cast<size_t>(max_lag) + 1)
		return estimate;
	const auto at = [&](int lag) {
		return static_cast<double>(correlation[static_cast<size_t>(std::clamp(lag + max_lag, 0, 2 * max_lag))]);
	};
	int best = -max_lag;
	double squares = 0;
	for (int lag = -max_lag; lag <= max_lag; lag++) {
		squares += at(lag) * at(lag);
		if (at(lag) > at(best))
			best = lag;
	}
	const double rms = std::sqrt(squares / static_cast<double>(correlation.size()));

	// Between samples, from a parabola through the peak and its neighbours
	const double left = at(best - 1);
	const double center = at(best);
	const double right = at(best + 1);
	const double curve = left - 2 * center + right;
	const double offset = std::abs(curve) > 1e-12 ? 0.5 * (left - right) / curve : 0;
	estimate.lag = best + std::clamp(offset, -0.5, 0.5);
	estimate.clarity = rms > 0 ? center / rms : 0;
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
