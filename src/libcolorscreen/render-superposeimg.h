#ifndef RENDERSUPERPOSEIMG_H
#define RENDERSUPERPOSEIMG_H
#include <assert.h>
#include "include/render-to-scr.h"
#include "include/screen.h"
class render_superpose_img : public render_to_scr
{
public:
  inline render_superpose_img (scr_to_img_parameters &param, image_data &data, render_parameters &rparam, int dst_maxval, bool preview)
   : render_to_scr (param, data, rparam, dst_maxval),
     m_screen (NULL), m_color (false)
  { 
    coord_t radius = preview ? 0 : m_params.screen_blur_radius * pixel_size ();
    m_screen = get_screen (m_scr_to_img.get_type (), preview, radius, NULL);
  }
  inline ~render_superpose_img ()
  {
    if (m_screen)
      release_screen (m_screen);
  }
  void inline render_pixel_img (coord_t x, coord_t y, int *r, int *g, int *b);
  void inline render_pixel_img_antialias (coord_t x, coord_t y, coord_t pixelsize, int steps, int *r, int *g, int *b);
  void inline analyze_tile (int x, int y, int w, int h, int stepx, int stepy, luminosity_t *r, luminosity_t *g, luminosity_t *b);
  /* If set, use color scan for input.  */
  void set_color_display () { if (m_img.rgbdata) m_color = 1; }
  void get_color_data (rgbdata *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *);
private:
  void inline sample_pixel_img (coord_t x, coord_t y, coord_t scr_x, coord_t scr_y, luminosity_t *r, luminosity_t *g, luminosity_t *b);
  inline rgbdata fast_sample_pixel_img (int x, int y);
  screen *m_screen;
  bool m_color;
};

flatten_attr inline rgbdata
render_superpose_img::fast_sample_pixel_img (int x, int y)
{
  coord_t scr_x, scr_y;
  luminosity_t rs, gs, bs;
  luminosity_t ra, ga, ba;

  unsigned long ix, iy;
  m_scr_to_img.to_scr (x + 0.5, y + 0.5, &scr_x, &scr_y);

  ix = (unsigned long long) nearest_int (scr_x* screen::size) & (unsigned)(screen::size - 1);
  iy = (unsigned long long) nearest_int (scr_y* screen::size) & (unsigned)(screen::size - 1);
  rs = m_screen->mult[iy][ix][0];
  gs = m_screen->mult[iy][ix][1];
  bs = m_screen->mult[iy][ix][2];
  ra = m_screen->add[iy][ix][0];
  ga = m_screen->add[iy][ix][1];
  ba = m_screen->add[iy][ix][2];

  rgbdata ret;
  if (!m_color)
    {
      luminosity_t graydata = get_img_pixel (x+0.5, y+0.5);
      ret.red = graydata * rs + ra;
      ret.green = graydata * gs + ga;
      ret.blue = graydata * bs + ba;
    }
  else
    {
      luminosity_t rr = get_data_red (x, y), gg = get_data_green (x, y), bb = get_data_blue (x, y);
      ret.red = rr * m_screen->mult[iy][ix][0] + m_screen->add[iy][ix][0];
      ret.green = gg * m_screen->mult[iy][ix][1] + m_screen->add[iy][ix][1];
      ret.blue = bb * m_screen->mult[iy][ix][2] + m_screen->add[iy][ix][2];
    }
  ret.red = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, ret.red));
  ret.green = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, ret.green));
  ret.blue = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, ret.blue));
  return ret;
}

inline void
render_superpose_img::get_color_data (rgbdata *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *progress)
{ 
  downscale<render_superpose_img, rgbdata, &render_superpose_img::fast_sample_pixel_img, &account_rgb_pixel> (data, x, y, width, height, pixelsize, progress);
}

