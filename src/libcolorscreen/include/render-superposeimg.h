#ifndef RENDERSUPERPOSEIMG_H
#define RENDERSUPERPOSEIMG_H
#include <assert.h>
#include "render.h"
#include "screen.h"
class render_superpose_img : public render_to_scr
{
public:
  inline render_superpose_img (scr_to_img_parameters &param, image_data &data, render_parameters &rparam, int dst_maxval, bool empty, bool preview)
   : render_to_scr (param, data, rparam, dst_maxval),
     m_color (false)
  { 
    coord_t x,x2, y, y2;
    if (empty)
      {
        static screen empty_screen;
	static bool initialized;
	if (!initialized)
	  {
	    empty_screen.empty ();
	    initialized = true;
	  }
	m_screen = &empty_screen;
      }
    else if (preview)
      {
        static screen preview_screen;
	static bool initialized;
	static enum scr_type t;
	if (!initialized != t != m_scr_to_img.get_type ())
	  {
	    preview_screen.initialize_preview (m_scr_to_img.get_type ());
	    initialized = true;
	  }
	m_screen = &preview_screen;
      }
    else if (!preview && !empty)
      {
        static screen blured_screen;
	static coord_t r = -1;
	static enum scr_type t;
	coord_t radius = m_params.screen_blur_radius;
	m_scr_to_img.to_scr (0, 0, &x, &y);
	m_scr_to_img.to_scr (1, 0, &x2, &y2);
	radius *= sqrt ((x2 - x) * (x2 - x) + (y2 - y) * (y2 - y));

	if (t != m_scr_to_img.get_type () || fabs (r - radius) > 0.01)
	  {
	    screen *s = new screen;
	    s->initialize (m_scr_to_img.get_type ());
	    blured_screen.initialize_with_blur (*s, radius);
	    t = m_scr_to_img.get_type ();
	    r = radius;
	  }
	m_screen = &blured_screen;
      }

  }
  void inline render_pixel_img (coord_t x, coord_t y, int *r, int *g, int *b);
  void inline render_pixel_img_antialias (coord_t x, coord_t y, coord_t pixelsize, int steps, int *r, int *g, int *b);
  void inline analyze_tile (int x, int y, int w, int h, int stepx, int stepy, luminosity_t *r, luminosity_t *g, luminosity_t *b);
  /* If set, use color scan for input.  */
  void set_color_display () { if (m_img.rgbdata) m_color = 1; }
private:
  void inline sample_pixel_img (coord_t x, coord_t y, coord_t scr_x, coord_t scr_y, luminosity_t *r, luminosity_t *g, luminosity_t *b);
  screen *m_screen;
  bool m_color;
};

flatten_attr inline void
render_superpose_img::sample_pixel_img (coord_t x, coord_t y, coord_t scr_x, coord_t scr_y, luminosity_t *r, luminosity_t *g, luminosity_t *b)
{
  int ix, iy;

  ix = (unsigned long long) round (scr_x* screen::size) & (unsigned)(screen::size - 1);
  iy = (unsigned long long) round (scr_y* screen::size) & (unsigned)(screen::size - 1);
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
  double rw = 0, rr = 0, gw = 0, gg = 0, bw = 0, bb = 0;
  for (int x = xs; x < xs + w; x+=stepx)
    for (int y = ys; y < ys + h; y+=stepy)
      {
	coord_t scr_x, scr_y;
	luminosity_t l = fast_get_img_pixel (x, y);

	m_scr_to_img.to_scr (x + 0.5, y + 0.5, &scr_x, &scr_y);
	int ix = (unsigned long long) round (scr_x * screen::size) & (unsigned)(screen::size - 1);
 	int iy = (unsigned long long) round (scr_y * screen::size) & (unsigned)(screen::size - 1);
	if (m_screen->mult[iy][ix][0] > 0.8)
	  {
	    rr += m_screen->mult[iy][ix][0] * l;
	    rw += m_screen->mult[iy][ix][0];
	  }
	if (m_screen->mult[iy][ix][1] > 0.8)
	  {
	    gg += m_screen->mult[iy][ix][1] * l;
	    gw += m_screen->mult[iy][ix][1];
	  }
	if (m_screen->mult[iy][ix][2] > 0.8)
	  {
	    bb += m_screen->mult[iy][ix][2] * l;
	    bw += m_screen->mult[iy][ix][2];
	  }
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
