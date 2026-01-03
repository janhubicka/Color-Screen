#ifndef RENDER_TO_SCR_H
#define RENDER_TO_SCR_H
#include "include/progress-info.h"
#include "include/scr-to-img.h"
#include "include/render-parameters.h"
#include "render.h"
#include "screen.h"
#include "simulate.h"

namespace colorscreen
{
class screen;
struct render_to_file_params;

class screen_table
{
public:
  screen_table (scanner_blur_correction_parameters *param, scr_type type, luminosity_t dufay_red_strip_width, luminosity_t dufay_red_strip_height, progress_info *progress);
  screen &get_screen (int x, int y)
  {
    return m_screen_table[y * m_width + x];
  }
  uint64_t get_id ()
  {
    return m_id;
  }
  int get_width ()
  {
    return m_width;
  }
  int get_height ()
  {
    return m_height;
  }
private:
  /* Unique id of the image (used for caching).  */
  uint64_t m_id;
  int m_width, m_height;
  std::vector <screen> m_screen_table;
};
class saturation_loss_table
{
public:
  saturation_loss_table (screen_table *screen_table, screen *collection_screen, int img_width, int img_height, scr_to_img *map, luminosity_t collection_threshold, const sharpen_parameters &sharpen, progress_info *progress);
  color_matrix &get_saturation_loss (int x, int y)
  {
    return m_saturation_loss_table[y * m_width + x];
  }
  __attribute__ ((pure))
  rgbdata
  compensate_saturation_loss_img (point_t p, rgbdata c)
  {
      int x, y;
      coord_t rx = my_modf (p.x * m_xstepinv - 0.5, &x);
      coord_t ry = my_modf (p.y * m_ystepinv - 0.5, &y);
      if (x < 0)
	x = 0, rx = 0;
      if (y < 0)
	y = 0, ry = 0;
      if (x >= m_width - 1)
	x = m_width - 2, rx = 1;
      if (y >= m_height - 1)
	y = m_height - 2, ry = 1;
      rgbdata c00, c01, c10, c11;
      m_saturation_loss_table [y * m_width + x].apply_to_rgb (c.red, c.green, c.blue, &c00.red, &c00.green, &c00.blue);
      m_saturation_loss_table [y * m_width + x + 1].apply_to_rgb (c.red, c.green, c.blue, &c10.red, &c10.green, &c10.blue);
      m_saturation_loss_table [(y + 1) * m_width + x].apply_to_rgb (c.red, c.green, c.blue, &c01.red, &c01.green, &c01.blue);
      m_saturation_loss_table [(y + 1) * m_width + x + 1].apply_to_rgb (c.red, c.green, c.blue, &c11.red, &c11.green, &c11.blue);
#if 0
      c01.red = 1;
      c11.green = 1;
      color_matrix ret;
      for (int i = 0; i < 4; i++)
	for (int j = 0; j < 4; j++)
	  ret.m_elements[i][j] = 
	      (m_saturation_matrices [y * m_saturation_width + x].m_elements[i][j] * (1 - rx) + m_saturation_matrices [y * m_saturation_width + x + 1].m_elements[i][j] * rx) * (1 - ry) +
	      (m_saturation_matrices [(y + 1) * m_saturation_width + x].m_elements[i][j] * (1 - rx) + m_saturation_matrices [(y + 1) * m_saturation_width + x + 1].m_elements[i][j] * rx) * ry;
       return ret;
#endif
       return (c00 * (1 - rx) + c10 * rx) * (1 - ry)
	       + (c01 * (1 - rx) + c11 * rx) * ry;
  }
private:
  /* Unique id of the image (used for caching).  */
  uint64_t m_id;
  int m_width, m_height;
  int m_img_width, m_img_height;
  coord_t m_xstepinv, m_ystepinv;
  std::vector <color_matrix> m_saturation_loss_table;
};

/* Base class for renderes that use mapping between image and screen
 * coordinates.  */
class render_to_scr : public render
{
public:
  render_to_scr (const scr_to_img_parameters &param, const image_data &img,
                 render_parameters &rparam, int dstmaxval)
      : render (img, rparam, dstmaxval),
	m_scr_to_img_param (param),
	m_screen_table (NULL), m_saturation_loss_table (NULL),
 	m_simulated_screen (NULL), m_simulated_screen_id (0)
  {
    m_final_width = -1;
    m_final_height = -1;
    m_final_xshift = INT_MIN;
    m_final_yshift = INT_MIN;
    m_scr_to_img.set_parameters (param, img);
  }
  ~render_to_scr ();
  pure_attr inline luminosity_t get_img_pixel_scr (coord_t x, coord_t y) const;
  pure_attr inline luminosity_t get_unadjusted_img_pixel_scr (coord_t x,
                                                              coord_t y) const;
  pure_attr inline rgbdata get_unadjusted_rgb_pixel_scr (coord_t x,
                                                         coord_t y) const;
  coord_t pixel_size ();
  DLL_PUBLIC bool precompute_all (bool grayscale_needed,
                                  bool normalized_patches,
                                  progress_info *progress);
  DLL_PUBLIC bool precompute (bool grayscale_needed, bool normalized_patches,
                              coord_t, coord_t, coord_t, coord_t,
                              progress_info *progress);
  DLL_PUBLIC bool precompute_img_range (bool grayscale_needed,
                                        bool normalized_patches, coord_t,
                                        coord_t, coord_t, coord_t,
                                        progress_info *progress);
  void simulate_screen (progress_info *);
  void
  compute_final_range ()
  {
    if (m_final_width < 0)
      m_scr_to_img.get_final_range (m_img.width, m_img.height, &m_final_xshift,
                                    &m_final_yshift, &m_final_width,
                                    &m_final_height);
  }
  /* This returns screen coordinate width of rendered output.  */
  int
  get_final_width ()
  {
    assert (!colorscreen_checking || m_final_width > 0);
    return m_final_width;
  }
  /* This returns screen coordinate height of rendered output.  */
  int
  get_final_height ()
  {
    assert (!colorscreen_checking || m_final_height > 0);
    return m_final_height;
  }
  int
  get_final_xshift () const
  {
    assert (!colorscreen_checking || m_final_xshift != INT_MIN);
    return m_final_xshift;
  }
  int
  get_final_yshift () const
  {
    assert (!colorscreen_checking || m_final_yshift != INT_MIN);
    return m_final_yshift;
  }
  static bool render_tile (render_type_parameters rtparam,
                           scr_to_img_parameters &param, image_data &img,
                           render_parameters &rparam, unsigned char *pixels,
                           int rowstride, int pixelbytes, int width,
                           int height, double xoffset, double yoffset,
                           double step, progress_info *progress = NULL);
  static const char *render_to_file (render_to_file_params &rfparams,
                                     render_type_parameters rtparam,
                                     scr_to_img_parameters param,
                                     render_parameters rparam, image_data &img,
                                     int black, progress_info *progress);
  inline luminosity_t sample_scr_diag_square (coord_t xc, coord_t yc,
                                              coord_t s);
  inline luminosity_t sample_scr_square (coord_t xc, coord_t yc, coord_t w,
                                         coord_t h);
  static screen *get_screen (enum scr_type t, bool preview,
			     bool anticipate_sharpening,
   			     const sharpen_parameters &sharpen,
                             coord_t dufay_red_strip_width,
                             coord_t dufay_green_strip_height,
                             progress_info *progress = NULL,
                             uint64_t *id = NULL);
  static void release_screen (screen *scr);
  bool compute_screen_table (progress_info *progress);
  bool compute_saturation_loss_table (screen *collection_screen, uint64_t collection_screen_uid, luminosity_t collection_treshold, const sharpen_parameters &sharpen, progress_info *progress = NULL);
/* Determine grayscale value at a given position in the image.
   Use bicubic interpolation.  */