flatten_attr inline void
render_superpose_img::sample_pixel_img (coord_t x, coord_t y, coord_t scr_x, coord_t scr_y, luminosity_t *r, luminosity_t *g, luminosity_t *b)
{
  int ix, iy;

  ix = (unsigned long long) nearest_int (scr_x* screen::size) & (unsigned)(screen::size - 1);
  iy = (unsigned long long) nearest_int (scr_y* screen::size) & (unsigned)(screen::size - 1);
  if (!m_color)
    {
      luminosity_t graydata = get_img_pixel (x, y);
      *r = graydata * m_screen->mult[iy][ix][0] + m_screen->add[iy][ix][0];
      *g = graydata * m_screen->mult[iy][ix][1] + m_screen->add[iy][ix][1];
      *b = graydata * m_screen->mult[iy][ix][2] + m_screen->add[iy][ix][2];
    }
  else
    {
      luminosity_t rr, gg, bb;
      get_img_rgb_pixel (x, y, &rr, &gg, &bb);
      *r = rr * m_screen->mult[iy][ix][0] + m_screen->add[iy][ix][0];
      *g = gg * m_screen->mult[iy][ix][1] + m_screen->add[iy][ix][1];
      *b = bb * m_screen->mult[iy][ix][2] + m_screen->add[iy][ix][2];
    }
}
flatten_attr void
render_superpose_img::render_pixel_img (coord_t x, coord_t y, int *r, int *g, int *b)
{
  luminosity_t rr, gg, bb;
  coord_t scr_x, scr_y;
  m_scr_to_img.to_scr (x, y, &scr_x, &scr_y);
  sample_pixel_img (x, y, scr_x, scr_y, &rr, &gg, &bb);
  set_color (rr, gg, bb, r,g,b);
}
flatten_attr void
render_superpose_img::render_pixel_img_antialias (coord_t x, coord_t y, coord_t pixelsize, int steps, int *r, int *g, int *b)
{
  luminosity_t rr = 0, gg = 0, bb = 0;
  coord_t scr_x, scr_y;

  if (pixelsize <= 1)
    {
      m_scr_to_img.to_scr (x, y, &scr_x, &scr_y);
      sample_pixel_img (x, y, scr_x, scr_y, &rr, &gg, &bb);
      set_color (rr, gg, bb, r,g,b);
      return;
    }
  else
    {
      x -= pixelsize/4;
      y -= pixelsize/4;
      pixelsize *= 2;
      int steps2 = (pixelsize + 0.5) * 2;
      if (steps2 < steps)
	steps = steps2;
    }
  for (int xx = 0; xx < steps; xx ++)
    for (int yy = 0; yy < steps; yy ++)
      {
        luminosity_t rrr, ggg, bbb;
	coord_t xp = x + xx * (pixelsize / steps);
	coord_t yp = y + yy * (pixelsize / steps);
        m_scr_to_img.to_scr (xp, yp, &scr_x, &scr_y);
        sample_pixel_img (xp, yp, scr_x, scr_y, &rrr, &ggg, &bbb);
	rr += rrr;
	bb += bbb;
	gg += ggg;
      }
  set_color (3 * rr / (steps * steps), 3 * gg / (steps * steps), 3 * bb / (steps * steps), r,g,b);
}

/* Analyze average r, g and b color in a given tile in the image coordinates.  */
flatten_attr inline void
render_superpose_img::analyze_tile (int xs, int ys, int w, int h, int stepx, int stepy, luminosity_t *r, luminosity_t *g, luminosity_t *b)
{
  luminosity_t rw = 0, rr = 0, gw = 0, gg = 0, bw = 0, bb = 0;
  for (int x = xs; x < xs + w; x+=stepx)
    for (int y = ys; y < ys + h; y+=stepy)
      {
	coord_t scr_x, scr_y;
	luminosity_t l = fast_get_img_pixel (x, y);

	m_scr_to_img.to_scr (x + 0.5, y + 0.5, &scr_x, &scr_y);
	int ix = (unsigned long long) round (scr_x * screen::size) & (unsigned)(screen::size - 1);
 	int iy = (unsigned long long) round (scr_y * screen::size) & (unsigned)(screen::size - 1);
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
#endif