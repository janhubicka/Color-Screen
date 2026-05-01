/* High-level rendering to screen coordinates.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#ifndef RENDER_TO_SCR_H
#define RENDER_TO_SCR_H
#include "include/progress-info.h"
#include "include/scr-to-img.h"
#include "include/render-parameters.h"
#include "render.h"
#include "screen.h"
#include "simulate.h"
#include "lru-cache.h"

namespace colorscreen
{
class screen;
class screen_table;
class saturation_loss_table;
struct render_to_file_params;


/* Table of screens for adaptive sharpening/blurring.  */
class screen_table
{
public:
  /* Initialize screen table for PARAM, TYPE, DUFAY_RED_STRIP_WIDTH, DUFAY_RED_STRIP_HEIGHT and SHARPEN.
     Update PROGRESS.  */
  screen_table (scanner_blur_correction_parameters *param, scr_type type, luminosity_t dufay_red_strip_width, luminosity_t dufay_red_strip_height, const sharpen_parameters &sharpen, progress_info *progress);
  
  /* Return screen at X, Y.  */
  pure_attr screen &get_screen (int x, int y)
  {
    return m_screen_table[y * m_width + x];
  }

  /* Return unique id of the table.  */
  pure_attr uint64_t get_id () const
  {
    return m_id;
  }

  /* Return width of the table.  */
  pure_attr int get_width () const
  {
    return m_width;
  }

  /* Return height of the table.  */
  pure_attr int get_height () const
  {
    return m_height;
  }
private:
  /* Unique id of the image (used for caching).  */
  uint64_t m_id;
  int m_width, m_height;
  std::vector <screen> m_screen_table;
};

/* Table of saturation loss compensation matrices.  */
class saturation_loss_table
{
public:
  /* Initialize saturation loss table for SCREEN_TABLE, COLLECTION_SCREEN, IMG_WIDTH, IMG_HEIGHT, MAP, COLLECTION_THRESHOLD and SHARPEN.
     Update PROGRESS.  */
  saturation_loss_table (screen_table *screen_table, screen *collection_screen, int img_width, int img_height, scr_to_img *map, luminosity_t collection_threshold, const sharpen_parameters &sharpen, progress_info *progress);
  
  /* Return saturation loss matrix at X, Y.  */
  pure_attr color_matrix &get_saturation_loss (int x, int y)
  {
    return m_saturation_loss_table[y * m_width + x];
  }

