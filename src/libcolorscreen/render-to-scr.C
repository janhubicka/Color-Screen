#include <cassert>
#include "include/render-to-scr.h"

/* Return approximate size of an scan pixel in screen corrdinates.  */
coord_t
render_to_scr::pixel_size ()
{
  coord_t x,x2, y, y2;
  m_scr_to_img.to_scr (0, 0, &x, &y);
  m_scr_to_img.to_scr (1, 0, &x2, &y2);
  return sqrt ((x2 - x) * (x2 - x) + (y2 - y) * (y2 - y));
}

bool
render_to_scr::precompute_all(progress_info *progress)
{
  return render::precompute_all (m_scr_to_img.get_type () != Dufay, progress);
}
