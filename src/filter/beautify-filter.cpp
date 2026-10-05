// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "beautify-filter.hpp"

#include "skin-model.hpp"
#include "style-library.hpp"
#include "../face/face-geometry.hpp"
#include "../face/face-tracker.hpp"

#include <graphics/vec2.h>
#include <graphics/vec4.h>
#include <obs-module.h>
#include <obs.hpp>
#include <plugin-support.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace bmagicam {

namespace {

constexpr const char *kAdvanced = "advanced";
constexpr const char *kSaveStyle = "saveStyle";
constexpr const char *kDeleteStyle = "deleteStyle";
constexpr const char *kDefaultStyle = "natural";
// Statistics blocks across the picture; down it, as many as its shape gives
constexpr uint32_t kStatsColumns = 48;
// Statistics are made every this many frames and read the next time, so reading them never waits for the GPU
constexpr uint64_t kStatsEvery = 6;
// How fast the skin model and the mask follow the picture
constexpr float kModelSeconds = 1.5f;
constexpr float kMaskSeconds = 0.05f;
// Face tracking gets frames at most this large on their longer side, as often as it asks for them. The interval is
// checked a little short, so a 60 fps picture is tracked every second frame although the frame times are rounded.
constexpr uint32_t kTrackSize = 960;
constexpr double kTrackSlack = 0.003;
// The mask is drawn where the faces' motion takes them by the time it is shown, at most this far ahead
constexpr double kMaxAhead = 0.1;
// The mask leaves out eyes, brows and lips grown by these factors, so its soft edge stays off them
constexpr float kEyeGrowth = 1.4f;
constexpr float kBrowGrowth = 1.3f;
constexpr float kLipGrowth = 1.15f;
// The faces' mask is drawn from one vertex buffer: per face, its mesh and the fans of its eyes, brows and lips
constexpr size_t kMaxFaces = 4;
constexpr size_t kFaceVertices =
	static_cast<size_t>(kFaceMeshTriangles) * 3 +
	3 * (kRightEye.size() + kLeftEye.size() + kRightBrow.size() + kLeftBrow.size() + kLips.size());
// Where faces are tracked, smoothing scales with their width: a face this wide smooths as any picture 1080 high
// does without tracking, which is about a webcam close-up
constexpr float kFaceWidth = 400.0f;

BeautifyActions beautify_actions;

float squared(float value)
{
	return value * value;
}

class BeautifyFilter {
public:
	BeautifyFilter(obs_source_t *context, obs_data_t *settings);
	~BeautifyFilter();

	obs_source_t *context() const { return context_; }
	void update(obs_data_t *settings);
	void tick(float seconds);
	void render();
	gs_color_space color_space(size_t count, const gs_color_space *preferred) const;

private:
	// What rendering takes from the settings
	struct Look {
		BeautyAmounts amounts;
		bool show_mask = false;
		bool active = false;
	};
	struct Params {
		gs_eparam_t *image = nullptr;
		gs_eparam_t *image2 = nullptr;
		gs_eparam_t *image3 = nullptr;
		gs_eparam_t *image4 = nullptr;
		gs_eparam_t *blur_step = nullptr;
		gs_eparam_t *block_step = nullptr;
		gs_eparam_t *texel = nullptr;
		gs_eparam_t *skin_chroma = nullptr;
		gs_eparam_t *skin_spread = nullptr;
		gs_eparam_t *mask_blend = nullptr;
		gs_eparam_t *tracking = nullptr;
		gs_eparam_t *guide_eps = nullptr;
		gs_eparam_t *smoothing = nullptr;
		gs_eparam_t *texture_keep = nullptr;
		gs_eparam_t *evening = nullptr;
		gs_eparam_t *sharpen = nullptr;
		gs_eparam_t *glow = nullptr;
		gs_eparam_t *keep_edge = nullptr;
		gs_eparam_t *spot_depth = nullptr;
		gs_eparam_t *coring = nullptr;
		gs_eparam_t *mask_view = nullptr;
	};
	using Inputs = std::initializer_list<std::pair<gs_eparam_t *, gs_texture_t *>>;

	bool capture(obs_source_t *target, obs_source_t *parent, uint32_t width, uint32_t height);
	void pass(gs_texrender_t *target, const char *technique, uint32_t width, uint32_t height, Inputs inputs);
	void blur_step(float x, float y);
	void measure(gs_texture_t *half, gs_texture_t *quarter, gs_texture_t *faces, uint32_t half_width,
		     uint32_t half_height);
	void read_stats();
	void track(gs_texture_t *half, gs_texture_t *quarter, uint32_t width, uint32_t height);
	float draw_faces(uint32_t width, uint32_t height, float &face_width);
	void composite(const Look &look, uint32_t width, uint32_t height);

	obs_source_t *context_;
	std::mutex mutex_;
	Look look_;

