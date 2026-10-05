// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "face-models.hpp"

#include <datareader.h>
#include <net.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace bmagicam {

namespace {

// Smaller copies stop at about the detector's input size
constexpr int kSmallestLevel = 96;
// Where a crop reaches outside the picture: mid gray, which the models' inputs map to zero
constexpr unsigned int kBorder = 0xff808080u;

bool read_file(const std::filesystem::path &path, std::vector<unsigned char> &contents)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
		return false;
	contents.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	return !contents.empty();
}

// Each 2 × 2 block of the level becomes one pixel of the next
FaceImage::Level halve(const FaceImage::Level &level)
{
	FaceImage::Level result;
	result.width = level.width / 2;
	result.height = level.height / 2;
	result.pixels.resize(static_cast<size_t>(result.width) * result.height * 4);
	const size_t stride = static_cast<size_t>(level.width) * 4;
	for (int y = 0; y < result.height; y++) {
		const uint8_t *top = level.pixels.data() + static_cast<size_t>(2 * y) * stride;
		const uint8_t *bottom = top + stride;
		uint8_t *out = result.pixels.data() + static_cast<size_t>(y) * result.width * 4;
		for (int x = 0; x < result.width * 4; x++) {
			const int column = (x / 4) * 8 + x % 4;
			out[x] = static_cast<uint8_t>(
				(top[column] + top[column + 4] + bottom[column] + bottom[column + 4] + 2) / 4);
		}
	}
	return result;
}

// Samples the region that the transform maps the input's pixels to, from the copy of the image nearest the region's
// scale. The transform maps input coordinates to image coordinates, both from the top left corner of the first pixel.
ncnn::Mat sample(const FaceImage &image, const std::array<float, 6> &transform, int size, float mean, float norm)
{
	const float pixels_per_sample = std::hypot(transform[0], transform[3]);
	float scale = 1;
	const FaceImage::Level &level = image.level_for(pixels_per_sample, scale);
	// Pixel centers: input pixel x is at x + 0.5, image pixel i spans i to i + 1
	float tm[6];
	for (int row = 0; row < 2; row++) {
		const float *m = transform.data() + 3 * row;
		tm[3 * row] = m[0] * scale;
		tm[3 * row + 1] = m[1] * scale;
		tm[3 * row + 2] = (m[2] + 0.5f * (m[0] + m[1])) * scale - 0.5f;
	}
	std::vector<uint8_t> crop(static_cast<size_t>(size) * size * 4);
	ncnn::warpaffine_bilinear_c4(level.pixels.data(), level.width, level.height, crop.data(), size, size, tm, 0,
				     kBorder);
	ncnn::Mat input = ncnn::Mat::from_pixels(crop.data(), ncnn::Mat::PIXEL_RGBA2RGB, size, size);
	const float means[3] = {mean, mean, mean};
	const float norms[3] = {norm, norm, norm};
	input.substract_mean_normalize(means, norms);
	return input;
}

} // namespace

FaceImage::FaceImage(std::vector<uint8_t> pixels, int width, int height)
{
	levels_.push_back({std::move(pixels), width, height});
	while (std::min(levels_.back().width, levels_.back().height) / 2 >= kSmallestLevel)
		levels_.push_back(halve(levels_.back()));
}

const FaceImage::Level &FaceImage::level_for(float pixels_per_sample, float &scale) const
{
	size_t index = 0;
	scale = 1;
	// A bilinear sample averages up to 2 × 2 pixels, so each copy serves until a sample covers two of its pixels
	while (index + 1 < levels_.size() && pixels_per_sample * scale > 2.0f) {
		index++;
		scale = static_cast<float>(levels_[index].width) / static_cast<float>(levels_.front().width);
	}
	return levels_[index];
}

struct FaceModels::Nets {
	ncnn::Net detector;
	ncnn::Net landmarks;
	// The weights stay where ncnn reads them from
	std::vector<unsigned char> detector_weights;
	std::vector<unsigned char> landmark_weights;
};

