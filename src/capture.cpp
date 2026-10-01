#include "capture.h"
#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/vec4.h>
#include <algorithm>
#include <cmath>
#include <cstring>

Capture::~Capture()
{
	obs_enter_graphics();
	if (tr_)
		gs_texrender_destroy(tr_);
	if (st_)
		gs_stagesurface_destroy(st_);
	obs_leave_graphics();
}

bool Capture::grab(obs_source_t *source, int targetWidth, std::vector<uint8_t> &bgra, int &w, int &h, int &linesize)
{
	return grabRegion(source, 0, 0, 1, 1, targetWidth, bgra, w, h, linesize);
}

struct Capture::RenderCtx {
	Capture *self;
	obs_source_t *source;
	obs_source_t *below;
	float x0, x1, y0, y1;
	int w, h;
	std::vector<uint8_t> *bgra;
	int *linesize;
	bool ok = false;
};

// Runs the texrender + stage-surface work (and the obs_source_video_render/obs_source_skip_video_filter
// call) on OBS's own graphics thread via obs_queue_task, not on whichever worker thread asked for the
// grab. obs_enter_graphics() only locks the GPU device context, which does not stop this call from
// landing in the middle of that same source's own tick() on the real graphics thread - e.g. a window
// capture tearing down its hook/texture right as its captured window closes. Rendering from a second
// thread at that moment crashed OBS inside win-capture.dll (LOG-2524); running the render as a task on
// the graphics thread keeps it ordered with that source's own tick/render instead of racing it.
void Capture::renderTask(void *param)
{
	auto *c = static_cast<RenderCtx *>(param);
	Capture &self = *c->self;
	if (!self.tr_)
		self.tr_ = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
	if (!self.st_ || self.stW_ != c->w || self.stH_ != c->h) {
		if (self.st_)
			gs_stagesurface_destroy(self.st_);
		self.st_ = gs_stagesurface_create(c->w, c->h, GS_BGRA);
		self.stW_ = c->w;
		self.stH_ = c->h;
	}
	gs_texrender_reset(self.tr_);
	if (gs_texrender_begin(self.tr_, c->w, c->h)) {
		struct vec4 zero;
		vec4_zero(&zero);
		gs_clear(GS_CLEAR_COLOR, &zero, 0.0f, 0);
		// the projection window is the part: everything outside it falls off the texture
		gs_ortho(c->x0, c->x1, c->y0, c->y1, -100.0f, 100.0f);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		if (c->below)
			obs_source_skip_video_filter(c->below); // renders what that filter is given
		else
			obs_source_video_render(c->source);
		gs_blend_state_pop();
		gs_texrender_end(self.tr_);
		gs_stage_texture(self.st_, gs_texrender_get_texture(self.tr_));
		uint8_t *data = nullptr;
		uint32_t ls = 0;
		if (gs_stagesurface_map(self.st_, &data, &ls)) {
			*c->linesize = (int)ls;
			c->bgra->resize((size_t)ls * c->h);
			memcpy(c->bgra->data(), data, (size_t)ls * c->h);
			gs_stagesurface_unmap(self.st_);
			c->ok = true;
		}
	}
}

bool Capture::grabRegion(obs_source_t *source, double rx, double ry, double rw, double rh, int targetWidth,
			 std::vector<uint8_t> &bgra, int &w, int &h, int &linesize, obs_source_t *below)
{
	if (!source)
		return false;
	uint32_t sw = obs_source_get_width(source), sh = obs_source_get_height(source);
	if (sw == 0 || sh == 0)
		return false;
	rx = std::max(0.0, std::min(1.0, rx));
	ry = std::max(0.0, std::min(1.0, ry));
	rw = std::max(0.0, std::min(1.0 - rx, rw));
	rh = std::max(0.0, std::min(1.0 - ry, rh));
	double pw = rw * sw, ph = rh * sh; // the part, in source pixels
	if (pw < 4 || ph < 4)
		return false;
	w = targetWidth > 0 ? targetWidth : (int)std::lround(pw);
	h = (int)std::lround(ph * w / pw);
	if (h < 8)
		return false;
	RenderCtx ctx{
		this, source, below, (float)(rx * sw), (float)(rx * sw + pw), (float)(ry * sh), (float)(ry * sh + ph),
		w,    h,      &bgra, &linesize};
	obs_queue_task(OBS_TASK_GRAPHICS, renderTask, &ctx, true);
	return ctx.ok;
}
