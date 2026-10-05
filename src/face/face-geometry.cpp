// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "face-geometry.hpp"

#include <algorithm>
#include <cmath>

namespace bmagicam {

namespace {

constexpr float kPi = 3.14159265358979f;
// The landmark model sees the face with this much margin around the box that encloses it
constexpr float kRoiScale = 1.5f;
// Detections that overlap this much show the same face
constexpr float kSameFace = 0.3f;
// Detection squares of one size overlap by this share
constexpr float kSquareOverlap = 0.25f;
// Landmarks that set the face's turn: the outer corners of the eyes, the person's right one first
constexpr int kRightEyeCorner = 33;
constexpr int kLeftEyeCorner = 263;

// The detector's anchors, as centers in the square from 0 to 1: two per cell of a 16 × 16 grid, then six per cell
// of an 8 × 8 grid
const std::vector<Point> &anchors()
{
	static const std::vector<Point> points = [] {
		std::vector<Point> result;
		result.reserve(kDetectorAnchors);
		for (const auto &[grid, count] : {std::pair{16, 2}, std::pair{8, 6}}) {
			for (int row = 0; row < grid; row++)
				for (int column = 0; column < grid; column++)
					for (int i = 0; i < count; i++)
						result.push_back({(column + 0.5f) / grid, (row + 0.5f) / grid});
		}
		return result;
	}();
	return points;
}

// Positions of squares of the size along one side of the frame, evenly spread, overlapping
std::vector<float> positions(float length, float size)
{
	if (length <= size)
		return {(length - size) / 2};
	const int count = static_cast<int>(std::ceil((length - size) / (size * (1 - kSquareOverlap)))) + 1;
	std::vector<float> result;
	for (int i = 0; i < count; i++)
		result.push_back((length - size) * static_cast<float>(i) / static_cast<float>(count - 1));
	return result;
}

float alpha(float cutoff, float seconds)
{
	const float tau = 1.0f / (2.0f * kPi * cutoff);
	return 1.0f / (1.0f + tau / seconds);
}

} // namespace

std::vector<Detection> decode_detections(const float *values, const float *scores, const Square &square,
					 float min_score)
{
	std::vector<Detection> result;
	const auto &centers = anchors();
	const float scale = square.size / kDetectorSize;
	for (int i = 0; i < kDetectorAnchors; i++) {
		const float score = 1.0f / (1.0f + std::exp(-std::clamp(scores[i], -100.0f, 100.0f)));
		if (score < min_score)
			continue;
		const float *v = values + static_cast<size_t>(i) * kDetectorValues;
		const float x = square.left + centers[i].x * square.size;
		const float y = square.top + centers[i].y * square.size;
		Detection detection;
		detection.score = score;
		detection.left = x + (v[0] - v[2] / 2) * scale;
		detection.top = y + (v[1] - v[3] / 2) * scale;
		detection.right = x + (v[0] + v[2] / 2) * scale;
		detection.bottom = y + (v[1] + v[3] / 2) * scale;
		for (size_t k = 0; k < detection.keypoints.size(); k++)
			detection.keypoints[k] = {x + v[4 + 2 * k] * scale, y + v[5 + 2 * k] * scale};
		result.push_back(detection);
	}
	return result;
}

float box_overlap(const Detection &a, const Detection &b)
{
	const float width = std::min(a.right, b.right) - std::max(a.left, b.left);
	const float height = std::min(a.bottom, b.bottom) - std::max(a.top, b.top);
	if (width <= 0 || height <= 0)
		return 0;
	const float intersection = width * height;
	const float area_a = (a.right - a.left) * (a.bottom - a.top);
	const float area_b = (b.right - b.left) * (b.bottom - b.top);
	return intersection / (area_a + area_b - intersection);
}

std::vector<Detection> merge_detections(std::vector<Detection> detections)
{
	std::sort(detections.begin(), detections.end(),
		  [](const Detection &a, const Detection &b) { return a.score > b.score; });
	std::vector<Detection> result;
	while (!detections.empty()) {
		const Detection best = detections.front();
		Detection merged;
		merged.score = best.score;
		float total = 0;
		std::vector<Detection> rest;
		for (const Detection &detection : detections) {
			if (box_overlap(best, detection) <= kSameFace) {
				rest.push_back(detection);
				continue;
			}
			const float w = detection.score;
			total += w;
			merged.left += w * detection.left;
			merged.top += w * detection.top;
			merged.right += w * detection.right;
			merged.bottom += w * detection.bottom;
			for (size_t k = 0; k < merged.keypoints.size(); k++) {
				merged.keypoints[k].x += w * detection.keypoints[k].x;
				merged.keypoints[k].y += w * detection.keypoints[k].y;
			}
		}
		merged.left /= total;
		merged.top /= total;
		merged.right /= total;
		merged.bottom /= total;
		for (Point &point : merged.keypoints) {
			point.x /= total;
			point.y /= total;
		}
		result.push_back(merged);
		detections = std::move(rest);
	}
	return result;
}

std::vector<Square> detection_squares(int width, int height)
{
	const auto w = static_cast<float>(width);
	const auto h = static_cast<float>(height);
	const float longer = std::max(w, h);
	const float shorter = std::min(w, h);
	std::vector<Square> result;
	result.push_back({(w - longer) / 2, (h - longer) / 2, longer});
	// The short side's squares only add something for frames that are clearly not square
	const std::vector<float> sizes = longer > shorter * 1.2f ? std::vector<float>{shorter, shorter / 2}
								 : std::vector<float>{shorter / 2};
	for (float size : sizes)
		for (float top : positions(h, size))
			for (float left : positions(w, size))
				result.push_back({left, top, size});
	return result;
}

Roi roi_from_detection(const Detection &detection)
{
	const Point &right_eye = detection.keypoints[0];
	const Point &left_eye = detection.keypoints[1];
	Roi roi;
	roi.center = {(detection.left + detection.right) / 2, (detection.top + detection.bottom) / 2};
	roi.size = std::max(detection.right - detection.left, detection.bottom - detection.top) * kRoiScale;
	roi.angle = std::atan2(left_eye.y - right_eye.y, left_eye.x - right_eye.x);
	return roi;
}

Roi roi_from_landmarks(const std::vector<Point> &landmarks)
{
	float left = landmarks[0].x;
	float right = left;
	float top = landmarks[0].y;
	float bottom = top;
	for (const Point &point : landmarks) {
		left = std::min(left, point.x);
		right = std::max(right, point.x);
		top = std::min(top, point.y);
		bottom = std::max(bottom, point.y);
	}
	const Point &right_eye = landmarks[kRightEyeCorner];
	const Point &left_eye = landmarks[kLeftEyeCorner];
	Roi roi;
	roi.center = {(left + right) / 2, (top + bottom) / 2};
	roi.size = std::max(right - left, bottom - top) * kRoiScale;
	roi.angle = std::atan2(left_eye.y - right_eye.y, left_eye.x - right_eye.x);
	return roi;
}

std::array<float, 6> roi_transform(const Roi &roi, int input_size)
{
	const float scale = roi.size / static_cast<float>(input_size);
	const float c = std::cos(roi.angle) * scale;
	const float s = std::sin(roi.angle) * scale;
	const float half = static_cast<float>(input_size) / 2;
	return {c, -s, roi.center.x - (c - s) * half, s, c, roi.center.y - (s + c) * half};
}

float OneEuroFilter::filter(float value, float seconds)
{
	if (!started_) {
		started_ = true;
		raw_ = value;
		value_ = value;
		rate_ = 0;
		return value_;
	}
	if (seconds <= 0)
		return value_;
	rate_ += alpha(derivative_cutoff_, seconds) * ((value - raw_) / seconds - rate_);
	raw_ = value;
	const float cutoff = min_cutoff_ + beta_ * std::fabs(rate_);
	value_ += alpha(cutoff, seconds) * (value - value_);
	return value_;
}

// MediaPipe's face mesh numbers its 478 landmarks the same in every face; these follow its outlines
const std::array<int, 16> kRightEye = {33, 7, 163, 144, 145, 153, 154, 155, 133, 173, 157, 158, 159, 160, 161, 246};
const std::array<int, 16> kLeftEye = {263, 249, 390, 373, 374, 380, 381, 382, 362, 398, 384, 385, 386, 387, 388, 466};
const std::array<int, 10> kRightBrow = {70, 63, 105, 66, 107, 55, 65, 52, 53, 46};
const std::array<int, 10> kLeftBrow = {300, 293, 334, 296, 336, 285, 295, 282, 283, 276};
const std::array<int, 20> kLips = {61,  146, 91,  181, 84,  17, 314, 405, 321, 375,
				   291, 409, 270, 269, 267, 0,  37,  39,  40,  185};

} // namespace bmagicam