FaceModels::FaceModels() : nets_(std::make_unique<Nets>()) {}

FaceModels::~FaceModels() = default;

std::shared_ptr<FaceModels> FaceModels::load(const std::string &directory)
{
	const std::filesystem::path base = std::filesystem::u8path(directory);
	std::shared_ptr<FaceModels> models(new FaceModels());
	Nets &nets = *models->nets_;
	const auto load_net = [&](ncnn::Net &net, const char *name, std::vector<unsigned char> &weights) {
		std::vector<unsigned char> param;
		if (!read_file(base / (std::string(name) + ".param"), param) ||
		    !read_file(base / (std::string(name) + ".bin"), weights))
			return false;
		param.push_back(0);
		// The tracker has a thread of its own; the models are small enough for one thread each
		net.opt.num_threads = 1;
		const unsigned char *cursor = param.data();
		ncnn::DataReaderFromMemory reader(cursor);
		return net.load_param(reader) == 0 && net.load_model(weights.data()) > 0;
	};
	if (!load_net(nets.detector, "face-detector", nets.detector_weights) ||
	    !load_net(nets.landmarks, "face-landmarks", nets.landmark_weights))
		return nullptr;
	return models;
}

std::vector<Detection> FaceModels::detect(const FaceImage &image, const Square &square, float min_score) const
{
	const float scale = square.size / kDetectorSize;
	const ncnn::Mat input =
		sample(image, {scale, 0, square.left, 0, scale, square.top}, kDetectorSize, 127.5f, 1.0f / 127.5f);
	ncnn::Extractor extractor = nets_->detector.create_extractor();
	extractor.input("input", input);
	ncnn::Mat values;
	ncnn::Mat scores;
	if (extractor.extract("regressors", values) != 0 || extractor.extract("scores", scores) != 0 ||
	    values.total() < static_cast<size_t>(kDetectorAnchors) * kDetectorValues ||
	    scores.total() < static_cast<size_t>(kDetectorAnchors))
		return {};
	// Both outputs are rows of one anchor each; ncnn may pad rows, so they are read row by row
	std::vector<float> flat_values(static_cast<size_t>(kDetectorAnchors) * kDetectorValues);
	std::vector<float> flat_scores(kDetectorAnchors);
	for (int i = 0; i < kDetectorAnchors; i++) {
		std::copy_n(values.row(i), kDetectorValues,
			    flat_values.data() + static_cast<size_t>(i) * kDetectorValues);
		flat_scores[i] = scores.row(i)[0];
	}
	return decode_detections(flat_values.data(), flat_scores.data(), square, min_score);
}

float FaceModels::landmarks(const FaceImage &image, const Roi &roi, std::vector<Point> &points) const
{
	const std::array<float, 6> transform = roi_transform(roi, kLandmarkSize);
	const ncnn::Mat input = sample(image, transform, kLandmarkSize, 0.0f, 1.0f / 255.0f);
	ncnn::Extractor extractor = nets_->landmarks.create_extractor();
	extractor.input("input", input);
	ncnn::Mat coordinates;
	ncnn::Mat presence;
	if (extractor.extract("landmarks", coordinates) != 0 || extractor.extract("presence", presence) != 0 ||
	    coordinates.total() < static_cast<size_t>(kLandmarkCount) * 3 || presence.total() < 1)
		return 0;
	// The landmark output is one value per channel
	const ncnn::Mat flat = coordinates.reshape(kLandmarkCount * 3);
	const auto *values = static_cast<const float *>(flat.data);
	points.resize(kLandmarkCount);
	for (int i = 0; i < kLandmarkCount; i++) {
		const float x = values[3 * i];
		const float y = values[3 * i + 1];
		points[i] = {transform[0] * x + transform[1] * y + transform[2],
			     transform[3] * x + transform[4] * y + transform[5]};
	}
	return 1.0f / (1.0f + std::exp(-static_cast<const float *>(presence.data)[0]));
}

} // namespace bmagicam
