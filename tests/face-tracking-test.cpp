// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "face/face-models.hpp"
#include "face/face-tracker.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace bmagicam;

namespace {

// tests/data/face.ppm: a NASA portrait of Kalpana Chawla (public domain), 240 × 300, the face between about
// (65, 35) and (170, 180)
struct Picture {
	std::vector<uint8_t> rgba;
	int width = 0;
	int height = 0;
};

Picture read_picture()
{
	std::ifstream file(FACE_IMAGE, std::ios::binary);
	std::string magic;
	int maximum = 0;
	Picture picture;
	file >> magic >> picture.width >> picture.height >> maximum;
	file.get();
	REQUIRE(magic == "P6");
	std::vector<char> rgb(static_cast<size_t>(picture.width) * picture.height * 3);
	file.read(rgb.data(), static_cast<std::streamsize>(rgb.size()));
	REQUIRE(file.good());
	for (size_t i = 0; i < rgb.size(); i += 3) {
		picture.rgba.insert(picture.rgba.end(), {static_cast<uint8_t>(rgb[i]), static_cast<uint8_t>(rgb[i + 1]),
							 static_cast<uint8_t>(rgb[i + 2]), 255});
	}
	return picture;
}

bool inside_face(const Point &point)
{
	return point.x > 60 && point.x < 175 && point.y > 30 && point.y < 185;
}

} // namespace

TEST_CASE("the models find the face and its landmarks")
{
	const std::shared_ptr<FaceModels> models = FaceModels::load(MODELS_DIR);
	REQUIRE(models);
	Picture picture = read_picture();
	const FaceImage image(std::move(picture.rgba), picture.width, picture.height);

	const std::vector<Detection> found =
		merge_detections(models->detect(image, detection_squares(image.width(), image.height())[0], 0.5f));
	REQUIRE(found.size() == 1);
	const Detection &face = found[0];
	CHECK(face.score > 0.8f);
	CHECK(inside_face({face.left, face.top}));
	CHECK(inside_face({face.right, face.bottom}));

	std::vector<Point> points;
	CHECK(models->landmarks(image, roi_from_detection(face), points) > 0.9f);
	REQUIRE(points.size() == static_cast<size_t>(kLandmarkCount));
	for (int index : kFaceOutline)
		CHECK(inside_face(points[index]));
	// Eyes above the mouth, the person's right eye on the picture's left
	CHECK(points[33].x < points[263].x);
	CHECK(points[33].y < points[13].y);

	// Where nothing is a face, the landmark model says so
	const Roi elsewhere{{30, 270}, 40, 0};
	CHECK(models->landmarks(image, elsewhere, points) < 0.5f);
}

TEST_CASE("the tracker follows the face and fades it in")
{
	FaceTracker tracker(MODELS_DIR);
	const Picture picture = read_picture();
	std::vector<TrackedFace> faces;
	double seconds = 0;
	for (int frame = 0; frame < 300 && (faces.empty() || faces[0].weight < 1.0f); frame++) {
		while (!tracker.wants_frame())
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		tracker.track(picture.rgba, picture.width, picture.height, seconds);
		seconds += 1.0 / 30;
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		faces = tracker.faces();
	}
	CHECK(tracker.state() == FaceTracker::State::Running);
	REQUIRE(faces.size() == 1);
	CHECK(faces[0].weight == doctest::Approx(1.0f));
	CHECK(faces[0].width > 0.3f);
	CHECK(faces[0].width < 0.6f);
	for (int index : kFaceOutline)
		CHECK(inside_face(
			{faces[0].landmarks[index].x * picture.width, faces[0].landmarks[index].y * picture.height}));
}

TEST_CASE("the tracker reports models that cannot load")
{
	FaceTracker tracker("no such directory");
	const Picture picture = read_picture();
	tracker.track(picture.rgba, picture.width, picture.height, 0);
	for (int i = 0; i < 500 && tracker.state() == FaceTracker::State::Starting; i++)
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	CHECK(tracker.state() == FaceTracker::State::Failed);
	CHECK_FALSE(tracker.wants_frame());
	CHECK(tracker.faces().empty());
}
