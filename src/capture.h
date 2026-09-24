#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <obs.h>

/// Renders an OBS source into a small BGRA buffer (targetWidth wide) from any thread.
/// The same pattern obs-websocket uses for GetSourceScreenshot: texrender + stage surface.
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
	gs_texrender_t *tr_ = nullptr;
	gs_stagesurf_t *st_ = nullptr;
	int stW_ = 0, stH_ = 0;
};
