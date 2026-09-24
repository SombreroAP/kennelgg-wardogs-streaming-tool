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
	bool ok = false;
	obs_enter_graphics();
	if (!tr_)
		tr_ = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
	if (!st_ || stW_ != w || stH_ != h) {
		if (st_)
			gs_stagesurface_destroy(st_);
		st_ = gs_stagesurface_create(w, h, GS_BGRA);
		stW_ = w;
		stH_ = h;
	}
	gs_texrender_reset(tr_);
	if (gs_texrender_begin(tr_, w, h)) {
		struct vec4 zero;
		vec4_zero(&zero);
		gs_clear(GS_CLEAR_COLOR, &zero, 0.0f, 0);
		// the projection window is the part: everything outside it falls off the texture
		gs_ortho((float)(rx * sw), (float)(rx * sw + pw), (float)(ry * sh), (float)(ry * sh + ph), -100.0f,
			 100.0f);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		if (below)
			obs_source_skip_video_filter(below); // renders what that filter is given
		else
			obs_source_video_render(source);
		gs_blend_state_pop();
		gs_texrender_end(tr_);
		gs_stage_texture(st_, gs_texrender_get_texture(tr_));
		uint8_t *data = nullptr;
		uint32_t ls = 0;
		if (gs_stagesurface_map(st_, &data, &ls)) {
			linesize = (int)ls;
			bgra.resize((size_t)ls * h);
			memcpy(bgra.data(), data, (size_t)ls * h);
			gs_stagesurface_unmap(st_);
			ok = true;
		}
	}
	obs_leave_graphics();
	return ok;
}
