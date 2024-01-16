#include <cassert>
#include "include/render-to-scr.h"
#include "include/screen.h"
#include "lru-cache.h"

namespace
{
struct screen_params
{
  enum scr_type t;
  bool preview;
  coord_t radius;

  bool
  operator==(screen_params &o)
  {
    return t == o.t
	   && preview == o.preview
	   && fabs (radius - o.radius) < 0.01;
  }
};

screen *
get_new_screen (struct screen_params &p, progress_info *progress)
{
  screen *s = new screen;
  if (progress)
    progress->set_task ("initializing screen", 1);
  if (p.preview)
    s->initialize_preview (p.t);
  else
    s->initialize (p.t);
  if (!p.radius)
    return s;
  screen *blurred = new screen;
  if (progress)
    progress->set_task ("bluring screen", 1);
  blurred->initialize_with_blur (*s, p.radius);
  delete s;
  return blurred;
}
static lru_cache <screen_params, screen, get_new_screen, 4> screen_cache ("screen");
}

/* Return approximate size of an scan pixel in screen corrdinates.  */
coord_t
render_to_scr::pixel_size ()
{
  coord_t x,x2, y, y2;
  coord_t bx = m_img.width / 2, by = m_img.height / 2;
  m_scr_to_img.to_scr (bx + 0, by + 0, &x, &y);
  m_scr_to_img.to_scr (bx + 1, by + 0, &x2, &y2);
  return sqrt ((x2 - x) * (x2 - x) + (y2 - y) * (y2 - y));
}

bool
render_to_scr::precompute_all(bool grayscale_needed, bool normalized_patches, progress_info *progress)
{
  return render::precompute_all (grayscale_needed, normalized_patches, m_scr_to_img.patch_proportions (), progress);
}
bool
render_to_scr::precompute (bool grayscale_needed, bool normalized_patches, coord_t, coord_t, coord_t, coord_t, progress_info *progress)
{
  return precompute_all (grayscale_needed, normalized_patches, progress);
}
bool
render_to_scr::precompute_img_range (bool grayscale_needed, bool normalized_patches, coord_t, coord_t, coord_t, coord_t, progress_info *progress)
{
  return precompute_all (grayscale_needed, normalized_patches, progress);
}

screen *
render_to_scr::get_screen (enum scr_type t, bool preview, coord_t radius, progress_info *progress, uint64_t *id)
{
  screen_params p = {t, preview, radius};
  return screen_cache.get (p, progress, id);
}

void
render_to_scr::release_screen (screen *s)
{
  screen_cache.release (s);
}
