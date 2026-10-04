// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "skin-model.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace bmagicam {

namespace {

// Before the first estimate: the skin-tone line, about 123° on a vectorscope, at a typical saturation
constexpr float kDefaultAngle = 2.15f;
constexpr float kDefaultChroma = 0.1f;
constexpr float kDefaultNoise = 0.006f;
// An estimate stays within plausible skin, whatever else the picture holds
constexpr float kMinAngle = 1.66f;
constexpr float kMaxAngle = 2.62f;
constexpr float kMinChroma = 0.04f;
constexpr float kMaxChroma = 0.16f;
// At least this much skin, in blocks that are fully skin-like, before the skin color moves
constexpr float kMinSkin = 2.0f;
// Refining around the estimate: how far a block's chroma may lie from it and still count
constexpr float kRefineSpread = 0.03f;
// Noise: this share of the mid-tone blocks with the least detail are flat areas
constexpr float kFlatShare = 0.2f;
constexpr size_t kMinNoiseBlocks = 8;
// Detail measured at half resolution against noise at full resolution
constexpr float kNoisePerDetail = 2.5f;
constexpr float kMinNoise = 0.001f;
constexpr float kMaxNoise = 0.05f;

float squared(float value)
{
	return value * value;
}

} // namespace

float from_half(uint16_t bits)
{
	const uint32_t sign = static_cast<uint32_t>(bits & 0x8000u) << 16;
	uint32_t exponent = (bits >> 10) & 0x1fu;
	uint32_t mantissa = bits & 0x3ffu;
	uint32_t result = sign;
	if (exponent == 0x1fu) {
		result |= 0x7f800000u | (mantissa << 13);
	} else if (exponent != 0) {
		result |= ((exponent + 112) << 23) | (mantissa << 13);
	} else if (mantissa != 0) {
		// Subnormal: normalized for the float's wider exponent
		exponent = 113;
		while (!(mantissa & 0x400u)) {
			mantissa <<= 1;
			exponent--;
		}
		result |= (exponent << 23) | ((mantissa & 0x3ffu) << 13);
	}
	float value;
	std::memcpy(&value, &result, sizeof(value));
	return value;
}

void SkinModel::reset()
{
	has_skin_ = false;
	has_noise_ = false;
	cb_ = kDefaultChroma * std::cos(kDefaultAngle);
	cr_ = kDefaultChroma * std::sin(kDefaultAngle);
	noise_ = kDefaultNoise;
}

void SkinModel::update(const std::vector<BlockStats> &blocks, float weight)
{
	weight = std::clamp(weight, 0.0f, 1.0f);

	// Skin: the mean of the skin-like blocks, then twice around it, so one area of skin wins over skin-like
	// things elsewhere in the picture
	float cb = 0, cr = 0, total = 0;
	for (const BlockStats &block : blocks) {
		const float w = squared(block.skin);
		cb += w * block.cb;
		cr += w * block.cr;
		total += w;
	}
	if (total >= kMinSkin) {
		cb /= total;
		cr /= total;
		for (int round = 0; round < 2; round++) {
			float next_cb = 0, next_cr = 0, sum = 0;
			for (const BlockStats &block : blocks) {
				const float distance = squared(block.cb - cb) + squared(block.cr - cr);
				const float w =
					squared(block.skin) * std::exp(-0.5f * distance / squared(kRefineSpread));
				next_cb += w * block.cb;
				next_cr += w * block.cr;
				sum += w;
			}
			if (sum <= 1e-6f)
				break;
			cb = next_cb / sum;
			cr = next_cr / sum;
		}
		const float angle = std::clamp(std::atan2(cr, cb), kMinAngle, kMaxAngle);
		const float chroma = std::clamp(std::hypot(cb, cr), kMinChroma, kMaxChroma);
		const float step = has_skin_ ? weight : 1.0f;
		cb_ += step * (chroma * std::cos(angle) - cb_);
		cr_ += step * (chroma * std::sin(angle) - cr_);
		has_skin_ = true;
	}

	// Noise: the mid-tone blocks with the least detail are flat, so their detail is the noise
	std::vector<float> details;
	for (const BlockStats &block : blocks)
		if (block.detail >= 0)
			details.push_back(block.detail);
	if (details.size() >= kMinNoiseBlocks) {
		const auto flat =
			details.begin() + static_cast<std::ptrdiff_t>(static_cast<float>(details.size()) * kFlatShare);
		std::nth_element(details.begin(), flat, details.end());
		const float estimate = std::clamp(*flat * kNoisePerDetail, kMinNoise, kMaxNoise);
		noise_ += (has_noise_ ? weight : 1.0f) * (estimate - noise_);
		has_noise_ = true;
	}
}

} // namespace bmagicam
