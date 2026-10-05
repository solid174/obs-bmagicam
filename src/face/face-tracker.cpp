// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "face-tracker.hpp"

#include "face-models.hpp"

#include <algorithm>
#include <cmath>

namespace bmagicam {

namespace {

// Up to four faces (FACE-5)
constexpr int kMaxFaces = 4;
// How sure the models must be: the detector to try a face, the landmark model to keep it
constexpr float kMinScore = 0.5f;
constexpr float kMinPresence = 0.5f;
// Seconds between searches for faces: often while there is none, now and then for more
constexpr double kSearchAlone = 0.25;
constexpr double kSearchMore = 1.0;
// Seconds a face takes to fade in when found and out when lost (FACE-4)
constexpr float kFadeIn = 0.25f;
constexpr float kFadeOut = 0.4f;
// After a gap this long, such as a hidden source, faces are looked for afresh
constexpr double kStale = 1.0;
// A detection that overlaps a face this much is that face
constexpr float kSameFace = 0.3f;
// The face's edges at the cheekbones, the person's right one first: their distance is the face's width at any turn
constexpr int kRightCheek = 234;
constexpr int kLeftCheek = 454;
// Landmark smoothing, in face sizes: still faces hold steady, moving ones are followed closely
constexpr float kMinCutoff = 0.5f;
constexpr float kBeta = 10.0f;
constexpr float kDerivativeCutoff = 1.0f;

std::shared_ptr<FaceModels> shared_models(const std::string &directory)
{
	static std::mutex mutex;
	static std::weak_ptr<FaceModels> cache;
	std::lock_guard lock(mutex);
	std::shared_ptr<FaceModels> models = cache.lock();
	if (!models) {
		models = FaceModels::load(directory);
		cache = models;
	}
	return models;
}

Detection box_of(const std::vector<Point> &points)
{
	Detection box;
	box.left = box.right = points.front().x;
	box.top = box.bottom = points.front().y;
	for (const Point &point : points) {
		box.left = std::min(box.left, point.x);
		box.right = std::max(box.right, point.x);
		box.top = std::min(box.top, point.y);
		box.bottom = std::max(box.bottom, point.y);
	}
	return box;
}

} // namespace

struct FaceTracker::Face {
	Roi roi;
	// Smoothed, and their motion per second, in pixels of the frame
	std::vector<Point> points;
	std::vector<Point> motion;
	std::vector<OneEuroFilter> filters;
	float weight = 0;
	bool lost = false;

	Face() : filters(static_cast<size_t>(kLandmarkCount) * 2, OneEuroFilter(kMinCutoff, kBeta, kDerivativeCutoff))
	{
	}