	// Only used on the graphics thread
	gs_effect_t *effect_ = nullptr;
	Params params_;
	gs_texrender_t *input_ = nullptr;
	gs_texrender_t *half_ = nullptr;
	gs_texrender_t *quarter_ = nullptr;
	gs_texrender_t *masks_[2] = {};
	gs_texrender_t *guide_half_ = nullptr;
	gs_texrender_t *guide_ = nullptr;
	gs_texrender_t *coefficients_half_ = nullptr;
	gs_texrender_t *coefficients_ = nullptr;
	gs_texrender_t *wide_half_ = nullptr;
	gs_texrender_t *wide_ = nullptr;
	gs_texrender_t *stats_ = nullptr;
	gs_stagesurf_t *stage_ = nullptr;
	bool staged_ = false;
	// Face tracking: frames go to the tracker through a staging surface, and its faces come back as a mask
	std::unique_ptr<FaceTracker> tracker_;
	FaceTracker::State tracker_state_ = FaceTracker::State::Starting;
	gs_texrender_t *track_ = nullptr;
	gs_stagesurf_t *track_stage_ = nullptr;
	bool track_staged_ = false;
	double track_seconds_ = 0;
	double last_track_ = -1;
	double clock_ = 0;
	gs_texrender_t *faces_ = nullptr;
	gs_vertbuffer_t *face_vertices_ = nullptr;
	// Which of the two masks is this frame's, and whether the other holds the previous frame's
	int mask_ = 0;
	bool mask_valid_ = false;
	// The passes ran for this frame; another view of the same frame only composites
	bool processed_ = false;
	uint32_t width_ = 0;
	uint32_t height_ = 0;
	uint64_t frames_ = 0;
	float frame_seconds_ = 0;
	float stats_seconds_ = 0;
	SkinModel model_;
};

BeautifyFilter::BeautifyFilter(obs_source_t *context, obs_data_t *settings) : context_(context)
{
	char *path = obs_module_file("effects/beautify.effect");
	obs_enter_graphics();
	char *errors = nullptr;
	effect_ = path ? gs_effect_create_from_file(path, &errors) : nullptr;
	if (effect_) {
		const auto param = [this](const char *name) {
			return gs_effect_get_param_by_name(effect_, name);
		};
		params_ = {param("image"),       param("image2"),       param("image3"),     param("image4"),
			   param("blur_step"),   param("block_step"),   param("texel"),      param("skin_chroma"),
			   param("skin_spread"), param("mask_blend"),   param("tracking"),   param("guide_eps"),
			   param("smoothing"),   param("texture_keep"), param("evening"),    param("sharpen"),
			   param("glow"),        param("keep_edge"),    param("spot_depth"), param("coring"),
			   param("mask_view")};
	} else {
		obs_log(LOG_WARNING, "Beautify cannot load its effect: %s", errors ? errors : "the file is missing");
	}
	bfree(errors);
	input_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	half_ = gs_texrender_create(GS_RGBA16F, GS_ZS_NONE);
	quarter_ = gs_texrender_create(GS_RGBA16F, GS_ZS_NONE);
	masks_[0] = gs_texrender_create(GS_RGBA16F, GS_ZS_NONE);
	masks_[1] = gs_texrender_create(GS_RGBA16F, GS_ZS_NONE);
	guide_half_ = gs_texrender_create(GS_RGBA16F, GS_ZS_NONE);
	guide_ = gs_texrender_create(GS_RGBA16F, GS_ZS_NONE);
	coefficients_half_ = gs_texrender_create(GS_RGBA16F, GS_ZS_NONE);
	coefficients_ = gs_texrender_create(GS_RGBA16F, GS_ZS_NONE);
	wide_half_ = gs_texrender_create(GS_RGBA16F, GS_ZS_NONE);
	wide_ = gs_texrender_create(GS_RGBA16F, GS_ZS_NONE);
	stats_ = gs_texrender_create(GS_RGBA16F, GS_ZS_NONE);
	track_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	faces_ = gs_texrender_create(GS_R8, GS_ZS_NONE);
	gs_vb_data *vertices = gs_vbdata_create();
	vertices->num = kMaxFaces * kFaceVertices;
	vertices->points = static_cast<vec3 *>(bzalloc(sizeof(vec3) * vertices->num));
	vertices->colors = static_cast<uint32_t *>(bzalloc(sizeof(uint32_t) * vertices->num));
	face_vertices_ = gs_vertexbuffer_create(vertices, GS_DYNAMIC);
	obs_leave_graphics();
	bfree(path);
	// The tracker loads its models and starts its thread with the first frame (PWR-1)
	char *models = obs_module_file("models");
	if (models)
		tracker_ = std::make_unique<FaceTracker>(models);
	else
		obs_log(LOG_WARNING, "Beautify cannot find its face models; the skin mask follows skin color alone");
	bfree(models);
	update(settings);
}

BeautifyFilter::~BeautifyFilter()
{
	obs_enter_graphics();
	for (gs_texrender_t *texrender : {input_, half_, quarter_, masks_[0], masks_[1], guide_half_, guide_,
					  coefficients_half_, coefficients_, wide_half_, wide_, stats_, track_, faces_})
		gs_texrender_destroy(texrender);
	gs_stagesurface_destroy(stage_);
	gs_stagesurface_destroy(track_stage_);
	gs_vertexbuffer_destroy(face_vertices_);
	gs_effect_destroy(effect_);
	obs_leave_graphics();
}

void BeautifyFilter::update(obs_data_t *settings)
{
	Look look;
	const int strength = static_cast<int>(obs_data_get_int(settings, kBeautyStrength));
	look.amounts = beauty_amounts(beauty_values(settings), strength);
	look.show_mask = obs_data_get_bool(settings, kBeautyShowMask);
	// At zero strength the picture stays as it is (BEA-7); the mask can still be shown
	look.active = strength > 0 || look.show_mask;
	std::lock_guard lock(mutex_);
	look_ = look;
}

void BeautifyFilter::tick(float seconds)
{
	frame_seconds_ = seconds;
	stats_seconds_ += seconds;
	clock_ += seconds;
	processed_ = false;
}

void BeautifyFilter::render()
{
	Look look;
	{
		std::lock_guard lock(mutex_);
		look = look_;
	}
	obs_source_t *target = obs_filter_get_target(context_);
	obs_source_t *parent = obs_filter_get_parent(context_);
	const uint32_t width = target ? obs_source_get_base_width(target) : 0;
	const uint32_t height = target ? obs_source_get_base_height(target) : 0;
	const gs_color_space spaces[] = {GS_CS_SRGB, GS_CS_SRGB_16F, GS_CS_709_EXTENDED};
	// Off, too small, or HDR: the picture passes through untouched, and no passes run (BEA-7, PWR-1)
	if (!look.active || !effect_ || !target || !parent || width < 8 || height < 8 ||
	    obs_source_get_color_space(target, std::size(spaces), spaces) != GS_CS_SRGB ||
	    gs_get_color_space() != GS_CS_SRGB) {
		obs_source_skip_video_filter(context_);
		return;
	}
	if (processed_ && width == width_ && height == height_) {
		composite(look, width, height);
		return;
	}
	if (width != width_ || height != height_) {
		width_ = width;
		height_ = height;
		mask_valid_ = false;
		staged_ = false;
		track_staged_ = false;
	}
	if (!capture(target, parent, width, height)) {
		obs_source_skip_video_filter(context_);
		return;
	}

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	const bool srgb = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(false);

	const uint32_t half_width = (width + 1) / 2;
	const uint32_t half_height = (height + 1) / 2;
	const uint32_t quarter_width = (half_width + 1) / 2;
	const uint32_t quarter_height = (half_height + 1) / 2;
	pass(half_, "Downsample", half_width, half_height, {{params_.image, gs_texrender_get_texture(input_)}});
	gs_texture_t *half = gs_texrender_get_texture(half_);
	pass(quarter_, "Downsample", quarter_width, quarter_height, {{params_.image, half}});
	gs_texture_t *quarter = gs_texrender_get_texture(quarter_);
	// While face tracking works, only faces are retouched; the color mask then only weighs how much inside them.
	// Without its models, the color mask works alone, as before face tracking.
	track(half, quarter, width, height);
	const bool tracking = tracker_ && tracker_->state() != FaceTracker::State::Failed;
	float face_width = 0;
	const float face_weight = tracking ? draw_faces(half_width, half_height, face_width) : 0.0f;
	gs_texture_t *faces = tracking ? gs_texrender_get_texture(faces_) : half;
	gs_effect_set_float(params_.tracking, tracking ? 1.0f : 0.0f);
	if (frames_++ % kStatsEvery == 0)
		measure(half, quarter, faces, half_width, half_height);

	const BeautyAmounts &amounts = look.amounts;
	const float noise = model_.noise();

	// Skin mask: Mask softness widens what counts as skin and softens the mask's edge
	const float softness = 0.7f + 0.8f * amounts.mask_softness;
	vec2 vector;
	vec2_set(&vector, model_.cb(), model_.cr());
	gs_effect_set_vec2(params_.skin_chroma, &vector);
	vec2_set(&vector, 1.0f / squared(0.28f * softness), 1.0f / squared(0.28f * softness));
	gs_effect_set_vec2(params_.skin_spread, &vector);
	gs_effect_set_float(params_.mask_blend, mask_valid_ ? 1.0f - std::exp(-frame_seconds_ / kMaskSeconds) : 1.0f);
	vec2_set(&vector, 1.0f / static_cast<float>(half_width), 1.0f / static_cast<float>(half_height));
	gs_effect_set_vec2(params_.texel, &vector);
	gs_texture_t *previous = mask_valid_ ? gs_texrender_get_texture(masks_[mask_]) : half;
	mask_ = 1 - mask_;
	pass(masks_[mask_], "Mask", half_width, half_height,
	     {{params_.image, half}, {params_.image2, previous}, {params_.image3, faces}});
	mask_valid_ = true;

	// Fine base: a guided filter on luma, which flattens small detail and keeps contours. Its size follows the
	// tracked faces (FACE-7), otherwise the picture.
	const float picture_scale = static_cast<float>(height) / 1080.0f;
	const float face_scale = face_width * static_cast<float>(width) / kFaceWidth;
	const float scale = picture_scale + (face_scale - picture_scale) * face_weight;
	const float spacing = std::max((3.0f + 9.0f * amounts.detail_size) * scale / 6.0f, 0.5f);
	blur_step(spacing / static_cast<float>(half_width), 0);
	pass(guide_half_, "GuideH", half_width, half_height,
	     {{params_.image, half}, {params_.image2, gs_texrender_get_texture(masks_[mask_])}});
	// Stronger smoothing also flattens larger variations
	gs_effect_set_float(params_.guide_eps, squared(0.04f + 0.06f * amounts.smoothing + 3.0f * noise));
	blur_step(0, spacing / static_cast<float>(half_height));
	pass(guide_, "GuideV", half_width, half_height, {{params_.image, gs_texrender_get_texture(guide_half_)}});
	blur_step(spacing / static_cast<float>(half_width), 0);
	pass(coefficients_half_, "Blur", half_width, half_height, {{params_.image, gs_texrender_get_texture(guide_)}});
	blur_step(0, spacing / static_cast<float>(half_height));
	pass(coefficients_, "Blur", half_width, half_height,
	     {{params_.image, gs_texrender_get_texture(coefficients_half_)}});

	// Wide base: the surrounding skin, for tone evening and glow
	const float wide_spacing = std::max((1.0f + 1.5f * amounts.detail_size) * scale, 0.5f);
	blur_step(wide_spacing / static_cast<float>(quarter_width), 0);
	pass(wide_half_, "Blur", quarter_width, quarter_height, {{params_.image, quarter}});
	blur_step(0, wide_spacing / static_cast<float>(quarter_height));
	pass(wide_, "Blur", quarter_width, quarter_height, {{params_.image, gs_texrender_get_texture(wide_half_)}});

	gs_enable_framebuffer_srgb(srgb);
	gs_blend_state_pop();
	processed_ = true;
	composite(look, width, height);
}

gs_color_space BeautifyFilter::color_space(size_t count, const gs_color_space *preferred) const
{
	const gs_color_space potential[] = {GS_CS_SRGB, GS_CS_SRGB_16F, GS_CS_709_EXTENDED};
	obs_source_t *target = obs_filter_get_target(context_);
	const gs_color_space source = target ? obs_source_get_color_space(target, std::size(potential), potential)
					     : GS_CS_SRGB;
	gs_color_space space = source;
	for (size_t index = 0; index < count; index++) {
		space = preferred[index];
		if (space == source)
			break;
	}
	return space;
}

bool BeautifyFilter::capture(obs_source_t *target, obs_source_t *parent, uint32_t width, uint32_t height)
{
	gs_texrender_reset(input_);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	const bool captured = gs_texrender_begin_with_color_space(input_, width, height, GS_CS_SRGB);
	if (captured) {
		vec4 clear;
		vec4_zero(&clear);
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, static_cast<float>(width), 0.0f, static_cast<float>(height), -100.0f, 100.0f);
		const uint32_t flags = obs_source_get_output_flags(target);
		if (target == parent && !(flags & OBS_SOURCE_CUSTOM_DRAW) && !(flags & OBS_SOURCE_ASYNC))
			obs_source_default_render(target);
		else
			obs_source_video_render(target);
		gs_texrender_end(input_);
	}
	gs_blend_state_pop();
	return captured;
}

