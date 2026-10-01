#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <obs.h>

/// Renders an OBS source into a small BGRA buffer (targetWidth wide): callable from any thread,
/// but the actual render (texrender + stage surface) always runs as a task on OBS's own graphics
/// thread, in step with that source's own tick/render instead of racing it.
class Capture {
public:
	~Capture();
	bool grab(obs_source_t *source, int targetWidth, std::vector<uint8_t> &bgra, int &w, int &h, int &linesize);
	/// Only a part of the source (fractions of its width and height), rendered straight into a
	/// texture of that part's size: the read-back is a few hundred kilobytes, not the whole frame.
	/// targetWidth 0 = the part's own size on the source.
	bool grabRegion(obs_source_t *source, double rx, double ry, double rw, double rh, int targetWidth,
			std::vector<uint8_t> &bgra, int &w, int &h, int &linesize, obs_source_t *below = nullptr);
	/// The source as it looks under one of its filters (our hide filter): a warm feed is fully
	/// transparent on stream, yet what it holds can still be looked at.
	bool grabBelow(obs_source_t *source, obs_source_t *filter, int targetWidth, std::vector<uint8_t> &bgra, int &w,
		       int &h, int &linesize)
	{
		return grabRegion(source, 0, 0, 1, 1, targetWidth, bgra, w, h, linesize, filter);
	}

private:
	struct RenderCtx;
	static void renderTask(void *param);

	gs_texrender_t *tr_ = nullptr;
	gs_stagesurf_t *st_ = nullptr;
	int stW_ = 0, stH_ = 0;
};
