// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include "face-geometry.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bmagicam {

// A picture to find faces in: 8-bit RGBA, rows without padding, and copies of it at half, quarter and smaller
// sizes, so the models' inputs are sampled from a copy near their own scale instead of skipping pixels
class FaceImage {
public:
	FaceImage(std::vector<uint8_t> pixels, int width, int height);

	int width() const { return levels_.front().width; }
	int height() const { return levels_.front().height; }

	struct Level {
		std::vector<uint8_t> pixels;
		int width = 0;
		int height = 0;
	};
	// The copy to sample from when one sample covers this many pixels of the full picture, and its scale
	const Level &level_for(float pixels_per_sample, float &scale) const;

private:
	std::vector<Level> levels_;
};

// The face detector and the face landmark model (docs/architecture.md, "Face tracking"). Both run on the CPU, on the
// caller's thread; one instance serves any number of threads.
class FaceModels {
public:
	// Loads both models from the directory with face-detector.* and face-landmarks.*; null when they cannot load
	static std::shared_ptr<FaceModels> load(const std::string &directory);
	~FaceModels();

	// The faces in one square of the image, with scores of at least the minimum
	std::vector<Detection> detect(const FaceImage &image, const Square &square, float min_score) const;
	// The face's landmarks in the region, and how sure the model is that a face is there, 0–1
	float landmarks(const FaceImage &image, const Roi &roi, std::vector<Point> &points) const;

private:
	FaceModels();
	struct Nets;
	std::unique_ptr<Nets> nets_;
};

} // namespace bmagicam
