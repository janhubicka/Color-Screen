#ifndef RENDERSUPERPOSEIMG_H
#define RENDERSUPERPOSEIMG_H
#include <assert.h>
#include "render-to-scr.h"
#include "screen.h"
namespace colorscreen
{
class render_superpose_img : public render_to_scr
{
public:
  inline render_superpose_img (scr_to_img_parameters &param, image_data &data,
                               render_parameters &rparam, int dst_maxval)
      : render_to_scr (param, data, rparam, dst_maxval), m_screen (),
        m_color (false), m_preview (false)
  {
  }
  void
  set_render_type (render_type_parameters rtparam)
  {
    m_preview = (rtparam.type == render_type_preview_grid);
    if (rtparam.color)
      set_color_display ();
  }
  void
  set_preview_grid ()
  {
    m_preview = true;
  }
  inline ~render_superpose_img ()
  {
  }
  bool
  precompute_all (progress_info *progress)
  {
    sharpen_parameters sharpen;
    if (!m_preview)
      {
	sharpen = m_params.sharpen;
	coord_t psize = pixel_size ();
	sharpen.usm_radius = m_params.screen_blur_radius * psize;
	sharpen.scanner_mtf_scale *= psize;
      }
    m_screen = get_screen (m_scr_to_img.get_type (), m_preview, 
			   !m_color && !m_preview,
			   sharpen,
                           m_params.red_strip_width,
                           m_params.green_strip_width, progress);
    return render_to_scr::precompute_all (!m_color, m_preview, progress);
  }
  bool
  precompute_img_range (int_image_area area, progress_info *progress = NULL)
  {
    (void)area;
    return precompute_all (progress);
  }
  inline rgbdata sample_pixel_img (point_t p) const;
  void inline analyze_tile (int x, int y, int w, int h, int stepx, int stepy,
                            luminosity_t *r, luminosity_t *g, luminosity_t *b);
  /* If set, use color scan for input.  */
  void
  set_color_display ()
  {
    if (m_img.has_rgb ())
      m_color = 1;
  }
  bool get_color_data (rgbdata *data, point_t p, int width,
                       int height, coord_t pixelsize, progress_info *);
  pure_attr rgbdata
  sample_pixel_final (point_t p) const
  {
    point_t scr = m_scr_to_img.final_to_scr (
        { p.x - get_final_xshift (), p.y - get_final_yshift () });
    point_t pi = m_scr_to_img.to_img (scr);
    return sample_pixel_img (pi, scr);
  }
  pure_attr rgbdata
  sample_pixel_scr (point_t p) const
  {
    point_t pi = m_scr_to_img.to_img (p);
    return sample_pixel_img (pi, p);
  }
  pure_attr inline rgbdata fast_sample_pixel_img (int_point_t p) const;

private:
  pure_attr inline rgbdata
  sample_pixel_img (point_t p, point_t scr) const;
  std::shared_ptr<screen> m_screen;
  bool m_color;
  bool m_preview;
};

inline rgbdata
render_superpose_img::fast_sample_pixel_img (int_point_t p) const
{
  luminosity_t rs, gs, bs;
  luminosity_t ra, ga, ba;

  int ix, iy;
  point_t scr = m_scr_to_img.to_scr ({ p.x + (coord_t)0.5, p.y + (coord_t)0.5 });

  ix = (uint64_t)nearest_int (scr.x * screen::size)
       & (unsigned)(screen::size - 1);
  iy = (uint64_t)nearest_int (scr.y * screen::size)
       & (unsigned)(screen::size - 1);
  rs = m_screen->mult[iy][ix][0];
  gs = m_screen->mult[iy][ix][1];
  bs = m_screen->mult[iy][ix][2];
  ra = m_screen->add[iy][ix][0];
  ga = m_screen->add[iy][ix][1];
  ba = m_screen->add[iy][ix][2];

  rgbdata ret;
  if (!m_color)
    {
      luminosity_t graydata = get_img_pixel ({(coord_t)(p.x + 0.5), (coord_t)(p.y + 0.5)});
      ret.red = graydata * rs + ra;
      ret.green = graydata * gs + ga;
      ret.blue = graydata * bs + ba;
    }
  else
    {
      luminosity_t rr = get_data_red ({p.x, p.y}), gg = get_data_green ({p.x, p.y}),
                   bb = get_data_blue ({p.x, p.y});
      ret.red = rr * m_screen->mult[iy][ix][0] + m_screen->add[iy][ix][0];
      ret.green = gg * m_screen->mult[iy][ix][1] + m_screen->add[iy][ix][1];
      ret.blue = bb * m_screen->mult[iy][ix][2] + m_screen->add[iy][ix][2];
    }
  ret.red
      = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, ret.red));
  ret.green
      = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, ret.green));
  ret.blue
      = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, ret.blue));
  return ret;
}