void BeautifyFilter::pass(gs_texrender_t *target, const char *technique, uint32_t width, uint32_t height, Inputs inputs)
{
	gs_texrender_reset(target);
	if (!gs_texrender_begin(target, width, height))
		return;
	gs_ortho(0.0f, static_cast<float>(width), 0.0f, static_cast<float>(height), -100.0f, 100.0f);
	for (const auto &[param, texture] : inputs)
		gs_effect_set_texture(param, texture);
	while (gs_effect_loop(effect_, technique))
		gs_draw_sprite(nullptr, 0, width, height);
	gs_texrender_end(target);
}

void BeautifyFilter::blur_step(float x, float y)
{
	vec2 step;
	vec2_set(&step, x, y);
	gs_effect_set_vec2(params_.blur_step, &step);
}

void BeautifyFilter::measure(gs_texture_t *half, gs_texture_t *quarter, gs_texture_t *faces, uint32_t half_width,
			     uint32_t half_height)
{
	read_stats();
	const uint32_t columns = kStatsColumns;
	const auto rows = static_cast<uint32_t>(
		std::clamp(std::lround(columns * static_cast<double>(half_height) / half_width), 4L, 64L));
	vec2 step;
	vec2_set(&step, 0.25f / static_cast<float>(columns), 0.25f / static_cast<float>(rows));
	gs_effect_set_vec2(params_.block_step, &step);
	pass(stats_, "Stats", columns, rows,
	     {{params_.image, half}, {params_.image2, quarter}, {params_.image3, faces}});
	if (!stage_ || gs_stagesurface_get_width(stage_) != columns || gs_stagesurface_get_height(stage_) != rows) {
		gs_stagesurface_destroy(stage_);
		stage_ = gs_stagesurface_create(columns, rows, GS_RGBA16F);
	}
	if (stage_) {
		gs_stage_texture(stage_, gs_texrender_get_texture(stats_));
		staged_ = true;
	}
}

