// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "microphone-sync.hpp"

#include "lag-estimate.hpp"

#include <plugin-support.h>
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
// Overlapping parts of the recording, each correlated on its own
constexpr size_t kWindow = kRate * 4;
constexpr size_t kHop = kRate * 2;
// The windows' lags that count as the same
constexpr double kAgreement = kRate * 0.005;
// The summed correlation's peak against its RMS: speech heard by both measured 15, unrelated sound 4 to 6 (SYN-3)
constexpr double kClearPeak = 8.0;
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
	std::vector<float> total;
	std::vector<double> window_lags;
	for (size_t begin = first; begin + kWindow <= last; begin += kHop) {
		const size_t end = begin + kWindow;
		const double microphone_level = loudness(microphone, begin, end);
		const double camera_level = loudness(camera, begin, std::min(camera.size(), end + kMaxLag));
		if (microphone_level < kSilence || camera_level < kSilence) {
			obs_log(LOG_INFO, "microphone sync: window at %.0f s: too quiet (%.4f, %.4f)",
				static_cast<double>(begin - first) / kRate, microphone_level, camera_level);
			continue;
		}
		const std::vector<float> correlation = correlate(microphone, camera, begin, end, kMaxLag);
		if (correlation.empty())
			continue;
		const LagEstimate estimate = peak_of(correlation, kMaxLag);
		obs_log(LOG_INFO, "microphone sync: window at %.0f s: lag %.1f ms, clarity %.1f",
			static_cast<double>(begin - first) / kRate, estimate.lag * 1000 / kRate, estimate.clarity);
		window_lags.push_back(estimate.lag);
		if (total.empty())
			total.assign(correlation.size(), 0);
		for (size_t lag = 0; lag < correlation.size(); lag++)
			total[lag] += correlation[lag];
	}
	if (window_lags.empty())
		return {Result::Silence, 0};

	// The windows together: the same sound adds up at its lag while unrelated sound does not. Two windows must also
	// find that lag on their own (SYN-3).
	const LagEstimate estimate = peak_of(total, kMaxLag);
	const auto agreeing = std::count_if(window_lags.begin(), window_lags.end(),
					    [&](double lag) { return std::abs(lag - estimate.lag) <= kAgreement; });
	obs_log(LOG_INFO, "microphone sync: lag %.1f ms, clarity %.1f, %d of %d windows agree",
		estimate.lag * 1000 / kRate, estimate.clarity, static_cast<int>(agreeing),
		static_cast<int>(window_lags.size()));
	if (estimate.clarity < kClearPeak || agreeing < 2)
		return {Result::Unclear, 0};
	return {Result::Measured, static_cast<int64_t>(std::llround(estimate.lag * kBinNs))};
}

} // namespace bmagicam