inline bool
render_superpose_img::get_color_data (rgbdata *data, point_t p,
                                      int width, int height, coord_t pixelsize,
                                      progress_info *progress)
{
  return downscale<render_superpose_img, rgbdata,
                   &render_superpose_img::fast_sample_pixel_img> (
      data, p, width, height, pixelsize, progress);
}

pure_attr inline rgbdata
render_superpose_img::sample_pixel_img (point_t p, point_t scr) const
{
  int ix, iy;

  ix = (uint64_t)nearest_int (scr.x * screen::size)
       & (unsigned)(screen::size - 1);
  iy = (uint64_t)nearest_int (scr.y * screen::size)
       & (unsigned)(screen::size - 1);
  if (!m_color)
    {
      luminosity_t graydata = get_img_pixel (p);
      return { graydata * m_screen->mult[iy][ix][0] + m_screen->add[iy][ix][0],
               graydata * m_screen->mult[iy][ix][1] + m_screen->add[iy][ix][1],
               graydata * m_screen->mult[iy][ix][2]
                   + m_screen->add[iy][ix][2] };
    }
  else
    {
      rgbdata c = get_img_rgb_pixel (p);
      return { c.red * m_screen->mult[iy][ix][0] + m_screen->add[iy][ix][0],
               c.green * m_screen->mult[iy][ix][1] + m_screen->add[iy][ix][1],
               c.blue * m_screen->mult[iy][ix][2] + m_screen->add[iy][ix][2] };
    }
}
pure_attr inline rgbdata
render_superpose_img::sample_pixel_img (point_t p) const
{
  point_t scr = m_scr_to_img.to_scr (p);
  return sample_pixel_img (p, scr);
}

/* Analyze average r, g and b color in a given tile in the image coordinates.
 */
inline void
render_superpose_img::analyze_tile (int xs, int ys, int w, int h, int stepx,
                                    int stepy, luminosity_t *r,
                                    luminosity_t *g, luminosity_t *b)
{
  luminosity_t rw = 0, rr = 0, gw = 0, gg = 0, bw = 0, bb = 0;
  for (int x = xs; x < xs + w; x += stepx)
    for (int y = ys; y < ys + h; y += stepy)
      {
        luminosity_t l = fast_get_img_pixel ({x, y});

        point_t scr
            = m_scr_to_img.to_scr ({ x + (coord_t)0.5, y + (coord_t)0.5 });
        int ix = (uint64_t)nearest_int (scr.x * screen::size)
                 & (unsigned)(screen::size - 1);
        int iy = (uint64_t)nearest_int (scr.y * screen::size)
                 & (unsigned)(screen::size - 1);
        rr += m_screen->mult[iy][ix][0] * l;
        rw += m_screen->mult[iy][ix][0];
        gg += m_screen->mult[iy][ix][1] * l;
        gw += m_screen->mult[iy][ix][1];
        bb += m_screen->mult[iy][ix][2] * l;
        bw += m_screen->mult[iy][ix][2];
      }
  if (rw)
    *r = rr / rw;
  else
    *r = 0;
  if (gw)
    *g = gg / gw;
  else
    *g = 0;
  if (bw)
    *b = bb / bw;
  else
    *b = 0;
}
}
#endif