void BeautifyFilter::read_stats()
{
	if (!staged_ || !stage_)
		return;
	staged_ = false;
	uint8_t *data = nullptr;
	uint32_t linesize = 0;
	if (!gs_stagesurface_map(stage_, &data, &linesize))
		return;
	const uint32_t columns = gs_stagesurface_get_width(stage_);
	const uint32_t rows = gs_stagesurface_get_height(stage_);
	std::vector<BlockStats> blocks;
	blocks.reserve(static_cast<size_t>(columns) * rows);
	for (uint32_t y = 0; y < rows; y++) {
		const auto row = reinterpret_cast<const uint16_t *>(data + static_cast<size_t>(y) * linesize);
		for (uint32_t x = 0; x < columns; x++)
			blocks.push_back({from_half(row[4 * x]), from_half(row[4 * x + 1]), from_half(row[4 * x + 2]),
					  from_half(row[4 * x + 3])});
	}
	gs_stagesurface_unmap(stage_);
	model_.update(blocks, 1.0f - std::exp(-stats_seconds_ / kModelSeconds));
	stats_seconds_ = 0;
}

void BeautifyFilter::track(gs_texture_t *half, gs_texture_t *quarter, uint32_t width, uint32_t height)
{
	if (!tracker_)
		return;
	const FaceTracker::State state = tracker_->state();
	if (state != tracker_state_) {
		tracker_state_ = state;
		if (state == FaceTracker::State::Running)
			obs_log(LOG_INFO, "Beautify loaded its face models");
		else if (state == FaceTracker::State::Failed)
			obs_log(LOG_WARNING,
				"Beautify cannot load its face models; the skin mask follows skin color alone");
	}
	if (state == FaceTracker::State::Failed)
		return;
	// The frame staged last time is ready by now, so reading it never waits for the GPU
	if (track_staged_ && track_stage_) {
		track_staged_ = false;
		uint8_t *data = nullptr;
		uint32_t linesize = 0;
		if (gs_stagesurface_map(track_stage_, &data, &linesize)) {
			const uint32_t frame_width = gs_stagesurface_get_width(track_stage_);
			const uint32_t frame_height = gs_stagesurface_get_height(track_stage_);
			std::vector<uint8_t> pixels(static_cast<size_t>(frame_width) * frame_height * 4);
			for (uint32_t y = 0; y < frame_height; y++)
				std::memcpy(pixels.data() + static_cast<size_t>(y) * frame_width * 4,
					    data + static_cast<size_t>(y) * linesize,
					    static_cast<size_t>(frame_width) * 4);
			gs_stagesurface_unmap(track_stage_);
			tracker_->track(std::move(pixels), static_cast<int>(frame_width),
					static_cast<int>(frame_height), track_seconds_);
		}
	}
	if (clock_ - last_track_ < tracker_->interval() - kTrackSlack || !tracker_->wants_frame())
		return;
	// Scaled down from the copy nearest the tracker's size, so each pixel averages the ones it stands for
	const float scale =
		std::min(1.0f, static_cast<float>(kTrackSize) / static_cast<float>(std::max(width, height)));
	const auto frame_width = static_cast<uint32_t>(std::max(1L, std::lround(width * scale)));
	const auto frame_height = static_cast<uint32_t>(std::max(1L, std::lround(height * scale)));
	gs_texture_t *source = scale <= 0.25f ? quarter : scale <= 0.5f ? half : gs_texrender_get_texture(input_);
	pass(track_, "Downsample", frame_width, frame_height, {{params_.image, source}});
	if (!track_stage_ || gs_stagesurface_get_width(track_stage_) != frame_width ||
	    gs_stagesurface_get_height(track_stage_) != frame_height) {
		gs_stagesurface_destroy(track_stage_);
		track_stage_ = gs_stagesurface_create(frame_width, frame_height, GS_RGBA);
	}
	if (!track_stage_)
		return;
	gs_stage_texture(track_stage_, gs_texrender_get_texture(track_));
	track_staged_ = true;
	track_seconds_ = clock_;
	last_track_ = clock_;
}