	void smooth(const std::vector<Point> &found, float seconds)
	{
		const float size = std::max(roi.size, 1.0f);
		points.resize(found.size());
		motion.resize(found.size());
		for (size_t i = 0; i < found.size(); i++) {
			points[i].x = filters[2 * i].filter(found[i].x / size, seconds) * size;
			points[i].y = filters[2 * i + 1].filter(found[i].y / size, seconds) * size;
			motion[i] = {filters[2 * i].rate() * size, filters[2 * i + 1].rate() * size};
		}
	}
};

FaceTracker::FaceTracker(std::string model_directory) : model_directory_(std::move(model_directory)) {}

FaceTracker::~FaceTracker()
{
	{
		std::lock_guard lock(mutex_);
		stopping_ = true;
	}
	wake_.notify_all();
	if (thread_.joinable())
		thread_.join();
}

bool FaceTracker::wants_frame() const
{
	std::lock_guard lock(mutex_);
	return !has_frame_ && state_ != State::Failed;
}

void FaceTracker::track(std::vector<uint8_t> pixels, int width, int height, double seconds)
{
	{
		std::lock_guard lock(mutex_);
		if (state_ == State::Failed)
			return;
		waiting_ = {std::move(pixels), width, height, seconds};
		has_frame_ = true;
		if (!thread_.joinable())
			thread_ = std::thread(&FaceTracker::run, this);
	}
	wake_.notify_one();
}

std::vector<TrackedFace> FaceTracker::faces() const
{
	std::lock_guard lock(mutex_);
	return published_;
}

FaceTracker::State FaceTracker::state() const
{
	std::lock_guard lock(mutex_);
	return state_;
}

void FaceTracker::run()
{
	for (;;) {
		Frame frame;
		{
			std::unique_lock lock(mutex_);
			wake_.wait(lock, [this] { return stopping_ || has_frame_; });
			if (stopping_)
				return;
			frame = std::move(waiting_);
			has_frame_ = false;
		}
		if (!models_) {
			models_ = shared_models(model_directory_);
			std::lock_guard lock(mutex_);
			state_ = models_ ? State::Running : State::Failed;
			if (!models_)
				return;
		}
		const FaceImage image(std::move(frame.pixels), frame.width, frame.height);
		step(image, frame.seconds);
	}
}

void FaceTracker::step(const FaceImage &image, double seconds)
{
	float elapsed = static_cast<float>(seconds - last_seconds_);
	if (image.width() != width_ || image.height() != height_ || elapsed > kStale || elapsed < 0) {
		faces_.clear();
		last_search_ = -1e9;
		width_ = image.width();
		height_ = image.height();
		elapsed = 0;
	}
	last_seconds_ = seconds;

	int active = 0;
	for (auto &face : faces_) {
		if (!face->lost && !follow(image, *face, elapsed))
			face->lost = true;
		if (face->lost) {
			face->weight = std::max(face->weight - elapsed / kFadeOut, 0.0f);
		} else {
			face->weight = std::min(face->weight + elapsed / kFadeIn, 1.0f);
			active++;
		}
	}
	// Two followed faces that end up on the same face: the newer one goes
	for (size_t i = 0; i < faces_.size(); i++)
		for (size_t j = i + 1; j < faces_.size(); j++)
			if (!faces_[i]->lost && !faces_[j]->lost &&
			    box_overlap(box_of(faces_[i]->points), box_of(faces_[j]->points)) > kSameFace) {
				faces_[j]->lost = true;
				active--;
			}
	faces_.erase(std::remove_if(faces_.begin(), faces_.end(),
				    [](const auto &face) { return face->lost && face->weight <= 0; }),
		     faces_.end());

	if (active < kMaxFaces && seconds - last_search_ >= (active == 0 ? kSearchAlone : kSearchMore)) {
		last_search_ = seconds;
		search(image, active);
	}
	publish(image.width(), image.height(), seconds);
}

bool FaceTracker::follow(const FaceImage &image, Face &face, float seconds)
{
	std::vector<Point> points;
	if (models_->landmarks(image, face.roi, points) < kMinPresence)
		return false;
	// The next frame looks where the face is now, from the landmarks as found, as MediaPipe does
	face.roi = roi_from_landmarks(points);
	face.smooth(points, seconds);
	return true;
}

void FaceTracker::search(const FaceImage &image, int active)
{
	std::vector<Detection> found;
	for (const Square &square : detection_squares(image.width(), image.height())) {
		std::vector<Detection> detections = models_->detect(image, square, kMinScore);
		found.insert(found.end(), detections.begin(), detections.end());
	}
	for (const Detection &detection : merge_detections(std::move(found))) {
		// A face already followed, or one fading out that is found again
		Face *known = nullptr;
		for (auto &face : faces_)
			if (box_overlap(detection, box_of(face->points)) > kSameFace)
				known = face.get();
		if ((known && !known->lost) || active >= kMaxFaces)
			continue;
		std::vector<Point> points;
		if (models_->landmarks(image, roi_from_detection(detection), points) < kMinPresence)
			continue;
		if (!known) {
			faces_.push_back(std::make_unique<Face>());
			known = faces_.back().get();
		}
		known->lost = false;
		known->roi = roi_from_landmarks(points);
		for (OneEuroFilter &filter : known->filters)
			filter.reset();
		known->smooth(points, 0);
		active++;
	}
}

void FaceTracker::publish(int width, int height, double seconds)
{
	std::vector<TrackedFace> faces;
	for (const auto &face : faces_) {
		if (face->weight <= 0)
			continue;
		TrackedFace tracked;
		tracked.weight = face->weight;
		tracked.seconds = seconds;
		const auto w = static_cast<float>(width);
		const auto h = static_cast<float>(height);
		tracked.landmarks.reserve(face->points.size());
		tracked.motion.reserve(face->points.size());
		for (size_t i = 0; i < face->points.size(); i++) {
			// A lost face stays where it was last seen while it fades out
			const Point moving = face->lost ? Point{} : face->motion[i];
			tracked.landmarks.push_back({face->points[i].x / w, face->points[i].y / h});
			tracked.motion.push_back({moving.x / w, moving.y / h});
		}
		const Point &right = face->points[kRightCheek];
		const Point &left = face->points[kLeftCheek];
		tracked.width = std::hypot(left.x - right.x, left.y - right.y) / static_cast<float>(width);
		faces.push_back(std::move(tracked));
	}
	std::lock_guard lock(mutex_);
	published_ = std::move(faces);
}

} // namespace bmagicam
