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
  coord_t dufay_red_strip_width, dufay_green_strip_height;

  bool
  operator==(screen_params &o)
  {
    return t == o.t
	   && preview == o.preview
	   && fabs (radius - o.radius) < 0.01
	   && dufay_red_strip_width == o.dufay_red_strip_width
	   && dufay_green_strip_height == o.dufay_green_strip_height;
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
    s->initialize (p.t, p.dufay_red_strip_width, p.dufay_green_strip_height);
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
  return m_scr_to_img.pixel_size (m_img.width, m_img.height);
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
render_to_scr::get_screen (enum scr_type t, bool preview, coord_t radius, coord_t red_strip_width, coord_t green_strip_width, progress_info *progress, uint64_t *id)
{
  screen_params p = {t, preview, radius, red_strip_width, green_strip_width};
  return screen_cache.get (p, progress, id);
}

void
render_to_scr::release_screen (screen *s)
{
  screen_cache.release (s);
}

void
render_img::get_color_data (rgbdata *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *progress)
{
  if (m_profiled)
    downscale<render_img, rgbdata, &render_img::get_profiled_rgb_pixel, &account_rgb_pixel> (data, x, y, width, height, pixelsize, progress);
  else
    render::get_color_data (data, x, y, width, height, pixelsize, progress);
}
