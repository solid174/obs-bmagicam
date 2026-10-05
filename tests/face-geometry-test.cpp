// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "face/face-geometry.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

using namespace bmagicam;

TEST_CASE("detection squares cover the whole frame at every size")
{
	const std::vector<Square> squares = detection_squares(960, 540);
	REQUIRE(squares.size() > 2);
	// The first holds the whole frame, padded above and below
	CHECK(squares[0].size == 960.0f);
	CHECK(squares[0].left == 0.0f);
	CHECK(squares[0].top == -210.0f);
	for (float size : {540.0f, 270.0f}) {
		for (int y = 0; y < 540; y += 10) {
			for (int x = 0; x < 960; x += 10) {
				bool covered = false;
				for (const Square &square : squares)
					covered |= square.size == size && x >= square.left && x < square.left + size &&
						   y >= square.top && y < square.top + size;
				CHECK(covered);
			}
		}
	}
	// A square frame needs no squares of its shorter side besides the whole frame
	int whole = 0;
	for (const Square &square : detection_squares(500, 500))
		whole += square.size == 500.0f;
	CHECK(whole == 1);
}

TEST_CASE("a detection is read from its anchor's values")
{
	std::vector<float> values(static_cast<size_t>(kDetectorAnchors) * kDetectorValues, 0.0f);
	std::vector<float> scores(kDetectorAnchors, -10.0f);
	// Anchor 0 sits at (4, 4) of the 128 × 128 input; the face is 20 × 30 and 2 pixels right of it
	scores[0] = 3.0f;
	values[0] = 2.0f;
	values[2] = 20.0f;
	values[3] = 30.0f;
	values[4] = -5.0f;
	values[5] = 1.0f;
	const Square square{100.0f, 50.0f, 256.0f};
	const std::vector<Detection> found = decode_detections(values.data(), scores.data(), square, 0.5f);
	REQUIRE(found.size() == 1);
	const Detection &face = found[0];
	CHECK(face.score == doctest::Approx(1.0 / (1.0 + std::exp(-3.0))));
	// Twice the input's scale in the square
	CHECK(face.left == doctest::Approx(100 + 2 * (4 + 2 - 10)));
	CHECK(face.right == doctest::Approx(100 + 2 * (4 + 2 + 10)));
	CHECK(face.top == doctest::Approx(50 + 2 * (4 - 15)));
	CHECK(face.bottom == doctest::Approx(50 + 2 * (4 + 15)));
	CHECK(face.keypoints[0].x == doctest::Approx(100 + 2 * (4 - 5)));
	CHECK(face.keypoints[0].y == doctest::Approx(50 + 2 * (4 + 1)));
}

TEST_CASE("overlapping detections become one, weighted by score")
{
	Detection a;
	a.score = 0.9f;
	a.left = 0;
	a.top = 0;
	a.right = 100;
	a.bottom = 100;
	Detection b = a;
	b.score = 0.3f;
	b.left = 10;
	b.right = 110;
	Detection far = a;
	far.score = 0.6f;
	far.left = 500;
	far.right = 600;
	const std::vector<Detection> merged = merge_detections({b, far, a});
	REQUIRE(merged.size() == 2);
	CHECK(merged[0].score == doctest::Approx(0.9));
	CHECK(merged[0].left == doctest::Approx((0.9 * 0 + 0.3 * 10) / 1.2));
	CHECK(merged[0].right == doctest::Approx((0.9 * 100 + 0.3 * 110) / 1.2));
	CHECK(merged[1].left == doctest::Approx(500));
}

TEST_CASE("the landmark model's region turns with the face")
{
	Detection face;
	face.left = 100;
	face.top = 100;
	face.right = 200;
	face.bottom = 220;
	// The person's left eye is lower on the screen: the head leans to the right of the picture
	face.keypoints[0] = {130, 140};
	face.keypoints[1] = {170, 180};
	const Roi roi = roi_from_detection(face);
	CHECK(roi.center.x == doctest::Approx(150));
	CHECK(roi.center.y == doctest::Approx(160));
	CHECK(roi.size == doctest::Approx(180));
	CHECK(roi.angle == doctest::Approx(std::atan2(40.0, 40.0)));

	const std::array<float, 6> m = roi_transform(roi, 256);
	const auto map = [&](float x, float y) {
		return Point{m[0] * x + m[1] * y + m[2], m[3] * x + m[4] * y + m[5]};
	};
	// The input's center is the region's center, and its right edge points along the eyes
	CHECK(map(128, 128).x == doctest::Approx(150));
	CHECK(map(128, 128).y == doctest::Approx(160));
	const Point right = map(256, 128);
	CHECK(right.x - 150 == doctest::Approx(90 * std::cos(roi.angle)));
	CHECK(right.y - 160 == doctest::Approx(90 * std::sin(roi.angle)));
}

TEST_CASE("the One Euro filter holds still values and follows moving ones")
{
	OneEuroFilter still(0.5f, 10.0f, 1.0f);
	float jitter = 0;
	for (int i = 0; i < 120; i++) {
		const float noisy = 1.0f + ((i % 2) ? 0.01f : -0.01f);
		jitter = std::fabs(still.filter(noisy, 1.0f / 60) - 1.0f);
	}
	CHECK(jitter < 0.002f);

	OneEuroFilter moving(0.5f, 10.0f, 1.0f);
	float value = 0;
	for (int i = 0; i <= 60; i++)
		value = moving.filter(static_cast<float>(i) / 60, 1.0f / 60);
	// One face size per second: within a few frames of the motion, and its rate is the motion's
	CHECK(value > 0.9f);
	CHECK(moving.rate() == doctest::Approx(1.0).epsilon(0.05));
}

TEST_CASE("the face mesh's triangles are the face's landmarks")
{
	std::vector<bool> used(468, false);
	for (uint16_t index : kFaceMesh) {
		REQUIRE(index < 468);
		used[index] = true;
	}
	// Every landmark of the face is a corner of some triangle, so the mesh reaches every part of the face
	for (size_t i = 0; i < used.size(); i++)
		CHECK(used[i]);
	for (const auto *outline : {kRightEye.data(), kLeftEye.data(), kLips.data()})
		CHECK(used[outline[0]]);
}
