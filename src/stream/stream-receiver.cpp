// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "stream-receiver.hpp"

#include "media-clock.hpp"

#include <plugin-support.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace bmagicam {

namespace {

constexpr AVRational kNanoseconds = {1, 1'000'000'000};
constexpr int kAudioRate = 48000;
// Larger differences between the audio and the timeline are closed with a jump
constexpr int64_t kAudioResyncNs = 20'000'000;
// Smaller ones by stretching or squeezing the audio by at most 0.5 %
constexpr int kAudioMaxCorrection = kAudioRate / 200;
// A video packet is decoded this long before its presentation time
constexpr int64_t kDecodeLeadNs = 30'000'000;
// Safety net for a video thread that stopped keeping up: ten seconds at 60 fps
constexpr size_t kMaxQueuedPackets = 600;
// A frame handed over this much after its presentation time counts as late
constexpr int64_t kLateNs = 8'000'000;
// A frame this far behind is skipped: it would only be shown late
constexpr int64_t kSkipNs = 50'000'000;
constexpr uint64_t kStatsIntervalNs = 10'000'000'000;
constexpr int kMaxLoggedErrors = 5;

#if defined(__APPLE__)
constexpr AVHWDeviceType kHardwareTypes[] = {AV_HWDEVICE_TYPE_VIDEOTOOLBOX};
#elif defined(_WIN32)
constexpr AVHWDeviceType kHardwareTypes[] = {AV_HWDEVICE_TYPE_D3D11VA};
#else
constexpr AVHWDeviceType kHardwareTypes[] = {AV_HWDEVICE_TYPE_VAAPI, AV_HWDEVICE_TYPE_CUDA};
#endif

std::string error_text(int error)
{
	char text[AV_ERROR_MAX_STRING_SIZE] = {};
	av_strerror(error, text, sizeof(text));
	return text;
}

int64_t to_ns(int64_t value, AVRational time_base)
{
	return av_rescale_q(value, time_base, kNanoseconds);
}

} // namespace

// One connection from the phone, from listening until the stream ends. Demuxing and audio run on the receiving
// thread, video decoding and handing over on a thread of its own.
class StreamReceiver::Connection {
public:
	explicit Connection(StreamReceiver &receiver) : receiver_(receiver) {}
	~Connection();

	Connection(const Connection &) = delete;
	Connection &operator=(const Connection &) = delete;

	// Waits for the phone to connect, then receives until the stream ends. Returns false if it could not listen.
	bool run();

private:
	struct VideoDecoder {
		AVCodecContext *codec = nullptr;
		int stream = -1;
		AVPixelFormat hardware_format = AV_PIX_FMT_NONE;
		const char *hardware_name = nullptr;
	};

	struct AudioDecoder {
		AVCodecContext *codec = nullptr;
		int stream = -1;
		SwrContext *resampler = nullptr;
	};

	struct DecodedFrame {
		AVFrame *frame;
		int64_t pts_ns;
	};

	static int interrupt(void *opaque);
	static AVPixelFormat get_format(AVCodecContext *context, const AVPixelFormat *formats);

	bool open();
	void receive();
	void handle_packet(const AVPacket *packet, uint64_t arrival);
	AVCodecContext *open_decoder(const AVStream *stream);
	void use_hardware(AVCodecContext *context);

	void decode_audio(const AVPacket *packet);
	void output_audio(const AVFrame *frame);

	void video_loop();
	void decode_video(const AVPacket *packet, std::deque<DecodedFrame> &frames);
	int64_t video_pts_ns(const AVPacket *packet) const;
	void stop_video();

	void log_error(const char *what, int error);
	void log_stats(uint64_t now);

	StreamReceiver &receiver_;
	AVFormatContext *format_ = nullptr;
	VideoDecoder video_;
	AudioDecoder audio_;

	// Shared by the receiving thread and the video thread
	std::mutex mutex_;
	std::condition_variable video_cv_;
	MediaClock clock_;
	std::deque<AVPacket *> video_packets_;
	bool video_stopping_ = false;
	std::thread video_thread_;

