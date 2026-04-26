#ifndef RENDER_H
#define RENDER_H
#include <memory>
#include <math.h>
#include <assert.h>
#include <algorithm>
#ifdef _OPENMP
#include <omp.h>
#endif
#include "include/base.h"
#include "include/imagedata.h"
#include "include/color.h"
#include "include/progress-info.h"
#include "include/sensitivity.h"
#include "include/tone-curve.h"
#include "include/scr-to-img.h"
#include "include/render-parameters.h"
#include "include/render-type-parameters.h"
#include "include/stitch.h"
#include "out-color-adjustments.h"
#include "backlight-correction.h"
#include "mem-luminosity.h"
#include "lru-cache.h"
#include <vector>
#include "cubic-interpolate.h"

namespace colorscreen
{
class histogram;

/* Helper for downscaling template for grayscale rendering.
   Account VAL * SCALE to DATA.  */
template <bool UseAtomic = false>
inline void
account_pixel (luminosity_t *data, luminosity_t val, luminosity_t scale)
{
  if constexpr (UseAtomic)
    {
#pragma omp atomic
      *data += val * scale;
    }
  else
    *data += val * scale;
}

/* Helper for downscaling template for color rendering.
   Account VAL * SCALE to DATA.  */
template <bool UseAtomic = false>
inline void
account_rgb_pixel (rgbdata *data, rgbdata val, luminosity_t scale)
{
  if constexpr (UseAtomic)
    {
#pragma omp atomic
      data->red += val.red * scale;
#pragma omp atomic
      data->green += val.green * scale;
#pragma omp atomic
      data->blue += val.blue * scale;
    }
  else
    {
      data->red += val.red * scale;
      data->green += val.green * scale;
      data->blue += val.blue * scale;
    }
}

/* Unified accounting dispatcher.  Account VAL * SCALE to DATA.  */
template <typename T, bool UseAtomic = false>
inline void
do_account (T *data, T val, luminosity_t scale)
{
  if constexpr (std::is_same_v<T, rgbdata>)
    account_rgb_pixel<UseAtomic> (data, val, scale);
  else
    account_pixel<UseAtomic> (data, val, scale);
}

/* Base class for rendering routines.  */
class render
{
public:
  /* Initialize renderer for image IMG using parameters RPARAM and
     outputting to range 0..DSTMAXVAL.  */
  render (const image_data &img, const render_parameters &rparam,
          int dstmaxval)
      : out_color (dstmaxval), m_img (img), m_params (rparam)
  {
  }

  /* Determine grayscale value at a given position in the image.  */
  pure_attr inline luminosity_t get_img_pixel (point_t p) const noexcept;

  /* Determine grayscale value at a given position in the image without
     luminosity adjustments.  */
  pure_attr inline luminosity_t
  get_unadjusted_img_pixel (point_t p) const noexcept;

  /* Determine RGB value at a given position X, Y in the image.  */
  pure_attr inline rgbdata get_img_rgb_pixel (point_t p) const noexcept;

  /* Determine RGB value at a given position XP, YP in the image without
     luminosity adjustments.  */
  pure_attr inline rgbdata
  get_unadjusted_img_rgb_pixel (point_t p) const noexcept;

  /* Sample square patch with center C and corner offsets P1, P2.  */
  pure_attr luminosity_t sample_img_square (point_t c, point_t p1,
                                            point_t p2) const;

  /* Quickly determine grayscale value at a given position X, Y in the image.
   */
  pure_attr inline luminosity_t
  fast_get_img_pixel (int_point_t p) const noexcept;

  static const int num_color_models = render_parameters::color_model_max;

  /* Fetch lookup tables for image IMG and given GAMMA.  Store results in RET.
     Return false on failure.  */
  static bool get_lookup_tables (std::shared_ptr<luminosity_t[]> *ret,
                                 luminosity_t gamma, const image_data *img,
                                 progress_info *progress = nullptr);

  /* Get sharpened grayscale value at index X, Y.  */
  pure_attr inline luminosity_t get_data (int_point_t p) const noexcept;

  /* Get unadjusted sharpened grayscale value at index X, Y.  */
  pure_attr inline luminosity_t
  get_unadjusted_data (int_point_t p) const noexcept;