// The tracked faces as a mask at the mask's resolution: each face's mesh, less its eyes, brows and lips, as bright as
// the face has faded in. The mesh's triangles together cover the face as far as it is seen, at any turn of the head;
// with no face, the mask is empty. Returns the most faded-in face's weight, and the faces' mean width as a share of
// the picture's.
float BeautifyFilter::draw_faces(uint32_t width, uint32_t height, float &face_width)
{
	std::vector<TrackedFace> faces = tracker_->faces();
	if (faces.size() > kMaxFaces)
		faces.resize(kMaxFaces);
	float weight = 0;
	float total = 0;
	face_width = 0;
	for (const TrackedFace &face : faces) {
		weight = std::max(weight, face.weight);
		total += face.weight;
		face_width += face.weight * face.width;
	}
	if (total > 0)
		face_width /= total;

	gs_vb_data *data = gs_vertexbuffer_get_data(face_vertices_);
	size_t count = 0;
	const auto add = [&](const Point &point, uint32_t color) {
		vec3_set(&data->points[count], point.x, point.y, 0.0f);
		data->colors[count++] = color;
	};
	// Eyes, brows and lips as fans of triangles from their centers, which their outlines are convex enough for
	const auto cut = [&](const std::vector<Point> &points, const int *outline, size_t size, float growth) {
		Point center;
		for (size_t i = 0; i < size; i++) {
			center.x += points[outline[i]].x / static_cast<float>(size);
			center.y += points[outline[i]].y / static_cast<float>(size);
		}
		const auto corner = [&](size_t i) {
			const Point &point = points[outline[i % size]];
			return Point{center.x + (point.x - center.x) * growth,
				     center.y + (point.y - center.y) * growth};
		};
		for (size_t i = 0; i < size; i++) {
			add(center, 0xff000000u);
			add(corner(i), 0xff000000u);
			add(corner(i + 1), 0xff000000u);
		}
	};
	// The landmarks are from a frame or two ago; each moves on as it was moving
	std::vector<std::vector<Point>> placed;
	for (const TrackedFace &face : faces) {
		const auto ahead = static_cast<float>(std::clamp(clock_ - face.seconds, 0.0, kMaxAhead));
		std::vector<Point> points(face.landmarks.size());
		for (size_t i = 0; i < points.size(); i++)
			points[i] = {face.landmarks[i].x + face.motion[i].x * ahead,
				     face.landmarks[i].y + face.motion[i].y * ahead};
		placed.push_back(std::move(points));
	}
	for (size_t f = 0; f < faces.size(); f++) {
		const auto level = static_cast<uint32_t>(std::lround(255.0f * std::clamp(faces[f].weight, 0.0f, 1.0f)));
		const uint32_t color = 0xff000000u | level << 16 | level << 8 | level;
		for (uint16_t index : kFaceMesh)
			add(placed[f][index], color);
	}
	for (const std::vector<Point> &points : placed) {
		cut(points, kRightEye.data(), kRightEye.size(), kEyeGrowth);
		cut(points, kLeftEye.data(), kLeftEye.size(), kEyeGrowth);
		cut(points, kRightBrow.data(), kRightBrow.size(), kBrowGrowth);
		cut(points, kLeftBrow.data(), kLeftBrow.size(), kBrowGrowth);
		cut(points, kLips.data(), kLips.size(), kLipGrowth);
	}
	if (count > 0)
		gs_vertexbuffer_flush(face_vertices_);

	gs_texrender_reset(faces_);
	if (!gs_texrender_begin(faces_, width, height))
		return 0;
	vec4 color;
	vec4_zero(&color);
	gs_clear(GS_CLEAR_COLOR, &color, 0.0f, 0);
	if (count > 0) {
		gs_ortho(0.0f, 1.0f, 0.0f, 1.0f, -100.0f, 100.0f);
		gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
		vec4_set(&color, 1.0f, 1.0f, 1.0f, 1.0f);
		gs_effect_set_vec4(gs_effect_get_param_by_name(solid, "color"), &color);
		gs_load_vertexbuffer(face_vertices_);
		gs_load_indexbuffer(nullptr);
		while (gs_effect_loop(solid, "SolidColored"))
			gs_draw(GS_TRIS, 0, static_cast<uint32_t>(count));
		gs_load_vertexbuffer(nullptr);
	}
	gs_texrender_end(faces_);
	return weight;
}

