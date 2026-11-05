#pragma once

#include <algorithm>
#include <cmath>

namespace zoominator {

struct ItemCrop {
	int l = 0, t = 0, r = 0, b = 0;
};

/* Where a scene item's visible (cropped) pixels land, relative to its position
 * and before rotation: top-left offset and per-axis scale on the canvas. */
struct ItemPlacement {
	double offX = 0.0, offY = 0.0;
	double scaleX = 1.0, scaleY = 1.0;
};

/* libobs alignment flags and bounds types, kept local so this header has no
 * OBS dependency and can be unit tested on its own. */
static constexpr unsigned kAlignLeft = 1u, kAlignRight = 2u, kAlignTop = 4u, kAlignBottom = 8u;
enum BoundsType { BoundsNone, BoundsStretch, BoundsInner, BoundsOuter, BoundsWidth, BoundsHeight, BoundsMaxOnly };

/* libobs add_alignment(): integer sizes, halved with integer division. */
inline void add_alignment(double &x, double &y, unsigned align, int cx, int cy)
{
	if (align & kAlignRight)
		x += cx;
	else if (!(align & kAlignLeft))
		x += cx / 2;
	if (align & kAlignBottom)
		y += cy;
	else if (!(align & kAlignTop))
		y += cy / 2;
}

/* Mirrors libobs update_item_transform() and calculate_bounds_data() for an
 * item with visible size visW x visH (crop-to-bounds overdraw not modelled). */
inline ItemPlacement item_placement(int boundsType, unsigned align, unsigned boundsAlign, double boundsW,
				    double boundsH, double visW, double visH, double scaleX, double scaleY)
{
	ItemPlacement p;
	p.scaleX = scaleX;
	p.scaleY = scaleY;
	double originX = 0.0, originY = 0.0;
	int cx = 0, cy = 0;

	if (boundsType != BoundsNone && boundsW > 0.0 && boundsH > 0.0 && visW > 0.0 && visH > 0.0) {
		double width = visW * std::fabs(scaleX);
		double height = visH * std::fabs(scaleY);
		const double itemAspect = width / height;
		const double boundsAspect = boundsW / boundsH;
		int type = boundsType;
		if (boundsType == BoundsMaxOnly && (width > boundsW || height > boundsH))
			type = BoundsInner;

		if (type == BoundsInner || type == BoundsOuter) {
			bool useWidth = boundsAspect < itemAspect;
			if (boundsType == BoundsOuter)
				useWidth = !useWidth;
			const double mul = useWidth ? boundsW / width : boundsH / height;
			p.scaleX *= mul;
			p.scaleY *= mul;
		} else if (type == BoundsWidth) {
			p.scaleX *= boundsW / width;
			p.scaleY *= boundsW / width;
		} else if (type == BoundsHeight) {
			p.scaleX *= boundsH / height;
			p.scaleY *= boundsH / height;
		} else if (type == BoundsStretch) {
			p.scaleX = std::copysign(boundsW / visW, scaleX);
			p.scaleY = std::copysign(boundsH / visH, scaleY);
		}

		width = visW * p.scaleX;
		height = visH * p.scaleY;
		const double widthDiff = boundsW - std::fabs(width);
		const double heightDiff = boundsH - std::fabs(height);
		cx = (int)(unsigned)boundsW;
		cy = (int)(unsigned)boundsH;
		add_alignment(originX, originY, boundsAlign, (int)-widthDiff, (int)-heightDiff);
	} else {
		cx = (int)(unsigned)(visW * scaleX);
		cy = (int)(unsigned)(visH * scaleY);
	}

	add_alignment(originX, originY, align, cx, cy);
	p.offX = -originX;
	p.offY = -originY;
	return p;
}

/* Crop that shows 1/z of an item's originally visible region, centred on the
 * focus point (source pixels) and kept inside that region. Rendered at the
 * item's original size, this zooms the content without leaving its frame. */
inline bool box_zoom_crop(int srcW, int srcH, const ItemCrop &orig, double z, double focusU, double focusV,
			  ItemCrop &out)
{
	const int visW = srcW - orig.l - orig.r;
	const int visH = srcH - orig.t - orig.b;
	if (srcW <= 0 || srcH <= 0 || visW <= 0 || visH <= 0)
		return false;
	if (!(z > 1.0)) {
		out = orig;
		return true;
	}

	const int newW = std::max(1, (int)std::lround(visW / z));
	const int newH = std::max(1, (int)std::lround(visH / z));
	const long left = std::clamp(std::lround(focusU - newW * 0.5), (long)orig.l, (long)(orig.l + visW - newW));
	const long top = std::clamp(std::lround(focusV - newH * 0.5), (long)orig.t, (long)(orig.t + visH - newH));

	out.l = (int)left;
	out.t = (int)top;
	out.r = srcW - out.l - newW;
	out.b = srcH - out.t - newH;
	return true;
}

} // namespace zoominator