  /* Adjust luminosity LUM considering infrared sensitivity.  */
  pure_attr inline luminosity_t
  adjust_luminosity_ir (luminosity_t lum) const noexcept;

  /* Get sharpened color values at index X, Y.  */
  pure_attr inline luminosity_t get_data_red (int_point_t p) const noexcept;
  pure_attr inline luminosity_t get_data_green (int_point_t p) const noexcept;
  pure_attr inline luminosity_t get_data_blue (int_point_t p) const noexcept;

  /* Get linearized color values at index X, Y.  */
  pure_attr inline luminosity_t
  get_linearized_data_red (int_point_t p) const noexcept;
  pure_attr inline luminosity_t
  get_linearized_data_green (int_point_t p) const noexcept;
  pure_attr inline luminosity_t
  get_linearized_data_blue (int_point_t p) const noexcept;

  /* Precompute all data needed for rendering.  GRAYSCALE_NEEDED
     indicates if gray data is required.  If NORMALIZED_PATCHES is true,
     spectral computation is normalized.  PATCH_PROPORTIONS specifies
     color proportions.  Report progress to PROGRESS.  Return false on failure.
   */
  nodiscard_attr DLL_PUBLIC bool precompute_all (bool grayscale_needed,
                                                 bool normalized_patches,
                                                 rgbdata patch_proportions,
                                                 progress_info *progress);

  /* Get linearized RGB pixel value at index X, Y.  */
  pure_attr inline rgbdata
  get_linearized_rgb_pixel (int_point_t p) const noexcept
  {
    if (colorscreen_checking)
      assert (p.x >= 0 && p.x < m_img.width && p.y >= 0 && p.y < m_img.height);
    image_data::pixel pxl = m_img.get_rgb_pixel (p.x, p.y);
    rgbdata d = { m_rgb_lookup_table[0][pxl.r], m_rgb_lookup_table[1][pxl.g],
                  m_rgb_lookup_table[2][pxl.b] };
    return d;
  }

  /* Get unadjusted RGB pixel value at index X, Y.  */
  pure_attr inline rgbdata
  get_unadjusted_rgb_pixel (int_point_t p) const noexcept
  {
    rgbdata d = get_linearized_rgb_pixel (p);
    if (m_backlight_correction)
      {
        d.red = m_backlight_correction->apply (
            d.red, p.x, p.y, backlight_correction_parameters::red, true);
        d.green = m_backlight_correction->apply (
            d.green, p.x, p.y, backlight_correction_parameters::green, true);
        d.blue = m_backlight_correction->apply (
            d.blue, p.x, p.y, backlight_correction_parameters::blue, true);
      }
    return d;
  }

  /* Adjust RGB value D based on dark point and exposure.  */
  pure_attr inline rgbdata
  adjust_rgb (rgbdata d) const noexcept
  {
    d.red = (d.red - m_params.dark_point) * m_params.scan_exposure;
    d.green = (d.green - m_params.dark_point) * m_params.scan_exposure;
    d.blue = (d.blue - m_params.dark_point) * m_params.scan_exposure;
    return d;
  }

  /* Get final RGB pixel value at index X, Y.  */
  pure_attr inline rgbdata
  get_rgb_pixel (int_point_t p) const noexcept
  {
    return adjust_rgb (get_unadjusted_rgb_pixel (p));
  }

  /* Fetch matrix translating RGB values to XYZ.  Uses info from IMG,
     NORMALIZED_PATCHES, and PATCH_PROPORTIONS.  */
  color_matrix
  get_rgb_to_xyz_matrix (bool normalized_patches, rgbdata patch_proportions)
  {
    return m_params.get_rgb_to_xyz_matrix (&m_img, normalized_patches,
                                           patch_proportions);
  }

  /* Placeholder for final range computation.  */
  void
  compute_final_range ()
  {
  }

  /* Compute grayscale data for downscaled region at X, Y with WIDTH,
     HEIGHT and PIXELSIZE.  Store result in DATA.  Report progress
     to PROGRESS.  Return false on failure or cancellation.  */
  nodiscard_attr bool get_gray_data (luminosity_t *data, point_t p, int width,
                                     int height, coord_t pixelsize,
                                     progress_info *progress);