void BeautifyFilter::composite(const Look &look, uint32_t width, uint32_t height)
{
	const BeautyAmounts &amounts = look.amounts;
	const float noise = model_.noise();
	gs_texture_t *input = gs_texrender_get_texture(input_);
	gs_effect_set_texture(params_.image, input);
	gs_effect_set_texture(params_.image2, gs_texrender_get_texture(coefficients_));
	gs_effect_set_texture(params_.image3, gs_texrender_get_texture(wide_));
	gs_effect_set_texture(params_.image4, gs_texrender_get_texture(half_));
	gs_effect_set_float(params_.smoothing, amounts.smoothing);
	gs_effect_set_float(params_.texture_keep, amounts.texture);
	gs_effect_set_float(params_.evening, amounts.evening);
	gs_effect_set_float(params_.sharpen, amounts.sharpen);
	gs_effect_set_float(params_.glow, amounts.glow);
	// Edge thresholds follow the noise, so low and high ISO give the same result
	gs_effect_set_float(params_.keep_edge, 0.12f + 4.0f * noise);
	gs_effect_set_float(params_.spot_depth, 0.25f);
	gs_effect_set_float(params_.coring, 1.5f * noise);
	gs_effect_set_float(params_.mask_view, look.show_mask ? 1.0f : 0.0f);
	const bool srgb = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(false);
	while (gs_effect_loop(effect_, "Composite"))
		gs_draw_sprite(input, 0, width, height);
	gs_enable_framebuffer_srgb(srgb);
}

