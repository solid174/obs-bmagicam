// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <vector>

namespace bmagicam {

// What the GPU measures in one block of the picture (docs/architecture.md, "Beautify")
struct BlockStats {
	// Mean chroma of the block's skin-like samples (Cb and Cr of Rec. 709, −0.5 to 0.5)
	float cb = 0;
	float cr = 0;
	// How skin-like the block is, 0–1
	float skin = 0;
	// Mean small-scale luma detail of the block's mid-tones; negative when it has none
	float detail = -1;
};

// The skin color and the camera's noise, learned from the picture and followed slowly, so the skin mask holds for
// every skin tone and white balance, and edge thresholds give the same result at low and high ISO.
class SkinModel {
public:
	SkinModel() { reset(); }

	// Skin chroma the mask centers on
	float cb() const { return cb_; }
	float cr() const { return cr_; }
	// The picture's noise as a luma standard deviation, 0–1
	float noise() const { return noise_; }

	// Moves towards the blocks' estimate by the weight, 0–1. The first estimate is taken whole.
	void update(const std::vector<BlockStats> &blocks, float weight);
	void reset();

private:
	bool has_skin_ = false;
	bool has_noise_ = false;
	float cb_ = 0;
	float cr_ = 0;
	float noise_ = 0;
};

} // namespace bmagicam