	bool audio_started_ = false;
	uint64_t audio_start_ = 0;
	uint64_t audio_samples_ = 0;
	std::vector<float> audio_buffer_[2];

	bool logged_first_frame_ = false;
	std::atomic<int> logged_errors_ = 0;
	uint64_t stats_start_ = 0;
	uint64_t stats_bytes_ = 0;
	std::atomic<uint32_t> stats_frames_ = 0;
	std::atomic<uint32_t> stats_late_ = 0;
	std::atomic<uint32_t> stats_dropped_ = 0;
};

StreamReceiver::Connection::~Connection()
{
	stop_video();
	for (AVPacket *&packet : video_packets_)
		av_packet_free(&packet);
	avcodec_free_context(&video_.codec);
	avcodec_free_context(&audio_.codec);
	swr_free(&audio_.resampler);
	avformat_close_input(&format_);
}

int StreamReceiver::Connection::interrupt(void *opaque)
{
	return static_cast<Connection *>(opaque)->receiver_.stopping_ ? 1 : 0;
}

bool StreamReceiver::Connection::run()
{
	if (!open())
		return false;

	obs_log(LOG_INFO, "phone connected on port %d", receiver_.settings_.port);
	receiver_.sink_.stream_started();
	video_thread_ = std::thread(&Connection::video_loop, this);
	receive();
	stop_video();
	receiver_.sink_.stream_ended();
	return true;
}

bool StreamReceiver::Connection::open()
{
	format_ = avformat_alloc_context();
	if (!format_)
		return false;
	format_->interrupt_callback.callback = interrupt;
	format_->interrupt_callback.opaque = this;

	// Blocks until the phone connects. The latency is in microseconds.
	const Settings &settings = receiver_.settings_;
	const std::string url = "srt://0.0.0.0:" + std::to_string(settings.port) +
				"?mode=listener&transtype=live&latency=" + std::to_string(settings.latency_ms * 1000LL);
	const int result = avformat_open_input(&format_, url.c_str(), av_find_input_format("mpegts"), nullptr);
	if (result < 0) {
		// avformat_open_input has freed the context
		if (!receiver_.stopping_)
			obs_log(LOG_WARNING, "cannot listen on port %d: %s", settings.port, error_text(result).c_str());
		return false;
	}
	return true;
}

void StreamReceiver::Connection::receive()
{
	AVPacket *packet = av_packet_alloc();
	stats_start_ = receiver_.clock_();

	int result = 0;
	while (!receiver_.stopping_) {
		result = av_read_frame(format_, packet);
		if (result < 0)
			break;

		const uint64_t arrival = receiver_.clock_();
		handle_packet(packet, arrival);
		av_packet_unref(packet);

		if (arrival - stats_start_ >= kStatsIntervalNs)
			log_stats(arrival);
	}
	av_packet_free(&packet);

	if (!receiver_.stopping_)
		obs_log(LOG_INFO, "stream ended: %s",
			result == AVERROR_EOF ? "the phone stopped streaming" : error_text(result).c_str());
}

void StreamReceiver::Connection::handle_packet(const AVPacket *packet, uint64_t arrival)
{
	const AVStream *stream = format_->streams[packet->stream_index];
	const AVMediaType type = stream->codecpar->codec_type;
	const bool video = type == AVMEDIA_TYPE_VIDEO;
	if (!video && type != AVMEDIA_TYPE_AUDIO)
		return;

	// Decoders are made from the program map alone, without probing the stream first, which would take seconds
	int &stream_index = video ? video_.stream : audio_.stream;
	AVCodecContext *&codec = video ? video_.codec : audio_.codec;
	if (stream_index < 0) {
		stream_index = packet->stream_index;
		codec = open_decoder(stream);
	}
	if (packet->stream_index != stream_index || !codec)
		return;

	stats_bytes_ += static_cast<uint64_t>(packet->size);

	{
		std::lock_guard lock(mutex_);
		if (packet->pts != AV_NOPTS_VALUE)
			clock_.on_packet(video ? MediaClock::Stream::Video : MediaClock::Stream::Audio,
					 to_ns(packet->pts, stream->time_base), static_cast<int64_t>(arrival));
		if (video) {
			if (video_packets_.size() >= kMaxQueuedPackets) {
				av_packet_free(&video_packets_.front());
				video_packets_.pop_front();
				stats_dropped_++;
			}
			video_packets_.push_back(av_packet_clone(packet));
		}
	}
	video_cv_.notify_one();

	if (!video)
		decode_audio(packet);
}

AVCodecContext *StreamReceiver::Connection::open_decoder(const AVStream *stream)
{
	const AVCodecID codec_id = stream->codecpar->codec_id;
	const AVCodec *codec = avcodec_find_decoder(codec_id);
	if (!codec) {
		obs_log(LOG_ERROR, "no decoder for %s", avcodec_get_name(codec_id));
		return nullptr;
	}

	AVCodecContext *context = avcodec_alloc_context3(codec);
	avcodec_parameters_to_context(context, stream->codecpar);
	context->pkt_timebase = stream->time_base;

	if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
		context->flags |= AV_CODEC_FLAG_LOW_DELAY;
		if (receiver_.settings_.hardware_decoding)
			use_hardware(context);
		if (!context->hw_device_ctx) {
			// Frame threads would hold frames back; slices keep each frame on time
			context->thread_count = 0;
			context->thread_type = FF_THREAD_SLICE;
		}
	}

