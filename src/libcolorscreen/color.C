#include "include/color.h"
color_matrix
matrix_by_dye_xy (luminosity_t rx, luminosity_t ry,
		  luminosity_t gx, luminosity_t gy,
		  luminosity_t bx, luminosity_t by)
{
  xyz r = xyY_to_xyz (rx, ry, 0.2126 );
  xyz g = xyY_to_xyz (gx, gy, 0.7152 );
  xyz b = xyY_to_xyz (bx, by, 0.0722 );
  color_matrix m (r.x, g.x, b.x, 0,
		  r.y, g.y, b.y, 0,
		  r.z, g.z, b.z, 0,
		  0,   0,   0,   1);
  xyz white;
  srgb_to_xyz (1, 1, 1, &white.x, &white.y, &white.z);
  m.normalize_grayscale (white.x, white.y, white.z);
  return m;
}

color_matrix
matrix_by_dye_xyY (luminosity_t rx, luminosity_t ry, luminosity_t rY,
		   luminosity_t gx, luminosity_t gy, luminosity_t gY,
		   luminosity_t bx, luminosity_t by, luminosity_t bY)
{
  xyz r = xyY_to_xyz (rx, ry, rY);
  xyz g = xyY_to_xyz (gx, gy, gY);
  xyz b = xyY_to_xyz (bx, by, bY);
  color_matrix m (r.x, g.x, b.x, 0,
		  r.y, g.y, b.y, 0,
		  r.z, g.z, b.z, 0,
		  0,   0,   0,   1);
  xyz white;
  srgb_to_xyz (1, 1, 1, &white.x, &white.y, &white.z);
  m.normalize_grayscale (white.x, white.y, white.z);
  return m;
}