  pure_attr inline rgbdata get_simulated_screen_pixel (coord_t xp, coord_t yp) const;
  pure_attr inline rgbdata get_simulated_screen_pixel_fast (int xp, int yp) const;

protected:
  /* Transformation between screen and image coordinates.  */
  scr_to_img m_scr_to_img;
  const scr_to_img_parameters &m_scr_to_img_param;
  screen_table *m_screen_table;
  saturation_loss_table *m_saturation_loss_table;
  uint64_t m_screen_table_uid;
  simulated_screen *m_simulated_screen;
  uint64_t m_simulated_screen_id;

private:
  int m_final_xshift, m_final_yshift;
  int m_final_width, m_final_height;
};

/* Do no rendering of color screen.  */
class render_img : public render_to_scr
{
public:
  render_img (scr_to_img_parameters &param, image_data &img,
              render_parameters &rparam, int dstmaxval)
      : render_to_scr (param, img, rparam, dstmaxval), m_color (false),
        m_profiled (false)
  {
  }
  void
  set_color_display (bool profiled = false)
  {
    if (m_img.rgbdata)
      {
        m_color = 1;
        m_profiled = profiled;
      }
  }
  void
  set_render_type (render_type_parameters rtparam)
  {
    if (rtparam.color)
      set_color_display (rtparam.type == render_type_profiled_original);
  }
  bool
  precompute_all (progress_info *progress = NULL)
  {
    if (!render_to_scr::precompute_all (!m_color, m_profiled, progress))
      return false;
    /* When doing profiled matrix, we need to pre-scale the profile so
       black point corretion goes right. Without doing so, for exmaple
       black from red pixels would be subtracted too agressively, since we
       account for every pixel in image, not only red patch portion.  */
    if (m_color && m_profiled)
      profile_matrix = m_params.get_profile_matrix (
	  m_scr_to_img.patch_proportions (&m_params));
    return true;
  }
  bool
  precompute_img_range (int, int, int, int, progress_info *progress = NULL)
  {
    return precompute_all (progress);
  }
  pure_attr inline rgbdata
  sample_pixel_img (coord_t x, coord_t y) const
  {
    rgbdata ret;
    if (!m_color)
      ret.red = ret.green = ret.blue = fast_get_img_pixel (x, y);
    else if (!m_profiled)
      ret = get_rgb_pixel (x, y);
    else
      {
        ret = get_unadjusted_rgb_pixel (x, y);
        profile_matrix.apply_to_rgb (ret.red, ret.green, ret.blue, &ret.red,
                                     &ret.green, &ret.blue);
        ret.red = adjust_luminosity_ir (ret.red);
        ret.green = adjust_luminosity_ir (ret.green);
        ret.blue = adjust_luminosity_ir (ret.blue);
      }
    return ret;
  }
  pure_attr inline rgbdata
  fast_sample_pixel_img (int x, int y) const
  {
    rgbdata ret;
    if (x < 0 || x >= m_img.width || y < 0 || y >= m_img.height)
      return ret;
    if (!m_color)
      ret.red = ret.green = ret.blue = get_data (x, y);
    else if (!m_profiled)
      ret = get_rgb_pixel (x, y);
    else
      {
        ret = get_unadjusted_rgb_pixel (x, y);
        profile_matrix.apply_to_rgb (ret.red, ret.green, ret.blue, &ret.red,
                                     &ret.green, &ret.blue);
        ret.red = adjust_luminosity_ir (ret.red);
        ret.green = adjust_luminosity_ir (ret.green);
        ret.blue = adjust_luminosity_ir (ret.blue);
      }
    return ret;
  }
  pure_attr rgbdata inline get_profiled_rgb_pixel (int x, int y) const
  {
    rgbdata c = get_unadjusted_rgb_pixel (x, y);
    profile_matrix.apply_to_rgb (c.red, c.green, c.blue, &c.red, &c.green,
                                 &c.blue);
    c.red = adjust_luminosity_ir (c.red);
    c.green = adjust_luminosity_ir (c.green);
    c.blue = adjust_luminosity_ir (c.blue);
    return c;
  }
  pure_attr inline rgbdata
  sample_pixel_final (coord_t x, coord_t y) const
  {
    point_t p = m_scr_to_img.final_to_img (
        { x - get_final_xshift (), y - get_final_yshift () });
    return sample_pixel_img (p.x, p.y);
  }
  inline rgbdata
  sample_pixel_scr (coord_t x, coord_t y)
  {
    point_t p = m_scr_to_img.to_img ({ x, y });
    return sample_pixel_img (p.x, p.y);
  }
  /* Compute RGB data of downscaled image.  */
  void get_color_data (rgbdata *data, coord_t x, coord_t y, int width,
                       int height, coord_t pixelsize, progress_info *progress);

private:
  bool m_color;
  bool m_profiled;
  color_matrix profile_matrix;
};

/* Sample diagonal square.
   Square is specified by its center and size of diagonal.  */
luminosity_t
render_to_scr::sample_scr_diag_square (coord_t xc, coord_t yc,
                                       coord_t diagonal_size)
{
  point_t pc = m_scr_to_img.to_img ({ xc, yc });
  point_t p1 = m_scr_to_img.to_img ({ xc + diagonal_size / 2, yc });
  point_t p2 = m_scr_to_img.to_img ({ xc, yc + diagonal_size / 2 });
  return sample_img_square (pc.x, pc.y, p1.x - pc.x, p1.y - pc.y, p2.x - pc.x,
                            p2.y - pc.y);
}

/* Sample diagonal square.
   Square is specified by center and width/height  */
luminosity_t
render_to_scr::sample_scr_square (coord_t xc, coord_t yc, coord_t width,
                                  coord_t height)
{
  point_t pc = m_scr_to_img.to_img ({ xc, yc });
  point_t p1 = m_scr_to_img.to_img ({ xc - width / 2, yc + height / 2 });
  point_t p2 = m_scr_to_img.to_img ({ xc + width / 2, yc + height / 2 });
  return sample_img_square (pc.x, pc.y, p1.x - pc.x, p1.y - pc.y, p2.x - pc.x,
                            p2.y - pc.y);
}

/* Determine grayscale value at a given position in the image.
   The position is in the screen coordinates.  */
pure_attr inline luminosity_t
render_to_scr::get_img_pixel_scr (coord_t x, coord_t y) const
{
  point_t p = m_scr_to_img.to_img ({ x, y });
  return get_img_pixel (p.x, p.y);
}

/* Determine grayscale value at a given position in the image.
   The position is in the screen coordinates.  */
pure_attr inline luminosity_t
render_to_scr::get_unadjusted_img_pixel_scr (coord_t x, coord_t y) const
{
  point_t p = m_scr_to_img.to_img ({ x, y });
  return get_unadjusted_img_pixel (p.x, p.y);
}

/* Determine RGB value at a given position in the image.
   The position is in the screen coordinates.  */
pure_attr inline rgbdata
render_to_scr::get_unadjusted_rgb_pixel_scr (coord_t x, coord_t y) const
{
  point_t p = m_scr_to_img.to_img ({ x, y });
  rgbdata ret;
  render::get_unadjusted_img_rgb_pixel (p.x, p.y, &ret.red, &ret.green,
                                        &ret.blue);
  return ret;
}

pure_attr inline rgbdata
render_to_scr::get_simulated_screen_pixel_fast (int xp, int yp) const
{
  return m_simulated_screen->get_pixel (yp, xp);
}

pure_attr inline pure_attr rgbdata
render_to_scr::get_simulated_screen_pixel (coord_t xp, coord_t yp) const
{
  return m_simulated_screen->get_interpolated_pixel (xp, yp);
}

/* Determine image pixel X,Y in screen filter SCR using MAP.
   Do antialiasing.  */
inline rgbdata
antialias_screen (const screen &scr, const scr_to_img &map,
		  int x, int y, point_t *retp = NULL)
{
  point_t p = map.to_scr ({ x + (coord_t)0.5, y + (coord_t)0.5 });
  point_t px = map.to_scr ({ x + (coord_t)1.5, y + (coord_t)0.5 });
  point_t py = map.to_scr ({ x + (coord_t)0.5, y + (coord_t)1.5 });
  rgbdata am = { 0, 0, 0 };
  point_t pdx = (px - p) * (1.0 / 6.0);
  point_t pdy = (py - p) * (1.0 / 6.0);
  if (retp)
    *retp = p;

  for (int yy = -2; yy <= 2; yy++)
    for (int xx = -2; xx <= 2; xx++)
      am += scr.interpolated_mult (p + pdx * xx + pdy * yy);
  return am * ((coord_t)1.0 / 25);
}

struct scr_detect_parameters;
struct solver_parameters;
}
#endif