bool user_style_selected(obs_data_t *settings)
{
	const std::vector<BeautyStyle> styles = user_styles();
	return find_style(styles, obs_data_get_string(settings, kBeautyStyle)) != nullptr;
}

void show_advanced(obs_properties_t *properties, obs_data_t *settings)
{
	const bool advanced = obs_data_get_bool(settings, kAdvanced);
	for (const char *key : kBeautyKeys)
		obs_property_set_visible(obs_properties_get(properties, key), advanced);
	obs_property_set_visible(obs_properties_get(properties, kBeautyShowMask), advanced);
	obs_property_set_visible(obs_properties_get(properties, kSaveStyle), advanced);
	obs_property_set_visible(obs_properties_get(properties, kDeleteStyle),
				 advanced && user_style_selected(settings));
}

bool advanced_modified(obs_properties_t *properties, obs_property_t *, obs_data_t *settings)
{
	show_advanced(properties, settings);
	return true;
}

// Choosing a style shows its values
bool style_modified(obs_properties_t *properties, obs_property_t *, obs_data_t *settings)
{
	const std::vector<BeautyStyle> styles = all_styles();
	if (const BeautyStyle *style = find_style(styles, obs_data_get_string(settings, kBeautyStyle)))
		set_beauty_values(settings, style->values);
	show_advanced(properties, settings);
	return true;
}

// Moving an advanced slider away from the style's value makes the style Custom
bool value_modified(obs_properties_t *, obs_property_t *, obs_data_t *settings)
{
	const std::vector<BeautyStyle> styles = all_styles();
	const BeautyStyle *style = find_style(styles, obs_data_get_string(settings, kBeautyStyle));
	if (!style || style->values == beauty_values(settings))
		return false;
	obs_data_set_string(settings, kBeautyStyle, kCustomStyle);
	return true;
}

bool save_style_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	auto filter = static_cast<BeautifyFilter *>(data);
	const std::string name = beautify_actions.ask_style_name ? beautify_actions.ask_style_name() : std::string();
	if (name.empty())
		return false;
	OBSDataAutoRelease settings = obs_source_get_settings(filter->context());
	if (!save_user_style(name, beauty_values(settings)))
		return false;
	obs_data_set_string(settings, kBeautyStyle, name.c_str());
	obs_source_update(filter->context(), settings);
	return true;
}

bool delete_style_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	auto filter = static_cast<BeautifyFilter *>(data);
	OBSDataAutoRelease settings = obs_source_get_settings(filter->context());
	const std::string name = obs_data_get_string(settings, kBeautyStyle);
	if (!user_style_selected(settings) ||
	    (beautify_actions.confirm_delete_style && !beautify_actions.confirm_delete_style(name)))
		return false;
	delete_user_style(name);
	// The filter keeps the values
	obs_data_set_string(settings, kBeautyStyle, kCustomStyle);
	obs_source_update(filter->context(), settings);
	return true;
}

const char *beautify_get_name(void *)
{
	return obs_module_text("Beautify.Name");
}

void *beautify_create(obs_data_t *settings, obs_source_t *source)
{
	return new BeautifyFilter(source, settings);
}

void beautify_destroy(void *data)
{
	delete static_cast<BeautifyFilter *>(data);
}

void beautify_update(void *data, obs_data_t *settings)
{
	static_cast<BeautifyFilter *>(data)->update(settings);
}

void beautify_tick(void *data, float seconds)
{
	static_cast<BeautifyFilter *>(data)->tick(seconds);
}

void beautify_render(void *data, gs_effect_t *)
{
	static_cast<BeautifyFilter *>(data)->render();
}

gs_color_space beautify_color_space(void *data, size_t count, const gs_color_space *preferred)
{
	return static_cast<BeautifyFilter *>(data)->color_space(count, preferred);
}

void beautify_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, kBeautyStyle, kDefaultStyle);
	obs_data_set_default_int(settings, kBeautyStrength, kDefaultBeautyStrength);
	if (const BeautyStyle *style = find_style(builtin_styles(), kDefaultStyle))
		for (size_t index = 0; index < kBeautyKeys.size(); index++)
			obs_data_set_default_int(settings, kBeautyKeys[index], beauty_value(style->values, index));
}