  /* Compensate saturation loss for pixel C at position P in image coordinates.  */
  pure_attr rgbdata
  compensate_saturation_loss_img (point_t p, rgbdata c) const
  {
      int x, y;
      coord_t rx = my_modf (p.x * m_xstepinv - (coord_t)0.5, &x);
      coord_t ry = my_modf (p.y * m_ystepinv - (coord_t)0.5, &y);
      if (x < 0)
	x = 0, rx = (coord_t)0;
      if (y < 0)
	y = 0, ry = (coord_t)0;
      if (x >= m_width - 1)
	x = m_width - 2, rx = (coord_t)1;
      if (y >= m_height - 1)
	y = m_height - 2, ry = (coord_t)1;
      rgbdata c00, c01, c10, c11;
      m_saturation_loss_table [y * m_width + x].apply_to_rgb (c.red, c.green, c.blue, &c00.red, &c00.green, &c00.blue);
      m_saturation_loss_table [y * m_width + x + 1].apply_to_rgb (c.red, c.green, c.blue, &c10.red, &c10.green, &c10.blue);
      m_saturation_loss_table [(y + 1) * m_width + x].apply_to_rgb (c.red, c.green, c.blue, &c01.red, &c01.green, &c01.blue);
      m_saturation_loss_table [(y + 1) * m_width + x + 1].apply_to_rgb (c.red, c.green, c.blue, &c11.red, &c11.green, &c11.blue);

       return (c00 * ((coord_t)1.0 - rx) + c10 * rx) * ((coord_t)1.0 - ry)
	       + (c01 * ((coord_t)1.0 - rx) + c11 * rx) * ry;
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
   coordinates.  */
class render_to_scr : public render
{
public:
  /* Initialize renderer for PARAM, IMG, RPARAM and DSTMAXVAL.  */
  render_to_scr (const scr_to_img_parameters &param, const image_data &img,
                 const render_parameters &rparam, int dstmaxval)
      : render (img, rparam, dstmaxval),
	m_scr_to_img_param (param),
	m_screen_table (), m_saturation_loss_table (),
 	m_simulated_screen (), m_simulated_screen_id (0),
	m_final_range ()
  {
    /* Initialize early so we can determine ranges before precomputing.  */
    m_ok = m_scr_to_img.set_parameters (param, img);
    if (m_ok)
      m_pixel_size = m_scr_to_img.pixel_size (rparam.get_image_area (img.width, img.height));
  }
  ~render_to_scr ();

  /* Determine grayscale value at position P in screen coordinates.  */
  pure_attr inline luminosity_t get_img_pixel_scr (point_t p) const noexcept;

  /* Determine unadjusted grayscale value at position P in screen coordinates.  */
  pure_attr inline luminosity_t get_unadjusted_img_pixel_scr (point_t p) const noexcept;

  /* Determine unadjusted RGB value at position P in screen coordinates.  */
  pure_attr inline rgbdata get_unadjusted_rgb_pixel_scr (point_t p) const noexcept;

  /* Return approximate size of an scan pixel in screen coordinates.  */
  pure_attr coord_t pixel_size () const noexcept;

  /* Precompute all data needed for rendering.  Update PROGRESS.
     GRAYSCALE_NEEDED specifies if grayscale only rendering is sufficient.
     NORMALIZED_PATCHES specifies if patches should be normalized.
     PATCH_PROPORTIONS specifies proportions of R, G and B patches.  */
  DLL_PUBLIC bool precompute_all (bool grayscale_needed, bool normalized_patches, rgbdata patch_proportions, progress_info *progress)
  {
    abort ();
  }

  /* Precompute all data needed for rendering.  Update PROGRESS.
     GRAYSCALE_NEEDED specifies if grayscale only rendering is sufficient.
     NORMALIZED_PATCHES specifies if patches should be normalized.  */
  DLL_PUBLIC bool precompute_all (bool grayscale_needed, bool normalized_patches, progress_info *progress);

  /* Precompute all data needed for rendering in AREA.  Update PROGRESS.
     GRAYSCALE_NEEDED specifies if grayscale only rendering is sufficient.
     NORMALIZED_PATCHES specifies if patches should be normalized.  */
  DLL_PUBLIC bool precompute_img_range (bool grayscale_needed, bool normalized_patches, int_image_area area, progress_info *progress);

  /* Simulate screen rendering for PROGRESS.  */
  void simulate_screen (progress_info *progress);

  /* Compute range of the final image.  */
  void
  compute_final_range ()
  {
    if (m_final_range.empty_p ())
        m_final_range = m_scr_to_img.get_final_range (m_img.width, m_img.height);
  }

  /* Return screen coordinate width of rendered output.  */
  pure_attr int
  get_final_width () const
  {
    assert (!colorscreen_checking || m_final_range.width > 0);
    return m_final_range.width;
  }

  /* Return screen coordinate height of rendered output.  */
  pure_attr int
  get_final_height () const
  {
    assert (!colorscreen_checking || m_final_range.height > 0);
    return m_final_range.height;
  }

  /* Return screen coordinate X shift of rendered output.  */
  pure_attr int
  get_final_xshift () const
  {
    assert (!colorscreen_checking || !m_final_range.empty_p ());
    return -m_final_range.x;
  }

  /* Return screen coordinate Y shift of rendered output.  */
  pure_attr int
  get_final_yshift () const
  {
    assert (!colorscreen_checking || !m_final_range.empty_p ());
    return -m_final_range.y;
  }

  /* Render TILE of image IMG for PARAM, RPARAM and RTPARAM.
     Update PROGRESS.  XOFFSET, YOFFSET, STEP and ROWSTRIDE, PIXELBYTES, WIDTH, HEIGHT
     specify tile geometry.  */
  static bool render_tile (render_type_parameters rtparam,
                           scr_to_img_parameters &param, image_data &img,
                           render_parameters &rparam, unsigned char *pixels,
                           int rowstride, int pixelbytes, int width,
                           int height, double xoffset, double yoffset,
                           double step, progress_info *progress = NULL);

  /* Render image IMG to file RFPARAMS for RTPARAM, PARAM, RPARAM and BLACK point.
     Update PROGRESS.  */
  static const char *render_to_file (render_to_file_params &rfparams,
                                     render_type_parameters rtparam,
                                     scr_to_img_parameters param,
                                     render_parameters rparam, image_data &img,
                                     int black, progress_info *progress);

  /* Sample diagonal square with center XC, YC and diagonal size S.  */
  inline luminosity_t sample_scr_diag_square (coord_t xc, coord_t yc,
                                               coord_t s) const;

  /* Sample square with center XC, YC and width W, height H.  */
  inline luminosity_t sample_scr_square (coord_t xc, coord_t yc, coord_t w,
                                           coord_t h) const;


  /* Return screen of type T in PREVIEW mode.  Sharpen it according to SHARPEN parameters
     if ANTICIPATE_SHARPENING is true.  RED_STRIP_WIDTH and DUFAY_GREEN_STRIP_HEIGHT specify
     strip widths.  Update PROGRESS and return screen unique ID.  */
  static std::shared_ptr<screen> get_screen (enum scr_type t, bool preview,
			     bool anticipate_sharpening,
    			     const sharpen_parameters &sharpen,
                             coord_t red_strip_width,
                             coord_t dufay_green_strip_height,
                             progress_info *progress = NULL,
                             uint64_t *id = NULL);

  /* Release screen S.  */
  static void release_screen (screen *s);

  /* Compute screen table for PROGRESS.  */
  bool compute_screen_table (progress_info *progress);

  /* Compute saturation loss table for COLLECTION_SCREEN with ID COLLECTION_SCREEN_UID,
     COLLECTION_THRESHOLD and SHARPEN parameters.  Update PROGRESS.  */
  bool compute_saturation_loss_table (screen *collection_screen, uint64_t collection_screen_uid, luminosity_t collection_threshold, const sharpen_parameters &sharpen, progress_info *progress = NULL);

  /* Return simulated screen RGB value at position P in image coordinates.  */
  pure_attr inline rgbdata get_simulated_screen_pixel (point_t p) const noexcept;

  /* Return simulated screen RGB value at position P in image coordinates.  */
  pure_attr inline rgbdata get_simulated_screen_pixel_fast (int_point_t p) const noexcept;

protected:
  /* Transformation between screen and image coordinates.  */
  scr_to_img m_scr_to_img;
  const scr_to_img_parameters &m_scr_to_img_param;
  std::shared_ptr<screen_table> m_screen_table;
  std::shared_ptr<saturation_loss_table> m_saturation_loss_table;
  uint64_t m_screen_table_uid = 0;
  std::shared_ptr<simulated_screen> m_simulated_screen;
  uint64_t m_simulated_screen_id = 0;

private:
  int_image_area m_final_range;
  bool m_ok;
  coord_t m_pixel_size = -1;
};

/* Do no rendering of color screen.  */
class render_img : public render_to_scr
{
public:
  /* Initialize image renderer for PARAM, IMG, RPARAM and DSTMAXVAL.  */
  render_img (const scr_to_img_parameters &param, const image_data &img,
              render_parameters &rparam, int dstmaxval)
      : render_to_scr (param, img, rparam, dstmaxval), m_color (false),
        m_profiled (false), profile_matrix ()
  {
  }

  /* Set if color display is needed. PROFILED specifies if profiling is needed.  */
  void
  set_color_display (bool profiled = false)
  {
    if (m_img.has_rgb ())
      {
        m_color = 1;
        m_profiled = profiled;
      }
  }

  /* Set render type RTPARAM.  */
  void
  set_render_type (render_type_parameters rtparam)
  {
    switch (rtparam.type)
    {
      case render_type_image_layer:
	break;
      case render_type_original:
	set_color_display (false);
        break;
      case render_type_profiled_original:
	set_color_display (true);
        break;
      default:
	abort ();
    }
  }

  /* Precompute all data needed for rendering.  Update PROGRESS.  */
  nodiscard_attr bool
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

  /* Precompute all data needed for rendering in AREA.  Update PROGRESS.  */
  nodiscard_attr bool
  precompute_img_range (int_image_area area, progress_info *progress = NULL)
  {
    (void)area;
    return precompute_all (progress);
  }

  /* Sample pixel at position P in image coordinates.  */
  pure_attr inline rgbdata
  sample_pixel_img (point_t p) const
  {
    rgbdata ret;
    int_point_t pi = p.nearest ();
    if (pi.x < 0 || pi.x >= m_img.width || pi.y < 0 || pi.y >= m_img.height)
      return ret;
    if (!m_color)
      ret.red = ret.green = ret.blue = fast_get_img_pixel (pi);
    else if (!m_profiled)
      ret = get_rgb_pixel (pi);
    else
      {
        ret = get_unadjusted_rgb_pixel (pi);
        profile_matrix.apply_to_rgb (ret.red, ret.green, ret.blue, &ret.red,
                                     &ret.green, &ret.blue);
        ret.red = adjust_luminosity_ir (ret.red);
        ret.green = adjust_luminosity_ir (ret.green);
        ret.blue = adjust_luminosity_ir (ret.blue);
      }
    return ret;
  }

  /* Sample pixel at position P in image coordinates (integer coordinates).  */
  pure_attr inline rgbdata
  fast_sample_pixel_img (int_point_t p) const
  {
    rgbdata ret;
    if (p.x < 0 || p.x >= m_img.width || p.y < 0 || p.y >= m_img.height)
      return ret;
    if (!m_color)
      ret.red = ret.green = ret.blue = get_data (p);
    else if (!m_profiled)
      ret = get_rgb_pixel (p);
    else
      {
        ret = get_unadjusted_rgb_pixel (p);
        profile_matrix.apply_to_rgb (ret.red, ret.green, ret.blue, &ret.red,
                                     &ret.green, &ret.blue);
        ret.red = adjust_luminosity_ir (ret.red);
        ret.green = adjust_luminosity_ir (ret.green);
        ret.blue = adjust_luminosity_ir (ret.blue);
      }
    return ret;
  }

  /* Return profiled RGB pixel at position P in image coordinates.  */
  pure_attr rgbdata inline get_profiled_rgb_pixel (int_point_t p) const
  {
    rgbdata c = get_unadjusted_rgb_pixel (p);
    profile_matrix.apply_to_rgb (c.red, c.green, c.blue, &c.red, &c.green,
                                 &c.blue);
    c.red = adjust_luminosity_ir (c.red);
    c.green = adjust_luminosity_ir (c.green);
    c.blue = adjust_luminosity_ir (c.blue);
    return c;
  }

  /* Sample pixel at position P in final image coordinates.  */
  pure_attr inline rgbdata
  sample_pixel_final (point_t p) const
  {
    point_t pi = m_scr_to_img.final_to_img (
        { p.x - (coord_t)get_final_xshift (), p.y - (coord_t)get_final_yshift () });
    return sample_pixel_img (pi);
  }

  /* Sample pixel at position P in screen coordinates.  */
  pure_attr inline rgbdata
  sample_pixel_scr (point_t p) const
  {
    point_t pi = m_scr_to_img.to_img (p);
    return sample_pixel_img (pi);
  }

  /* Compute RGB data of downscaled image.  Update PROGRESS.  */
  nodiscard_attr bool get_color_data (rgbdata *data, point_t p, int width,
                        int height, coord_t pixelsize, progress_info *progress);

private:
  bool m_color;
  bool m_profiled;
  color_matrix profile_matrix;
};

/* Sample diagonal square.
   Square is specified by its center XC, YC and size of diagonal DIAGONAL_SIZE.  */
inline luminosity_t
render_to_scr::sample_scr_diag_square (coord_t xc, coord_t yc,
                                       coord_t diagonal_size) const
{
  point_t pc = m_scr_to_img.to_img ({ xc, yc });
  point_t p1 = m_scr_to_img.to_img ({ xc + diagonal_size / (coord_t)2.0, yc });
  point_t p2 = m_scr_to_img.to_img ({ xc, yc + diagonal_size / (coord_t)2.0 });
  return sample_img_square (pc, p1 - pc, p2 - pc);
}

/* Sample square.
   Square is specified by center XC, YC and WIDTH/HEIGHT  */
inline luminosity_t
render_to_scr::sample_scr_square (coord_t xc, coord_t yc, coord_t width,
                                  coord_t height) const
{
  point_t pc = m_scr_to_img.to_img ({ xc, yc });
  point_t p1 = m_scr_to_img.to_img ({ xc - width / (coord_t)2.0, yc + height / (coord_t)2.0 });
  point_t p2 = m_scr_to_img.to_img ({ xc + width / (coord_t)2.0, yc + height / (coord_t)2.0 });
  return sample_img_square (pc, p1 - pc, p2 - pc);
}

/* Determine grayscale value at position P in screen coordinates.  */
pure_attr inline luminosity_t
render_to_scr::get_img_pixel_scr (point_t p) const noexcept
{
  point_t pi = m_scr_to_img.to_img (p);
  return get_img_pixel (pi);
}

/* Determine unadjusted grayscale value at position P in screen coordinates.  */
pure_attr inline luminosity_t
render_to_scr::get_unadjusted_img_pixel_scr (point_t p) const noexcept
{
  point_t pi = m_scr_to_img.to_img (p);
  return get_unadjusted_img_pixel (pi);
}

/* Determine unadjusted RGB value at position P in screen coordinates.  */
pure_attr inline rgbdata
render_to_scr::get_unadjusted_rgb_pixel_scr (point_t p) const noexcept
{
  point_t pi = m_scr_to_img.to_img (p);
  return get_unadjusted_img_rgb_pixel (pi);
}

/* Return simulated screen RGB value at position P in image coordinates (integer coordinates).  */
pure_attr inline rgbdata
render_to_scr::get_simulated_screen_pixel_fast (int_point_t p) const noexcept
{
  return m_simulated_screen->get_pixel (p.y, p.x);
}

/* Return simulated screen RGB value at position P in image coordinates.  */
pure_attr inline rgbdata
render_to_scr::get_simulated_screen_pixel (point_t p) const noexcept
{
  return m_simulated_screen->get_interpolated_pixel (p.x, p.y);
}

/* Determine image pixel X,Y in screen filter SCR using MAP.
   Do antialiasing.  Return screen position in RETP.  */
pure_attr inline rgbdata
antialias_screen (const screen &scr, const scr_to_img &map,
		  int x, int y, point_t *retp = NULL) noexcept
{
  point_t p = map.to_scr ({ x + (coord_t)0.5, y + (coord_t)0.5 });
  point_t px = map.to_scr ({ x + (coord_t)1.5, y + (coord_t)0.5 });
  point_t py = map.to_scr ({ x + (coord_t)0.5, y + (coord_t)1.5 });
  rgbdata am = { (luminosity_t)0.0, (luminosity_t)0.0, (luminosity_t)0.0 };
  point_t pdx = (px - p) * ((coord_t)1.0 / (coord_t)6.0);
  point_t pdy = (py - p) * ((coord_t)1.0 / (coord_t)6.0);
  if (retp)
    *retp = p;

  for (int yy = -2; yy <= 2; yy++)
    for (int xx = -2; xx <= 2; xx++)
      am += scr.interpolated_mult (p + pdx * (coord_t)xx + pdy * (coord_t)yy);
  return am * ((coord_t)1.0 / (coord_t)25);
}

/* Determine image pixel X,Y in screen filter SCR using MAP.
   Do no antialiasing.  Return screen position in RETP.  */
pure_attr inline rgbdata
noantialias_screen (const screen &scr, const scr_to_img &map,
		    int x, int y, point_t *retp = NULL) noexcept
{
  point_t p = map.to_scr ({ x + (coord_t)0.5, y + (coord_t)0.5 });
  if (retp)
    *retp = p;

  return scr.interpolated_mult (p);
}


struct scr_detect_parameters;
struct solver_parameters;
}
#endif