  /* Return number of pixel computations considered profitable for OMP.  */
  const_attr size_t
  openmp_size ()
  {
    return 128 * 1024;
  }

  /* Fetch histogram for the current scan area.  Report progress to PROGRESS.
   */
  std::shared_ptr<histogram> get_image_layer_histogram (progress_info *progress
                                                        = nullptr);

  /* Output color adjustments.  */
  out_color_adjustments out_color;

  /* Generic image downscaling routine using bilinear weights.
     Compute downscaled region at X, Y with WIDTH, HEIGHT and PIXELSIZE.
     Store result in DATA.  GET_PIXEL is the pixel fetching function.
     ACCOUNT_P is optional accounting function.  Report progress
     to PROGRESS.  */
  template <typename D, typename T, T (D::*get_pixel) (int_point_t p) const,
            auto account_p = nullptr>
  bool downscale (T *data, point_t p, int width, int height, coord_t pixelsize,
                  progress_info *progress) noexcept;

protected:
  /* Compute color data for downscaled region at X, Y with WIDTH, HEIGHT
     and PIXELSIZE.  Store result in DATA.  Report progress
     to PROGRESS.  Return false on failure or cancellation.  */
  nodiscard_attr bool get_color_data (rgbdata *data, point_t p, int width,
                                      int height, coord_t pixelsize,
                                      progress_info *progress);

  /* Inner loop for image downscaling processing single line YY from input
     to output DATA at line PY.  XSTART and XEND specify the horizontal
     range.  WIDTH and HEIGHT are output dimensions.  Y0 and Y1 indicate
     if output lines PY and PY+1 are affected.  SCALE is global scaling,
     YWEIGHT is the bilinear weight for the Y axis.  IF USEATOMIC is true,
     writes are updated atomically.  ACCOUNT_P is optional accounting function.
   */
  template <typename T, typename D, T (D::*get_pixel) (int_point_t p) const,
            bool UseAtomic = false, auto account_p = nullptr>
  void process_line (T *data, int *pixelpos, luminosity_t *weights, int xstart,
                     int xend, int width, int height, int py, int yy, bool y0,
                     bool y1, luminosity_t scale,
                     luminosity_t yweight) noexcept;

  /* Inner loop for image downscaling processing single pixel PIXEL at PX, PY.
     WIDTH and HEIGHT are output dimensions.  X0, X1, Y0, Y1 indicate if
     neighboring output pixels are affected.  SCALE is global scaling.
     XWEIGHT and YWEIGHT are bilinear weights.  IF USEATOMIC is true, writes
     are updated atomically.  ACCOUNT_P is optional accounting function.  */
  template <typename T, bool UseAtomic = false, auto account_p = nullptr>
  inline void process_pixel (T *data, int width, int height, int px, int py,
                             bool x0, bool x1, bool y0, bool y1, T pixel,
                             luminosity_t scale, luminosity_t xweight,
                             luminosity_t yweight) noexcept;

  /* Scanned image.  */
  const image_data &m_img;

  /* Rendering parameters.  */
  render_parameters m_params;

  /* ID of graydata computed.  */
  uint64_t m_gray_data_id = m_img.id;

  /* Sharpened data we render from.  */
  mem_luminosity_t *m_sharpened_data = nullptr;

  /* Wrapping class to cause proper destruction.  */
  std::shared_ptr<class sharpened_data> m_sharpened_data_holder = nullptr;

  /* Maximal value in M_IMG.  */
  int m_maxval = m_img.has_grayscale_or_ir () ? m_img.maxval : 65535;

  /* Translates input rgb channel values into normalized range.  */
  std::shared_ptr<luminosity_t[]> m_rgb_lookup_table[3];

  /* Backlight correction handler.  */
  std::shared_ptr<backlight_correction> m_backlight_correction = nullptr;
  uint64_t m_backlight_correction_id = 0;

