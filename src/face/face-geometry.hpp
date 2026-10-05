// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace bmagicam {

// Face tracking's geometry: reading the face detector's output, choosing where the landmark model looks, and
// smoothing what it finds (docs/architecture.md, "Face tracking"). Coordinates are pixels of the tracked frame.

struct Point {
	float x = 0;
	float y = 0;
};

// The models' input sizes and outputs
constexpr int kDetectorSize = 128;
constexpr int kDetectorAnchors = 896;
constexpr int kDetectorValues = 16;
constexpr int kLandmarkSize = 256;
constexpr int kLandmarkCount = 478;

// A square of the frame that the detector looks at. Parts outside the frame are padding, so the whole frame
// fits into one square.
struct Square {
	float left = 0;
	float top = 0;
	float size = 0;
};

// A face the detector found
struct Detection {
	float score = 0;
	float left = 0;
	float top = 0;
	float right = 0;
	float bottom = 0;
	// The detector's key points: the person's right eye, left eye, nose tip, mouth, right ear, left ear
	std::array<Point, 6> keypoints;
};

// The faces in the detector's output for one square: per anchor, 16 values (box center, size, key points) and a
// score logit
std::vector<Detection> decode_detections(const float *values, const float *scores, const Square &square,
					 float min_score);
// One detection per face: detections that overlap the best one are averaged into it, weighted by score
std::vector<Detection> merge_detections(std::vector<Detection> detections);
// Intersection over union of two boxes
float box_overlap(const Detection &a, const Detection &b);

// The squares the detector looks at to find faces of every size: the whole frame, then smaller squares that
// cover it with overlap, for faces too small to find in the whole frame
std::vector<Square> detection_squares(int width, int height);

// Where the landmark model looks: a square around the face, turned with it
struct Roi {
	Point center;
	float size = 0;
	// Radians; positive turns clockwise on the screen
	float angle = 0;
};

Roi roi_from_detection(const Detection &detection);
Roi roi_from_landmarks(const std::vector<Point> &landmarks);
// The affine transform from the landmark model's input pixels to frame pixels: x' = m[0]·x + m[1]·y + m[2],
// y' = m[3]·x + m[4]·y + m[5]
std::array<float, 6> roi_transform(const Roi &roi, int input_size);

// The One Euro filter (Casiez, Roussel and Vogel, CHI 2012) for one value: smooths jitter at rest and follows fast
// motion with little lag. Cutoffs are in hertz.
class OneEuroFilter {
public:
	OneEuroFilter(float min_cutoff, float beta, float derivative_cutoff)
		: min_cutoff_(min_cutoff),
		  beta_(beta),
		  derivative_cutoff_(derivative_cutoff)
	{
	}

	float filter(float value, float seconds);
	void reset() { started_ = false; }
	// How fast the value changes, per second, smoothed
	float rate() const { return rate_; }

private:
	float min_cutoff_;
	float beta_;
	float derivative_cutoff_;
	bool started_ = false;
	float raw_ = 0;
	float value_ = 0;
	float rate_ = 0;
};

// The triangles of MediaPipe's canonical face mesh, three landmark indices each (src/face/face-mesh.cpp). Together they
// cover the face as far as it is seen, at any turn.
constexpr int kFaceMeshTriangles = 898;
extern const std::array<uint16_t, kFaceMeshTriangles * 3> kFaceMesh;

// Landmark outlines of the parts the skin mask leaves out, in order around each
extern const std::array<int, 16> kRightEye;
extern const std::array<int, 16> kLeftEye;
extern const std::array<int, 10> kRightBrow;
extern const std::array<int, 10> kLeftBrow;
extern const std::array<int, 20> kLips;

} // namespace bmagicam
