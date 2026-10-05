// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include "face-geometry.hpp"

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace bmagicam {

class FaceImage;
class FaceModels;

// A face as the mask draws it
struct TrackedFace {
	// The smoothed landmarks, 0–1 across the frame's width and height
	std::vector<Point> landmarks;
	// Fades in when the face is found and out when it is lost, 0–1 (FACE-4)
	float weight = 0;
	// The face's width from cheek to cheek, as a share of the frame's width
	float width = 0;
};

// Follows up to four faces in frames handed over by the Beautify filter, on a thread of its own
// (docs/architecture.md, "Face tracking"). The models load with the first frame (PWR-1).
class FaceTracker {
public:
	explicit FaceTracker(std::string model_directory);
	~FaceTracker();
	FaceTracker(const FaceTracker &) = delete;
	FaceTracker &operator=(const FaceTracker &) = delete;

	// Whether a frame handed over now would be tracked next: none is waiting already
	bool wants_frame() const;
	// Hands over a frame of 8-bit RGBA pixels taken at the time, in seconds. A frame still waiting is replaced.
	void track(std::vector<uint8_t> pixels, int width, int height, double seconds);
	// The faces as of the latest tracked frame
	std::vector<TrackedFace> faces() const;
	// The models load with the first frame; when they cannot, the filter masks by color alone
	enum class State { Starting, Running, Failed };
	State state() const;

private:
	struct Face;
	struct Frame {
		std::vector<uint8_t> pixels;
		int width = 0;
		int height = 0;
		double seconds = 0;
	};

	void run();
	void step(const FaceImage &image, double seconds);
	void search(const FaceImage &image, int active);
	bool follow(const FaceImage &image, Face &face, float seconds);
	void publish(int width, int height);

	const std::string model_directory_;
	std::shared_ptr<FaceModels> models_;

	mutable std::mutex mutex_;
	std::condition_variable wake_;
	Frame waiting_;
	bool has_frame_ = false;
	bool stopping_ = false;
	State state_ = State::Starting;
	std::vector<TrackedFace> published_;
	std::thread thread_;

	// Only used on the tracker's thread
	std::vector<std::unique_ptr<Face>> faces_;
	double last_seconds_ = 0;
	double last_search_ = -1e9;
	int width_ = 0;
	int height_ = 0;
};

} // namespace bmagicam
