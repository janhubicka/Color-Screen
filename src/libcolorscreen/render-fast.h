/* Fast rendering for previews.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#ifndef RENDERFAST_H
#define RENDERFAST_H
#include "render-to-scr.h"

namespace colorscreen
{
/* Renderer that uses fast sampling (nearest neighbor or simple averaging)
   suitable for previews.  */
class render_fast : public render_to_scr
{
public:
  /* Initialize renderer for PARAM, IMG, PARAMS and DST_MAXVAL.  */
  render_fast (const scr_to_img_parameters &param, const image_data &img,
	       const render_parameters &params, int dst_maxval);

  /* Set render type.  Not used for fast rendering.  */
  void
  set_render_type (render_type_parameters)
  {
  }

  /* Precompute all data needed for rendering.  Update PROGRESS.  */
  nodiscard_attr bool
  precompute_all (progress_info *progress)
  {
    return render_to_scr::precompute_all (true, true, progress);
  }

  /* Precompute all data needed for rendering in AREA.  Update PROGRESS.  */
  nodiscard_attr bool
  precompute_img_range (int_image_area area, progress_info *progress)
  {
    return render_to_scr::precompute_img_range (true, true, area, progress);
  }

  /* Sample pixel at position P in screen coordinates.  */
  pure_attr rgbdata
  sample_pixel_scr (point_t p) const
  {
    int_point_t isp = {nearest_int (p.x), nearest_int (p.y)};
    point_t ip = m_scr_to_img.to_img ({(coord_t)isp.x, (coord_t)isp.y});
    return sample_pixel (isp, ip);
  }

  /* Sample pixel at position P in image coordinates.  */
  pure_attr rgbdata
  sample_pixel_img (point_t p) const
  {
    point_t pscr = m_scr_to_img.to_scr ({p.x + (coord_t)0.5, p.y + (coord_t)0.5});
    /* We can not call directly sample_pixel; 
       img coordinates it expects should be representing the center of screen coordinates.  */
    return sample_pixel_scr (pscr);
  }

  /* Sample pixel at position P in integer image coordinates.  */
  pure_attr rgbdata
  fast_sample_pixel_img (int_point_t p) const
  {
    point_t pscr = m_scr_to_img.to_scr ({p.x + (coord_t)0.5, p.y + (coord_t)0.5});
    /* We can not call directly sample_pixel; 
       img coordinates it expects should be representing the center of screen coordinates.  */
    return sample_pixel_scr (pscr);
  }

  /* Fetch color data for downscaling.  Not implemented for fast renderer.
     P, WIDTH, HEIGHT, PIXELSIZE and PROGRESS are ignored.  */
  nodiscard_attr bool
  get_color_data (rgbdata *data, point_t p, int width, int height,
		  coord_t pixelsize, progress_info *progress)
  {
    (void)data; (void)p; (void)width; (void)height; (void)pixelsize; (void)progress;
    abort ();
  }

  /* Render final pixel at position P.  */
  pure_attr int_rgbdata
  render_pixel_final (point_t p) const;

private:
  /* Sample pixel at screen position P with image coordinates IMG_P.  */
  pure_attr rgbdata
  sample_pixel (int_point_t p, point_t img_p) const;
};
}
#endif
