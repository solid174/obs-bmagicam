// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "sync/lag-estimate.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <random>
#include <vector>

using bmagicam::correlate;
using bmagicam::loudness;
using bmagicam::peak_of;

namespace {

constexpr int kRate = 8000;

// Speech-like test sound: bursts of noise with pauses
std::vector<float> bursts(size_t length, unsigned seed)
{
	std::mt19937 random(seed);
	std::normal_distribution<float> noise(0, 0.2f);
	std::vector<float> signal(length, 0);
	for (size_t i = 0; i < length; i++) {
		const bool speaking = (i / (kRate / 4)) % 3 != 2;
		signal[i] = speaking ? noise(random) : 0;
	}
	return signal;
}

// The same sound later, through another microphone: colored, quieter and with its own noise
std::vector<float> heard_later(const std::vector<float> &sound, size_t lag, unsigned seed)
{
	std::mt19937 random(seed);
	std::normal_distribution<float> noise(0, 0.01f);
	std::vector<float> later(sound.size() + lag, 0);
	float previous = 0;
	for (size_t i = 0; i < sound.size(); i++) {
		previous = 0.6f * sound[i] + 0.3f * previous;
		later[i + lag] = 0.5f * previous;
	}
	for (float &sample : later)
		sample += noise(random);
	return later;
}

} // namespace

constexpr int kMaxLag = kRate * 3 / 2;

bmagicam::LagEstimate estimate(const std::vector<float> &earlier, const std::vector<float> &later, size_t begin,
			       size_t end)
{
	return peak_of(correlate(earlier, later, begin, end, kMaxLag), kMaxLag);
}

TEST_CASE("the lag of the same sound heard later is found within a sample")
{
	const std::vector<float> microphone = bursts(kRate * 8, 1);
	for (size_t lag : {0, 120, 3300, 9000}) {
		const std::vector<float> camera = heard_later(microphone, lag, 2);
		const auto found = estimate(microphone, camera, kRate * 2, kRate * 6);
		CHECK(std::abs(found.lag - static_cast<double>(lag)) < 1.0);
		CHECK(found.clarity > 8.0);
	}
}

TEST_CASE("a microphone that runs behind gives a negative lag")
{
	const std::vector<float> camera = bursts(kRate * 8, 3);
	const std::vector<float> microphone = heard_later(camera, 400, 4);
	const auto found = estimate(microphone, camera, kRate * 2, kRate * 6);
	CHECK(std::abs(found.lag + 400) < 1.0);
}

TEST_CASE("unrelated sounds give no clear lag")
{
	const std::vector<float> microphone = bursts(kRate * 8, 5);
	const std::vector<float> camera = bursts(kRate * 8, 6);
	CHECK(estimate(microphone, camera, kRate * 2, kRate * 6).clarity < 8.0);
}

TEST_CASE("a window of a long recording is measured quickly")
{
	// Beyond 2^17 points FFmpeg's FFT falls back to a slow transform; a window must not need one
	const std::vector<float> microphone = bursts(kRate * 20, 7);
	const std::vector<float> camera = heard_later(microphone, 3300, 8);
	const auto start = std::chrono::steady_clock::now();
	const auto found = estimate(microphone, camera, kRate * 14, kRate * 18);
	const auto elapsed = std::chrono::steady_clock::now() - start;
	CHECK(std::abs(found.lag - 3300) < 1.0);
	CHECK(elapsed < std::chrono::seconds(1));
}

TEST_CASE("loudness is the root mean square")
{
	const std::vector<float> signal = {0.5f, -0.5f, 0.5f, -0.5f};
	CHECK(loudness(signal, 0, 4) == doctest::Approx(0.5));
	CHECK(loudness(signal, 2, 2) == 0);
}
