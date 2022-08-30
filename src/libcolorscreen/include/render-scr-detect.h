#ifndef RENDER_SCR_DETECT_H
#define RENDER_SCR_DETECT_H
#include "render.h"
#include "scr-detect.h"
class render_scr_detect : public render
{
public:
  render_scr_detect (scr_detect_parameters &param, image_data &img, render_parameters &rparam, int dstmaxval)
    : render (img, rparam, dstmaxval)
  {
    m_scr_detect.set_parameters (param);
  }
  scr_detect::color_class classify_pixel (int x, int y)
  {
    if (x < 0 || x >= m_img.width || y < 0 || y >= m_img.height)
      return scr_detect::unknown;
    scr_detect::color_class t = m_scr_detect.classify_color (m_img.rgbdata[y][x].r / (luminosity_t)m_img.maxval,
							     m_img.rgbdata[y][x].g / (luminosity_t)m_img.maxval,
							     m_img.rgbdata[y][x].b / (luminosity_t)m_img.maxval);
    if (t == scr_detect::unknown)
      return scr_detect::unknown;
    for (int yy = std::max (y - 1, 0); yy < std::min (y + 1, m_img.height); yy++)
      for (int xx = std::max (x - 1, 0); xx < std::min (x + 1, m_img.width); xx++)
	if (xx != x || yy != y)
	  {
	    scr_detect::color_class q = m_scr_detect.classify_color (m_img.rgbdata[yy][xx].r / (luminosity_t)m_img.maxval,
								     m_img.rgbdata[yy][xx].g / (luminosity_t)m_img.maxval,
								     m_img.rgbdata[yy][xx].b / (luminosity_t)m_img.maxval);
	    if (q != scr_detect::unknown && q != t)
	      return scr_detect::unknown;
	  }
    return t;
  }
  inline 
  void get_screen_color (coord_t xp, coord_t yp, luminosity_t *r, luminosity_t *g, luminosity_t *b)
  {
    /* Center of pixel [0,0] is [0.5,0.5].  */
    xp -= (coord_t)0.5;
    yp -= (coord_t)0.5;
    int sx, sy;
    coord_t rx = my_modf (xp, &sx);
    coord_t ry = my_modf (yp, &sy);

    if (sx >= 1 && sx < m_img.width - 2 && sy >= 1 && sy < m_img.height - 2)
      {
	*r = cubic_interpolate (cubic_interpolate (m_color_class_map.get_color_red ( sx-1, sy-1), m_color_class_map.get_color_red (sx-1, sy), m_color_class_map.get_color_red (sx-1, sy+1), m_color_class_map.get_color_red (sx-1, sy+2), ry),
				cubic_interpolate (m_color_class_map.get_color_red ( sx-0, sy-1), m_color_class_map.get_color_red (sx-0, sy), m_color_class_map.get_color_red (sx-0, sy+1), m_color_class_map.get_color_red (sx-0, sy+2), ry),
				cubic_interpolate (m_color_class_map.get_color_red ( sx+1, sy-1), m_color_class_map.get_color_red (sx+1, sy), m_color_class_map.get_color_red (sx+1, sy+1), m_color_class_map.get_color_red (sx+1, sy+2), ry),
				cubic_interpolate (m_color_class_map.get_color_red ( sx+2, sy-1), m_color_class_map.get_color_red (sx+2, sy), m_color_class_map.get_color_red (sx+2, sy+1), m_color_class_map.get_color_red (sx+2, sy+2), ry),
				rx);
	*g = cubic_interpolate (cubic_interpolate (m_color_class_map.get_color_green ( sx-1, sy-1), m_color_class_map.get_color_green (sx-1, sy), m_color_class_map.get_color_green (sx-1, sy+1), m_color_class_map.get_color_green (sx-1, sy+2), ry),
				cubic_interpolate (m_color_class_map.get_color_green ( sx-0, sy-1), m_color_class_map.get_color_green (sx-0, sy), m_color_class_map.get_color_green (sx-0, sy+1), m_color_class_map.get_color_green (sx-0, sy+2), ry),
				cubic_interpolate (m_color_class_map.get_color_green ( sx+1, sy-1), m_color_class_map.get_color_green (sx+1, sy), m_color_class_map.get_color_green (sx+1, sy+1), m_color_class_map.get_color_green (sx+1, sy+2), ry),
				cubic_interpolate (m_color_class_map.get_color_green ( sx+2, sy-1), m_color_class_map.get_color_green (sx+2, sy), m_color_class_map.get_color_green (sx+2, sy+1), m_color_class_map.get_color_green (sx+2, sy+2), ry),
				rx);
	*b = cubic_interpolate (cubic_interpolate (m_color_class_map.get_color_blue ( sx-1, sy-1), m_color_class_map.get_color_blue (sx-1, sy), m_color_class_map.get_color_blue (sx-1, sy+1), m_color_class_map.get_color_blue (sx-1, sy+2), ry),
				cubic_interpolate (m_color_class_map.get_color_blue ( sx-0, sy-1), m_color_class_map.get_color_blue (sx-0, sy), m_color_class_map.get_color_blue (sx-0, sy+1), m_color_class_map.get_color_blue (sx-0, sy+2), ry),
				cubic_interpolate (m_color_class_map.get_color_blue ( sx+1, sy-1), m_color_class_map.get_color_blue (sx+1, sy), m_color_class_map.get_color_blue (sx+1, sy+1), m_color_class_map.get_color_blue (sx+1, sy+2), ry),
				cubic_interpolate (m_color_class_map.get_color_blue ( sx+2, sy-1), m_color_class_map.get_color_blue (sx+2, sy), m_color_class_map.get_color_blue (sx+2, sy+1), m_color_class_map.get_color_blue (sx+2, sy+2), ry),
				rx);
	 *r = std::min (std::max (*r, (luminosity_t)0), (luminosity_t)1);
	 *g = std::min (std::max (*g, (luminosity_t)0), (luminosity_t)1);
	 *b = std::min (std::max (*b, (luminosity_t)0), (luminosity_t)1);
     }
    else
      {
	*r = 0;
	*g = 0;
	*b = 0;
	return;
      }
  }
  void precompute_all ();
  enum render_scr_detect_type_t
  {
    render_type_original,
    render_type_adjusted_color,
    render_type_pixel_colors,
    render_type_realistic_scr,
    render_type_scr_blur
  };
  void inline render_adjusted_pixel_img (coord_t x, coord_t y, int *r, int *g, int *b)
  {
    int xx = x + 0.5;
    int yy = y + 0.5;
    if (xx < 0 || xx >= m_img.width || y < 0 || y >= m_img.height)
      {
	set_color (0, 0, 0, r,g,b);
	return;
      }
    luminosity_t rr = m_img.rgbdata[yy][xx].r, gg = m_img.rgbdata[yy][xx].g, bb = m_img.rgbdata[yy][xx].b;
    rr /= m_img.maxval;
    gg /= m_img.maxval;
    bb /= m_img.maxval;
    m_scr_detect.adjust_color (rr, gg, bb, &rr, &gg, &bb);
    set_color (rr, gg, bb, r,g,b);
  }
  DLL_PUBLIC static void render_tile (enum render_scr_detect_type_t render_type, scr_detect_parameters &param, image_data &img, render_parameters &rparam,
				      bool color, unsigned char *pixels, int rowstride, int pixelbytes, int width, int height,
				      double xoffset, double yoffset, double step);
protected:
  scr_detect m_scr_detect;
  color_class_map m_color_class_map;
};
class render_scr_detect_superpose_img : public render_scr_detect
{
public:
  inline render_scr_detect_superpose_img (scr_detect_parameters &param, image_data &data, render_parameters &rparam, int dst_maxval)
   : render_scr_detect (param, data, rparam, dst_maxval)
  { 
  }
  void inline render_pixel_img (coord_t x, coord_t y, int *r, int *g, int *b);
  void inline render_pixel_img_antialias (coord_t x, coord_t y, coord_t pixelsize, int steps, int *r, int *g, int *b);
  void inline analyze_tile (int x, int y, int w, int h, int stepx, int stepy, luminosity_t *r, luminosity_t *g, luminosity_t *b);
private:
  void inline sample_pixel_img (coord_t x, coord_t y, luminosity_t *r, luminosity_t *g, luminosity_t *b);
};
flatten_attr inline void
render_scr_detect_superpose_img::sample_pixel_img (coord_t x, coord_t y, luminosity_t *r, luminosity_t *g, luminosity_t *b)
{
  luminosity_t rr, gg, bb;
  get_screen_color (x, y, &rr, &gg, &bb);
  luminosity_t graydata = get_img_pixel (x, y);
  *r = graydata * rr;
  *g = graydata * gg;
  *b = graydata * bb;
}
flatten_attr void
render_scr_detect_superpose_img::render_pixel_img (coord_t x, coord_t y, int *r, int *g, int *b)
{
  luminosity_t rr, gg, bb;
  sample_pixel_img (x, y, &rr, &gg, &bb);
  set_color (rr, gg, bb, r,g,b);
}
flatten_attr void
render_scr_detect_superpose_img::render_pixel_img_antialias (coord_t x, coord_t y, coord_t pixelsize, int steps, int *r, int *g, int *b)
{
  luminosity_t rr = 0, gg = 0, bb = 0;

  if (pixelsize <= 1)
    {
      sample_pixel_img (x, y, &rr, &gg, &bb);
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
        sample_pixel_img (xp, yp, &rrr, &ggg, &bbb);
	rr += rrr;
	bb += bbb;
	gg += ggg;
      }
  set_color (3 * rr / (steps * steps), 3 * gg / (steps * steps), 3 * bb / (steps * steps), r,g,b);
}

