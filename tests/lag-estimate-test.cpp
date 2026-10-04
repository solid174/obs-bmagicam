// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "sync/lag-estimate.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <random>
#include <vector>

using bmagicam::estimate_lag;
using bmagicam::loudness;

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

TEST_CASE("the lag of the same sound heard later is found within a sample")
{
	const std::vector<float> microphone = bursts(kRate * 8, 1);
	for (size_t lag : {0, 120, 3300, 9000}) {
		const std::vector<float> camera = heard_later(microphone, lag, 2);
		const auto estimate = estimate_lag(microphone, camera, kRate * 2, kRate * 6, kRate * 3 / 2);
		CHECK(std::abs(estimate.lag - static_cast<double>(lag)) < 1.0);
		CHECK(estimate.clarity > 2.0);
	}
}

TEST_CASE("a microphone that runs behind gives a negative lag")
{
	const std::vector<float> camera = bursts(kRate * 8, 3);
	const std::vector<float> microphone = heard_later(camera, 400, 4);
	const auto estimate = estimate_lag(microphone, camera, kRate * 2, kRate * 6, kRate * 3 / 2);
	CHECK(std::abs(estimate.lag + 400) < 1.0);
}

TEST_CASE("unrelated sounds give no clear lag")
{
	const std::vector<float> microphone = bursts(kRate * 8, 5);
	const std::vector<float> camera = bursts(kRate * 8, 6);
	const auto estimate = estimate_lag(microphone, camera, kRate * 2, kRate * 6, kRate * 3 / 2);
	CHECK(estimate.clarity < 2.0);
}

TEST_CASE("loudness is the root mean square")
{
	const std::vector<float> signal = {0.5f, -0.5f, 0.5f, -0.5f};
	CHECK(loudness(signal, 0, 4) == doctest::Approx(0.5));
	CHECK(loudness(signal, 2, 2) == 0);
}
