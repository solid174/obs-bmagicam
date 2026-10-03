// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "camera-source.hpp"

#include "../stream/ffmpeg-check.hpp"
#include "../stream/stream-receiver.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
}

namespace bmagicam {

namespace {

constexpr const char *kPort = "port";
constexpr const char *kLatency = "latency";
constexpr const char *kHardwareDecoding = "hardware_decoding";

video_format obs_format(int format)
{
	switch (format) {
	case AV_PIX_FMT_NV12:
		return VIDEO_FORMAT_NV12;
	case AV_PIX_FMT_P010LE:
		return VIDEO_FORMAT_P010;
	case AV_PIX_FMT_YUV420P:
	case AV_PIX_FMT_YUVJ420P:
		return VIDEO_FORMAT_I420;
	case AV_PIX_FMT_YUV420P10LE:
		return VIDEO_FORMAT_I010;
	default:
		return VIDEO_FORMAT_NONE;
	}
}

video_colorspace obs_colorspace(const AVFrame &frame)
{
	switch (frame.color_trc) {
	case AVCOL_TRC_ARIB_STD_B67:
		return VIDEO_CS_2100_HLG;
	case AVCOL_TRC_SMPTE2084:
		return VIDEO_CS_2100_PQ;
	default:
		break;
	}
	switch (frame.colorspace) {
	case AVCOL_SPC_SMPTE170M:
	case AVCOL_SPC_BT470BG:
		return VIDEO_CS_601;
	default:
		return VIDEO_CS_709;
	}
}

video_trc obs_trc(const AVFrame &frame)
{
	switch (frame.color_trc) {
	case AVCOL_TRC_ARIB_STD_B67:
		return VIDEO_TRC_HLG;
	case AVCOL_TRC_SMPTE2084:
		return VIDEO_TRC_PQ;
	default:
		return VIDEO_TRC_DEFAULT;
	}
}

class CameraSource final : public StreamReceiver::Sink {
public:
	CameraSource(obs_source_t *source, obs_data_t *settings) : source_(source), receiver_(*this, os_gettime_ns)
	{
		update(settings);
	}

	// The receiver calls back into this object, so it stops before the object goes away
	~CameraSource() override { receiver_.stop(); }

	void update(obs_data_t *settings);

	void stream_started() override {}
	void stream_video(const AVFrame &frame, uint64_t timestamp) override;
	void stream_audio(const float *const planes[2], uint32_t frames, uint64_t timestamp) override;
	void stream_ended() override { obs_source_output_video(source_, nullptr); }

private:
	obs_source_t *source_;
	StreamReceiver receiver_;
	StreamReceiver::Settings settings_;
	bool receiving_ = false;
	bool logged_format_ = false;
};

void CameraSource::update(obs_data_t *settings)
{
	StreamReceiver::Settings next;
	next.port = static_cast<int>(obs_data_get_int(settings, kPort));
	next.latency_ms = static_cast<int>(obs_data_get_int(settings, kLatency));
	next.hardware_decoding = obs_data_get_bool(settings, kHardwareDecoding);

	const bool changed = next.port != settings_.port || next.latency_ms != settings_.latency_ms ||
			     next.hardware_decoding != settings_.hardware_decoding;
	if (receiving_ && !changed)
		return;

	settings_ = next;
	if (!ffmpeg_usable())
		return;

	receiver_.start(settings_);
	receiving_ = true;
}

void CameraSource::stream_video(const AVFrame &frame, uint64_t timestamp)
{
	const video_format format = obs_format(frame.format);
	if (format == VIDEO_FORMAT_NONE) {
		if (!logged_format_) {
			logged_format_ = true;
			obs_log(LOG_ERROR, "the video arrives as %s, which OBS cannot show",
				av_get_pix_fmt_name(static_cast<AVPixelFormat>(frame.format)));
		}
		return;
	}

	obs_source_frame2 out = {};
	for (size_t i = 0; i < MAX_AV_PLANES; i++) {
		out.data[i] = frame.data[i];
		out.linesize[i] = static_cast<uint32_t>(frame.linesize[i]);
	}
	out.width = static_cast<uint32_t>(frame.width);
	out.height = static_cast<uint32_t>(frame.height);
	out.timestamp = timestamp;
	out.format = format;
	out.range = frame.color_range == AVCOL_RANGE_JPEG ? VIDEO_RANGE_FULL : VIDEO_RANGE_PARTIAL;
	video_format_get_parameters_for_format(obs_colorspace(frame), out.range, format, out.color_matrix,
					       out.color_range_min, out.color_range_max);
	out.trc = static_cast<uint8_t>(obs_trc(frame));
	obs_source_output_video2(source_, &out);
}

void CameraSource::stream_audio(const float *const planes[2], uint32_t frames, uint64_t timestamp)
{
	obs_source_audio audio = {};
	audio.data[0] = reinterpret_cast<const uint8_t *>(planes[0]);
	audio.data[1] = reinterpret_cast<const uint8_t *>(planes[1]);
	audio.frames = frames;
	audio.speakers = SPEAKERS_STEREO;
	audio.format = AUDIO_FORMAT_FLOAT_PLANAR;
	audio.samples_per_sec = 48000;
	audio.timestamp = timestamp;
	obs_source_output_audio(source_, &audio);
}

const char *camera_get_name(void *)
{
	return obs_module_text("Camera.Name");
}

void *camera_create(obs_data_t *settings, obs_source_t *source)
{
	return new CameraSource(source, settings);
}

void camera_destroy(void *data)
{
	delete static_cast<CameraSource *>(data);
}

void camera_update(void *data, obs_data_t *settings)
{
	static_cast<CameraSource *>(data)->update(settings);
}

void camera_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, kPort, 9710);
	obs_data_set_default_int(settings, kLatency, 120);
	obs_data_set_default_bool(settings, kHardwareDecoding, true);
}

obs_properties_t *camera_properties(void *)
{
	obs_properties_t *properties = obs_properties_create();

	obs_property_t *port =
		obs_properties_add_int(properties, kPort, obs_module_text("Camera.Port"), 1024, 65535, 1);
	obs_property_set_long_description(port, obs_module_text("Camera.Port.Tooltip"));

	obs_property_t *latency =
		obs_properties_add_int(properties, kLatency, obs_module_text("Camera.Latency"), 20, 1000, 10);
	obs_property_int_set_suffix(latency, " ms");
	obs_property_set_long_description(latency, obs_module_text("Camera.Latency.Tooltip"));

	obs_property_t *hardware =
		obs_properties_add_bool(properties, kHardwareDecoding, obs_module_text("Camera.HardwareDecoding"));
	obs_property_set_long_description(hardware, obs_module_text("Camera.HardwareDecoding.Tooltip"));

	return properties;
}

} // namespace

void register_camera_source()
{
	obs_source_info info = {};
	info.id = kCameraSourceId;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE;
	info.icon_type = OBS_ICON_TYPE_CAMERA;
	info.get_name = camera_get_name;
	info.create = camera_create;
	info.destroy = camera_destroy;
	info.update = camera_update;
	info.get_defaults = camera_defaults;
	info.get_properties = camera_properties;
	obs_register_source(&info);
}

} // namespace bmagicam
