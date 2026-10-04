// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "microphone-sync.hpp"

#include "lag-estimate.hpp"

#include <util/platform.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

namespace bmagicam {

namespace {

// Both sources are mixed to mono and binned at 8 kHz on OBS's timeline
constexpr int kRate = 8000;
constexpr uint64_t kBinNs = 1'000'000'000 / kRate;
// Room before the start, and after the end for the iPhone's later copy
constexpr uint64_t kLeadNs = 2'000'000'000;
constexpr uint64_t kTailNs = 3'000'000'000;
// The lag is searched within ±1.5 s
constexpr int kMaxLag = kRate * 3 / 2;
// Overlapping parts of the recording, each measured on its own; two in a row must agree
constexpr size_t kWindow = kRate * 4;
constexpr size_t kHop = kRate * 2;
constexpr double kAgreement = kRate * 0.005;
// A peak this many times higher than the rest is clear (SYN-3)
constexpr double kClearPeak = 2.0;
// Quieter than about -60 dBFS is silence
constexpr double kSilence = 0.001;

} // namespace

// What one source was heard to say, in 8 kHz bins from the start time
struct MicrophoneSync::Capture {
	uint64_t start_ns = 0;
	std::mutex mutex;
	std::vector<float> sum;
	std::vector<uint16_t> count;

	explicit Capture(uint64_t start) : start_ns(start)
	{
		const size_t bins =
			(kLeadNs + static_cast<uint64_t>(kListenTime.count()) * 1'000'000'000 + kTailNs) / kBinNs;
		sum.assign(bins, 0);
		count.assign(bins, 0);
	}

	void add(const struct audio_data *audio)
	{
		const uint32_t rate = audio_output_get_sample_rate(obs_get_audio());
		const size_t channels = audio_output_get_channels(obs_get_audio());
		if (rate == 0 || channels == 0)
			return;
		std::lock_guard lock(mutex);
		for (uint32_t frame = 0; frame < audio->frames; frame++) {
			const uint64_t time = audio->timestamp + static_cast<uint64_t>(frame) * 1'000'000'000 / rate;
			if (time < start_ns)
				continue;
			const size_t bin = static_cast<size_t>((time - start_ns) / kBinNs);
			if (bin >= sum.size())
				break;
			float mono = 0;
			for (size_t channel = 0; channel < channels && audio->data[channel]; channel++)
				mono += reinterpret_cast<const float *>(audio->data[channel])[frame];
			sum[bin] += mono / static_cast<float>(channels);
			count[bin]++;
		}
	}

	std::vector<float> signal()
	{
		std::lock_guard lock(mutex);
		std::vector<float> result(sum.size(), 0);
		for (size_t bin = 0; bin < sum.size(); bin++)
			result[bin] = count[bin] ? sum[bin] / count[bin] : 0;
		return result;
	}
};

MicrophoneSync::MicrophoneSync(obs_source_t *camera, obs_source_t *microphone)
	: camera_(obs_source_get_weak_source(camera)),
	  microphone_(obs_source_get_weak_source(microphone))
{
	const uint64_t start = os_gettime_ns() - kLeadNs;
	camera_capture_ = std::make_unique<Capture>(start);
	microphone_capture_ = std::make_unique<Capture>(start);
	obs_source_add_audio_capture_callback(camera, captured, camera_capture_.get());
	obs_source_add_audio_capture_callback(microphone, captured, microphone_capture_.get());
	listening_ = true;
}

MicrophoneSync::~MicrophoneSync()
{
	stop();
	obs_weak_source_release(camera_);
	obs_weak_source_release(microphone_);
}

void MicrophoneSync::captured(void *param, obs_source_t *, const struct audio_data *audio, bool)
{
	// Muted sources are heard too: an iPhone muted in OBS, with the computer's microphone used instead, is the usual case
	static_cast<Capture *>(param)->add(audio);
}

void MicrophoneSync::stop()
{
	if (!listening_)
		return;
	listening_ = false;
	if (obs_source_t *camera = obs_weak_source_get_source(camera_)) {
		obs_source_remove_audio_capture_callback(camera, captured, camera_capture_.get());
		obs_source_release(camera);
	}
	if (obs_source_t *microphone = obs_weak_source_get_source(microphone_)) {
		obs_source_remove_audio_capture_callback(microphone, captured, microphone_capture_.get());
		obs_source_release(microphone);
	}
}

MicrophoneSync::Outcome MicrophoneSync::finish()
{
	stop();
	const std::vector<float> microphone = microphone_capture_->signal();
	const std::vector<float> camera = camera_capture_->signal();

	const size_t first = kLeadNs / kBinNs;
	const size_t last = first + static_cast<size_t>(kListenTime.count()) * kRate;
	bool heard = false;
	bool have_previous = false;
	double previous = 0;
	for (size_t begin = first; begin + kWindow <= last; begin += kHop) {
		const size_t end = begin + kWindow;
		if (loudness(microphone, begin, end) < kSilence ||
		    loudness(camera, begin, std::min(camera.size(), end + kMaxLag)) < kSilence) {
			have_previous = false;
			continue;
		}
		heard = true;
		const LagEstimate estimate = estimate_lag(microphone, camera, begin, end, kMaxLag);
		if (estimate.clarity < kClearPeak) {
			have_previous = false;
			continue;
		}
		if (have_previous && std::abs(estimate.lag - previous) <= kAgreement) {
			const double lag = (estimate.lag + previous) / 2;
			return {Result::Measured, static_cast<int64_t>(std::llround(lag * kBinNs))};
		}
		previous = estimate.lag;
		have_previous = true;
	}
	return {heard ? Result::Unclear : Result::Silence, 0};
}

} // namespace bmagicam
