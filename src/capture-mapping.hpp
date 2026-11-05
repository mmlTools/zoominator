#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace zoominator {

/* Where a capture source's pixels come from on screen, and where the scene item
 * draws them. Screen values are global desktop points (the space QCursor::pos()
 * uses); source and crop values are source pixels; pos/scale are the item's
 * effective top-left placement and per-axis scale on the canvas. */
struct CaptureMap {
	double rectX = 0.0, rectY = 0.0, rectW = 0.0, rectH = 0.0;
	double srcW = 0.0, srcH = 0.0;
	double cropL = 0.0, cropT = 0.0, cropR = 0.0, cropB = 0.0;
	double posX = 0.0, posY = 0.0, scaleX = 1.0, scaleY = 1.0;
	double rotDeg = 0.0;
};

/* Screen-space rectangle of the part of the capture that survives the crop. */
inline bool capture_visible_rect(const CaptureMap &m, double &l, double &t, double &r, double &b)
{
	if (m.rectW <= 0.0 || m.rectH <= 0.0 || m.srcW <= 0.0 || m.srcH <= 0.0)
		return false;
	if (m.srcW - m.cropL - m.cropR <= 0.0 || m.srcH - m.cropT - m.cropB <= 0.0)
		return false;

	const double kx = m.srcW / m.rectW; /* source pixels per screen point */
	const double ky = m.srcH / m.rectH;
	l = m.rectX + m.cropL / kx;
	t = m.rectY + m.cropT / ky;
	r = m.rectX + (m.srcW - m.cropR) / kx;
	b = m.rectY + (m.srcH - m.cropB) / ky;
	return true;
}

/* Maps a cursor position to canvas coordinates through the capture's own screen
 * area and crop. A cursor outside the visible area is clamped to its edge. */
inline bool capture_map_cursor(const CaptureMap &m, double cx, double cy, double &sx, double &sy, bool &inside)
{
	double l, t, r, b;
	if (!capture_visible_rect(m, l, t, r, b))
		return false;

	inside = cx >= l && cx < r && cy >= t && cy < b;

	const double kx = m.srcW / m.rectW;
	const double ky = m.srcH / m.rectH;
	const double visW = m.srcW - m.cropL - m.cropR;
	const double visH = m.srcH - m.cropT - m.cropB;
	const double lx = std::clamp((cx - m.rectX) * kx - m.cropL, 0.0, visW) * m.scaleX;
	const double ly = std::clamp((cy - m.rectY) * ky - m.cropT, 0.0, visH) * m.scaleY;

	double s = 0.0, c = 1.0;
	if (m.rotDeg != 0.0) {
		const double rad = m.rotDeg * 3.14159265358979323846 / 180.0;
		s = std::sin(rad);
		c = std::cos(rad);
	}
	sx = m.posX + lx * c - ly * s;
	sy = m.posY + lx * s + ly * c;
	return true;
}

/* Picks the capture the cursor is over: the topmost (last) one whose visible
 * area contains it, otherwise the nearest one. Returns -1 when none is usable. */
inline int capture_pick(const std::vector<CaptureMap> &maps, double cx, double cy)
{
	int best = -1;
	double bestDist = 0.0;
	for (int i = 0; i < (int)maps.size(); ++i) {
		double l, t, r, b;
		if (!capture_visible_rect(maps[(size_t)i], l, t, r, b))
			continue;
		const double dx = cx < l ? l - cx : (cx >= r ? cx - r : 0.0);
		const double dy = cy < t ? t - cy : (cy >= b ? cy - b : 0.0);
		const double dist = dx * dx + dy * dy;
		if (best < 0 || dist < bestDist || (dist == 0.0 && bestDist == 0.0)) {
			best = i;
			bestDist = dist;
		}
	}
	return best;
}

} // namespace zoominator
