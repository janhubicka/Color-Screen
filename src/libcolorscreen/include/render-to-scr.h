#ifndef RENDER_TO_SCR_H
#define RENDER_TO_SCR_H
#include "progress-info.h"
#include "render.h"
#include "scr-to-img.h"

/* Base class for renderes that use mapping between image and screen coordinates.  */
class render_to_scr : public render
{
public:
  render_to_scr (scr_to_img_parameters &param, image_data &img, render_parameters &rparam, int dstmaxval)
    : render (img, rparam, dstmaxval)
  {
    m_scr_to_img.set_parameters (param, img);
    m_scr_to_img.get_range (m_img.width, m_img.height, &m_scr_xshift, &m_scr_yshift, &m_scr_width, &m_scr_height);
  }
  inline luminosity_t get_img_pixel_scr (coord_t x, coord_t y);
  coord_t pixel_size ();
  DLL_PUBLIC bool precompute_all (progress_info *progress);
  DLL_PUBLIC bool precompute (luminosity_t, luminosity_t, luminosity_t, luminosity_t, progress_info *progress) {return precompute_all (progress);}
  DLL_PUBLIC bool precompute_img_range (luminosity_t, luminosity_t, luminosity_t, luminosity_t, progress_info *progress) {return precompute_all (progress);}
  /* This returns screen coordinate width of rendered output.  */
  int get_width ()
  {
    return m_scr_width;
  }
  /* This returns screen coordinate height of rendered output.  */
  int get_height ()
  {
    return m_scr_height;
  }
  DLL_PUBLIC static bool render_tile (enum render_type_t render_type, scr_to_img_parameters &param, image_data &img, render_parameters &rparam,
				      bool color, unsigned char *pixels, int rowstride, int pixelbytes, int width, int height,
				      double xoffset, double yoffset, double step, progress_info *progress = NULL);
protected:
  inline luminosity_t sample_scr_diag_square (coord_t xc, coord_t yc, coord_t s);
  inline luminosity_t sample_scr_square (coord_t xc, coord_t yc, coord_t w, coord_t h);
  static class screen *get_screen (enum scr_type t, bool preview, coord_t radius, progress_info *progress);
  static void release_screen (class screen *scr);

  /* Transformation between screen and image coordinates.  */
  scr_to_img m_scr_to_img;
  /* Rectangular section of the screen to which the whole image fits.

     The section is having dimensions scr_width x scr_height and will
     start at position (-scr_xshift, -scr_yshift).  */
  int m_scr_xshift, m_scr_yshift;
  int m_scr_width, m_scr_height;
};

/* Do no rendering of color screen.  */
class render_img : public render_to_scr
{
public:
  render_img (scr_to_img_parameters &param, image_data &img, render_parameters &rparam, int dstmaxval)
    : render_to_scr (param, img, rparam, dstmaxval), m_color (false)
  { }
  void set_color_display () { if (m_img.rgbdata) m_color = 1; }
  void inline render_pixel_img (coord_t x, coord_t y, int *r, int *g, int *b)
  {
    luminosity_t gg, rr, bb;
    if (!m_color)
      rr = gg = bb = get_img_pixel (x, y);
    else
      get_img_rgb_pixel (x, y, &rr, &gg, &bb);
    set_color (rr, gg, bb, r, g, b);
  }
  void inline fast_render_pixel_img (coord_t x, coord_t y, int *r, int *g, int *b)
  {
    luminosity_t gg, rr, bb;
    if (!m_color)
      rr = gg = bb = fast_get_img_pixel (x, y);
    else
      get_img_rgb_pixel (x, y, &rr, &gg, &bb);
    set_color (rr, gg, bb, r, g, b);
  }
  int inline render_raw_pixel (int x, int y)
  {
    return m_gray_data[y][x] * (long)m_img.maxval / m_maxval;
  }
  void inline render_pixel (coord_t x, coord_t y, int *r, int *g, int *b)
  {
    coord_t xx, yy;
    m_scr_to_img.to_img (x, y, &xx, &yy);
    render_pixel_img (xx, yy, r, g, b);
  }
private:
  bool m_color;
};

/* Sample diagonal square.
   Square is specified by its center and size of diagonal.  */
luminosity_t
render_to_scr::sample_scr_diag_square (coord_t xc, coord_t yc, coord_t diagonal_size)
{
  coord_t xxc, yyc, x1, y1, x2, y2;
  m_scr_to_img.to_img (xc, yc, &xxc, &yyc);
  m_scr_to_img.to_img (xc + diagonal_size / 2, yc, &x1, &y1);
  m_scr_to_img.to_img (xc, yc + diagonal_size / 2, &x2, &y2);
  return sample_img_square (xxc, yyc, x1 - xxc, y1 - yyc, x2 - xxc, y2 - yyc);
}

/* Sample diagonal square.
   Square is specified by center and width/height  */
luminosity_t
render_to_scr::sample_scr_square (coord_t xc, coord_t yc, coord_t width, coord_t height)
{
  coord_t xxc, yyc, x1, y1, x2, y2;
  m_scr_to_img.to_img (xc, yc, &xxc, &yyc);
  m_scr_to_img.to_img (xc - width / 2, yc + height / 2, &x1, &y1);
  m_scr_to_img.to_img (xc + width / 2, yc + height / 2, &x2, &y2);
  return sample_img_square (xxc, yyc, x1 - xxc, y1 - yyc, x2 - xxc, y2 - yyc);
}

/* Determine grayscale value at a given position in the image.
   The position is in the screen coordinates.  */
inline luminosity_t
render_to_scr::get_img_pixel_scr (coord_t x, coord_t y)
{
  coord_t xp, yp;
  m_scr_to_img.to_img (x, y, &xp, &yp);
  return get_img_pixel (xp, yp);
}

struct scr_detect_parameters;
DLL_PUBLIC bool save_csp (FILE *f, scr_to_img_parameters *param, scr_detect_parameters *dparam, render_parameters *rparam);
DLL_PUBLIC bool load_csp (FILE *f, scr_to_img_parameters *param, scr_detect_parameters *dparam, render_parameters *rparam, const char **error);

#endif
