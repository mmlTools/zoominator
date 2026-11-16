#pragma once

#include <algorithm>

namespace zoominator {

/* Edge zone ("camera dead zone") for cursor-follow.
 *
 * The zoomed view is tracked by its centre. While the cursor stays inside the
 * inner rectangle of the view the centre does not move; once the cursor enters
 * the edge zone - the outer `zoneRatio` of the view on each side - the centre
 * gives way by exactly the overshoot, so the cursor rides the zone boundary
 * and the view stops as soon as the cursor does.
 *
 * Kept free of OBS types, like box-zoom.hpp, so it can be unit tested alone. */

/* One axis: the view centre after the cursor has moved. `viewSize` is the
 * extent of the zoomed view along that axis. */
inline double edge_zone_center(double center, double cursor, double viewSize, double zoneRatio)
{
	const double inner = std::max(0.0, 0.5 - zoneRatio) * viewSize;
	return std::clamp(center, cursor - inner, cursor + inner);
}

/* The view centre after a zoom change that keeps the cursor on the same spot
 * of the view, i.e. zooms about the cursor instead of about the view centre. */
inline double edge_zone_rezoom(double center, double cursor, double zoomFrom, double zoomTo)
{
	return cursor - (cursor - center) * (zoomFrom / zoomTo);
}

/* The canvas zoom without overflow scales the scene about the focal point
 * itself (p' = f + (p - f) * z), so there the focal point is not the centre of
 * the view. These convert between the two for a canvas of `size` at z > 1. */
inline double focal_to_view_center(double focal, double size, double z)
{
	return focal * (1.0 - 1.0 / z) + size / (2.0 * z);
}

inline double view_center_to_focal(double center, double size, double z)
{
	return (center * z - size * 0.5) / (z - 1.0);
}

} // namespace zoominator
