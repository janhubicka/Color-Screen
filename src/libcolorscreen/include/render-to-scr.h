#ifndef RENDER_TO_SCR_H
#define RENDER_TO_SCR_H
#include "progress-info.h"
#include "render.h"
#include "scr-to-img.h"

struct screen;

/* Base class for renderes that use mapping between image and screen coordinates.  */
class render_to_scr : public render
{
public:
  render_to_scr (scr_to_img_parameters &param, image_data &img, render_parameters &rparam, int dstmaxval)
    : render (img, rparam, dstmaxval)
  {
    m_scr_to_img.set_parameters (param, img);
    m_scr_to_img.get_final_range (m_img.width, m_img.height, &m_final_xshift, &m_final_yshift, &m_final_width, &m_final_height);
  }
  inline luminosity_t get_img_pixel_scr (coord_t x, coord_t y);
  inline luminosity_t get_unadjusted_img_pixel_scr (coord_t x, coord_t y);
  coord_t pixel_size ();
  DLL_PUBLIC bool precompute_all (bool grayscale_needed, progress_info *progress);
  DLL_PUBLIC bool precompute (bool grayscale_needed, coord_t, coord_t, coord_t, coord_t, progress_info *progress);
  DLL_PUBLIC bool precompute_img_range (bool grayscale_needed, coord_t, coord_t, coord_t, coord_t, progress_info *progress);
  /* This returns screen coordinate width of rendered output.  */
  int get_final_width ()
  {
    return m_final_width;
  }
  /* This returns screen coordinate height of rendered output.  */
  int get_final_height ()
  {
    return m_final_height;
  }
  int get_final_xshift ()
  {
    return m_final_xshift;
  }
  int get_final_yshift ()
  {
    return m_final_yshift;
  }
  DLL_PUBLIC static bool render_tile (enum render_type_t render_type, scr_to_img_parameters &param, image_data &img, render_parameters &rparam,
				      bool color, unsigned char *pixels, int rowstride, int pixelbytes, int width, int height,
				      double xoffset, double yoffset, double step, progress_info *progress = NULL);
  inline luminosity_t sample_scr_diag_square (coord_t xc, coord_t yc, coord_t s);
  inline luminosity_t sample_scr_square (coord_t xc, coord_t yc, coord_t w, coord_t h);
  static screen *get_screen (enum scr_type t, bool preview, coord_t radius, progress_info *progress, uint64_t *id = NULL);
  static void release_screen (screen *scr);
protected:

  /* Transformation between screen and image coordinates.  */
  scr_to_img m_scr_to_img;

  int m_final_xshift, m_final_yshift;
  int m_final_width, m_final_height;
};

/* Do no rendering of color screen.  */
class render_img : public render_to_scr
{
public:
  render_img (scr_to_img_parameters &param, image_data &img, render_parameters &rparam, int dstmaxval)
    : render_to_scr (param, img, rparam, dstmaxval), m_color (false)
  { }
  void set_color_display () { if (m_img.rgbdata) m_color = 1; }
  bool precompute_all (progress_info *progress = NULL)
  {
    return render_to_scr::precompute_all (!m_color, progress);
  }
  inline rgbdata sample_pixel_img (coord_t x, coord_t y)
  {
    rgbdata ret;
    if (!m_color)
      ret.red = ret.green = ret.blue = get_img_pixel (x, y);
    else
      get_img_rgb_pixel (x, y, &ret.red, &ret.green, &ret.blue);
    return ret;
  }
  void inline render_pixel_img (coord_t x, coord_t y, int *r, int *g, int *b)
  {
    rgbdata d = sample_pixel_img (x, y);
    set_color (d.red, d.green, d.blue, r, g, b);
  }
  void inline render_hdr_pixel_img (coord_t x, coord_t y, luminosity_t *r, luminosity_t *g, luminosity_t *b)
  {
    rgbdata d = sample_pixel_img (x, y);
    set_hdr_color (d.red, d.green, d.blue, r, g, b);
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
  void inline render_pixel_scr (coord_t x, coord_t y, int *r, int *g, int *b)
  {
    coord_t xx, yy;
    m_scr_to_img.to_img (x, y, &xx, &yy);
    render_pixel_img (xx, yy, r, g, b);
  }
  void inline render_hdr_pixel_scr (coord_t x, coord_t y, luminosity_t *r, luminosity_t *g, luminosity_t *b)
  {
    coord_t xx, yy;
    m_scr_to_img.to_img (x, y, &xx, &yy);
    render_hdr_pixel_img (xx, yy, r, g, b);
  }
  inline rgbdata sample_pixel_final (coord_t x, coord_t y)
  {
    coord_t xx, yy;
    m_scr_to_img.final_to_img (x - m_final_xshift, y - m_final_yshift, &xx, &yy);
    return sample_pixel_img (xx, yy);
  }
  inline rgbdata sample_pixel_scr (coord_t x, coord_t y)
  {
    coord_t xx, yy;
    m_scr_to_img.to_img (x, y, &xx, &yy);
    return sample_pixel_img (xx, yy);
  }
  void inline render_pixel_final (coord_t x, coord_t y, int *r, int *g, int *b)
  {
    coord_t xx, yy;
    m_scr_to_img.final_to_img (x - m_final_xshift, y - m_final_yshift, &xx, &yy);
    render_pixel_img (xx, yy, r, g, b);
  }
  void inline render_hdr_pixel_final (coord_t x, coord_t y, luminosity_t *r, luminosity_t *g, luminosity_t *b)
  {
    coord_t xx, yy;
    m_scr_to_img.final_to_img (x - m_final_xshift, y - m_final_yshift, &xx, &yy);
    render_hdr_pixel_img (xx, yy, r, g, b);
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

/* Determine grayscale value at a given position in the image.
   The position is in the screen coordinates.  */
inline luminosity_t
render_to_scr::get_unadjusted_img_pixel_scr (coord_t x, coord_t y)
{
  coord_t xp, yp;
  m_scr_to_img.to_img (x, y, &xp, &yp);
  return get_unadjusted_img_pixel (xp, yp);
}

struct scr_detect_parameters;
struct solver_parameters;
DLL_PUBLIC bool save_csp (FILE *f, scr_to_img_parameters *param, scr_detect_parameters *dparam, render_parameters *rparam, solver_parameters *sparam);
DLL_PUBLIC bool load_csp (FILE *f, scr_to_img_parameters *param, scr_detect_parameters *dparam, render_parameters *rparam, solver_parameters *sparam, const char **error);

#endif