obs_properties_t *beautify_properties(void *data)
{
	obs_properties_t *properties = obs_properties_create();
	obs_property_t *style = obs_properties_add_list(properties, kBeautyStyle, obs_module_text("Beautify.Style"),
							OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_set_long_description(style, obs_module_text("Beautify.Style.Tooltip"));
	for (const BeautyStyle &item : all_styles()) {
		const std::string name = item.builtin ? obs_module_text(("Beautify.Style." + item.id).c_str())
						      : item.id;
		obs_property_list_add_string(style, name.c_str(), item.id.c_str());
	}
	obs_property_list_add_string(style, obs_module_text("Beautify.Style.Custom"), kCustomStyle);
	obs_property_set_modified_callback(style, style_modified);

	obs_property_t *strength = obs_properties_add_int_slider(properties, kBeautyStrength,
								 obs_module_text("Beautify.Strength"), 0, 100, 1);
	obs_property_set_long_description(strength, obs_module_text("Beautify.Strength.Tooltip"));

	obs_property_t *advanced = obs_properties_add_bool(properties, kAdvanced, obs_module_text("Beautify.Advanced"));
	obs_property_set_long_description(advanced, obs_module_text("Beautify.Advanced.Tooltip"));
	obs_property_set_modified_callback(advanced, advanced_modified);

	for (size_t index = 0; index < kBeautyKeys.size(); index++) {
		obs_property_t *value = obs_properties_add_int_slider(
			properties, kBeautyKeys[index], obs_module_text(kBeautyValueNames[index]), 0, 100, 1);
		obs_property_set_long_description(
			value, obs_module_text((std::string(kBeautyValueNames[index]) + ".Tooltip").c_str()));
		obs_property_set_modified_callback(value, value_modified);
	}
	obs_property_t *mask =
		obs_properties_add_bool(properties, kBeautyShowMask, obs_module_text("Beautify.ShowMask"));
	obs_property_set_long_description(mask, obs_module_text("Beautify.ShowMask.Tooltip"));
	obs_property_t *save = obs_properties_add_button2(properties, kSaveStyle, obs_module_text("Beautify.SaveStyle"),
							  save_style_clicked, data);
	obs_property_set_long_description(save, obs_module_text("Beautify.SaveStyle.Tooltip"));
	obs_property_t *remove = obs_properties_add_button2(
		properties, kDeleteStyle, obs_module_text("Beautify.DeleteStyle"), delete_style_clicked, data);
	obs_property_set_long_description(remove, obs_module_text("Beautify.DeleteStyle.Tooltip"));
	return properties;
}

} // namespace

void register_beautify_filter()
{
	obs_source_info info = {};
	info.id = kBeautifyFilterId;
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB;
	info.get_name = beautify_get_name;
	info.create = beautify_create;
	info.destroy = beautify_destroy;
	info.update = beautify_update;
	info.get_defaults = beautify_defaults;
	info.get_properties = beautify_properties;
	info.video_tick = beautify_tick;
	info.video_render = beautify_render;
	info.video_get_color_space = beautify_color_space;
	obs_register_source(&info);
}

void set_beautify_actions(BeautifyActions actions)
{
	beautify_actions = std::move(actions);
}

bool is_beautify_filter(obs_source_t *source)
{
	const char *id = source ? obs_source_get_unversioned_id(source) : nullptr;
	return id && std::strcmp(id, kBeautifyFilterId) == 0;
}

obs_source_t *find_beautify_filter(obs_source_t *source)
{
	obs_source_t *found = nullptr;
	obs_source_enum_filters(
		source,
		[](obs_source_t *, obs_source_t *filter, void *param) {
			auto found = static_cast<obs_source_t **>(param);
			if (!*found && is_beautify_filter(filter))
				*found = obs_source_get_ref(filter);
		},
		&found);
	return found;
}

obs_source_t *add_beautify_filter(obs_source_t *source, const std::string &style, int strength)
{
	OBSDataAutoRelease settings = obs_data_create();
	const std::vector<BeautyStyle> styles = all_styles();
	if (const BeautyStyle *chosen = find_style(styles, style))
		select_style(settings, *chosen);
	obs_data_set_int(settings, kBeautyStrength, strength);
	// Filter names are unique per source
	std::string name = obs_module_text("Beautify.Name");
	for (int number = 2;; number++) {
		OBSSourceAutoRelease existing = obs_source_get_filter_by_name(source, name.c_str());
		if (!existing)
			break;
		name = std::string(obs_module_text("Beautify.Name")) + " " + std::to_string(number);
	}
	obs_source_t *filter = obs_source_create(kBeautifyFilterId, name.c_str(), settings, nullptr);
	if (filter)
		obs_source_filter_add(source, filter);
	return filter;
}

BeautyValues beauty_values(obs_data_t *settings)
{
	BeautyValues values;
	for (size_t index = 0; index < kBeautyKeys.size(); index++)
		beauty_value(values, index) =
			std::clamp(static_cast<int>(obs_data_get_int(settings, kBeautyKeys[index])), 0, 100);
	return values;
}

void set_beauty_values(obs_data_t *settings, const BeautyValues &values)
{
	for (size_t index = 0; index < kBeautyKeys.size(); index++)
		obs_data_set_int(settings, kBeautyKeys[index], beauty_value(values, index));
}

void select_style(obs_data_t *settings, const BeautyStyle &style)
{
	obs_data_set_string(settings, kBeautyStyle, style.id.c_str());
	set_beauty_values(settings, style.values);
}

} // namespace bmagicam