	const int result = avcodec_open2(context, codec, nullptr);
	if (result < 0) {
		obs_log(LOG_ERROR, "cannot open the %s decoder: %s", codec->name, error_text(result).c_str());
		avcodec_free_context(&context);
	}
	return context;
}

void StreamReceiver::Connection::use_hardware(AVCodecContext *context)
{
	for (const AVHWDeviceType type : kHardwareTypes) {
		const AVCodecHWConfig *config = nullptr;
		for (int i = 0; (config = avcodec_get_hw_config(context->codec, i)) != nullptr; i++) {
			if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) && config->device_type == type)
				break;
		}
		if (!config)
			continue;

		AVBufferRef *device = nullptr;
		if (av_hwdevice_ctx_create(&device, type, nullptr, nullptr, 0) < 0)
			continue;

		context->hw_device_ctx = device;
		context->opaque = &video_;
		context->get_format = get_format;
		video_.hardware_format = config->pix_fmt;
		video_.hardware_name = av_hwdevice_get_type_name(type);
		return;
	}
}

AVPixelFormat StreamReceiver::Connection::get_format(AVCodecContext *context, const AVPixelFormat *formats)
{
	const auto decoder = static_cast<const VideoDecoder *>(context->opaque);
	for (const AVPixelFormat *format = formats; *format != AV_PIX_FMT_NONE; format++) {
		if (*format == decoder->hardware_format)
			return *format;
	}

	// The hardware cannot decode this stream, so it is decoded in software
	for (const AVPixelFormat *format = formats; *format != AV_PIX_FMT_NONE; format++) {
		if (!(av_pix_fmt_desc_get(*format)->flags & AV_PIX_FMT_FLAG_HWACCEL))
			return *format;
	}
	return AV_PIX_FMT_NONE;
}

void StreamReceiver::Connection::decode_audio(const AVPacket *packet)
{
	int result = avcodec_send_packet(audio_.codec, packet);
	if (result < 0) {
		log_error("cannot decode audio", result);
		return;
	}

	AVFrame *frame = av_frame_alloc();
	while ((result = avcodec_receive_frame(audio_.codec, frame)) >= 0) {
		output_audio(frame);
		av_frame_unref(frame);
	}
	if (result != AVERROR(EAGAIN) && result != AVERROR_EOF)
		log_error("cannot decode audio", result);
	av_frame_free(&frame);
}