  /* Film sensitivity handlers for simulated contact copies.  */
  std::unique_ptr<film_sensitivity> m_sensitivity = nullptr;
  std::unique_ptr<hd_curve> m_sensitivity_hd_curve = nullptr;
  std::unique_ptr<precomputed_function<luminosity_t>> m_adjust_luminosity
      = nullptr;
};

/* Get sharpened grayscale value at index X, Y without adjustments.  */
pure_attr inline luminosity_t always_inline_attr
render::get_unadjusted_data (int_point_t p) const noexcept
{
  if (colorscreen_checking)
    assert (p.x >= 0 && p.x < m_img.width && p.y >= 0 && p.y < m_img.height);
  return m_sharpened_data[p.y * m_img.width + p.x];
}

/* Adjust luminosity LUM considering infrared sensitivity and dark point.  */
pure_attr inline luminosity_t always_inline_attr
render::adjust_luminosity_ir (luminosity_t lum) const noexcept
{
  if (m_adjust_luminosity)
    return m_adjust_luminosity->apply (lum);
  lum = (lum - m_params.dark_point) * m_params.scan_exposure;
  if (m_sensitivity)
    lum = m_sensitivity->apply (lum);
  return lum;
}

/* Get sharpened grayscale value at index X, Y.  */
pure_attr inline luminosity_t always_inline_attr
render::get_data (int_point_t p) const noexcept
{
  return adjust_luminosity_ir (get_unadjusted_data (p));
}

/* Get linearized red channel value at index X, Y.  */
pure_attr inline luminosity_t always_inline_attr
render::get_linearized_data_red (int_point_t p) const noexcept
{
  if (colorscreen_checking)
    assert (p.x >= 0 && p.x < m_img.width && p.y >= 0 && p.y < m_img.height);
  return m_rgb_lookup_table[0][m_img.get_rgb_pixel (p.x, p.y).r];
}

/* Get linearized green channel value at index X, Y.  */
pure_attr inline luminosity_t always_inline_attr
render::get_linearized_data_green (int_point_t p) const noexcept
{
  if (colorscreen_checking)
    assert (p.x >= 0 && p.x < m_img.width && p.y >= 0 && p.y < m_img.height);
  return m_rgb_lookup_table[1][m_img.get_rgb_pixel (p.x, p.y).g];
}

/* Get linearized blue channel value at index X, Y.  */
pure_attr inline luminosity_t always_inline_attr
render::get_linearized_data_blue (int_point_t p) const noexcept
{
  if (colorscreen_checking)
    assert (p.x >= 0 && p.x < m_img.width && p.y >= 0 && p.y < m_img.height);
  return m_rgb_lookup_table[2][m_img.get_rgb_pixel (p.x, p.y).b];
}

/* Get sharpened red channel value at index X, Y.  */
pure_attr inline luminosity_t
render::get_data_red (int_point_t p) const noexcept
{
  if (colorscreen_checking)
    assert (p.x >= 0 && p.x < m_img.width && p.y >= 0 && p.y < m_img.height);
  luminosity_t v = m_rgb_lookup_table[0][m_img.get_rgb_pixel (p.x, p.y).r];
  if (m_backlight_correction)
    v = m_backlight_correction->apply (
        v, p.x, p.y, backlight_correction_parameters::red, true);
  v = (v - m_params.dark_point) * m_params.scan_exposure;
  return v;
}

/* Get sharpened green channel value at index X, Y.  */
pure_attr inline luminosity_t
render::get_data_green (int_point_t p) const noexcept
{
  if (colorscreen_checking)
    assert (p.x >= 0 && p.x < m_img.width && p.y >= 0 && p.y < m_img.height);
  luminosity_t v = m_rgb_lookup_table[1][m_img.get_rgb_pixel (p.x, p.y).g];
  if (m_backlight_correction)
    v = m_backlight_correction->apply (
        v, p.x, p.y, backlight_correction_parameters::green, true);
  v = (v - m_params.dark_point) * m_params.scan_exposure;
  return v;
}

/* Get sharpened blue channel value at index X, Y.  */
pure_attr inline luminosity_t
render::get_data_blue (int_point_t p) const noexcept
{
  if (colorscreen_checking)
    assert (p.x >= 0 && p.x < m_img.width && p.y >= 0 && p.y < m_img.height);
  luminosity_t v = m_rgb_lookup_table[2][m_img.get_rgb_pixel (p.x, p.y).b];
  if (m_backlight_correction)
    v = m_backlight_correction->apply (
        v, p.x, p.y, backlight_correction_parameters::blue, true);
  v = (v - m_params.dark_point) * m_params.scan_exposure;
  return v;
}

/* Quickly determine grayscale value at a given position in the image.  */
pure_attr inline luminosity_t
render::fast_get_img_pixel (int_point_t p) const noexcept
{
  if (p.x < 0 || p.x >= m_img.width || p.y < 0 || p.y >= m_img.height)
    return 0;
  return render::get_data (p);
}

/* Determine grayscale value at position XP, YP in the image using bicubic
   interpolation without adjustments.  */
pure_attr inline luminosity_t
render::get_unadjusted_img_pixel (point_t p) const noexcept
{
  /* Center of pixel [0,0] is [0.5,0.5].  */
  p.x -= (coord_t)0.5;
  p.y -= (coord_t)0.5;
  int sx, sy;
  coord_t rx = my_modf (p.x, &sx);
  coord_t ry = my_modf (p.y, &sy);

  if (sx >= 1 && sx < m_img.width - 2 && sy >= 1 && sy < m_img.height - 2)
    {
      vec_luminosity_t v1 = { get_unadjusted_data ({ sx - 1, sy - 1 }),
                              get_unadjusted_data ({ sx, sy - 1 }),
                              get_unadjusted_data ({ sx + 1, sy - 1 }),
                              get_unadjusted_data ({ sx + 2, sy - 1 }) };
      vec_luminosity_t v2 = { get_unadjusted_data ({ sx - 1, sy - 0 }),
                              get_unadjusted_data ({ sx, sy - 0 }),
                              get_unadjusted_data ({ sx + 1, sy - 0 }),
                              get_unadjusted_data ({ sx + 2, sy - 0 }) };
      vec_luminosity_t v3 = { get_unadjusted_data ({ sx - 1, sy + 1 }),
                              get_unadjusted_data ({ sx, sy + 1 }),
                              get_unadjusted_data ({ sx + 1, sy + 1 }),
                              get_unadjusted_data ({ sx + 2, sy + 1 }) };
      vec_luminosity_t v4 = { get_unadjusted_data ({ sx - 1, sy + 2 }),
                              get_unadjusted_data ({ sx, sy + 2 }),
                              get_unadjusted_data ({ sx + 1, sy + 2 }),
                              get_unadjusted_data ({ sx + 2, sy + 2 }) };
      return do_bicubic_interpolate (v1, v2, v3, v4, { rx, ry });
    }
  return 0;
}

/* Determine grayscale value at position XP, YP in the image using bicubic
   interpolation.  */
pure_attr inline luminosity_t
render::get_img_pixel (point_t p) const noexcept
{
  return adjust_luminosity_ir (get_unadjusted_img_pixel (p));
}

/* Determine RGB value at position XP, YP in the image using bicubic
   interpolation without adjustments.  */
pure_attr inline rgbdata // always_inline_attr
render::get_unadjusted_img_rgb_pixel (point_t p) const noexcept
{
  /* Center of pixel [0,0] is [0.5,0.5].  */
  p.x -= (coord_t)0.5;
  p.y -= (coord_t)0.5;
  int sx, sy;
  coord_t rx = my_modf (p.x, &sx);
  coord_t ry = my_modf (p.y, &sy);

  if (sx >= 1 && sx < m_img.width - 2 && sy >= 1 && sy < m_img.height - 2)
    {
      rgbdata ret;
      vec_luminosity_t r1 = { get_linearized_data_red ({ sx - 1, sy - 1 }),
                              get_linearized_data_red ({ sx, sy - 1 }),
                              get_linearized_data_red ({ sx + 1, sy - 1 }),
                              get_linearized_data_red ({ sx + 2, sy - 1 }) };
      vec_luminosity_t r2 = { get_linearized_data_red ({ sx - 1, sy - 0 }),
                              get_linearized_data_red ({ sx, sy - 0 }),
                              get_linearized_data_red ({ sx + 1, sy - 0 }),
                              get_linearized_data_red ({ sx + 2, sy - 0 }) };
      vec_luminosity_t r3 = { get_linearized_data_red ({ sx - 1, sy + 1 }),
                              get_linearized_data_red ({ sx, sy + 1 }),
                              get_linearized_data_red ({ sx + 1, sy + 1 }),
                              get_linearized_data_red ({ sx + 2, sy + 1 }) };
      vec_luminosity_t r4 = { get_linearized_data_red ({ sx - 1, sy + 2 }),
                              get_linearized_data_red ({ sx, sy + 2 }),
                              get_linearized_data_red ({ sx + 1, sy + 2 }),
                              get_linearized_data_red ({ sx + 2, sy + 2 }) };
      ret.red = do_bicubic_interpolate (r1, r2, r3, r4, { rx, ry });

      vec_luminosity_t g1 = { get_linearized_data_green ({ sx - 1, sy - 1 }),
                              get_linearized_data_green ({ sx, sy - 1 }),
                              get_linearized_data_green ({ sx + 1, sy - 1 }),
                              get_linearized_data_green ({ sx + 2, sy - 1 }) };
      vec_luminosity_t g2 = { get_linearized_data_green ({ sx - 1, sy - 0 }),
                              get_linearized_data_green ({ sx, sy - 0 }),
                              get_linearized_data_green ({ sx + 1, sy - 0 }),
                              get_linearized_data_green ({ sx + 2, sy - 0 }) };
      vec_luminosity_t g3 = { get_linearized_data_green ({ sx - 1, sy + 1 }),
                              get_linearized_data_green ({ sx, sy + 1 }),
                              get_linearized_data_green ({ sx + 1, sy + 1 }),
                              get_linearized_data_green ({ sx + 2, sy + 1 }) };
      vec_luminosity_t g4 = { get_linearized_data_green ({ sx - 1, sy + 2 }),
                              get_linearized_data_green ({ sx, sy + 2 }),
                              get_linearized_data_green ({ sx + 1, sy + 2 }),
                              get_linearized_data_green ({ sx + 2, sy + 2 }) };
      ret.green = do_bicubic_interpolate (g1, g2, g3, g4, { rx, ry });

      vec_luminosity_t b1 = { get_linearized_data_blue ({ sx - 1, sy - 1 }),
                              get_linearized_data_blue ({ sx, sy - 1 }),
                              get_linearized_data_blue ({ sx + 1, sy - 1 }),
                              get_linearized_data_blue ({ sx + 2, sy - 1 }) };
      vec_luminosity_t b2 = { get_linearized_data_blue ({ sx - 1, sy - 0 }),
                              get_linearized_data_blue ({ sx, sy - 0 }),
                              get_linearized_data_blue ({ sx + 1, sy - 0 }),
                              get_linearized_data_blue ({ sx + 2, sy - 0 }) };
      vec_luminosity_t b3 = { get_linearized_data_blue ({ sx - 1, sy + 1 }),
                              get_linearized_data_blue ({ sx, sy + 1 }),
                              get_linearized_data_blue ({ sx + 1, sy + 1 }),
                              get_linearized_data_blue ({ sx + 2, sy + 1 }) };
      vec_luminosity_t b4 = { get_linearized_data_blue ({ sx - 1, sy + 2 }),
                              get_linearized_data_blue ({ sx, sy + 2 }),
                              get_linearized_data_blue ({ sx + 1, sy + 2 }),
                              get_linearized_data_blue ({ sx + 2, sy + 2 }) };
      ret.blue = do_bicubic_interpolate (b1, b2, b3, b4, { rx, ry });
      if (m_backlight_correction)
        {
          ret.red = m_backlight_correction->apply (
              ret.red, p.x, p.y, backlight_correction_parameters::red, true);
          ret.green = m_backlight_correction->apply (
              ret.green, p.x, p.y, backlight_correction_parameters::green,
              true);
          ret.blue = m_backlight_correction->apply (
              ret.blue, p.x, p.y, backlight_correction_parameters::blue, true);
        }
      return ret;
    }
  return rgbdata{};
}

/* Determine RGB value at position XP, YP in the image using bicubic
 * interpolation.  */
pure_attr inline rgbdata always_inline_attr
render::get_img_rgb_pixel (point_t p) const noexcept
{
  return adjust_rgb (get_unadjusted_img_rgb_pixel (p));
}

/* Inner loop for image downscaling processing single pixel PX, PY in result
   DATA from source pixel VAL.  */
template <typename T, bool UseAtomic, auto account_p>
inline void
render::process_pixel (T *data, int width, int height, int px, int py, bool x0,
                       bool x1, bool y0, bool y1, T pixel, luminosity_t scale,
                       luminosity_t xweight, luminosity_t yweight) noexcept
{
  if (colorscreen_checking)
    {
      assert (px >= (x0 ? 0 : -1) && px < (x1 ? width - 1 : width));
      assert (py >= (y0 ? 0 : -1) && py < (y1 ? height - 1 : height));
    }
  if (x0)
    {
      if (y0)
        do_account<T, UseAtomic> (data + px + py * width, pixel,
                                  scale * (1 - yweight) * (1 - xweight));
      if (y1)
        do_account<T, UseAtomic> (data + px + (py + 1) * width, pixel,
                                  scale * yweight * (1 - xweight));
    }
  if (x1)
    {
      if (y0)
        do_account<T, UseAtomic> (data + px + (py * width) + 1, pixel,
                                  scale * (1 - yweight) * xweight);
      if (y1)
        do_account<T, UseAtomic> (data + px + (py + 1) * width + 1, pixel,
                                  scale * yweight * xweight);
    }
}

/* Inner loop for image downscaling processing single line.  */
template <typename T, typename D, T (D::*get_pixel) (int_point_t p) const,
          bool UseAtomic, auto account_p>
inline void
render::process_line (T *data, int *pixelpos, luminosity_t *weights,
                      int xstart, int xend, int width, int height, int py,
                      int yy, bool y0, bool y1, luminosity_t scale,
                      luminosity_t yweight) noexcept
{
  int px = xstart;
  int xx = pixelpos[px];
  int stop;
  if (yy < 0 || yy >= m_img.height || xx >= m_img.width)
    return;
  if (px >= 0 && xx >= 0)
    {
      T pixel = (((D *)this)->*get_pixel) ({ xx, yy });
      process_pixel<T, UseAtomic, account_p> (data, width, height, px - 1, py,
                                              false, true, y0, y1, pixel,
                                              scale, weights[px], yweight);
    }
  xx++;
  if (xx < 0)
    xx = 0;
  stop = pixelpos[px + 1];
  for (; xx < stop; xx++)
    {
      T pixel = (((D *)this)->*get_pixel) ({ xx, yy });
      process_pixel<T, UseAtomic, account_p> (data, width, height, px, py,
                                              true, false, y0, y1, pixel,
                                              scale, 0, yweight);
    }
  px++;
  while (px <= xend)
    {
      T pixel = (((D *)this)->*get_pixel) ({ xx, yy });
      process_pixel<T, UseAtomic, account_p> (data, width, height, px - 1, py,
                                              true, true, y0, y1, pixel, scale,
                                              weights[px], yweight);
      stop = pixelpos[px + 1];
      xx++;
      for (; xx < stop; xx++)
        {
          T pixel = (((D *)this)->*get_pixel) ({ xx, yy });
          process_pixel<T, UseAtomic, account_p> (data, width, height, px, py,
                                                  true, false, y0, y1, pixel,
                                                  scale, 0, yweight);
        }
      px++;
    }
  if (xx < m_img.width)
    {
      T pixel = (((D *)this)->*get_pixel) ({ xx, yy });
      process_pixel<T, UseAtomic, account_p> (data, width, height, px - 1, py,
                                              true, false, y0, y1, pixel,
                                              scale, weights[px], yweight);
    }
}

template <typename D, typename T, T (D::*get_pixel) (int_point_t p) const,
          auto account_p>
bool
render::downscale (T *data, point_t p, int width, int height,
                   coord_t pixelsize, progress_info *progress) noexcept
{
  coord_t x = p.x;
  coord_t y = p.y;
  int pxstart = std::max (0, (int)(-x / pixelsize));
  int pxend = std::min (width - 1, (int)((m_img.width - x) / pixelsize));

  std::fill (data, data + (size_t)width * height, T{});

  if (pxstart > pxend)
    return true;

  if (progress)
    {
      int pystart = std::max (0, (int)(-y / pixelsize));
      int pyend = std::min (height - 1, (int)((m_img.height - y) / pixelsize));
      progress->set_task ("downscaling", pyend - pystart + 1);
    }

  /* Precompute scaling weights for X coordinate.  */
  std::vector<int> pixelpos (width + 2);
  std::vector<luminosity_t> weights (width + 2);

  for (int px = pxstart; px <= pxend + 1; px++)
    {
      coord_t ix = x + pixelsize * px;
      int xx = my_floor (ix);
      pixelpos[px] = std::min (xx, m_img.width);
      weights[px] = 1 - (ix - xx);
    }

#define ypixelpos(p) ((int)my_floor (y + pixelsize * (p)))
#define weight(p) (1 - (y + pixelsize * (p) - ypixelpos (p)))

#pragma omp parallel shared(progress, data, pixelsize, width, height,         \
                                pixelpos, x, y, pxstart, pxend,               \
                                weights) default(none)
  {
    luminosity_t scale = (luminosity_t)1.0 / (pixelsize * pixelsize);
    int pystart = std::max (0, (int)(-y / pixelsize));
    int pyend = std::min (height - 1, (int)((m_img.height - y) / pixelsize));

#ifdef _OPENMP
    int tn = omp_get_thread_num ();
    int threads = omp_get_num_threads ();
#else
    int tn = 0;
    int threads = 1;
#endif

    int ystart = pystart + (pyend + 1 - pystart) * tn / threads;
    int yend = pystart + (pyend + 1 - pystart) * (tn + 1) / threads - 1;

    int py = ystart;
    int yy = ypixelpos (py);
    int stop;

    if (ystart <= yend)
      {
        /* The first line of each thread's range may overlap with the previous
           thread's last line because bilinear interpolation spans two output
           rows.  We use UseAtomic=true for boundary rows.  */
        if (py >= 0 && yy >= 0)
          process_line<T, D, get_pixel, true, account_p> (
              data, pixelpos.data (), weights.data (), pxstart, pxend, width,
              height, py - 1, yy, false, true, scale, weight (py));
        yy++;
        stop = std::min (ypixelpos (py + 1), m_img.height);
        for (; yy < stop; yy++)
          {
            /* If we have only one output row in our chunk, use atomics.  */
            if (py == yend)
              process_line<T, D, get_pixel, true, account_p> (
                  data, pixelpos.data (), weights.data (), pxstart, pxend,
                  width, height, py, yy, true, false, scale, 0);
            else
              process_line<T, D, get_pixel, false, account_p> (
                  data, pixelpos.data (), weights.data (), pxstart, pxend,
                  width, height, py, yy, true, false, scale, 0);
          }
        py++;

        while (py <= yend && (!progress || !progress->cancel_requested ()))
          {
            /* Only use atomics if this is the last row of our range, which
               might be shared with the next thread.  */
            if (py == yend)
              process_line<T, D, get_pixel, true, account_p> (
                  data, pixelpos.data (), weights.data (), pxstart, pxend,
                  width, height, py - 1, yy, true, true, scale, weight (py));
            else
              process_line<T, D, get_pixel, false, account_p> (
                  data, pixelpos.data (), weights.data (), pxstart, pxend,
                  width, height, py - 1, yy, true, true, scale, weight (py));

            stop = std::min (ypixelpos (py + 1), m_img.height);
            yy++;
            for (; yy < stop; yy++)
              {
                if (py == yend)
                  process_line<T, D, get_pixel, true, account_p> (
                      data, pixelpos.data (), weights.data (), pxstart, pxend,
                      width, height, py, yy, true, false, scale, 0);
                else
                  process_line<T, D, get_pixel, false, account_p> (
                      data, pixelpos.data (), weights.data (), pxstart, pxend,
                      width, height, py, yy, true, false, scale, 0);
              }
            py++;
            if (progress)
              progress->inc_progress ();
          }
        /* Final line of input image might also contribute to the last row of
         * our range.  */
        if (yy < m_img.height)
          process_line<T, D, get_pixel, true, account_p> (
              data, pixelpos.data (), weights.data (), pxstart, pxend, width,
              height, py - 1, yy, true, false, scale, weight (py));
      }
  }

#undef ypixelpos
#undef weight
  return !progress || !progress->cancelled ();
}
}
#endif
