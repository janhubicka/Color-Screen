#ifndef RENDERFAST_H
#define RENDERFAST_H
#include "render-to-scr.h"
namespace colorscreen
{
class render_fast : public render_to_scr
{
public:
  render_fast (scr_to_img_parameters &param, image_data &img, render_parameters &params, int dst_maxval);
  void
  set_render_type (render_type_parameters)
  {
  }
  bool precompute_all (progress_info *progress)
  {
    return render_to_scr::precompute_all (true, true, progress);
  }
  bool precompute_img_range (int_image_area area, progress_info *progress)
  {
    return render_to_scr::precompute_img_range (true, true, area, progress);
  }
  rgbdata sample_pixel_scr (point_t p) const
  {
    int_point_t isp = {nearest_int (p.x), nearest_int (p.y)};
    point_t ip = m_scr_to_img.to_img ({(coord_t)isp.x, (coord_t)isp.y});
    return sample_pixel (isp, ip);

#if 0
    int_point_t ip = {nearest_int (p.x), nearest_int (p.y)};
    p.x = ip.x + 0.5;
    p.y = ip.y + 0.5;
    point_t pi = m_scr_to_img.to_img (p);
    return sample_pixel ({nearest_int (p.x), nearest_int (p.y)}, pi);
#endif
  }
  rgbdata sample_pixel_img (point_t p) const
  {
    point_t pscr = m_scr_to_img.to_scr ({p.x+(coord_t)0.5, p.y+(coord_t)0.5});
    /* We can not call directly sample_pixel; 
       img coordinates it expects should be representing the center of screen coordinates.  */
    return sample_pixel_scr (pscr);
  }
  rgbdata fast_sample_pixel_img (int_point_t p) const
  {
    point_t pscr = m_scr_to_img.to_scr ({p.x+(coord_t)0.5, p.y+(coord_t)0.5});
    /* We can not call directly sample_pixel; 
       img coordinates it expects should be representing the center of screen coordinates.  */
    return sample_pixel_scr (pscr);
  }
  /* Unimplemented; just exists to make rendering templates happy. We never downscale.  */
  bool get_color_data (rgbdata *data, point_t p, int width, int height, coord_t pixelsize, progress_info *progress) override
  {
    abort ();
  }
  void render_pixel_final (point_t p, int *r, int *g, int *b)
  {
    point_t scr = m_scr_to_img.final_to_scr ({(coord_t)(p.x - get_final_xshift ()), (coord_t)(p.y - get_final_yshift ())});
    int ix = scr.x + 0.5;
    int iy = scr.y + 0.5;
    point_t pi = m_scr_to_img.to_img ({(coord_t)ix, (coord_t)iy});
    rgbdata d = sample_pixel ({ix, iy}, pi);
    int_rgbdata out = out_color.final_color (d);
    *r = out.red; *g = out.green; *b = out.blue;
   }
private:
  pure_attr 
  rgbdata sample_pixel (int_point_t p, point_t img_p) const;
};
}
#endif