/* Analyze average r, g and b color in a given tile in the image coordinates.  */
flatten_attr inline void
render_scr_detect_superpose_img::analyze_tile (int xs, int ys, int w, int h, int stepx, int stepy, luminosity_t *r, luminosity_t *g, luminosity_t *b)
{
  luminosity_t rw = 0, rr = 0, gw = 0, gg = 0, bw = 0, bb = 0;
  for (int x = xs; x < xs + w; x+=stepx)
    for (int y = ys; y < ys + h; y+=stepy)
      {
	luminosity_t l = fast_get_img_pixel (x, y);
	luminosity_t sr, sg, sb;
	m_color_class_map.get_color (x, y, &sr, &sg, &sb);

	rr += sr * l;
	rw += sr;
	gg += sg * l;
	gw += sg;
	bb += sb * l;
	bw += sb;
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
class render_scr_blur : public render_scr_detect
{
public:
  inline render_scr_blur (scr_detect_parameters &param, image_data &data, render_parameters &rparam, int dst_maxval)
   : render_scr_detect (param, data, rparam, dst_maxval)
  { 
  }
  flatten_attr void
  render_pixel_img (coord_t x, coord_t y, int *r, int *g, int *b)
  {
    int xmin = x - 0.5 - m_params.screen_blur_radius;
    int ymin = y - 0.5 - m_params.screen_blur_radius;
    /* TODO*/
    int xmax = floor (x - 0.5 + m_params.screen_blur_radius);
    int ymax = floor (y - 0.5 + m_params.screen_blur_radius);
    luminosity_t val[3] = {0, 0, 0};
    luminosity_t w[3] = {0, 0, 0};
    for (int yy = ymin; yy <= ymax; yy++)
      for (int xx = xmin; xx <= xmax; xx++)
	{
	  scr_detect::color_class t = classify_pixel (xx, yy);
	  if (t != scr_detect::unknown)
	    {
	      luminosity_t dist = sqrt ((xx + 0.5 - x) * (xx + 0.5 - x) + (yy + 0.5 - y) * (yy + 0.5 - y));
	      if (dist >= m_params.screen_blur_radius)
		continue;
	      luminosity_t weight = m_params.screen_blur_radius - dist;
	      val[(int)t] += get_img_pixel (xx, yy) * weight;
	      w[(int)t] += weight;
	    }
	}
    if (!w[0] || !w[1] || !w[2])
      set_color (0,0,0,r,g,b);
    else
      set_color (val[0]/w[0],val[1]/w[1],val[2]/w[2],r,g,b);
  }
private:
};
#endif
