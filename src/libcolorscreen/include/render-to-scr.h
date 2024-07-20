#ifndef RENDER_TO_SCR_H
#define RENDER_TO_SCR_H
#include "progress-info.h"
#include "render.h"
#include "scr-to-img.h"

class screen;
struct render_to_file_params;

/* Base class for renderes that use mapping between image and screen coordinates.  */
class render_to_scr : public render
{
public:
  render_to_scr (const scr_to_img_parameters &param, const image_data &img, render_parameters &rparam, int dstmaxval)
    : render (img, rparam, dstmaxval)
  {
    m_scr_to_img.set_parameters (param, img);
    m_scr_to_img.get_final_range (m_img.width, m_img.height, &m_final_xshift, &m_final_yshift, &m_final_width, &m_final_height);
  }
  inline luminosity_t get_img_pixel_scr (coord_t x, coord_t y);
  inline luminosity_t get_unadjusted_img_pixel_scr (coord_t x, coord_t y);
  inline rgbdata get_unadjusted_rgb_pixel_scr (coord_t x, coord_t y);
  coord_t pixel_size ();
  DLL_PUBLIC bool precompute_all (bool grayscale_needed, bool normalized_patches, progress_info *progress);
  DLL_PUBLIC bool precompute (bool grayscale_needed, bool normalized_patches, coord_t, coord_t, coord_t, coord_t, progress_info *progress);
  DLL_PUBLIC bool precompute_img_range (bool grayscale_needed, bool normalized_patches, coord_t, coord_t, coord_t, coord_t, progress_info *progress);
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
  DLL_PUBLIC static bool render_tile (render_type_parameters rtparam, scr_to_img_parameters &param, image_data &img, render_parameters &rparam,
				      unsigned char *pixels, int rowstride, int pixelbytes, int width, int height,
				      double xoffset, double yoffset, double step, progress_info *progress = NULL);
  static const char *render_to_file (render_to_file_params &rfparams, render_type_parameters rtparam, scr_to_img_parameters param, render_parameters rparam, image_data &img, int black, progress_info *progress);
  inline luminosity_t sample_scr_diag_square (coord_t xc, coord_t yc, coord_t s);
  inline luminosity_t sample_scr_square (coord_t xc, coord_t yc, coord_t w, coord_t h);
  static screen *get_screen (enum scr_type t, bool preview, coord_t radius, coord_t dufay_red_strip_width, coord_t dufay_green_strip_height, progress_info *progress, uint64_t *id = NULL);
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
    : render_to_scr (param, img, rparam, dstmaxval), m_color (false), m_profiled (false)
  { }
  void set_color_display (bool profiled = false)
  { 
    if (m_img.rgbdata)
      {
        m_color = 1;
	m_profiled = profiled;
	  /* When doing profiled matrix, we need to pre-scale the profile so black point corretion goes right.
	     Without doing so, for exmaple black from red pixels would be subtracted too agressively, since
	     we account for every pixel in image, not only red patch portion.  */
	if (profiled)
	  profile_matrix = m_params.get_profile_matrix (m_scr_to_img.patch_proportions (&m_params));
      }
  }
  void set_render_type (render_type_parameters rtparam)
  {
    if (rtparam.color)
      set_color_display (rtparam.type == render_type_profiled_original);
  }
  bool precompute_all (progress_info *progress = NULL)
  {
    return render_to_scr::precompute_all (!m_color, m_profiled, progress);
  }
  bool precompute_img_range (int, int, int, int, progress_info *progress = NULL)
  {
    return precompute_all (progress);
  }
  inline rgbdata sample_pixel_img (coord_t x, coord_t y)
  {
    rgbdata ret;
    if (!m_color)
      ret.red = ret.green = ret.blue = get_img_pixel (x, y);
    else if (!m_profiled)
      get_img_rgb_pixel (x, y, &ret.red, &ret.green, &ret.blue);
    else
      {
	get_unadjusted_img_rgb_pixel (x, y, &ret.red, &ret.green, &ret.blue);
        profile_matrix.apply_to_rgb (ret.red, ret.green, ret.blue, &ret.red, &ret.green, &ret.blue);
        ret.red = adjust_luminosity_ir (ret.red);
        ret.green = adjust_luminosity_ir (ret.green);
        ret.blue = adjust_luminosity_ir (ret.blue);
      }
    return ret;
  }
  rgbdata inline get_profiled_rgb_pixel (int x, int y)
  {
    rgbdata c = get_unadjusted_rgb_pixel (x, y);
    profile_matrix.apply_to_rgb (c.red, c.green, c.blue, &c.red, &c.green, &c.blue);
    c.red = adjust_luminosity_ir (c.red);
    c.green = adjust_luminosity_ir (c.green);
    c.blue = adjust_luminosity_ir (c.blue);
    return c;
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
  /* Compute RGB data of downscaled image.  */
  void
  get_color_data (rgbdata *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *progress);
private:
  bool m_color;
  bool m_profiled;
  color_matrix profile_matrix;
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

/* Determine RGB value at a given position in the image.
   The position is in the screen coordinates.  */
inline rgbdata
render_to_scr::get_unadjusted_rgb_pixel_scr (coord_t x, coord_t y)
{
  coord_t xp, yp;
  m_scr_to_img.to_img (x, y, &xp, &yp);
  rgbdata ret;
  render::get_unadjusted_img_rgb_pixel (xp, yp, &ret.red, &ret.green, &ret.blue);
  return ret;
}

struct scr_detect_parameters;
struct solver_parameters;

#endif