void StreamReceiver::Connection::output_audio(const AVFrame *frame)
{
	if (!audio_.resampler) {
		AVChannelLayout stereo;
		av_channel_layout_default(&stereo, 2);
		int result = swr_alloc_set_opts2(&audio_.resampler, &stereo, AV_SAMPLE_FMT_FLTP, kAudioRate,
						 &frame->ch_layout, static_cast<AVSampleFormat>(frame->format),
						 frame->sample_rate, 0, nullptr);
		av_channel_layout_uninit(&stereo);
		if (result >= 0) {
			// Resample from the start, so that stretching the audio later on does not reset the resampler
			av_opt_set_int(audio_.resampler, "flags", SWR_FLAG_RESAMPLE, 0);
			result = swr_init(audio_.resampler);
		}
		if (result < 0) {
			log_error("cannot convert the audio", result);
			swr_free(&audio_.resampler);
			return;
		}
	}

	const int64_t pts = frame->best_effort_timestamp;
	if (pts == AV_NOPTS_VALUE)
		return;

	int64_t timestamp = 0;
	{
		std::lock_guard lock(mutex_);
		if (!clock_.settled())
			return;
		timestamp = clock_.to_obs(to_ns(pts, audio_.codec->pkt_timebase));
	}

	// The audio is handed over without gaps, and kept on the timeline by stretching or squeezing it slightly
	const auto next = static_cast<int64_t>(audio_start_ + audio_samples_ * 1'000'000'000ULL / kAudioRate);
	const int64_t error = timestamp - next;
	if (!audio_started_ || std::llabs(error) > kAudioResyncNs) {
		audio_started_ = true;
		audio_start_ = static_cast<uint64_t>(timestamp);
		audio_samples_ = 0;
		swr_set_compensation(audio_.resampler, 0, 0);
	} else {
		const auto correction = static_cast<int>(error * kAudioRate / 1'000'000'000);
		swr_set_compensation(audio_.resampler,
				     std::clamp(correction, -kAudioMaxCorrection, kAudioMaxCorrection), kAudioRate);
	}

	const int capacity = swr_get_out_samples(audio_.resampler, frame->nb_samples);
	if (capacity <= 0)
		return;
	for (std::vector<float> &buffer : audio_buffer_)
		buffer.resize(std::max(buffer.size(), static_cast<size_t>(capacity)));

	uint8_t *out[2] = {reinterpret_cast<uint8_t *>(audio_buffer_[0].data()),
			   reinterpret_cast<uint8_t *>(audio_buffer_[1].data())};
	const int samples = swr_convert(audio_.resampler, out, capacity,
					const_cast<const uint8_t **>(frame->extended_data), frame->nb_samples);
	if (samples <= 0)
		return;

	const float *planes[2] = {audio_buffer_[0].data(), audio_buffer_[1].data()};
	receiver_.sink_.stream_audio(planes, static_cast<uint32_t>(samples),
				     audio_start_ + audio_samples_ * 1'000'000'000ULL / kAudioRate);
	audio_samples_ += static_cast<uint64_t>(samples);
}

int64_t StreamReceiver::Connection::video_pts_ns(const AVPacket *packet) const
{
	return to_ns(packet->pts, video_.codec->pkt_timebase);
}

void StreamReceiver::Connection::video_loop()
{
	std::deque<DecodedFrame> frames;
	std::unique_lock lock(mutex_);
	while (!video_stopping_) {
		const auto now = static_cast<int64_t>(receiver_.clock_());

		// Hand over the next frame when it is due; nothing is presented before the map has settled
		if (!frames.empty()) {
			const DecodedFrame next = frames.front();
			const bool settled = clock_.settled();
			const int64_t due = clock_.to_obs(next.pts_ns);
			if (!settled || due <= now) {
				frames.pop_front();
				lock.unlock();
				if (settled && now - due > kSkipNs) {
					stats_dropped_++;
				} else if (settled) {
					if (now - due > kLateNs)
						stats_late_++;
					stats_frames_++;
					receiver_.sink_.stream_video(*next.frame, static_cast<uint64_t>(due));
				}
				AVFrame *frame = next.frame;
				av_frame_free(&frame);
				lock.lock();
				continue;
			}
		}

		// Decode the next packet shortly before it is due
		if (!video_packets_.empty()) {
			AVPacket *packet = video_packets_.front();
			if (!clock_.settled() || packet->pts == AV_NOPTS_VALUE ||
			    clock_.to_obs(video_pts_ns(packet)) - kDecodeLeadNs <= now) {
				video_packets_.pop_front();
				lock.unlock();
				decode_video(packet, frames);
				av_packet_free(&packet);
				lock.lock();
				continue;
			}
		}

		int64_t wake = INT64_MAX;
		if (!frames.empty())
			wake = clock_.to_obs(frames.front().pts_ns);
		if (!video_packets_.empty())
			wake = std::min(wake, clock_.to_obs(video_pts_ns(video_packets_.front())) - kDecodeLeadNs);
		if (wake == INT64_MAX)
			video_cv_.wait(lock);
		else
			video_cv_.wait_for(lock, std::chrono::nanoseconds(wake - now));
	}
	lock.unlock();

	for (DecodedFrame &frame : frames)
		av_frame_free(&frame.frame);
}

void StreamReceiver::Connection::decode_video(const AVPacket *packet, std::deque<DecodedFrame> &frames)
{
	int result = avcodec_send_packet(video_.codec, packet);
	if (result < 0) {
		log_error("cannot decode video", result);
		return;
	}

	for (;;) {
		AVFrame *frame = av_frame_alloc();
		result = avcodec_receive_frame(video_.codec, frame);
		if (result < 0) {
			av_frame_free(&frame);
			if (result != AVERROR(EAGAIN) && result != AVERROR_EOF)
				log_error("cannot decode video", result);
			return;
		}

		const bool hardware = frame->format == video_.hardware_format;
		if (hardware) {
			AVFrame *software = av_frame_alloc();
			result = av_hwframe_transfer_data(software, frame, 0);
			if (result >= 0)
				av_frame_copy_props(software, frame);
			av_frame_free(&frame);
			if (result < 0) {
				log_error("cannot copy a frame from the video decoder", result);
				av_frame_free(&software);
				continue;
			}
			frame = software;
		}

		if (!logged_first_frame_) {
			logged_first_frame_ = true;
			obs_log(LOG_INFO, "receiving %dx%d %s, decoding with %s", frame->width, frame->height,
				avcodec_get_name(video_.codec->codec_id), hardware ? video_.hardware_name : "software");
		}

		const int64_t pts = frame->best_effort_timestamp;
		if (pts == AV_NOPTS_VALUE) {
			av_frame_free(&frame);
			continue;
		}
		frames.push_back({frame, to_ns(pts, video_.codec->pkt_timebase)});
	}
}

void StreamReceiver::Connection::stop_video()
{
	{
		std::lock_guard lock(mutex_);
		video_stopping_ = true;
	}
	video_cv_.notify_all();
	if (video_thread_.joinable())
		video_thread_.join();
}

void StreamReceiver::Connection::log_error(const char *what, int error)
{
	if (logged_errors_++ < kMaxLoggedErrors)
		obs_log(LOG_WARNING, "%s: %s", what, error_text(error).c_str());
}

void StreamReceiver::Connection::log_stats(uint64_t now)
{
	const double seconds = static_cast<double>(now - stats_start_) / 1e9;
	obs_log(LOG_INFO, "stream: %.1f fps, %.1f Mb/s, %u late and %u dropped frames",
		static_cast<double>(stats_frames_.exchange(0)) / seconds,
		static_cast<double>(stats_bytes_) * 8 / seconds / 1e6, stats_late_.exchange(0),
		stats_dropped_.exchange(0));
	stats_start_ = now;
	stats_bytes_ = 0;
}

StreamReceiver::StreamReceiver(Sink &sink, Clock clock) : sink_(sink), clock_(clock) {}

StreamReceiver::~StreamReceiver()
{
	stop();
}

void StreamReceiver::start(const Settings &settings)
{
	stop();

	settings_ = settings;
	stopping_ = false;
	receive_thread_ = std::thread(&StreamReceiver::receive_loop, this);
}

void StreamReceiver::stop()
{
	if (!receive_thread_.joinable())
		return;

	stopping_ = true;
	receive_thread_.join();
}

void StreamReceiver::receive_loop()
{
	while (!stopping_) {
		Connection connection(*this);
		if (connection.run())
			continue;

		// Listening failed, for example because another program uses the port: try again in a second
		for (int i = 0; i < 10 && !stopping_; i++)
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
}

} // namespace bmagicam
