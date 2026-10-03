// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "camera-source.hpp"

#include "../camera/camera-session.hpp"
#include "../camera/stream-presets.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
}

#include <mutex>
#include <set>
#include <string>

namespace bmagicam {

namespace {

constexpr const char *kStatus = "status";
constexpr const char *kAddress = "address";
constexpr const char *kPreset = "preset";
constexpr const char *kPort = "port";
constexpr const char *kLatency = "latency";
constexpr const char *kHardwareDecoding = "hardware_decoding";

// Automatic ports are the first free ones from here; the setup guide opens this range in firewalls
constexpr int kFirstPort = 9710;
constexpr int kLastPort = 9719;

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

// UDP ports taken by iPhone Camera sources in this OBS
class Ports {
public:
	// The requested port, or the first free one when it is 0 (automatic). Gives up the source's current port.
	static int take(int requested, int current)
	{
		std::lock_guard lock(mutex());
		used().erase(current);
		int port = requested;
		for (int candidate = kFirstPort; port == 0 && candidate <= kLastPort; candidate++) {
			if (!used().count(candidate))
				port = candidate;
		}
		if (port == 0)
			port = kLastPort + 1 + static_cast<int>(used().size());
		used().insert(port);
		return port;
	}

	static void release(int port)
	{
		std::lock_guard lock(mutex());
		used().erase(port);
	}

private:
	static std::mutex &mutex()
	{
		static std::mutex instance;
		return instance;
	}

	static std::set<int> &used()
	{
		static std::set<int> instance;
		return instance;
	}
};

std::string status_text(const CameraSession::Status &status)
{
	switch (status.state) {
	case CameraSession::State::NoPhone:
		return obs_module_text("Status.NoPhone");
	case CameraSession::State::Connecting:
		return obs_module_text("Status.Connecting");
	case CameraSession::State::Starting:
		return obs_module_text("Status.Starting");
	case CameraSession::State::Live:
		return std::string(obs_module_text("Status.Live")) + " · " + status.detail;
	case CameraSession::State::Paused:
		return obs_module_text("Status.Paused");
	case CameraSession::State::Error:
		break;
	}

	switch (status.problem) {
	case CameraSession::Problem::NotAnswering:
		return obs_module_text("Status.NotAnswering");
	case CameraSession::Problem::MonitorOnly:
		return obs_module_text("Status.MonitorOnly");
	case CameraSession::Problem::CannotReach:
		return obs_module_text("Status.CannotReach");
	case CameraSession::Problem::Unavailable:
		return std::string(obs_module_text("Status.Unavailable")) + " (" + status.detail + ")";
	case CameraSession::Problem::Refused:
		return obs_module_text("Status.Refused");
	case CameraSession::Problem::FFmpeg:
		return obs_module_text("Status.FFmpeg");
	case CameraSession::Problem::None:
		break;
	}
	return {};
}

class CameraSource final : public CameraSession::Output {
public:
	CameraSource(obs_source_t *source, obs_data_t *settings) : source_(source), session_(*this, os_gettime_ns)
	{
		update(settings);
	}

	~CameraSource() override { Ports::release(port_); }

	void update(obs_data_t *settings);
	void set_shown(bool shown) { session_.set_active(shown); }
	void add_status(obs_properties_t *properties) const;

	void session_video(const AVFrame &frame, uint64_t timestamp) override;
	void session_audio(const float *const planes[2], uint32_t frames, uint64_t timestamp) override;
	void session_cleared() override { obs_source_output_video(source_, nullptr); }
	void session_status_changed() override { obs_source_update_properties(source_); }

private:
	obs_source_t *source_;
	int port_ = 0;
	bool logged_format_ = false;
	// Last, so that it ends first and stops calling back into this object
	CameraSession session_;
};

void CameraSource::update(obs_data_t *settings)
{
	port_ = Ports::take(static_cast<int>(obs_data_get_int(settings, kPort)), port_);

	CameraSession::Settings next;
	next.address = obs_data_get_string(settings, kAddress);
	next.preset = obs_data_get_string(settings, kPreset);
	next.receiver.port = port_;
	next.receiver.latency_ms = static_cast<int>(obs_data_get_int(settings, kLatency));
	next.receiver.hardware_decoding = obs_data_get_bool(settings, kHardwareDecoding);
	session_.update(next);
}

void CameraSource::add_status(obs_properties_t *properties) const
{
	const CameraSession::Status status = session_.status();
	std::string text = status_text(status);
	if (!status.phone.empty() && status.state != CameraSession::State::Error)
		text = status.phone + ": " + text;

	obs_property_t *property = obs_properties_add_text(properties, kStatus, text.c_str(), OBS_TEXT_INFO);
	obs_property_text_set_info_type(property, status.state == CameraSession::State::Error ? OBS_TEXT_INFO_ERROR
											      : OBS_TEXT_INFO_NORMAL);
	obs_property_text_set_info_word_wrap(property, true);
}

void CameraSource::session_video(const AVFrame &frame, uint64_t timestamp)
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

void CameraSource::session_audio(const float *const planes[2], uint32_t frames, uint64_t timestamp)
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

void camera_show(void *data)
{
	static_cast<CameraSource *>(data)->set_shown(true);
}

void camera_hide(void *data)
{
	static_cast<CameraSource *>(data)->set_shown(false);
}

void camera_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, kPreset, stream_presets().front().id);
	obs_data_set_default_int(settings, kPort, 0);
	obs_data_set_default_int(settings, kLatency, 120);
	obs_data_set_default_bool(settings, kHardwareDecoding, true);
}

obs_properties_t *camera_properties(void *data)
{
	obs_properties_t *properties = obs_properties_create();
	if (data)
		static_cast<const CameraSource *>(data)->add_status(properties);

	obs_property_t *address =
		obs_properties_add_text(properties, kAddress, obs_module_text("Camera.Address"), OBS_TEXT_DEFAULT);
	obs_property_set_long_description(address, obs_module_text("Camera.Address.Tooltip"));

	obs_property_t *preset = obs_properties_add_list(properties, kPreset, obs_module_text("Camera.Preset"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	std::string tooltip = obs_module_text("Camera.Preset.Tooltip");
	for (const StreamPreset &item : stream_presets()) {
		obs_property_list_add_string(preset, item.profile, item.id);
		tooltip += std::string("\n") + item.profile + ": " + obs_module_text(item.description_key);
	}
	obs_property_set_long_description(preset, tooltip.c_str());

	obs_property_t *port = obs_properties_add_int(properties, kPort, obs_module_text("Camera.Port"), 0, 65535, 1);
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
	info.show = camera_show;
	info.hide = camera_hide;
	info.get_defaults = camera_defaults;
	info.get_properties = camera_properties;
	obs_register_source(&info);
}

} // namespace bmagicam
