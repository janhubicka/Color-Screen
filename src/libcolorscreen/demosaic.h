/* Demosaicing algorithms for early color processes.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#ifndef DEMOSAIC_H
#define DEMOSAIC_H
#include "bitmap.h"
#include "bspline.h"
#include "include/color.h"
#include "include/progress-info.h"
#include "include/render-parameters.h"
#include "denoise.h"
#include "cubic-interpolate.h"
#include "lanczos.h"
#include <utility>

template <typename T>
static inline T
my_abs (T x)
{
  return x < 0 ? -x : x;
}
namespace colorscreen
{
template <typename GEOMETRY> class analyze_base_worker;
class analyze_paget;
class analyze_dufay;
class dufay_geometry;
class paget_geometry;
class render;

/* Base class for all demosaicing implementations providing common
   data structures and basic accessors.  */
class demosaic_generic_base
{
protected:
  static constexpr const bool debug = colorscreen_checking;

public:
  /* Return reference to demosaiced data at [X, Y] with bounds clamping.  */
  rgbdata &
  demosaiced_data (int x, int y)
  {
    x = std::clamp (x, 0, m_area.width - 1);
    y = std::clamp (y, 0, m_area.height - 1);
    return m_demosaiced[y * m_area.width + x];
  };

  /* Return reference to demosaiced data at [X, Y] without clamping.  */
  rgbdata &
  fast_demosaiced_data (int x, int y)
  {
    return m_demosaiced[y * m_area.width + x];
  };

  /* Determine the robust maximum value in the demosaiced image using a
     histogram.  PROGRESS can be used to report progress and check for
     cancellation.  */
  luminosity_t
  find_robust_max (progress_info *progress)
  {
    histogram h;
    if (progress)
      progress->set_task ("determining demosaiced value range",
                          m_area.height * 2);

#pragma omp parallel for reduction(histogram_range : h)
    for (int y = 0; y < m_area.height; y++)
      {
        if (!progress || !progress->cancel_requested ())
          for (int x = 0; x < m_area.width; x++)
            {
              h.pre_account (fast_demosaiced_data (x, y).red);
              h.pre_account (fast_demosaiced_data (x, y).green);
              h.pre_account (fast_demosaiced_data (x, y).blue);
            }
        if (progress)
          progress->inc_progress ();
      }

    if (progress && progress->cancelled ())
      return 1;

    h.finalize_range (256);

#pragma omp parallel for reduction(histogram_entries : h)
    for (int y = 0; y < m_area.height; y++)
      {
        if (!progress || !progress->cancel_requested ())
          for (int x = 0; x < m_area.width; x++)
            {
              h.account (fast_demosaiced_data (x, y).red);
              h.account (fast_demosaiced_data (x, y).green);
              h.account (fast_demosaiced_data (x, y).blue);
            }
        if (progress)
          progress->inc_progress ();
      }

    if (progress && progress->cancelled ())
      return 1;
    h.finalize ();
    return h.find_max (0.03);
  }

  std::vector<rgbdata> m_demosaiced;

protected:
  int_image_area m_area;

  /* Internal accessor shortcut used in demosaicing algorithms.
     Returns reference to data at [X, Y] with clamping.  */
  always_inline_attr rgbdata &
  d (int x, int y)
  {
    x = std::clamp (x, 0, m_area.width - 1);
    y = std::clamp (y, 0, m_area.height - 1);
    return m_demosaiced[y * m_area.width + x];
  };
};

/* Base class for demosaicing implementations providing common
   interpolation kernels and data accessors.  GEOMETRY specifies
   the CFA pattern.  */
template <typename GEOMETRY, typename ANALYZER = analyze_base_worker<GEOMETRY>>
class demosaic_base : public demosaic_generic_base
{
  inline pure_attr rgbdata
  lanczos3_demosaiced_interpolate (point_t scr)
  {
    point_t p = GEOMETRY::to_demosaiced_coordinates (scr);
    int sx, sy;
    coord_t rx = my_modf (p.x, &sx);
    coord_t ry = my_modf (p.y, &sy);
    sx += m_area.xshift ();
    sy += m_area.yshift ();
    if (sx >= 2 && sx < m_area.width - 3 && sy >= 2 && sy < m_area.height - 3)
      {
        rgbdata ret = { 0, 0, 0 };
        const luminosity_t *wx = lanczos3_kernel (rx);
        const luminosity_t *wy = lanczos3_kernel (ry);

        for (int j = 0; j < 6; j++)
          {
            rgbdata row_sum = { 0, 0, 0 };
            for (int i = 0; i < 6; i++)
              {
                rgbdata d = fast_demosaiced_data (sx - 2 + i, sy - 2 + j);
                row_sum.red += d.red * wx[i];
                row_sum.green += d.green * wx[i];
                row_sum.blue += d.blue * wx[i];
              }
            ret.red += row_sum.red * wy[j];
            ret.green += row_sum.green * wy[j];
            ret.blue += row_sum.blue * wy[j];
          }
        return ret;
      }
    return { (luminosity_t)0, (luminosity_t)0, (luminosity_t)0 };
  }
  inline pure_attr rgbdata
  nearest_demosaiced_interpolate (point_t scr)
  {
    point_t p = GEOMETRY::to_demosaiced_coordinates (scr);
    int sx = nearest_int (p.x);
    int sy = nearest_int (p.y);
    sx += m_area.xshift ();
    sy += m_area.yshift ();
    if (sx >= 0 && sx < m_area.width && sy >= 0 && sy < m_area.height)
      {
        rgbdata ret;
        ret.red = fast_demosaiced_data (sx, sy).red;
        ret.green = fast_demosaiced_data (sx, sy).green;
        ret.blue = fast_demosaiced_data (sx, sy).blue;
        return ret;
      }
    return { (luminosity_t)0, (luminosity_t)0, (luminosity_t)0 };
  }
  inline pure_attr rgbdata
  linear_demosaiced_interpolate (point_t scr)
  {
    point_t p = GEOMETRY::to_demosaiced_coordinates (scr);
    int sx, sy;
    coord_t rx = my_modf (p.x, &sx);
    coord_t ry = my_modf (p.y, &sy);
    sx += m_area.xshift ();
    sy += m_area.yshift ();
    if (sx >= 0 && sx < m_area.width - 1 && sy >= 0 && sy < m_area.height - 1)
      {
        rgbdata ret;
        ret.red = do_linear_interpolate (
            fast_demosaiced_data (sx, sy).red,
            fast_demosaiced_data (sx + 1, sy).red,
            fast_demosaiced_data (sx, sy + 1).red,
            fast_demosaiced_data (sx + 1, sy + 1).red, { rx, ry });
        ret.green = do_linear_interpolate (
            fast_demosaiced_data (sx, sy).green,
            fast_demosaiced_data (sx + 1, sy).green,
            fast_demosaiced_data (sx, sy + 1).green,
            fast_demosaiced_data (sx + 1, sy + 1).green, { rx, ry });
        ret.blue = do_linear_interpolate (
            fast_demosaiced_data (sx, sy).blue,
            fast_demosaiced_data (sx + 1, sy).blue,
            fast_demosaiced_data (sx, sy + 1).blue,
            fast_demosaiced_data (sx + 1, sy + 1).blue, { rx, ry });
        return ret;
      }
    return { (luminosity_t)0, (luminosity_t)0, (luminosity_t)0 };
  }
  inline pure_attr rgbdata
  bspline_demosaiced_interpolate (point_t scr)
  {
    point_t p = GEOMETRY::to_demosaiced_coordinates (scr);
    int sx, sy;
    coord_t rx = my_modf (p.x, &sx);
    coord_t ry = my_modf (p.y, &sy);
    sx += m_area.xshift ();
    sy += m_area.yshift ();
    if (sx >= 1 && sx < m_area.width - 2 && sy >= 1 && sy < m_area.height - 2)
      {
        rgbdata ret;
        ret.red = do_bspline_interpolate (
            (vec_luminosity_t){ fast_demosaiced_data (sx - 1, sy - 1).red,
                                fast_demosaiced_data (sx, sy - 1).red,
                                fast_demosaiced_data (sx + 1, sy - 1).red,
                                fast_demosaiced_data (sx + 2, sy - 1).red },
            (vec_luminosity_t){ fast_demosaiced_data (sx - 1, sy).red,
                                fast_demosaiced_data (sx, sy).red,
                                fast_demosaiced_data (sx + 1, sy).red,
                                fast_demosaiced_data (sx + 2, sy).red },
            (vec_luminosity_t){ fast_demosaiced_data (sx - 1, sy + 1).red,
                                fast_demosaiced_data (sx, sy + 1).red,
                                fast_demosaiced_data (sx + 1, sy + 1).red,
                                fast_demosaiced_data (sx + 2, sy + 1).red },
            (vec_luminosity_t){ fast_demosaiced_data (sx - 1, sy + 2).red,
                                fast_demosaiced_data (sx, sy + 2).red,
                                fast_demosaiced_data (sx + 1, sy + 2).red,
                                fast_demosaiced_data (sx + 2, sy + 2).red },
            { rx, ry });
        ret.green = do_bspline_interpolate (
            (vec_luminosity_t){ fast_demosaiced_data (sx - 1, sy - 1).green,
                                fast_demosaiced_data (sx, sy - 1).green,
                                fast_demosaiced_data (sx + 1, sy - 1).green,
                                fast_demosaiced_data (sx + 2, sy - 1).green },
            (vec_luminosity_t){ fast_demosaiced_data (sx - 1, sy).green,
                                fast_demosaiced_data (sx, sy).green,
                                fast_demosaiced_data (sx + 1, sy).green,
                                fast_demosaiced_data (sx + 2, sy).green },
            (vec_luminosity_t){ fast_demosaiced_data (sx - 1, sy + 1).green,
                                fast_demosaiced_data (sx, sy + 1).green,
                                fast_demosaiced_data (sx + 1, sy + 1).green,
                                fast_demosaiced_data (sx + 2, sy + 1).green },
            (vec_luminosity_t){ fast_demosaiced_data (sx - 1, sy + 2).green,
                                fast_demosaiced_data (sx, sy + 2).green,
                                fast_demosaiced_data (sx + 1, sy + 2).green,
                                fast_demosaiced_data (sx + 2, sy + 2).green },
            { rx, ry });
        ret.blue = do_bspline_interpolate (
            (vec_luminosity_t){ fast_demosaiced_data (sx - 1, sy - 1).blue,
                                fast_demosaiced_data (sx, sy - 1).blue,
                                fast_demosaiced_data (sx + 1, sy - 1).blue,
                                fast_demosaiced_data (sx + 2, sy - 1).blue },
            (vec_luminosity_t){ fast_demosaiced_data (sx - 1, sy).blue,
                                fast_demosaiced_data (sx, sy).blue,
                                fast_demosaiced_data (sx + 1, sy).blue,
                                fast_demosaiced_data (sx + 2, sy).blue },
            (vec_luminosity_t){ fast_demosaiced_data (sx - 1, sy + 1).blue,
                                fast_demosaiced_data (sx, sy + 1).blue,
                                fast_demosaiced_data (sx + 1, sy + 1).blue,
                                fast_demosaiced_data (sx + 2, sy + 1).blue },
            (vec_luminosity_t){ fast_demosaiced_data (sx - 1, sy + 2).blue,
                                fast_demosaiced_data (sx, sy + 2).blue,
                                fast_demosaiced_data (sx + 1, sy + 2).blue,
                                fast_demosaiced_data (sx + 2, sy + 2).blue },
            { rx, ry });
        return ret;
      }
    return { (luminosity_t)0, (luminosity_t)0, (luminosity_t)0 };
  }
  inline pure_attr rgbdata
  bicubic_demosaiced_interpolate (point_t scr)
  {
    point_t p = GEOMETRY::to_demosaiced_coordinates (scr);
    int sx, sy;
    coord_t rx = my_modf (p.x, &sx);
    coord_t ry = my_modf (p.y, &sy);
    sx += m_area.xshift ();
    sy += m_area.yshift ();
    if (sx >= 1 && sx < m_area.width - 2 && sy >= 1 && sy < m_area.height - 2)
      {
        rgbdata ret;
        ret.red = do_bicubic_interpolate (
            (vec_luminosity_t){ fast_demosaiced_data (sx - 1, sy - 1).red,
                                fast_demosaiced_data (sx, sy - 1).red,
                                fast_demosaiced_data (sx + 1, sy - 1).red,
                                fast_demosaiced_data (sx + 2, sy - 1).red },
            (vec_luminosity_t){ fast_demosaiced_data (sx - 1, sy).red,
                                fast_demosaiced_data (sx, sy).red,
                                fast_demosaiced_data (sx + 1, sy).red,
                                fast_demosaiced_data (sx + 2, sy).red },
            (vec_luminosity_t){ fast_demosaiced_data (sx - 1, sy + 1).red,
                                fast_demosaiced_data (sx, sy + 1).red,
                                fast_demosaiced_data (sx + 1, sy + 1).red,
                                fast_demosaiced_data (sx + 2, sy + 1).red },
            (vec_luminosity_t){ fast_demosaiced_data (sx - 1, sy + 2).red,
                                fast_demosaiced_data (sx, sy + 2).red,
                                fast_demosaiced_data (sx + 1, sy + 2).red,
                                fast_demosaiced_data (sx + 2, sy + 2).red },
            { rx, ry });
        ret.green = do_bicubic_interpolate (
            (vec_luminosity_t){ fast_demosaiced_data (sx - 1, sy - 1).green,
                                fast_demosaiced_data (sx, sy - 1).green,
                                fast_demosaiced_data (sx + 1, sy - 1).green,
                                fast_demosaiced_data (sx + 2, sy - 1).green },
            (vec_luminosity_t){ fast_demosaiced_data (sx - 1, sy).green,
                                fast_demosaiced_data (sx, sy).green,
                                fast_demosaiced_data (sx + 1, sy).green,
                                fast_demosaiced_data (sx + 2, sy).green },
            (vec_luminosity_t){ fast_demosaiced_data (sx - 1, sy + 1).green,
                                fast_demosaiced_data (sx, sy + 1).green,
                                fast_demosaiced_data (sx + 1, sy + 1).green,
                                fast_demosaiced_data (sx + 2, sy + 1).green },
            (vec_luminosity_t){ fast_demosaiced_data (sx - 1, sy + 2).green,
                                fast_demosaiced_data (sx, sy + 2).green,
                                fast_demosaiced_data (sx + 1, sy + 2).green,
                                fast_demosaiced_data (sx + 2, sy + 2).green },
            { rx, ry });
        ret.blue = do_bicubic_interpolate (
            (vec_luminosity_t){ fast_demosaiced_data (sx - 1, sy - 1).blue,
                                fast_demosaiced_data (sx, sy - 1).blue,
                                fast_demosaiced_data (sx + 1, sy - 1).blue,
                                fast_demosaiced_data (sx + 2, sy - 1).blue },
            (vec_luminosity_t){ fast_demosaiced_data (sx - 1, sy).blue,
                                fast_demosaiced_data (sx, sy).blue,
                                fast_demosaiced_data (sx + 1, sy).blue,
                                fast_demosaiced_data (sx + 2, sy).blue },
            (vec_luminosity_t){ fast_demosaiced_data (sx - 1, sy + 1).blue,
                                fast_demosaiced_data (sx, sy + 1).blue,
                                fast_demosaiced_data (sx + 1, sy + 1).blue,
                                fast_demosaiced_data (sx + 2, sy + 1).blue },
            (vec_luminosity_t){ fast_demosaiced_data (sx - 1, sy + 2).blue,
                                fast_demosaiced_data (sx, sy + 2).blue,
                                fast_demosaiced_data (sx + 1, sy + 2).blue,
                                fast_demosaiced_data (sx + 2, sy + 2).blue },
            { rx, ry });
        return ret;
      }
    return { (luminosity_t)0, (luminosity_t)0, (luminosity_t)0 };
  }

public:
  inline pure_attr rgbdata
  interpolate (point_t scr, rgbdata patch_proportions,
               render_parameters::demosaiced_scaling_t scaling_mode)
  {
    switch (scaling_mode)
      {
      case render_parameters::lanczos3_scaling:
        return lanczos3_demosaiced_interpolate (scr);
      case render_parameters::bspline_scaling:
        return bspline_demosaiced_interpolate (scr);
      case render_parameters::bicubic_scaling:
        return bicubic_demosaiced_interpolate (scr);
      case render_parameters::linear_scaling:
        return linear_demosaiced_interpolate (scr);
      case render_parameters::nearest_scaling:
        return nearest_demosaiced_interpolate (scr);
      default:
        abort ();
      }
  }

protected:
  bool
  initialize (ANALYZER *analysis)
  {
    m_area = analysis->demosaiced_area ();
    m_demosaiced.resize ((size_t)m_area.width * m_area.height);
    return true;
  }

  /* Accessor for mosaiced channel data with sanity check that channel is
     defined and access is in bounds.  */
  always_inline_attr luminosity_t &
  dch (int x, int y, int ch)
  {
    int cx = std::clamp (x, 0, m_area.width - 1);
    int cy = std::clamp (y, 0, m_area.height - 1);
    assert (!debug || cx != x || cy != y
            || (int)GEOMETRY::demosaic_entry_color (cx, cy) == ch);
    return m_demosaiced[cy * m_area.width + cx][ch];
  };
  /* Return the known (mosaiced) channel value at position (x,y).
     The color is determined by GEOMETRY.  */
  always_inline_attr luminosity_t
  known (int x, int y)
  {
    int cx = std::clamp (x, 0, m_area.width - 1);
    int cy = std::clamp (y, 0, m_area.height - 1);
    switch (GEOMETRY::demosaic_entry_color (x, y))
      {
      case base_geometry::red:
        return m_demosaiced[cy * m_area.width + cx].red;
      case base_geometry::green:
        return m_demosaiced[cy * m_area.width + cx].green;
      default:
        return m_demosaiced[cy * m_area.width + cx].blue;
      }
  };
  /* Step 1 of the generic demosaicing algorithm: interpolate the dominating
     pattern (usually green). For each non-green pixel, check cardinal and
     diagonal neighbors and interpolate accordingly. The Laplacian correction
     uses the known channel at the current pixel and same-color pixels at
     distance 2.
     PROGRESS can be used to report progress and check for cancellation.
     AH_GREEN is the dominating channel.
     SMOOTHEN specifies whether to apply post-processing smoothing.  */
  template <int ah_green, bool smoothen>
  bool
  generic_dominating_channel (progress_info *progress)
  {
    int w = m_area.width, h = m_area.height;
#pragma omp parallel shared(progress, h, w) default(none)
    for (int y = 0; y < h; y++)
      {
        if (!progress || !progress->cancel_requested ())
          for (int x = 0; x < w; x++)
            {
              int color = GEOMETRY::demosaic_entry_color (x, y);
              if (color == ah_green)
                continue;

              luminosity_t known_val = known (x, y);

              /* Check which cardinal neighbors are green.  */
              bool left_g
                  = GEOMETRY::demosaic_entry_color (x - 1, y) == ah_green;
              bool right_g
                  = GEOMETRY::demosaic_entry_color (x + 1, y) == ah_green;
              bool up_g
                  = GEOMETRY::demosaic_entry_color (x, y - 1) == ah_green;
              bool down_g
                  = GEOMETRY::demosaic_entry_color (x, y + 1) == ah_green;
              bool h_green = left_g && right_g;
              bool v_green = up_g && down_g;

              if (h_green && v_green)
                {
                  /* Green in both cardinal directions: Hamilton-Adams.  */
                  luminosity_t g_left = dch (x - 1, y, ah_green);
                  luminosity_t g_right = dch (x + 1, y, ah_green);
                  luminosity_t g_up = dch (x, y - 1, ah_green);
                  luminosity_t g_down = dch (x, y + 1, ah_green);
                  luminosity_t kl2 = known (x - 2, y);
                  luminosity_t kr2 = known (x + 2, y);
                  luminosity_t ku2 = known (x, y - 2);
                  luminosity_t kd2 = known (x, y + 2);

                  luminosity_t grad_h = my_abs (g_left - g_right)
                                        + my_abs (2 * known_val - kl2 - kr2);
                  luminosity_t grad_v = my_abs (g_up - g_down)
                                        + my_abs (2 * known_val - ku2 - kd2);
                  luminosity_t lapl_h
                      = (2 * known_val - kl2 - kr2) * (luminosity_t)0.25;
                  luminosity_t lapl_v
                      = (2 * known_val - ku2 - kd2) * (luminosity_t)0.25;

                  if (grad_h < grad_v)
                    d (x, y)[(int)ah_green]
                        = (g_left + g_right) * (luminosity_t)0.5 + lapl_h;
                  else if (grad_v < grad_h)
                    d (x, y)[(int)ah_green]
                        = (g_up + g_down) * (luminosity_t)0.5 + lapl_v;
                  else
                    d (x, y)[(int)ah_green]
                        = (g_left + g_right + g_up + g_down)
                              * (luminosity_t)0.25
                          + (lapl_h + lapl_v) * (luminosity_t)0.5;
                }
              else if (h_green)
                {
                  /* Green only in horizontal direction.  */
                  luminosity_t g_left = dch (x - 1, y, ah_green);
                  luminosity_t g_right = dch (x + 1, y, ah_green);
                  luminosity_t kl2 = known (x - 2, y);
                  luminosity_t kr2 = known (x + 2, y);
                  d (x, y)[(int)ah_green]
                      = (g_left + g_right) * (luminosity_t)0.5
                        + (2 * known_val - kl2 - kr2) * (luminosity_t)0.25;
                }
              else if (v_green)
                {
                  /* Green only in vertical direction.  */
                  luminosity_t g_up = dch (x, y - 1, ah_green);
                  luminosity_t g_down = dch (x, y + 1, ah_green);
                  luminosity_t ku2 = known (x, y - 2);
                  luminosity_t kd2 = known (x, y + 2);
                  d (x, y)[(int)ah_green]
                      = (g_up + g_down) * (luminosity_t)0.5
                        + (2 * known_val - ku2 - kd2) * (luminosity_t)0.25;
                }
              else
                {
                  /* No cardinal green neighbors; use diagonal greens.  */
                  bool tl_g = GEOMETRY::demosaic_entry_color (x - 1, y - 1)
                              == ah_green;
                  bool tr_g = GEOMETRY::demosaic_entry_color (x + 1, y - 1)
                              == ah_green;
                  bool bl_g = GEOMETRY::demosaic_entry_color (x - 1, y + 1)
                              == ah_green;
                  bool br_g = GEOMETRY::demosaic_entry_color (x + 1, y + 1)
                              == ah_green;
                  int count = 0;
                  luminosity_t sum = 0;
                  if (tl_g)
                    {
                      sum += dch (x - 1, y - 1, ah_green);
                      count++;
                    }
                  if (tr_g)
                    {
                      sum += dch (x + 1, y - 1, ah_green);
                      count++;
                    }
                  if (bl_g)
                    {
                      sum += dch (x - 1, y + 1, ah_green);
                      count++;
                    }
                  if (br_g)
                    {
                      sum += dch (x + 1, y + 1, ah_green);
                      count++;
                    }

                  if (count > 0)
                    {
                      luminosity_t kl2 = known (x - 2, y);
                      luminosity_t kr2 = known (x + 2, y);
                      luminosity_t ku2 = known (x, y - 2);
                      luminosity_t kd2 = known (x, y + 2);
                      luminosity_t lapl
                          = (4 * known_val - kl2 - kr2 - ku2 - kd2)
                            * (luminosity_t)0.125;
                      d (x, y)[(int)ah_green] = sum / count + lapl;
                    }
                }
            }
        if (progress)
          progress->inc_progress ();
      }
    return !progress || !progress->cancelled ();
  }

  /* Step 2 of the generic demosaicing algorithm: interpolate the remaining
     channels (red and blue). Green is assumed to be fully populated.
     We use green-channel-guided color-difference interpolation:
     interpolate (C-G) from positions where C is known, then add local green.
     PROGRESS can be used to report progress and check for cancellation.
     AH_GREEN is the dominating channel.
     AH_RED is the red channel index.
     AH_BLUE is the blue channel index.  */
  template <int ah_green, int ah_red, int ah_blue>
  bool
  generic_interpolation_remaining_channels (progress_info *progress)
  {
    int w = m_area.width, h = m_area.height;
#pragma omp parallel shared(progress, h, w) default(none)
    for (int y = 0; y < h; y++)
      {
        if (!progress || !progress->cancel_requested ())
          for (int x = 0; x < w; x++)
            {
              int color = GEOMETRY::demosaic_entry_color (x, y);
              luminosity_t g_here = d (x, y)[(int)ah_green];

              /* Interpolate red if this pixel is not red.  */
              if (color != ah_red)
                {
                  /* Check cardinal neighbors for red.  */
                  bool l_r
                      = GEOMETRY::demosaic_entry_color (x - 1, y) == ah_red;
                  bool r_r
                      = GEOMETRY::demosaic_entry_color (x + 1, y) == ah_red;
                  bool u_r
                      = GEOMETRY::demosaic_entry_color (x, y - 1) == ah_red;
                  bool d_r
                      = GEOMETRY::demosaic_entry_color (x, y + 1) == ah_red;
                  bool h_r = l_r && r_r;
                  bool v_r = u_r && d_r;

                  if (h_r && v_r)
                    {
                      luminosity_t rdl = d (x - 1, y)[(int)ah_red]
                                         - d (x - 1, y)[(int)ah_green];
                      luminosity_t rdr = d (x + 1, y)[(int)ah_red]
                                         - d (x + 1, y)[(int)ah_green];
                      luminosity_t rdu = d (x, y - 1)[(int)ah_red]
                                         - d (x, y - 1)[(int)ah_green];
                      luminosity_t rdd = d (x, y + 1)[(int)ah_red]
                                         - d (x, y + 1)[(int)ah_green];
                      luminosity_t rgh = my_abs (rdl - rdr);
                      luminosity_t rgv = my_abs (rdu - rdd);
                      if (rgh < rgv)
                        d (x, y)[(int)ah_red]
                            = g_here + (rdl + rdr) * (luminosity_t)0.5;
                      else if (rgv < rgh)
                        d (x, y)[(int)ah_red]
                            = g_here + (rdu + rdd) * (luminosity_t)0.5;
                      else
                        d (x, y)[(int)ah_red]
                            = g_here
                              + (rdl + rdr + rdu + rdd) * (luminosity_t)0.25;
                    }
                  else if (h_r)
                    {
                      luminosity_t rdl = d (x - 1, y)[(int)ah_red]
                                         - d (x - 1, y)[(int)ah_green];
                      luminosity_t rdr = d (x + 1, y)[(int)ah_red]
                                         - d (x + 1, y)[(int)ah_green];
                      d (x, y)[(int)ah_red]
                          = g_here + (rdl + rdr) * (luminosity_t)0.5;
                    }
                  else if (v_r)
                    {
                      luminosity_t rdu = d (x, y - 1)[(int)ah_red]
                                         - d (x, y - 1)[(int)ah_green];
                      luminosity_t rdd = d (x, y + 1)[(int)ah_red]
                                         - d (x, y + 1)[(int)ah_green];
                      d (x, y)[(int)ah_red]
                          = g_here + (rdu + rdd) * (luminosity_t)0.5;
                    }
                  else
                    {
                      /* Check diagonals for red.  */
                      bool tl_r = GEOMETRY::demosaic_entry_color (x - 1, y - 1)
                                  == ah_red;
                      bool tr_r = GEOMETRY::demosaic_entry_color (x + 1, y - 1)
                                  == ah_red;
                      bool bl_r = GEOMETRY::demosaic_entry_color (x - 1, y + 1)
                                  == ah_red;
                      bool br_r = GEOMETRY::demosaic_entry_color (x + 1, y + 1)
                                  == ah_red;
                      int cnt = 0;
                      luminosity_t rdsum = 0;
                      if (tl_r)
                        {
                          rdsum += d (x - 1, y - 1)[(int)ah_red]
                                   - d (x - 1, y - 1)[(int)ah_green];
                          cnt++;
                        }
                      if (tr_r)
                        {
                          rdsum += d (x + 1, y - 1)[(int)ah_red]
                                   - d (x + 1, y - 1)[(int)ah_green];
                          cnt++;
                        }
                      if (bl_r)
                        {
                          rdsum += d (x - 1, y + 1)[(int)ah_red]
                                   - d (x - 1, y + 1)[(int)ah_green];
                          cnt++;
                        }
                      if (br_r)
                        {
                          rdsum += d (x + 1, y + 1)[(int)ah_red]
                                   - d (x + 1, y + 1)[(int)ah_green];
                          cnt++;
                        }
                      if (cnt > 0)
                        d (x, y)[(int)ah_red] = g_here + rdsum / cnt;
                    }
                }

              /* Interpolate blue if this pixel is not blue.  */
              if (color != ah_blue)
                {
                  /* Check cardinal neighbors for blue.  */
                  bool l_b
                      = GEOMETRY::demosaic_entry_color (x - 1, y) == ah_blue;
                  bool r_b
                      = GEOMETRY::demosaic_entry_color (x + 1, y) == ah_blue;
                  bool u_b
                      = GEOMETRY::demosaic_entry_color (x, y - 1) == ah_blue;
                  bool d_b
                      = GEOMETRY::demosaic_entry_color (x, y + 1) == ah_blue;
                  bool h_b = l_b && r_b;
                  bool v_b = u_b && d_b;

                  if (h_b && v_b)
                    {
                      luminosity_t bdl = d (x - 1, y)[(int)ah_blue]
                                         - d (x - 1, y)[(int)ah_green];
                      luminosity_t bdr = d (x + 1, y)[(int)ah_blue]
                                         - d (x + 1, y)[(int)ah_green];
                      luminosity_t bdu = d (x, y - 1)[(int)ah_blue]
                                         - d (x, y - 1)[(int)ah_green];
                      luminosity_t bdd = d (x, y + 1)[(int)ah_blue]
                                         - d (x, y + 1)[(int)ah_green];
                      luminosity_t bgh = my_abs (bdl - bdr);
                      luminosity_t bgv = my_abs (bdu - bdd);
                      if (bgh < bgv)
                        d (x, y)[(int)ah_blue]
                            = g_here + (bdl + bdr) * (luminosity_t)0.5;
                      else if (bgv < bgh)
                        d (x, y)[(int)ah_blue]
                            = g_here + (bdu + bdd) * (luminosity_t)0.5;
                      else
                        d (x, y)[(int)ah_blue]
                            = g_here
                              + (bdl + bdr + bdu + bdd) * (luminosity_t)0.25;
                    }
                  else if (h_b)
                    {
                      luminosity_t bdl = d (x - 1, y)[(int)ah_blue]
                                         - d (x - 1, y)[(int)ah_green];
                      luminosity_t bdr = d (x + 1, y)[(int)ah_blue]
                                         - d (x + 1, y)[(int)ah_green];
                      d (x, y)[(int)ah_blue]
                          = g_here + (bdl + bdr) * (luminosity_t)0.5;
                    }
                  else if (v_b)
                    {
                      luminosity_t bdu = d (x, y - 1)[(int)ah_blue]
                                         - d (x, y - 1)[(int)ah_green];
                      luminosity_t bdd = d (x, y + 1)[(int)ah_blue]
                                         - d (x, y + 1)[(int)ah_green];
                      d (x, y)[(int)ah_blue]
                          = g_here + (bdu + bdd) * (luminosity_t)0.5;
                    }
                  else
                    {
                      /* Check diagonals for blue.  */
                      bool tl_b = GEOMETRY::demosaic_entry_color (x - 1, y - 1)
                                  == ah_blue;
                      bool tr_b = GEOMETRY::demosaic_entry_color (x + 1, y - 1)
                                  == ah_blue;
                      bool bl_b = GEOMETRY::demosaic_entry_color (x - 1, y + 1)
                                  == ah_blue;
                      bool br_b = GEOMETRY::demosaic_entry_color (x + 1, y + 1)
                                  == ah_blue;
                      int cnt = 0;
                      luminosity_t bdsum = 0;
                      if (tl_b)
                        {
                          bdsum += d (x - 1, y - 1)[(int)ah_blue]
                                   - d (x - 1, y - 1)[(int)ah_green];
                          cnt++;
                        }
                      if (tr_b)
                        {
                          bdsum += d (x + 1, y - 1)[(int)ah_blue]
                                   - d (x + 1, y - 1)[(int)ah_green];
                          cnt++;
                        }
                      if (bl_b)
                        {
                          bdsum += d (x - 1, y + 1)[(int)ah_blue]
                                   - d (x - 1, y + 1)[(int)ah_green];
                          cnt++;
                        }
                      if (br_b)
                        {
                          bdsum += d (x + 1, y + 1)[(int)ah_blue]
                                   - d (x + 1, y + 1)[(int)ah_green];
                          cnt++;
                        }
                      if (cnt > 0)
                        d (x, y)[(int)ah_blue] = g_here + bdsum / cnt;
                    }
                }
            }
        if (progress)
          progress->inc_progress ();
      }
    return !progress || !progress->cancelled ();
  }

  /* Step 1 of the Hamilton-Adams demosaicing algorithm: interpolate the
     dominating channel (usually green). For each non-green pixel, check
     cardinal and diagonal neighbors and interpolate accordingly. The Laplacian
     correction uses the known channel at the current pixel and same-color
     pixels at distance 2.
     PROGRESS can be used to report progress and check for cancellation.
     AH_GREEN is the dominating channel.
     SMOOTHEN specifies whether to apply post-processing smoothing.  */
  template <int ah_green, bool smoothen>
  bool
  hamilton_adams_interpolation_dominating_channel (progress_info *progress)
  {
    int w = m_area.width, h = m_area.height;
    luminosity_t range = find_robust_max (progress);
    if (progress && progress->cancelled ())
      return false;
    luminosity_t TL = (luminosity_t)0.1 * range;
    luminosity_t TH = (luminosity_t)0.8 * range;

    bitmap_2d predA (smoothen ? w : 0, smoothen ? h : 0);
    if (progress)
      progress->set_task ("demosaicing dominating channel (hamilton-adams)",
                          h);
    /* Real hamilton adams can not be parallelized.  */
    std::vector<luminosity_t> pbv (w);
    for (int y = 0; y < h; y++)
      {
        luminosity_t pbh = 0;

        if (!progress || !progress->cancel_requested ())
          for (int x = 0; x < w; x++)
            {
              int color = GEOMETRY::demosaic_entry_color (x, y);
              if (color == ah_green)
                continue;

              luminosity_t g_10 = dch (x - 1, y, ah_green);
              luminosity_t g10 = dch (x + 1, y, ah_green);
              luminosity_t g0_1 = dch (x, y - 1, ah_green);
              luminosity_t g01 = dch (x, y + 1, ah_green);

              luminosity_t c00 = known (x, y);
              luminosity_t c_20 = known (x - 2, y);
              luminosity_t c20 = known (x + 2, y);
              luminosity_t c0_2 = known (x, y - 2);
              luminosity_t c02 = known (x, y + 2);

              luminosity_t bh
                  = (pbh + c0_2 - d (x, y - 2)[ah_green]) * (luminosity_t)0.5;
              pbh = bh;
              luminosity_t bv = (pbv[x] + c_20 - d (x - 2, y)[ah_green])
                                * (luminosity_t)0.5;
              pbv[x] = bv;

              luminosity_t h = my_abs (2 * c00 - c0_2 - c02)
                               + 2 * my_abs (g0_1 - g01)
                               + my_abs (2 * (c00 + bh) - g0_1 - g01);

              luminosity_t v = my_abs (2 * c00 - c_20 - c20)
                               + 2 * my_abs (g_10 - g10)
                               + my_abs (2 * (c00 + bv) - g_10 - g10);

              if (h < TL && v < TL)
                {
                  d (x, y)[(int)ah_green]
                      = (g_10 + g10 + g0_1 + g01) * (luminosity_t)0.25;
                  if (smoothen)
                    predA.set_bit (x, y);
                }
              else if (h > TH && v > TH)
                d (x, y)[(int)ah_green]
                    = (g_10 + g10 + g0_1 + g01) * (luminosity_t)0.25
                      + (4 * c00 - c_20 - c20 - c0_2 - c02)
                            * (1 / (luminosity_t)12);
              else if (h < v)
                d (x, y)[(int)ah_green]
                    = (g0_1 + g01) * (luminosity_t)0.5
                      + (2 * c00 - c0_2 - c02) * (luminosity_t)0.25;
              else
                d (x, y)[(int)ah_green]
                    = (g_10 + g10) * (luminosity_t)0.5
                      + (2 * c00 - c_20 - c20) * (luminosity_t)0.25;
            }
        if (progress)
          progress->inc_progress ();
      }
    if (progress && progress->cancelled ())
      return false;
    if (smoothen)
      {
        if (progress)
          progress->set_task ("smoothening noisy areas in the dominating "
                              "channel (hamilton-adams)",
                              h);
#pragma omp parallel shared(progress, h, w, predA) default(none)
        for (int y = 1; y < h - 1; y++)
          {
            if (!progress || !progress->cancel_requested ())
              for (int x = 1; x < w - 1; x++)
                {
                  int color = GEOMETRY::demosaic_entry_color (x, y);
                  if (color != ah_green
                      || (int)predA.test_bit (x - 1, y)
                                 + (int)predA.test_bit (x + 1, y)
                                 + (int)predA.test_bit (x, y - 1)
                                 + (int)predA.test_bit (x, y + 1)
                             <= 2)
                    continue;
                  d (x, y)[ah_green]
                      = (d (x - 1, y)[ah_green] + d (x + 1, y)[ah_green]
                         + d (x, y - 1)[ah_green] + d (x, y + 1)[ah_green])
                        * (luminosity_t)0.25;
                }
            if (progress)
              progress->inc_progress ();
          }
      }
    return !progress || !progress->cancelled ();
  }

  /* Basic interpolation logic of Hamilton-Adams for non-dominating color.
     G_HERE is the dominating channel value at the current pixel.
     X and Y are the coordinates of the pixel.
     AH_GREEN is the index of the dominating channel.
     AH_CHN is the index of the channel being interpolated.  */
  template <int ah_green, int ah_chn>
  always_inline_attr void
  hamilton_adams_interpolate_color (luminosity_t g_here, int x, int y)
  {
    /* Check cardinal neighbors for red.  */
    bool l_r = GEOMETRY::demosaic_entry_color (x - 1, y) == ah_chn;
    bool r_r = GEOMETRY::demosaic_entry_color (x + 1, y) == ah_chn;
    bool u_r = GEOMETRY::demosaic_entry_color (x, y - 1) == ah_chn;
    bool d_r = GEOMETRY::demosaic_entry_color (x, y + 1) == ah_chn;
    bool h_r = l_r && r_r;
    bool v_r = u_r && d_r;

    assert (!colorscreen_checking || (!h_r || !v_r));

    if (h_r)
      {
        luminosity_t rdl = dch (x - 1, y, ah_chn);
        luminosity_t rdr = dch (x + 1, y, ah_chn);
        luminosity_t gnl = d (x - 1, y)[ah_green];
        luminosity_t gnr = d (x + 1, y)[ah_green];
        d (x, y)[(int)ah_chn] = ((2 * g_here - gnl - gnr) + (rdl + rdr)) * 0.5;
      }
    else if (v_r)
      {
        luminosity_t rdu = dch (x, y - 1, ah_chn);
        luminosity_t rdd = dch (x, y + 1, ah_chn);
        luminosity_t gnu = d (x, y - 1)[ah_green];
        luminosity_t gnd = d (x, y + 1)[ah_green];
        d (x, y)[(int)ah_chn] = ((2 * g_here - gnu - gnd) + (rdu + rdd)) * 0.5;
      }
    else
      {
        luminosity_t g00 = d (x, y)[(int)ah_green];
        luminosity_t g_1_1 = d (x - 1, y - 1)[ah_green];
        luminosity_t g11 = d (x + 1, y + 1)[ah_green];
        luminosity_t g1_1 = d (x + 1, y - 1)[ah_green];
        luminosity_t g_11 = d (x - 1, y + 1)[ah_green];

        luminosity_t r_1_1 = dch (x - 1, y - 1, ah_chn);
        luminosity_t r11 = dch (x + 1, y + 1, ah_chn);
        luminosity_t r1_1 = dch (x + 1, y - 1, ah_chn);
        luminosity_t r_11 = dch (x - 1, y + 1, ah_chn);

        luminosity_t dn = my_abs (2 * g00 - g_1_1 - g11) + my_abs (r_1_1 - r11);
        luminosity_t dp = my_abs (2 * g00 - g_11 - g1_1) + my_abs (r_11 - r1_1);
        if (dn < dp)
          d (x, y)[(int)ah_chn]
              = (r_1_1 + r11 + 2 * g00 - g_1_1 - g11) * (luminosity_t)0.5;
        if (dp < dn)
          d (x, y)[(int)ah_chn]
              = (r_11 + r1_1 + 2 * g00 - g_11 - g1_1) * (luminosity_t)0.5;
        else
          d (x, y)[(int)ah_chn] = (r_1_1 + r11 + r_11 + r1_1 + 4 * g00 - g_1_1
                                   - g11 - g_11 - g1_1)
                                  * (luminosity_t)0.25;
      }
  };
  /* Step 3 of the Hamilton-Adams algorithm: interpolate red and blue
     channels at positions where they are missing.
     PROGRESS can be used to report progress and check for cancellation.
     AH_GREEN is the index of the dominating channel.
     AH_RED is the index of the red channel.
     AH_BLUE is the index of the blue channel.  */
  template <int ah_green, int ah_red, int ah_blue>
  bool
  hamilton_adams_interpolation_remaining_channels (progress_info *progress)
  {
    int h = m_area.height, w = m_area.width;
    if (progress)
      progress->set_task ("demosaicing remaining chanels (hamilton-adams)", h);
#pragma omp parallel shared(progress, h, w) default(none)
    for (int y = 0; y < h; y++)
      {
        if (!progress || !progress->cancel_requested ())
          for (int x = 0; x < w; x++)
            {
              int color = GEOMETRY::demosaic_entry_color (x, y);
              luminosity_t g_here = d (x, y)[(int)ah_green];

              if (color != ah_red)
                hamilton_adams_interpolate_color<ah_green, ah_red> (g_here, x,
                                                                    y);
              if (color != ah_blue)
                hamilton_adams_interpolate_color<ah_green, ah_blue> (g_here, x,
                                                                     y);
            }
        if (progress)
          progress->inc_progress ();
      }
    return !progress || !progress->cancelled ();
  }
  /* DIFFERENCE TO DCRAW:
     This interpolation algorithm shares the fundamental structural mathematics
     found in `dcraw`'s secondary step (bilinear averaging of color differences
     against the evaluated Green channel). However, `dcraw` strictly bounds
     these results utilizing minimum and maximum limits (its `CLIP` and `ULIM`
     macros). We intentionally omit interpolation clipping here to preserve
     overblown HDR highlight peaks and robust linear profiles.
   */
  /* Internal helper to interpolate missing color channel CH using
     evaluated dominating channel G_DIR at position [X, Y].
     This uses green-channel-guided color-difference bilinear interpolation
     without clipping, preserving HDR highlights.  */
  luminosity_t
  interp (int x, int y, int ch, const std::vector<luminosity_t> &G_dir)
  {
    int w = m_area.width, h = m_area.height;
    x = std::clamp (x, 0, w - 1);
    y = std::clamp (y, 0, h - 1);
    if (GEOMETRY::demosaic_entry_color (x, y) == ch)
      return dch (x, y, ch);

    bool l_c = GEOMETRY::demosaic_entry_color (x - 1, y) == ch;
    bool r_c = GEOMETRY::demosaic_entry_color (x + 1, y) == ch;
    bool u_c = GEOMETRY::demosaic_entry_color (x, y - 1) == ch;
    bool d_c = GEOMETRY::demosaic_entry_color (x, y + 1) == ch;

    luminosity_t g_here = G_dir[y * w + x];

    if (l_c && r_c)
      {
        int xl = std::clamp (x - 1, 0, w - 1);
        int xr = std::clamp (x + 1, 0, w - 1);
        return g_here
               + (dch (x - 1, y, ch) - G_dir[y * w + xl] + dch (x + 1, y, ch)
                  - G_dir[y * w + xr])
                     * (luminosity_t)0.5;
      }
    else if (u_c && d_c)
      {
        int yu = std::clamp (y - 1, 0, h - 1);
        int yd = std::clamp (y + 1, 0, h - 1);
        return g_here
               + (dch (x, y - 1, ch) - G_dir[yu * w + x] + dch (x, y + 1, ch)
                  - G_dir[yd * w + x])
                     * (luminosity_t)0.5;
      }
    else if (l_c)
      return g_here + dch (x - 1, y, ch)
             - G_dir[y * w + std::clamp (x - 1, 0, w - 1)];
    else if (r_c)
      return g_here + dch (x + 1, y, ch)
             - G_dir[y * w + std::clamp (x + 1, 0, w - 1)];
    else if (u_c)
      return g_here + dch (x, y - 1, ch)
             - G_dir[std::clamp (y - 1, 0, h - 1) * w + x];
    else if (d_c)
      return g_here + dch (x, y + 1, ch)
             - G_dir[std::clamp (y + 1, 0, h - 1) * w + x];
    else
      {
        bool tl = GEOMETRY::demosaic_entry_color (x - 1, y - 1) == ch;
        bool tr = GEOMETRY::demosaic_entry_color (x + 1, y - 1) == ch;
        bool bl = GEOMETRY::demosaic_entry_color (x - 1, y + 1) == ch;
        bool br = GEOMETRY::demosaic_entry_color (x + 1, y + 1) == ch;
        int cnt = 0;
        luminosity_t sum = 0;
        if (tl)
          {
            int cx = std::clamp (x - 1, 0, w - 1);
            int cy = std::clamp (y - 1, 0, h - 1);
            sum += dch (x - 1, y - 1, ch) - G_dir[cy * w + cx];
            cnt++;
          }
        if (tr)
          {
            int cx = std::clamp (x + 1, 0, w - 1);
            int cy = std::clamp (y - 1, 0, h - 1);
            sum += dch (x + 1, y - 1, ch) - G_dir[cy * w + cx];
            cnt++;
          }
        if (bl)
          {
            int cx = std::clamp (x - 1, 0, w - 1);
            int cy = std::clamp (y + 1, 0, h - 1);
            sum += dch (x - 1, y + 1, ch) - G_dir[cy * w + cx];
            cnt++;
          }
        if (br)
          {
            int cx = std::clamp (x + 1, 0, w - 1);
            int cy = std::clamp (y + 1, 0, h - 1);
            sum += dch (x + 1, y + 1, ch) - G_dir[cy * w + cx];
            cnt++;
          }
        if (cnt > 0)
          return g_here + sum / (luminosity_t)cnt;
      }
    return g_here;
  };
  /* AHD (Adaptive Homogeneity-Directed) interpolation for the dominating
     channel. This algorithm performs directional interpolations (horizontal
     and vertical) for the dominating channel. It interpolates the remaining
     channels in those directions if using the slow mode. It evaluates a
     homogeneity metric and selects the direction with the greatest
     homogeneity. The fast version uses a simple color difference variance
     metric. The slow version computes true CIELAB homogeneity mapped across a
     3x3 window using the full color delta metrics. Template parameters allow
     its application on arbitrary geometries where color channels can be
     exchanged.
   */
  template <int ah_green, int ah_red, int ah_blue, bool fast, bool smoothen>
  bool
  ahd_interpolation_dominating_channel (progress_info *progress)
  {
    int w = m_area.width, h = m_area.height;
    if (progress)
      progress->set_task (fast ? "demosaicing dominating channel (ahd fast)"
                               : "demosaicing dominating channel (ahd slow)",
                          fast ? (smoothen ? h * 2 : h)
                               : (smoothen ? h * 5 : h * 4));

    std::vector<luminosity_t> Green_H (w * h);
    std::vector<luminosity_t> Green_V (w * h);

    /* Real AHD requires filling Green_H and Green_V fully first.

       DIFFERENCE TO DCRAW:
       `dcraw` uses the same mathematical interpolation for Green
       (Hamilton-Adams base) but it aggressively clamps the result to strictly
       within the physical neighboring bounds using a `ULIM` macro to avoid
       overshoot artifacts:

       [dcraw snippet]
       for (i=0; i < 4; i++)
         FORC3 cpixels[i][c] = ULIM(cpixels[i][c], val1, val2);
    */
    if (progress)
      progress->set_task (fast ? "demosaicing dominating channel (ahd fast)"
                               : "demosaicing dominating channel (ahd slow)",
                          fast ? (smoothen ? h * 2 : h)
                               : (smoothen ? h * 5 : h * 4));
    std::vector<luminosity_t> green_h (w * h);
    std::vector<luminosity_t> green_v (w * h);

#pragma omp parallel shared(progress, h, w, green_h, green_v) default(none)
    for (int y = 0; y < h; y++)
      {
        if (!progress || !progress->cancel_requested ())
          for (int x = 0; x < w; x++)
            {
              int color = GEOMETRY::demosaic_entry_color (x, y);
              if (color == ah_green)
                {
                  green_h[y * w + x] = known (x, y);
                  green_v[y * w + x] = known (x, y);
                  continue;
                }

              luminosity_t g_10 = dch (x - 1, y, ah_green);
              luminosity_t g10 = dch (x + 1, y, ah_green);
              luminosity_t g0_1 = dch (x, y - 1, ah_green);
              luminosity_t g01 = dch (x, y + 1, ah_green);

              luminosity_t c00 = known (x, y);
              luminosity_t c_20 = known (x - 2, y);
              luminosity_t c20 = known (x + 2, y);
              luminosity_t c0_2 = known (x, y - 2);
              luminosity_t c02 = known (x, y + 2);

              green_h[y * w + x]
                  = (g_10 + g10) * (luminosity_t)0.5
                    + (2 * c00 - c_20 - c20) * (luminosity_t)0.25;
              green_v[y * w + x]
                  = (g0_1 + g01) * (luminosity_t)0.5
                    + (2 * c00 - c0_2 - c02) * (luminosity_t)0.25;
            }
        if (progress)
          progress->inc_progress ();
      }
    if (progress && progress->cancelled ())
      return false;

    std::vector<rgbdata> rgb_h;
    std::vector<rgbdata> rgb_v;
    std::vector<cie_lab> lab_h;
    std::vector<cie_lab> lab_v;

    if (!fast)
      {
        luminosity_t range = find_robust_max (progress);
        if (progress && progress->cancelled ())
          return false;
        if (range <= 0)
          range = 1.0f;

        rgb_h.resize (w * h);
        rgb_v.resize (w * h);

#pragma omp parallel shared(progress, h, w, rgb_h, rgb_v, green_h,            \
                                green_v) default(none)
        for (int y = 0; y < h; y++)
          {
            if (!progress || !progress->cancel_requested ())
              for (int x = 0; x < w; x++)
                {
                  int idx = y * w + x;
                  rgb_h[idx][ah_green] = green_h[idx];
                  rgb_h[idx][ah_red] = interp (x, y, ah_red, green_h);
                  rgb_h[idx][ah_blue] = interp (x, y, ah_blue, green_h);

                  rgb_v[idx][ah_green] = green_v[idx];
                  rgb_v[idx][ah_red] = interp (x, y, ah_red, green_v);
                  rgb_v[idx][ah_blue] = interp (x, y, ah_blue, green_v);
                }
            if (progress)
              progress->inc_progress ();
          }
        if (progress && progress->cancelled ())
          return false;

        // Convert to Lab sequentially to avoid omp initialization/sizing
        // overhead
        lab_h.resize (w * h, cie_lab (xyz (0, 0, 0), srgb_white));
        lab_v.resize (w * h, cie_lab (xyz (0, 0, 0), srgb_white));
#pragma omp parallel for shared(progress, h, w, range, rgb_h, rgb_v, lab_h,   \
                                    lab_v, srgb_white) default(none)
        for (int i = 0; i < h * w; i++)
          {
            xyz xyz_h = xyz::from_srgb (
                std::clamp ((float)(rgb_h[i].red / range), 0.0f, 1.0f),
                std::clamp ((float)(rgb_h[i].green / range), 0.0f, 1.0f),
                std::clamp ((float)(rgb_h[i].blue / range), 0.0f, 1.0f));
            lab_h[i] = cie_lab (xyz_h, srgb_white);

            xyz xyz_v = xyz::from_srgb (
                std::clamp ((float)(rgb_v[i].red / range), 0.0f, 1.0f),
                std::clamp ((float)(rgb_v[i].green / range), 0.0f, 1.0f),
                std::clamp ((float)(rgb_v[i].blue / range), 0.0f, 1.0f));
            lab_v[i] = cie_lab (xyz_v, srgb_white);
          }
      }

    bitmap_2d predA (smoothen ? w : 0, smoothen ? h : 0);

    std::vector<luminosity_t> h_scores (w * h, 0);
    std::vector<luminosity_t> v_scores (w * h, 0);

#pragma omp parallel shared(progress, h, w, green_h, green_v, lab_h, lab_v,   \
                                h_scores, v_scores) default(none)
    for (int y = 0; y < h; y++)
      {
        if (!progress || !progress->cancel_requested ())
          for (int x = 0; x < w; x++)
            {
              int color = GEOMETRY::demosaic_entry_color (x, y);
              if (color == ah_green)
                continue;

              if (fast)
                {
                  // Fast variance metric utilizing robust HA gradient
                  // calculations
                  luminosity_t gh = green_h[y * w + x];
                  luminosity_t gv = green_v[y * w + x];
                  luminosity_t c00 = known (x, y);

                  luminosity_t g_10 = dch (x - 1, y, ah_green);
                  luminosity_t g10 = dch (x + 1, y, ah_green);
                  luminosity_t g0_1 = dch (x, y - 1, ah_green);
                  luminosity_t g01 = dch (x, y + 1, ah_green);

                  luminosity_t c_20 = known (x - 2, y);
                  luminosity_t c20 = known (x + 2, y);
                  luminosity_t c0_2 = known (x, y - 2);
                  luminosity_t c02 = known (x, y + 2);

                  // h_scores is the penalty for horizontal interpolation
                  // (measures horizontal variance)
                  h_scores[y * w + x] = my_abs (2 * c00 - c_20 - c20)
                                        + 2 * my_abs (g_10 - g10)
                                        + my_abs (2 * gh - g_10 - g10);
                  // v_scores is the penalty for vertical interpolation
                  // (measures vertical variance)
                  v_scores[y * w + x] = my_abs (2 * c00 - c0_2 - c02)
                                        + 2 * my_abs (g0_1 - g01)
                                        + my_abs (2 * gv - g0_1 - g01);
                }
              else
                {
                  // Slow CIELAB homogeneity metric
                  /*
                    DIFFERENCE TO DCRAW:
                    `dcraw` assesses homogeneity by scanning only 4 cardinal
                    neighbors and checking if their differences fall within a
                    dynamically constructed threshold based on the Min/Max
                    variations of the opposing directions. It checks Luma
                    (ldiff) and Chroma (abdiff) spaces independently:

                    [dcraw snippet]
                    leps = MIN(MAX(ldiff[0][0],ldiff[0][1]),
                    MAX(ldiff[1][2],ldiff[1][3])); abeps =
                    MIN(MAX(abdiff[0][0],abdiff[0][1]),
                    MAX(abdiff[1][2],abdiff[1][3])); for (d=0; d < 2; d++) for
                    (i=0; i < 4; i++) if (ldiff[d][i] <= leps && abdiff[d][i]
                    <= abeps) homo[d][tr][tc]++;

                    Our implementation utilizes a true 3x3 surrounding window
                    (all 8 neighbors) and evaluates the standard perceptual
                    CIEDE2000-esque geometric threshold constraint
                    (`deltaE < 2.0f`).
                  */
                  int h_homo = 0;
                  int v_homo = 0;

                  // 3x3 window homogeneity check against the center pixel
                  for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++)
                      {
                        int nx = std::clamp (x + dx, 0, w - 1);
                        int ny = std::clamp (y + dy, 0, h - 1);

                        if (deltaE (lab_h[y * w + x], lab_h[ny * w + nx])
                            < 2.0f)
                          h_homo++;
                        if (deltaE (lab_v[y * w + x], lab_v[ny * w + nx])
                            < 2.0f)
                          v_homo++;
                      }

                  // Inverting the score so a lower score consistently means
                  // 'better'
                  h_scores[y * w + x] = -h_homo;
                  v_scores[y * w + x] = -v_homo;
                }
            }
      }

#pragma omp parallel shared(progress, h, w, predA, green_h, green_v,          \
                                h_scores, v_scores) default(none)
    for (int y = 0; y < h; y++)
      {
        if (!progress || !progress->cancel_requested ())
          for (int x = 0; x < w; x++)
            {
              int color = GEOMETRY::demosaic_entry_color (x, y);
              if (color == ah_green)
                continue;

              // Spatially smooth the decision map to reduce
              // checkerboard/blocking artifacts
              /*
                 DIFFERENCE TO DCRAW:
                 `dcraw` effectively does the exact same 3x3 sum mapping to
                 apply spatial smoothing over the homogeneity array. However,
                 our implementation forces a hard binary decision (`<=`), while
                 `dcraw` explicitly identifies ties
                 (`hm[0] == hm[1]`) and manually blends the horizontal and
                 vertical models instead of biasing direction:

                 [dcraw snippet]
                 for (d=0; d < 2; d++)
                   for (hm[d]=0, i=tr-1; i <= tr+1; i++)
                     for (j=tc-1; j <= tc+1; j++)
                       hm[d] += homo[d][i][j];

                 if (hm[0] != hm[1])
                   FORC3 image[row*width+col][c] = rgb[hm[1] >
                 hm[0]][tr][tc][c]; else FORC3 image[row*width+col][c] =
                 (rgb[0][tr][tc][c] + rgb[1][tr][tc][c]) >> 1;
              */
              luminosity_t smoothed_h = 0;
              luminosity_t smoothed_v = 0;

              for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                  {
                    int nx = std::clamp (x + dx, 0, w - 1);
                    int ny = std::clamp (y + dy, 0, h - 1);

                    if (GEOMETRY::demosaic_entry_color (nx, ny) != ah_green)
                      {
                        smoothed_h += h_scores[ny * w + nx];
                        smoothed_v += v_scores[ny * w + nx];
                      }
                  }

              if (smoothed_h <= smoothed_v)
                {
                  d (x, y)[ah_green] = green_h[y * w + x];
                  if (smoothen)
                    predA.set_bit (x, y);
                }
              else
                {
                  d (x, y)[ah_green] = green_v[y * w + x];
                }
            }
        if (progress)
          progress->inc_progress ();
      }
    if (progress && progress->cancelled ())
      return false;

    if (smoothen)
      {
        if (progress)
          progress->set_task ("smoothening noisy areas in the dominating "
                              "channel (ahd)",
                              h);
#pragma omp parallel shared(progress, h, w, predA) default(none)
        for (int y = 1; y < h - 1; y++)
          {
            if (!progress || !progress->cancel_requested ())
              for (int x = 1; x < w - 1; x++)
                {
                  int color = GEOMETRY::demosaic_entry_color (x, y);
                  if (color != ah_green
                      || (int)predA.test_bit (x - 1, y)
                                 + (int)predA.test_bit (x + 1, y)
                                 + (int)predA.test_bit (x, y - 1)
                                 + (int)predA.test_bit (x, y + 1)
                             <= 2)
                    continue;
                  d (x, y)[ah_green]
                      = (d (x - 1, y)[ah_green] + d (x + 1, y)[ah_green]
                         + d (x, y - 1)[ah_green] + d (x, y + 1)[ah_green])
                        * (luminosity_t)0.25;
                }
            if (progress)
              progress->inc_progress ();
          }
      }
    return !progress || !progress->cancelled ();
  }

  /* Step 2 of the AHD algorithm: interpolate red and blue channels at
     positions where they are missing. Now green is fully populated by AHD.
     We use a parallel color difference strategy that mirrors Hamilton-Adams's
     residual calculation for remaining channels.
     PROGRESS can be used to report progress and check for cancellation.
     AH_GREEN is the dominating channel.
     AH_RED is the red channel.
     AH_BLUE is the blue channel.  */
  template <int ah_green, int ah_red, int ah_blue>
  bool
  ahd_interpolation_remaining_channels (progress_info *progress)
  {
    int h = m_area.height, w = m_area.width;
    if (progress)
      progress->set_task ("demosaicing remaining channels (ahd)", h);
#pragma omp parallel shared(progress, h, w) default(none)
    for (int y = 0; y < h; y++)
      {
        if (!progress || !progress->cancel_requested ())
          for (int x = 0; x < w; x++)
            {
              int color = GEOMETRY::demosaic_entry_color (x, y);
              luminosity_t g_here = d (x, y)[(int)ah_green];

              if (color != ah_red)
                hamilton_adams_interpolate_color<ah_green, ah_red> (g_here, x,
                                                                    y);
              if (color != ah_blue)
                hamilton_adams_interpolate_color<ah_green, ah_blue> (g_here, x,
                                                                     y);
            }
        if (progress)
          progress->inc_progress ();
      }
    return !progress || !progress->cancelled ();
  }

  /* Median of three values.  Used extensively in AMaZE for bounding
     interpolation in regions of high saturation.  */
  static always_inline_attr luminosity_t
  median3 (luminosity_t a, luminosity_t b, luminosity_t c)
  {
    return std::max (std::min (a, b), std::min (std::max (a, b), c));
  }

  /* Hamilton-Adams green estimate from one cardinal direction.
     G_NEIGHBOR is the dominating channel value at the adjacent pixel,
     C_HERE is the known channel value at the current pixel,
     C_FAR is the same-channel value at distance 2 in that direction.
     Returns the estimated dominating channel value from that direction.

     This is the fundamental HA building block shared between Hamilton-Adams
     and AMaZE.  The estimate is: neighbor + 0.5 * (c_here - c_far),
     which adds a Laplacian correction to the simple average.

     Possible improvement: higher-order corrections using values at
     distance 3 or 4 could improve accuracy at the cost of larger
     support.  */
  static always_inline_attr luminosity_t
  ha_green_one_direction (luminosity_t g_neighbor, luminosity_t c_here,
                          luminosity_t c_far)
  {
    return g_neighbor + (c_here - c_far) * (luminosity_t)0.5;
  }

  /* AMaZE (Aliasing Minimization and Zipper Elimination) demosaicing
     algorithm.

     Based on the algorithm by Emil Martinec, incorporating ideas from
     Luis Sanz Rodrigues and Paul Lee. This implementation follows the
     RawTherapee reference implementation closely, adapted to work with
     the GEOMETRY template and floating-point linear data in the range
     0...find_robust_max.

     The algorithm has these main phases:
       1. Compute directional gradients on the CFA data.
       2. Interpolate color differences using adaptive color ratios and HA,
          selecting the lower-variance estimate.
       3. Determine adaptive weights for combining horizontal/vertical
          interpolations based on color-difference variance analysis.
       4. (Optional) Detect Nyquist textures and apply area interpolation.
       5. Populate the dominating channel using the computed weights.
       6. Interpolate remaining channels using existing HA color-difference
          method.

     Template parameters:
       AH_GREEN  - the dominating channel (plays role of "green" in Bayer)
       AH_RED    - first non-dominating channel
       AH_BLUE   - second non-dominating channel
       DO_NYQUIST - if true, enable Nyquist texture detection and handling

     Possible improvements:
       - SIMD vectorization of the inner loops
       - Tile-based processing to improve cache locality (as in RawTherapee)
       - Full diagonal chrominance interpolation for R/B instead of reusing
         Hamilton-Adams (would improve quality near diagonal edges)
       - Adaptive clipping threshold based on local statistics  */
  template <int ah_green, int ah_red, int ah_blue, bool do_nyquist>
  bool
  amaze_interpolation (progress_info *progress)
  {
    int w = m_area.width, h = m_area.height;
    luminosity_t range = find_robust_max (progress);
    if (progress && progress->cancelled ())
      return false;
    if (range <= 0)
      range = 1;

    /* Clipping thresholds for highlight fallback.
       When pixel values approach CLIP_PT, the adaptive color ratio
       method becomes unreliable, so we fall back to Hamilton-Adams.
       CLIP_PT8 is the soft threshold (80% of max).  */
    const luminosity_t clip_pt = range;
    const luminosity_t clip_pt8 = (luminosity_t)0.8 * range;

    /* Tolerance to avoid division by zero.  */
    constexpr luminosity_t eps = (luminosity_t)1e-5;
    constexpr luminosity_t epssq = (luminosity_t)1e-10;

    /* Adaptive ratio threshold: if |1 - color_ratio| < ARTHRESH,
       use the ratio-based interpolation; otherwise fall back to HA.
       The ratio method works well when the color ratio is close to 1,
       i.e., when adjacent pixels have similar values.  When the ratio
       deviates too far from 1, HA is more stable.

       Possible improvement: make this threshold adaptive based on
       local noise estimation.  */
    constexpr luminosity_t arthresh = (luminosity_t)0.75;

    /* Gaussian kernel for Nyquist texture test on 5x5 quincunx, sigma=1.2.  */
    constexpr luminosity_t gauss_odd[4]
        = { 0.14659727707323927f, 0.103592713382435f, 0.0732036125103057f,
            0.0365543548389495f };

    /* Nyquist test threshold.  */
    constexpr luminosity_t nyq_thresh = (luminosity_t)0.5;

    /* Gaussian kernel for gradient weighting, pre-multiplied with
       NYQ_THRESH.  */
    constexpr luminosity_t gauss_grad[6] = {
      nyq_thresh * 0.07384411893421103f, nyq_thresh * 0.06207511968171489f,
      nyq_thresh * 0.0521818194747806f,  nyq_thresh * 0.03687419286733595f,
      nyq_thresh * 0.03099732204057846f, nyq_thresh * 0.018413194161458882f
    };

    /* Count total progress steps across all phases.  */
    if (progress)
      progress->set_task ("demosaicing (amaze)", h * 6);

    /* Allocate working arrays.
       dir_wts_v/h: directional gradient weights (vertical/horizontal).
       vcd/hcd: vertical/horizontal color differences (G-C).
       dg_int_v/h: squared differences between opposing directional
       interpolations. hv_wt: the final per-pixel weight for combining
       horizontal and vertical interpolations (0 = pure horizontal, 1 = pure
       vertical).

       Possible improvement: use a tile-based approach to reduce peak
       memory usage, processing the image in overlapping tiles as
       RawTherapee does.  */
    std::vector<luminosity_t> dir_wts_v (w * h);
    std::vector<luminosity_t> dir_wts_h (w * h);
    std::vector<luminosity_t> vcd (w * h);
    std::vector<luminosity_t> hcd (w * h);
    std::vector<luminosity_t> dg_int_v (w * h);
    std::vector<luminosity_t> dg_int_h (w * h);
    std::vector<luminosity_t> hv_wt (w * h);

    /* Nyquist-specific working arrays (allocated only if needed).  */
    std::vector<luminosity_t> del_hv_sq_sum (do_nyquist ? w * h : 0);
    std::vector<luminosity_t> cd_diff_sq (do_nyquist ? w * h : 0);
    std::vector<uint8_t> nyquist_flags (do_nyquist ? w * h : 0);

    /* ================================================================
       Phase 1: Compute directional gradients.

       For each pixel, compute the horizontal and vertical gradient
       magnitudes from the CFA data.  These measure how rapidly the raw
       sensor values change in each direction.

       Gradients at distance 2 are used because in a Bayer-like pattern,
       same-color pixels are spaced at distance 2.
       ================================================================ */
#pragma omp parallel for schedule(dynamic, 16)
    for (int y = 0; y < h; y++)
      {
        if (progress && progress->cancel_requested ())
          continue;
        for (int x = 0; x < w; x++)
          {
            int idx = y * w + x;
            luminosity_t c00 = known (x, y);
            luminosity_t delh = my_abs (known (x + 1, y) - known (x - 1, y));
            luminosity_t delv = my_abs (known (x, y + 1) - known (x, y - 1));

            dir_wts_v[idx] = eps + my_abs (known (x, y - 2) - c00)
                             + my_abs (c00 - known (x, y + 2)) + delv;
            dir_wts_h[idx] = eps + my_abs (known (x - 2, y) - c00)
                             + my_abs (c00 - known (x + 2, y)) + delh;

            if (do_nyquist)
              del_hv_sq_sum[idx] = delh * delh + delv * delv;
          }
        if (progress)
          progress->inc_progress ();
      }
    if (progress && progress->cancelled ())
      return false;

    /* ================================================================
       Phase 2: Interpolate color differences using adaptive ratios and
       Hamilton-Adams, then bound in highlights.

       At each pixel we compute two independent estimates:
       (a) Adaptive color ratio: works well when the color ratio is
           locally constant (smooth regions).
       (b) Hamilton-Adams: more robust at edges.

       The sign convention ensures that the color difference always
       represents (G - C) regardless of pixel type:
         - At dominating (green) sites: sgn = +1
         - At non-dominating sites: sgn = -1

       Possible improvement: the adaptive ratio could use a larger
       neighborhood for more robust estimation.
       ================================================================ */
#pragma omp parallel for schedule(dynamic, 16)
    for (int y = 0; y < h; y++)
      {
        if (progress && progress->cancel_requested ())
          continue;
        for (int x = 0; x < w; x++)
          {
            int idx = y * w + x;
            luminosity_t c00 = known (x, y);

            /* Safely retrieve directional weights with clamping.  */
            auto wt_v = [&] (int xx, int yy) -> luminosity_t
              {
                return dir_wts_v[std::clamp (yy, 0, h - 1) * w
                                 + std::clamp (xx, 0, w - 1)];
              };
            auto wt_h = [&] (int xx, int yy) -> luminosity_t
              {
                return dir_wts_h[std::clamp (yy, 0, h - 1) * w
                                 + std::clamp (xx, 0, w - 1)];
              };

            /* CFA values at cardinal and distance-2 neighbors.  */
            luminosity_t cu1 = known (x, y - 1);
            luminosity_t cd1 = known (x, y + 1);
            luminosity_t cl1 = known (x - 1, y);
            luminosity_t cr1 = known (x + 1, y);
            luminosity_t cu2 = known (x, y - 2);
            luminosity_t cd2 = known (x, y + 2);
            luminosity_t cl2 = known (x - 2, y);
            luminosity_t cr2 = known (x + 2, y);

            /* Color ratios in each cardinal direction.
               Weighted by directional gradients for robustness.  */
            luminosity_t cru = cu1 * (wt_v (x, y - 2) + wt_v (x, y))
                               / (wt_v (x, y - 2) * (eps + c00)
                                  + wt_v (x, y) * (eps + cu2));
            luminosity_t crd = cd1 * (wt_v (x, y + 2) + wt_v (x, y))
                               / (wt_v (x, y + 2) * (eps + c00)
                                  + wt_v (x, y) * (eps + cd2));
            luminosity_t crl = cl1 * (wt_h (x - 2, y) + wt_h (x, y))
                               / (wt_h (x - 2, y) * (eps + c00)
                                  + wt_h (x, y) * (eps + cl2));
            luminosity_t crr = cr1 * (wt_h (x + 2, y) + wt_h (x, y))
                               / (wt_h (x + 2, y) * (eps + c00)
                                  + wt_h (x, y) * (eps + cr2));

            /* HA green estimates in four cardinal directions.  */
            luminosity_t guha = ha_green_one_direction (cu1, c00, cu2);
            luminosity_t gdha = ha_green_one_direction (cd1, c00, cd2);
            luminosity_t glha = ha_green_one_direction (cl1, c00, cl2);
            luminosity_t grha = ha_green_one_direction (cr1, c00, cr2);

            /* Adaptive ratio green estimates: use ratio if close to 1
               (smooth area), otherwise fall back to HA.  */
            luminosity_t guar = my_abs (1 - cru) < arthresh ? c00 * cru : guha;
            luminosity_t gdar = my_abs (1 - crd) < arthresh ? c00 * crd : gdha;
            luminosity_t glar = my_abs (1 - crl) < arthresh ? c00 * crl : glha;
            luminosity_t grar = my_abs (1 - crr) < arthresh ? c00 * crr : grha;

            /* Adaptive weights for combining up/down and left/right.  */
            luminosity_t wt_h_val
                = wt_h (x - 1, y) / (wt_h (x - 1, y) + wt_h (x + 1, y));
            luminosity_t wt_v_val
                = wt_v (x, y - 1) / (wt_v (x, y + 1) + wt_v (x, y - 1));

            /* HA-only blended estimates (alternative).  */
            luminosity_t g_int_v_ha = wt_v_val * gdha + (1 - wt_v_val) * guha;
            luminosity_t g_int_h_ha = wt_h_val * grha + (1 - wt_h_val) * glha;

            /* Primary and alternative color differences.  */
            luminosity_t vcd_val, hcd_val, vcd_alt, hcd_alt;
            if (GEOMETRY::demosaic_entry_color (x, y) == ah_green)
              {
                vcd_val = c00 - (wt_v_val * gdar + (1 - wt_v_val) * guar);
                hcd_val = c00 - (wt_h_val * grar + (1 - wt_h_val) * glar);
                vcd_alt = c00 - g_int_v_ha;
                hcd_alt = c00 - g_int_h_ha;
              }
            else
              {
                vcd_val = (wt_v_val * gdar + (1 - wt_v_val) * guar) - c00;
                hcd_val = (wt_h_val * grar + (1 - wt_h_val) * glar) - c00;
                vcd_alt = g_int_v_ha - c00;
                hcd_alt = g_int_h_ha - c00;
              }

            /* Near highlights, fall back to HA.  */
            if (c00 > clip_pt8 || g_int_v_ha > clip_pt8
                || g_int_h_ha > clip_pt8)
              {
                guar = guha;
                gdar = gdha;
                glar = glha;
                grar = grha;
                vcd_val = vcd_alt;
                hcd_val = hcd_alt;
              }

            vcd[idx] = vcd_val;
            hcd[idx] = hcd_val;

            /* Differences of interpolations in opposing directions.
               Uses minimum of HA and adaptive-ratio differences.  */
            dg_int_v[idx] = std::min ((guha - gdha) * (guha - gdha),
                                      (guar - gdar) * (guar - gdar));
            dg_int_h[idx] = std::min ((glha - grha) * (glha - grha),
                                      (glar - grar) * (glar - grar));
          }
        if (progress)
          progress->inc_progress ();
      }
    if (progress && progress->cancelled ())
      return false;

    /* ================================================================
       Phase 2b: Bound color differences in regions of high saturation.
       ================================================================ */
#pragma omp parallel for schedule(dynamic, 16)
    for (int y = 0; y < h; y++)
      {
        if (progress && progress->cancel_requested ())
          continue;
        for (int x = 0; x < w; x++)
          {
            int idx = y * w + x;
            bool is_green
                = (GEOMETRY::demosaic_entry_color (x, y) == ah_green);
            luminosity_t c00 = known (x, y);

            luminosity_t hcd_val = hcd[idx];
            if (is_green)
              {
                /* At G sites: g_int_h = c00 - hcd (the estimated other
                 * channel).  */
                luminosity_t g_int_h = c00 - hcd_val;
                luminosity_t h_med
                    = median3 (g_int_h, known (x - 1, y), known (x + 1, y));
                if (hcd_val > 0)
                  {
                    /* Over-correcting toward lower g_int_h.  */
                    luminosity_t bound = c00 - h_med;
                    if (3 * hcd_val > (g_int_h + c00))
                      hcd_val = bound;
                    else
                      {
                        luminosity_t w2
                            = 1 - 3 * hcd_val / (eps + g_int_h + c00);
                        hcd_val = w2 * hcd_val + (1 - w2) * bound;
                      }
                  }
                if (g_int_h > clip_pt)
                  hcd_val = c00 - h_med;
              }
            else
              {
                /* At R/B sites: g_int_h = c00 + hcd (the estimated G).  */
                luminosity_t g_int_h = c00 + hcd_val;
                luminosity_t h_med
                    = median3 (g_int_h, known (x - 1, y), known (x + 1, y));
                if (hcd_val < 0)
                  {
                    /* G_estimate is below CFA value: suspicious.  */
                    luminosity_t bound = h_med - c00;
                    if (3 * (-hcd_val) > (g_int_h + c00))
                      hcd_val = bound;
                    else
                      {
                        luminosity_t w2
                            = 1 + 3 * hcd_val / (eps + g_int_h + c00);
                        hcd_val = w2 * hcd_val + (1 - w2) * bound;
                      }
                  }
                if (g_int_h > clip_pt)
                  hcd_val = h_med - c00;
              }
            hcd[idx] = hcd_val;

            /* Bound vertical color difference (same structure).  */
            luminosity_t vcd_val = vcd[idx];
            if (is_green)
              {
                luminosity_t g_int_v = c00 - vcd_val;
                luminosity_t v_med
                    = median3 (g_int_v, known (x, y - 1), known (x, y + 1));
                if (vcd_val > 0)
                  {
                    luminosity_t bound = c00 - v_med;
                    if (3 * vcd_val > (g_int_v + c00))
                      vcd_val = bound;
                    else
                      {
                        luminosity_t w2
                            = 1 - 3 * vcd_val / (eps + g_int_v + c00);
                        vcd_val = w2 * vcd_val + (1 - w2) * bound;
                      }
                  }
                if (g_int_v > clip_pt)
                  vcd_val = c00 - v_med;
              }
            else
              {
                luminosity_t g_int_v = c00 + vcd_val;
                luminosity_t v_med
                    = median3 (g_int_v, known (x, y - 1), known (x, y + 1));
                if (vcd_val < 0)
                  {
                    luminosity_t bound = v_med - c00;
                    if (3 * (-vcd_val) > (g_int_v + c00))
                      vcd_val = bound;
                    else
                      {
                        luminosity_t w2
                            = 1 + 3 * vcd_val / (eps + g_int_v + c00);
                        vcd_val = w2 * vcd_val + (1 - w2) * bound;
                      }
                  }
                if (g_int_v > clip_pt)
                  vcd_val = v_med - c00;
              }
            vcd[idx] = vcd_val;

            /* Squared color-difference for Nyquist detection.  */
            if (do_nyquist)
              cd_diff_sq[idx] = (vcd_val - hcd_val) * (vcd_val - hcd_val);
          }
        if (progress)
          progress->inc_progress ();
      }
    if (progress && progress->cancelled ())
      return false;

    /* ================================================================
       Phase 3: Compute direction weights from variance analysis.
       ================================================================ */
#pragma omp parallel for schedule(dynamic, 16)
    for (int y = 0; y < h; y++)
      {
        if (progress && progress->cancel_requested ())
          continue;
        for (int x = 0; x < w; x++)
          {
            int idx = y * w + x;
            if (GEOMETRY::demosaic_entry_color (x, y) == ah_green)
              {
                hv_wt[idx] = (luminosity_t)0.5;
                continue;
              }

            /* Clamped accessors for the working arrays.  */
            auto vcd_at = [&] (int xx, int yy) -> luminosity_t
              {
                return vcd[std::clamp (yy, 0, h - 1) * w
                           + std::clamp (xx, 0, w - 1)];
              };
            auto hcd_at = [&] (int xx, int yy) -> luminosity_t
              {
                return hcd[std::clamp (yy, 0, h - 1) * w
                           + std::clamp (xx, 0, w - 1)];
              };
            auto dgv_at = [&] (int xx, int yy) -> luminosity_t
              {
                return dg_int_v[std::clamp (yy, 0, h - 1) * w
                                + std::clamp (xx, 0, w - 1)];
              };
            auto dgh_at = [&] (int xx, int yy) -> luminosity_t
              {
                return dg_int_h[std::clamp (yy, 0, h - 1) * w
                                + std::clamp (xx, 0, w - 1)];
              };
            auto wt_v = [&] (int xx, int yy) -> luminosity_t
              {
                return dir_wts_v[std::clamp (yy, 0, h - 1) * w
                                 + std::clamp (xx, 0, w - 1)];
              };
            auto wt_h = [&] (int xx, int yy) -> luminosity_t
              {
                return dir_wts_h[std::clamp (yy, 0, h - 1) * w
                                 + std::clamp (xx, 0, w - 1)];
              };

            luminosity_t wt_h_val
                = wt_h (x - 1, y) / (wt_h (x - 1, y) + wt_h (x + 1, y));
            luminosity_t wt_v_val
                = wt_v (x, y - 1) / (wt_v (x, y + 1) + wt_v (x, y - 1));

            /* Color-difference variance in four cardinal directions,
               computed over a 4-pixel window in each direction.  */
            luminosity_t vcd0 = vcd_at (x, y);
            luminosity_t uave = vcd0 + vcd_at (x, y - 1) + vcd_at (x, y - 2)
                                + vcd_at (x, y - 3);
            luminosity_t dave = vcd0 + vcd_at (x, y + 1) + vcd_at (x, y + 2)
                                + vcd_at (x, y + 3);
            luminosity_t dgrb_v_var_u
                = (vcd0 - uave) * (vcd0 - uave)
                  + (vcd_at (x, y - 1) - uave) * (vcd_at (x, y - 1) - uave)
                  + (vcd_at (x, y - 2) - uave) * (vcd_at (x, y - 2) - uave)
                  + (vcd_at (x, y - 3) - uave) * (vcd_at (x, y - 3) - uave);
            luminosity_t dgrb_v_var_d
                = (vcd0 - dave) * (vcd0 - dave)
                  + (vcd_at (x, y + 1) - dave) * (vcd_at (x, y + 1) - dave)
                  + (vcd_at (x, y + 2) - dave) * (vcd_at (x, y + 2) - dave)
                  + (vcd_at (x, y + 3) - dave) * (vcd_at (x, y + 3) - dave);

            luminosity_t hcd0 = hcd_at (x, y);
            luminosity_t lave = hcd0 + hcd_at (x - 1, y) + hcd_at (x - 2, y)
                                + hcd_at (x - 3, y);
            luminosity_t rave = hcd0 + hcd_at (x + 1, y) + hcd_at (x + 2, y)
                                + hcd_at (x + 3, y);
            luminosity_t dgrb_h_var_l
                = (hcd0 - lave) * (hcd0 - lave)
                  + (hcd_at (x - 1, y) - lave) * (hcd_at (x - 1, y) - lave)
                  + (hcd_at (x - 2, y) - lave) * (hcd_at (x - 2, y) - lave)
                  + (hcd_at (x - 3, y) - lave) * (hcd_at (x - 3, y) - lave);
            luminosity_t dgrb_h_var_r
                = (hcd0 - rave) * (hcd0 - rave)
                  + (hcd_at (x + 1, y) - rave) * (hcd_at (x + 1, y) - rave)
                  + (hcd_at (x + 2, y) - rave) * (hcd_at (x + 2, y) - rave)
                  + (hcd_at (x + 3, y) - rave) * (hcd_at (x + 3, y) - rave);

            /* Directionally-weighted variance.  */
            luminosity_t vcd_var = epssq + wt_v_val * dgrb_v_var_d
                                   + (1 - wt_v_val) * dgrb_v_var_u;
            luminosity_t hcd_var = epssq + wt_h_val * dgrb_h_var_r
                                   + (1 - wt_h_val) * dgrb_h_var_l;

            /* Interpolation fluctuation in each direction.  */
            luminosity_t dgrb_v_var_u2
                = dgv_at (x, y) + dgv_at (x, y - 1) + dgv_at (x, y - 2);
            luminosity_t dgrb_v_var_d2
                = dgv_at (x, y) + dgv_at (x, y + 1) + dgv_at (x, y + 2);
            luminosity_t dgrb_h_var_l2
                = dgh_at (x, y) + dgh_at (x - 1, y) + dgh_at (x - 2, y);
            luminosity_t dgrb_h_var_r2
                = dgh_at (x, y) + dgh_at (x + 1, y) + dgh_at (x + 2, y);

            luminosity_t vcd_var1 = epssq + dgv_at (x, y)
                                    + wt_v_val * dgrb_v_var_d2
                                    + (1 - wt_v_val) * dgrb_v_var_u2;
            luminosity_t hcd_var1 = epssq + dgh_at (x, y)
                                    + wt_h_val * dgrb_h_var_r2
                                    + (1 - wt_h_val) * dgrb_h_var_l2;

            /* Two direction weights.  */
            luminosity_t var_wt = hcd_var / (vcd_var + hcd_var);
            luminosity_t diff_wt = hcd_var1 / (vcd_var1 + hcd_var1);

            /* If both metrics agree AND fluctuation metric has weaker
               discrimination, use variance metric (more reliable).
               Otherwise use fluctuation metric.  */
            if (((luminosity_t)0.5 - var_wt) * ((luminosity_t)0.5 - diff_wt)
                    > 0
                && my_abs ((luminosity_t)0.5 - diff_wt)
                       < my_abs ((luminosity_t)0.5 - var_wt))
              hv_wt[idx] = var_wt;
            else
              hv_wt[idx] = diff_wt;
          }
        if (progress)
          progress->inc_progress ();
      }
    if (progress && progress->cancelled ())
      return false;

    /* Free phase 2 temporary arrays.  */
    dg_int_v.clear ();
    dg_int_v.shrink_to_fit ();
    dg_int_h.clear ();
    dg_int_h.shrink_to_fit ();

    /* ================================================================
       Phase 4: Nyquist texture detection (optional).

       Nyquist textures are regions where fine detail aliases with the
       CFA pattern, producing false color.  Detected by comparing
       Gaussian-weighted color-difference variance against CFA gradient.

       Possible improvement: use frequency domain analysis for more
       sophisticated aliasing detection.
       ================================================================ */
    if (do_nyquist)
      {
        /* Compute Nyquist test value for non-green pixels.  */
#pragma omp parallel for schedule(dynamic, 16)
        for (int y = 0; y < h; y++)
          {
            if (progress && progress->cancel_requested ())
              continue;
            for (int x = 0; x < w; x++)
              {
                if (GEOMETRY::demosaic_entry_color (x, y) == ah_green)
                  continue;

                auto cdq = [&] (int xx, int yy) -> luminosity_t
                  {
                    return cd_diff_sq[std::clamp (yy, 0, h - 1) * w
                                      + std::clamp (xx, 0, w - 1)];
                  };
                auto dhv = [&] (int xx, int yy) -> luminosity_t
                  {
                    return del_hv_sq_sum[std::clamp (yy, 0, h - 1) * w
                                         + std::clamp (xx, 0, w - 1)];
                  };

                luminosity_t val
                    = gauss_odd[0] * cdq (x, y)
                      + gauss_odd[1]
                            * (cdq (x - 1, y + 1) + cdq (x + 1, y - 1)
                               + cdq (x + 1, y + 1) + cdq (x - 1, y - 1))
                      + gauss_odd[2]
                            * (cdq (x, y - 2) + cdq (x - 2, y) + cdq (x + 2, y)
                               + cdq (x, y + 2))
                      + gauss_odd[3]
                            * (cdq (x - 2, y + 2) + cdq (x + 2, y - 2)
                               + cdq (x - 2, y - 2) + cdq (x + 2, y + 2));

                luminosity_t grad_val
                    = gauss_grad[0] * dhv (x, y)
                      + gauss_grad[1]
                            * (dhv (x, y - 1) + dhv (x - 1, y) + dhv (x + 1, y)
                               + dhv (x, y + 1))
                      + gauss_grad[2]
                            * (dhv (x - 1, y - 1) + dhv (x + 1, y - 1)
                               + dhv (x - 1, y + 1) + dhv (x + 1, y + 1))
                      + gauss_grad[3]
                            * (dhv (x, y - 2) + dhv (x - 2, y) + dhv (x + 2, y)
                               + dhv (x, y + 2))
                      + gauss_grad[4]
                            * (dhv (x - 1, y - 2) + dhv (x + 1, y - 2)
                               + dhv (x - 2, y - 1) + dhv (x + 2, y - 1)
                               + dhv (x - 2, y + 1) + dhv (x + 2, y + 1)
                               + dhv (x - 1, y + 2) + dhv (x + 1, y + 2))
                      + gauss_grad[5]
                            * (dhv (x - 2, y - 2) + dhv (x + 2, y - 2)
                               + dhv (x - 2, y + 2) + dhv (x + 2, y + 2));

                nyquist_flags[y * w + x] = (val - grad_val > 0) ? 1 : 0;
              }
          }

        /* Morphological cleanup: majority vote in same-color 3x3.  */
        std::vector<uint8_t> nyquist2 (w * h, 0);
        for (int y = 2; y < h - 2; y++)
          for (int x = 2; x < w - 2; x++)
            {
              if (GEOMETRY::demosaic_entry_color (x, y) == ah_green)
                continue;
              auto nf = [&] (int xx, int yy) -> int
                {
                  return nyquist_flags[std::clamp (yy, 0, h - 1) * w
                                       + std::clamp (xx, 0, w - 1)];
                };
              int sum = nf (x, y - 2) + nf (x - 1, y - 1) + nf (x + 1, y - 1)
                        + nf (x - 2, y) + nf (x + 2, y) + nf (x - 1, y + 1)
                        + nf (x + 1, y + 1) + nf (x, y + 2);
              nyquist2[y * w + x] = sum > 4 ? 1 : (sum < 4 ? 0 : nf (x, y));
            }
        nyquist_flags = std::move (nyquist2);

        /* In Nyquist regions, recompute hv_wt using area-based
           interpolation over a 7x7 same-color window.  */
        for (int y = 3; y < h - 3; y++)
          for (int x = 3; x < w - 3; x++)
            {
              if (GEOMETRY::demosaic_entry_color (x, y) == ah_green)
                continue;
              if (!nyquist_flags[y * w + x])
                continue;

              luminosity_t sumcfa = 0, sumh = 0, sumv = 0;
              luminosity_t sumsqh = 0, sumsqv = 0;
              luminosity_t area_wt = 0;

              for (int dy = -6; dy <= 6; dy += 2)
                for (int dx = -6; dx <= 6; dx += 2)
                  {
                    int nx = x + dx, ny = y + dy;
                    if (nx < 0 || nx >= w || ny < 0 || ny >= h)
                      continue;
                    if (!nyquist_flags[ny * w + nx])
                      continue;

                    luminosity_t cf = known (nx, ny);
                    sumcfa += cf;
                    luminosity_t nh = known (nx - 1, ny) + known (nx + 1, ny);
                    luminosity_t nv = known (nx, ny - 1) + known (nx, ny + 1);
                    sumh += nh;
                    sumv += nv;
                    sumsqh += (cf - known (nx - 1, ny))
                                  * (cf - known (nx - 1, ny))
                              + (cf - known (nx + 1, ny))
                                    * (cf - known (nx + 1, ny));
                    sumsqv += (cf - known (nx, ny - 1))
                                  * (cf - known (nx, ny - 1))
                              + (cf - known (nx, ny + 1))
                                    * (cf - known (nx, ny + 1));
                    area_wt += 1;
                  }

              if (area_wt > 0)
                {
                  sumh = sumcfa - sumh * (luminosity_t)0.5;
                  sumv = sumcfa - sumv * (luminosity_t)0.5;
                  area_wt *= (luminosity_t)0.5;
                  luminosity_t hcd_var
                      = epssq + my_abs (area_wt * sumsqh - sumh * sumh);
                  luminosity_t vcd_var
                      = epssq + my_abs (area_wt * sumsqv - sumv * sumv);
                  hv_wt[y * w + x] = hcd_var / (vcd_var + hcd_var);
                }
            }
      }

    /* Free Nyquist-specific working arrays.  */
    del_hv_sq_sum.clear ();
    del_hv_sq_sum.shrink_to_fit ();
    cd_diff_sq.clear ();
    cd_diff_sq.shrink_to_fit ();
    nyquist_flags.clear ();
    nyquist_flags.shrink_to_fit ();

    /* ================================================================
       Phase 5: Populate dominating channel at non-dominating sites.

       Blend horizontal and vertical color differences using hvwt_arr,
       optionally refine from neighboring same-type sites, then compute:
         G = CFA + Dgrb.

       After this phase, the dominating channel is fully populated.
       ================================================================ */
#pragma omp parallel for schedule(dynamic, 16)
    for (int y = 0; y < h; y++)
      {
        if (progress && progress->cancel_requested ())
          continue;
        for (int x = 0; x < w; x++)
          {
            if (GEOMETRY::demosaic_entry_color (x, y) == ah_green)
              continue;

            int idx = y * w + x;

            /* Check if nearby same-type sites provide stronger
               directional discrimination.  */
            auto hw = [&] (int xx, int yy) -> luminosity_t
              {
                return hv_wt[std::clamp (yy, 0, h - 1) * w
                             + std::clamp (xx, 0, w - 1)];
              };
            luminosity_t hv_wt_alt = (hw (x - 1, y - 1) + hw (x + 1, y - 1)
                                      + hw (x - 1, y + 1) + hw (x + 1, y + 1))
                                     * (luminosity_t)0.25;

            if (my_abs ((luminosity_t)0.5 - hv_wt[idx])
                < my_abs ((luminosity_t)0.5 - hv_wt_alt))
              hv_wt[idx] = hv_wt_alt;

            /* Blend vertical and horizontal color differences.  */
            luminosity_t dgrb
                = hv_wt[idx] * vcd[idx] + (1 - hv_wt[idx]) * hcd[idx];

            /* Write the dominating channel value.  */
            d (x, y)[(int)ah_green] = known (x, y) + dgrb;
          }
        if (progress)
          progress->inc_progress ();
      }
    if (progress && progress->cancelled ())
      return false;

    /* Free remaining phase working arrays.  */
    dir_wts_v.clear ();
    dir_wts_v.shrink_to_fit ();
    dir_wts_h.clear ();
    dir_wts_h.shrink_to_fit ();
    vcd.clear ();
    vcd.shrink_to_fit ();
    hcd.clear ();
    hcd.shrink_to_fit ();
    hv_wt.clear ();
    hv_wt.shrink_to_fit ();

    /* ================================================================
       Phase 6: Interpolate remaining channels.

       With the dominating channel (G) fully populated, we interpolate
       the remaining channels (R and B) using the existing Hamilton-Adams
       color-difference method.

       Possible improvement: use full diagonal chrominance interpolation
       for R/B instead of HA, which would further improve quality
       near diagonal edges.
       ================================================================ */
    if (progress)
      progress->set_task ("demosaicing (amaze: remaining channels)", h);
#pragma omp parallel for schedule(dynamic, 16)
    for (int y = 0; y < h; y++)
      {
        if (progress && progress->cancel_requested ())
          continue;
        for (int x = 0; x < w; x++)
          {
            int color = GEOMETRY::demosaic_entry_color (x, y);
            luminosity_t g_here = d (x, y)[(int)ah_green];

            if (color != ah_red)
              hamilton_adams_interpolate_color<ah_green, ah_red> (g_here, x,
                                                                  y);
            if (color != ah_blue)
              hamilton_adams_interpolate_color<ah_green, ah_blue> (g_here, x,
                                                                   y);
          }
        if (progress)
          progress->inc_progress ();
      }
    return !progress || !progress->cancelled ();
  }

  /* RCD (Ratio Corrected Demosaicing) algorithm.

     Based on the algorithm by Luis Sanz Rodríguez, release 2.3.
     This implementation follows the darktable reference
     (src/iop/demosaicing/rcd.c) closely, adapted to work with the GEOMETRY
     template and floating-point linear data.

     The algorithm operates in five main steps:
       1. Compute vertical and horizontal directional discrimination by
          analyzing squared high-pass filter responses on the CFA data.
       2. Compute a low-pass filter (LPF) incorporating all local CFA samples
          around each non-dominating site.
       3. Populate the dominating channel at non-dominating sites using
          ratio-corrected cardinal interpolation, weighted by cardinal
          gradients and the VH directional discrimination.
       4. Populate non-dominating channels at non-dominating sites using
          diagonal (P/Q) ratio-corrected interpolation with diagonal
          gradient weighting and PQ directional discrimination.
       5. Populate non-dominating channels at dominating sites using
          cardinal color-difference interpolation with VH directional
          discrimination.

     Key differences from the AMaZE approach:
       - Uses ratio-corrected interpolation (neighbor * ratio) instead of
         Hamilton-Adams Laplacian correction.
       - Simpler, fewer passes, generally faster.
       - Uses a 6th-order high-pass filter for directional analysis (wider
         support than AMaZE's gradient computation).

     Template parameters:
       AH_GREEN  - the dominating channel (plays role of "green" in Bayer)
       AH_RED    - first non-dominating channel
       AH_BLUE   - second non-dominating channel

     Possible improvements:
       - Tile-based processing for better cache locality (as in darktable)
       - SIMD vectorization of inner loops
       - Adaptive eps based on local noise estimation
       - Post-interpolation refinement pass (Nyquist texture handling)
       - Median filtering in highlight regions for clipping robustness  */
  template <int ah_green, int ah_red, int ah_blue>
  bool
  rcd_interpolation (progress_info *progress)
  {
    int w = m_area.width, h = m_area.height;

    /* Tolerance to avoid division by zero.  */
    constexpr luminosity_t eps = (luminosity_t)1e-5;
    constexpr luminosity_t epssq = (luminosity_t)1e-10;

    /* Total progress: step1(h) + step2(h) + step3(h) + step4diag(h)
       + step4.2(h) + step4.3(h) = 6*h.  */
    if (progress)
      progress->set_task ("demosaicing (rcd)", h * 6);

    /* ================================================================
       Step 1: Find vertical and horizontal interpolation directions.

       We compute a 6th-order high-pass filter on the CFA data in both
       vertical and horizontal directions.  The filter kernel is:
         HPF(x) = (x[-3] - x[-1] - x[+1] + x[+3])
                  - 3*(x[-2] + x[+2]) + 6*x[0]
       The squared magnitude of V and H HPF responses are accumulated
       over 3 consecutive same-direction rows/columns, and the ratio
       V_Stat / (V_Stat + H_Stat) gives the directional discrimination:
         VH_Dir near 1.0 = strong vertical variation = prefer horizontal
         VH_Dir near 0.0 = strong horizontal variation = prefer vertical
         VH_Dir near 0.5 = isotropic = blend equally

       This is the core innovation of RCD: using high-order color
       difference HPF statistics instead of simple gradients.

       Possible improvement: use a Gaussian-weighted accumulation instead
       of a box filter over 3 rows for smoother transitions.
       ================================================================ */
    std::vector<luminosity_t> vh_dir (w * h, 0);
    {
      /* Temporary buffers for squared vertical HPF values.
         We use a rolling buffer of 3 rows to avoid storing the full
         image of squared HPF values.  */
      std::vector<luminosity_t> v_sq_row_0 (w, 0);
      std::vector<luminosity_t> v_sq_row_1 (w, 0);
      std::vector<luminosity_t> v_sq_row_2 (w, 0);

      /* Pre-fill the rolling buffer for rows 3 and 4.  */
      for (int init_row = 3; init_row <= 4; init_row++)
        {
          luminosity_t *target
              = (init_row == 3) ? v_sq_row_0.data () : v_sq_row_1.data ();
          for (int x = 4; x < w - 4; x++)
            {
              /* Vertical 6th-order HPF.  */
              luminosity_t v
                  = (known (x, init_row - 3) - known (x, init_row - 1)
                     - known (x, init_row + 1) + known (x, init_row + 3))
                    - 3 * (known (x, init_row - 2) + known (x, init_row + 2))
                    + 6 * known (x, init_row);
              target[x] = v * v;
            }
        }

      for (int y = 4; y < h - 4; y++)
        {
          if (progress && progress->cancel_requested ())
            break;

          /* Compute squared vertical HPF for row y+1 into the oldest
             slot (v_sq_row_2 will be rotated to become the new "next").  */
          for (int x = 4; x < w - 4; x++)
            {
              int yr = y + 1;
              luminosity_t v = (known (x, yr - 3) - known (x, yr - 1)
                                - known (x, yr + 1) + known (x, yr + 3))
                               - 3 * (known (x, yr - 2) + known (x, yr + 2))
                               + 6 * known (x, yr);
              v_sq_row_2[x] = v * v;
            }

          /* Compute horizontal HPF and accumulate both directions.  */
          /* bufferH[x] stores the squared horizontal HPF for (x, y).
             We accumulate col-1, col, col+1 horizontally
             (in the darktable code these are bufferH[col-4], bufferH[col-3],
             bufferH[col-2] which corresponds to a 3-sample window).  */
          std::vector<luminosity_t> h_sq (w, 0);
          for (int x = 3; x < w - 3; x++)
            {
              luminosity_t hv = (known (x - 3, y) - known (x - 1, y)
                                 - known (x + 1, y) + known (x + 3, y))
                                - 3 * (known (x - 2, y) + known (x + 2, y))
                                + 6 * known (x, y);
              h_sq[x] = hv * hv;
            }

          for (int x = 4; x < w - 4; x++)
            {
              /* Accumulate 3-row vertical and 3-column horizontal
                 statistics.  */
              luminosity_t v_stat = std::max (
                  epssq, v_sq_row_0[x] + v_sq_row_1[x] + v_sq_row_2[x]);
              luminosity_t h_stat
                  = std::max (epssq, h_sq[x - 1] + h_sq[x] + h_sq[x + 1]);
              vh_dir[y * w + x] = v_stat / (v_stat + h_stat);
            }

          /* Roll the buffers: row0 <- row1, row1 <- row2,
             row2 <- row0 (to be overwritten next iteration).  */
          std::swap (v_sq_row_0, v_sq_row_1);
          std::swap (v_sq_row_1, v_sq_row_2);

          if (progress)
            progress->inc_progress ();
        }
    }
    if (progress && progress->cancelled ())
      return false;

    /* ================================================================
       Step 2: Compute the low-pass filter (LPF).

       At each non-dominating pixel, we compute a weighted average of
       the 3x3 neighborhood CFA values:
         LPF = center + 0.5*(N+S+W+E) + 0.25*(NW+NE+SW+SE)
       This incorporates samples from all three Bayer color channels
       to produce a luminance-like estimate.

       The LPF is stored only at non-dominating sites.  At dominating
       sites the LPF value is unused (set to 0).

       Possible improvement: use a larger kernel (5x5) for more robust
       estimation in noisy images.
       ================================================================ */
    std::vector<luminosity_t> lpf (w * h, 0);
#pragma omp parallel for schedule(dynamic, 16)
    for (int y = 2; y < h - 2; y++)
      {
        if (progress && progress->cancel_requested ())
          continue;
        for (int x = 2; x < w - 2; x++)
          {
            if (GEOMETRY::demosaic_entry_color (x, y) == ah_green)
              continue;
            lpf[y * w + x]
                = known (x, y)
                  + (luminosity_t)0.5
                        * (known (x, y - 1) + known (x, y + 1)
                           + known (x - 1, y) + known (x + 1, y))
                  + (luminosity_t)0.25
                        * (known (x - 1, y - 1) + known (x + 1, y - 1)
                           + known (x - 1, y + 1) + known (x + 1, y + 1));
          }
        if (progress)
          progress->inc_progress ();
      }
    if (progress && progress->cancelled ())
      return false;

    /* ================================================================
       Step 3: Populate the dominating channel at non-dominating sites.

       For each non-dominating pixel (R or B), estimate the dominating
       channel (G) using ratio-corrected cardinal interpolation.

       Cardinal gradients measure how much the CFA and interpolated
       values change in each direction (N/S/E/W), extending to distance 4.
       These provide edge-awareness.

       Cardinal estimates use ratio-corrected interpolation:
         G_est_N = cfa_N * (2 * LPF_center) / (LPF_center + LPF_N)
       This adapts the neighbor value by the ratio of local luminance
       estimates.  When LPF values are similar (smooth area), the ratio
       is near 1 and the estimate is basically the neighbor value.

       Vertical and horizontal estimates are weighted by their opposing
       gradients (cross-gradient weighting):
         V_Est = (S_Grad * N_Est + N_Grad * S_Est) / (N_Grad + S_Grad)

       The VH directional discrimination from Step 1 is refined by
       comparing the local value with the neighborhood average (diagonal
       neighbors), choosing whichever has stronger discrimination.

       Final G = lerp(VH_Disc, H_Est, V_Est).

       Possible improvement: use a 6-pixel gradient window like AMaZE
       for even more edge sensitivity.
       ================================================================ */
#pragma omp parallel for schedule(dynamic, 16)
    for (int y = 4; y < h - 4; y++)
      {
        if (progress && progress->cancel_requested ())
          continue;
        for (int x = 4; x < w - 4; x++)
          {
            if (GEOMETRY::demosaic_entry_color (x, y) == ah_green)
              continue;

            luminosity_t cfa_i = known (x, y);
            int idx = y * w + x;

            /* Cardinal gradients.
               Each gradient sums:
               - |neighbor1 - neighbor2| (same-channel variation)
               - |center - same_channel_at_distance_2|
               - |neighbor1 - same_channel_at_distance_3|
               - |same_channel_at_distance_2 - same_channel_at_distance_4|
               This provides a 4-pixel-deep edge measure.  */
            luminosity_t n_grad = eps
                                  + my_abs (known (x, y - 1) - known (x, y + 1))
                                  + my_abs (cfa_i - known (x, y - 2))
                                  + my_abs (known (x, y - 1) - known (x, y - 3))
                                  + my_abs (known (x, y - 2) - known (x, y - 4));
            luminosity_t s_grad = eps
                                  + my_abs (known (x, y + 1) - known (x, y - 1))
                                  + my_abs (cfa_i - known (x, y + 2))
                                  + my_abs (known (x, y + 1) - known (x, y + 3))
                                  + my_abs (known (x, y + 2) - known (x, y + 4));
            luminosity_t w_grad = eps
                                  + my_abs (known (x - 1, y) - known (x + 1, y))
                                  + my_abs (cfa_i - known (x - 2, y))
                                  + my_abs (known (x - 1, y) - known (x - 3, y))
                                  + my_abs (known (x - 2, y) - known (x - 4, y));
            luminosity_t e_grad = eps
                                  + my_abs (known (x + 1, y) - known (x - 1, y))
                                  + my_abs (cfa_i - known (x + 2, y))
                                  + my_abs (known (x + 1, y) - known (x + 3, y))
                                  + my_abs (known (x + 2, y) - known (x + 4, y));

            /* Ratio-corrected cardinal estimates.
               Each uses: neighbor * (2 * lpf_here) / (lpf_here +
               lpf_neighbor). This ratio correction is the core innovation of
               RCD—it adapts the interpolation to local luminance ratios
               instead of using additive Laplacian corrections like
               Hamilton-Adams.

               IMPORTANT: The LPF is only stored at non-dominating (R/B)
               sites.  The cardinal neighbors at distance 1 are dominating
               (G) sites where LPF = 0.  We must use the LPF at same-color
               pixels at distance 2, which correspond to the darktable
               half-resolution LPF buffer offsets (lpindx ± w1, lpindx ± 1
               in half-res = distance 2 in full-res).  */
            luminosity_t lpfi = lpf[idx];
            luminosity_t lpf2 = lpfi + lpfi;
            luminosity_t n_est = known (x, y - 1) * lpf2
                                 / (eps + lpfi + lpf[(y - 2) * w + x]);
            luminosity_t s_est = known (x, y + 1) * lpf2
                                 / (eps + lpfi + lpf[(y + 2) * w + x]);
            luminosity_t w_est = known (x - 1, y) * lpf2
                                 / (eps + lpfi + lpf[y * w + (x - 2)]);
            luminosity_t e_est = known (x + 1, y) * lpf2
                                 / (eps + lpfi + lpf[y * w + (x + 2)]);

            /* Vertical and horizontal estimates.
               Cross-gradient weighting: the estimate from the direction with
               less gradient (smoother) gets more weight.  */
            luminosity_t v_est
                = (s_grad * n_est + n_grad * s_est) / (n_grad + s_grad);
            luminosity_t h_est
                = (w_grad * e_est + e_grad * w_est) / (e_grad + w_grad);

            /* Refined VH directional discrimination.
               Compare the local vh_dir with the average of the four
               diagonal neighbors.  Use whichever has stronger
               discrimination (farther from 0.5).  */
            luminosity_t vh_central = vh_dir[idx];
            luminosity_t vh_neighbourhood
                = (luminosity_t)0.25
                  * (vh_dir[(y - 1) * w + (x - 1)]
                     + vh_dir[(y - 1) * w + (x + 1)]
                     + vh_dir[(y + 1) * w + (x - 1)]
                     + vh_dir[(y + 1) * w + (x + 1)]);
            luminosity_t vh_disc
                = (my_abs ((luminosity_t)0.5 - vh_central)
                   < my_abs ((luminosity_t)0.5 - vh_neighbourhood))
                      ? vh_neighbourhood
                      : vh_central;

            /* Clamp vh_disc to [0, 1] for safe interpolation.  */
            vh_disc = std::clamp (vh_disc, (luminosity_t)0, (luminosity_t)1);

            /* G@R or G@B: blend V and H estimates.
               vh_disc near 1 => prefer h_est (horizontal is smoother)
               vh_disc near 0 => prefer v_est (vertical is smoother)  */
            d (x, y)[(int)ah_green] = vh_disc * h_est + (1 - vh_disc) * v_est;
          }
        if (progress)
          progress->inc_progress ();
      }
    if (progress && progress->cancelled ())
      return false;

    /* ================================================================
       Step 4: Populate non-dominating channels (R and B).

       Step 4 has two sub-steps:
       4.2: At non-dominating sites (R@B or B@R) — diagonal interpolation
       4.3: At dominating sites (R@G or B@G) — cardinal interpolation

       Sub-step 4.0-4.1: Compute P/Q diagonal directional discrimination.

       Similar to Step 1, but using diagonal high-pass filters instead
       of cardinal ones.  The P diagonal goes NW-SE, the Q diagonal
       goes NE-SW.

       Possible improvement: combine Steps 4.0-4.1 with 4.2 to reduce
       memory usage—the PQ_Dir values are only needed at non-dominating
       sites.
       ================================================================ */
    /* Step 4.0-4.1: Diagonal directional discrimination.  */
    std::vector<luminosity_t> pq_dir (w * h, (luminosity_t)0.5);
    {
      /* Compute squared diagonal HPF at ALL sites (both dominating and
         non-dominating).  In the darktable half-resolution approach,
         every-other-column is processed on each row, covering both
         green and non-green sites across alternating rows.  With our
         full-resolution storage we simply compute at every pixel.
         The P-diagonal HPF uses offsets along the NW-SE direction.
         The Q-diagonal HPF uses the NE-SW direction.  */
      std::vector<luminosity_t> p_cdiff_hpf (w * h, 0);
      std::vector<luminosity_t> q_cdiff_hpf (w * h, 0);

#pragma omp parallel for schedule(dynamic, 16)
      for (int y = 3; y < h - 3; y++)
        {
          if (progress && progress->cancel_requested ())
            continue;
          for (int x = 3; x < w - 3; x++)
            {
              /* P diagonal (NW-SE): offsets (-3,-3)..(+3,+3).  */
              luminosity_t p
                  = (known (x - 3, y - 3) - known (x - 1, y - 1)
                     - known (x + 1, y + 1) + known (x + 3, y + 3))
                    - 3 * (known (x - 2, y - 2) + known (x + 2, y + 2))
                    + 6 * known (x, y);
              p_cdiff_hpf[y * w + x] = p * p;

              /* Q diagonal (NE-SW): offsets (+3,-3)..(-3,+3).  */
              luminosity_t q
                  = (known (x + 3, y - 3) - known (x + 1, y - 1)
                     - known (x - 1, y + 1) + known (x - 3, y + 3))
                    - 3 * (known (x + 2, y - 2) + known (x - 2, y + 2))
                    + 6 * known (x, y);
              q_cdiff_hpf[y * w + x] = q * q;
            }
        }

        /* Accumulate 3-sample diagonal statistics.  */
#pragma omp parallel for schedule(dynamic, 16)
      for (int y = 4; y < h - 4; y++)
        {
          if (progress && progress->cancel_requested ())
            continue;
          for (int x = 4; x < w - 4; x++)
            {
              if (GEOMETRY::demosaic_entry_color (x, y) == ah_green)
                continue;
              /* The 3 same-type diagonal neighbors for P and Q
                 accumulation are at (-1,-1), (0,0), (+1,+1) in the
                 half-resolution grid of the same color, which maps to
                 positions shifted by (-1,-1), (0,0), (+1,+1) in the
                 dominating-channel grid.  */
              luminosity_t p_stat
                  = std::max (epssq, p_cdiff_hpf[(y - 1) * w + (x - 1)]
                                         + p_cdiff_hpf[y * w + x]
                                         + p_cdiff_hpf[(y + 1) * w + (x + 1)]);
              luminosity_t q_stat
                  = std::max (epssq, q_cdiff_hpf[(y - 1) * w + (x + 1)]
                                         + q_cdiff_hpf[y * w + x]
                                         + q_cdiff_hpf[(y + 1) * w + (x - 1)]);
              pq_dir[y * w + x] = p_stat / (p_stat + q_stat);
            }
        }
    }
    if (progress && progress->cancelled ())
      return false;

    /* Step 4.2: R at B sites and B at R sites (diagonal interpolation).

       At each non-dominating pixel we already have the dominating channel
       (from Step 3).  The "other" non-dominating channel (the one that is
       NOT this pixel's native channel) is estimated using diagonal
       neighbors where that channel IS known.

       Diagonal gradients measure edge strength along NW-SE and NE-SW
       diagonals using:
       - the "other" channel difference across the diagonal
       - second-order difference (distance-3 neighbor)
       - dominating channel (green) curvature in the same direction

       Ratio-corrected estimates use color difference (C - G) at
       diagonal neighbors, then add back the local G.

       PQ_Dir discrimination (same refinement as VH_Dir) selects the
       dominant diagonal direction.  */
#pragma omp parallel for schedule(dynamic, 16)
    for (int y = 4; y < h - 4; y++)
      {
        if (progress && progress->cancel_requested ())
          continue;
        for (int x = 4; x < w - 4; x++)
          {
            int color = GEOMETRY::demosaic_entry_color (x, y);
            if (color == ah_green)
              continue;

            /* Determine which non-dominating channel to interpolate.
               If we are at a red pixel, we need blue.
               If we are at a blue pixel, we need red.  */
            int c = (color == ah_red) ? ah_blue : ah_red;
            int idx = y * w + x;

            /* Refined PQ directional discrimination.  */
            luminosity_t pq_central = pq_dir[idx];
            luminosity_t pq_neighbourhood
                = (luminosity_t)0.25
                  * (pq_dir[(y - 1) * w + (x - 1)]
                     + pq_dir[(y - 1) * w + (x + 1)]
                     + pq_dir[(y + 1) * w + (x - 1)]
                     + pq_dir[(y + 1) * w + (x + 1)]);
            luminosity_t pq_disc
                = (my_abs ((luminosity_t)0.5 - pq_central)
                   < my_abs ((luminosity_t)0.5 - pq_neighbourhood))
                      ? pq_neighbourhood
                      : pq_central;
            pq_disc = std::clamp (pq_disc, (luminosity_t)0, (luminosity_t)1);

            /* Diagonal gradients.
               Each incorporates:
               - opposite-diagonal same-channel difference
               - second-order same-channel difference (distance 3)
               - dominating channel curvature in the same direction  */
            luminosity_t g_here = d (x, y)[(int)ah_green];
            luminosity_t nw_grad
                = eps + my_abs (dch (x - 1, y - 1, c) - dch (x + 1, y + 1, c))
                  + my_abs (dch (x - 1, y - 1, c) - dch (x - 3, y - 3, c))
                  + my_abs (g_here - d (x - 2, y - 2)[(int)ah_green]);
            luminosity_t ne_grad
                = eps + my_abs (dch (x + 1, y - 1, c) - dch (x - 1, y + 1, c))
                  + my_abs (dch (x + 1, y - 1, c) - dch (x + 3, y - 3, c))
                  + my_abs (g_here - d (x + 2, y - 2)[(int)ah_green]);
            luminosity_t sw_grad
                = eps + my_abs (dch (x + 1, y - 1, c) - dch (x - 1, y + 1, c))
                  + my_abs (dch (x - 1, y + 1, c) - dch (x - 3, y + 3, c))
                  + my_abs (g_here - d (x - 2, y + 2)[(int)ah_green]);
            luminosity_t se_grad
                = eps + my_abs (dch (x - 1, y - 1, c) - dch (x + 1, y + 1, c))
                  + my_abs (dch (x + 1, y + 1, c) - dch (x + 3, y + 3, c))
                  + my_abs (g_here - d (x + 2, y + 2)[(int)ah_green]);

            /* Diagonal color differences (C - G at diagonal neighbor).  */
            luminosity_t nw_est
                = dch (x - 1, y - 1, c) - d (x - 1, y - 1)[(int)ah_green];
            luminosity_t ne_est
                = dch (x + 1, y - 1, c) - d (x + 1, y - 1)[(int)ah_green];
            luminosity_t sw_est
                = dch (x - 1, y + 1, c) - d (x - 1, y + 1)[(int)ah_green];
            luminosity_t se_est
                = dch (x + 1, y + 1, c) - d (x + 1, y + 1)[(int)ah_green];

            /* P (NW-SE) and Q (NE-SW) diagonal estimates.
               Cross-gradient weighting as in Step 3.  */
            luminosity_t p_est
                = (nw_grad * se_est + se_grad * nw_est) / (nw_grad + se_grad);
            luminosity_t q_est
                = (ne_grad * sw_est + sw_grad * ne_est) / (ne_grad + sw_grad);

            /* R@B or B@R: G + interpolated(C-G).  */
            d (x, y)[(int)c]
                = g_here + pq_disc * q_est + (1 - pq_disc) * p_est;
          }
        if (progress)
          progress->inc_progress ();
      }
    if (progress && progress->cancelled ())
      return false;

    /* Step 4.3: R and B at green CFA positions (cardinal interpolation).

       At dominating (green) pixels, both R and B need to be interpolated.
       We use cardinal color-difference interpolation (C - G at cardinal
       neighbors) with the same VH directional discrimination from Step 1.

       Cardinal gradients incorporate:
       - dominating channel second-order difference (G curvature)
       - same-channel north-south or east-west difference
       - same-channel second-order difference (C at distance 3)

       Possible improvement: use the lpf-based ratio correction here
       (as in Step 3) instead of simple color-difference interpolation,
       for better behavior in high-saturation regions.
       ================================================================ */
#pragma omp parallel for schedule(dynamic, 16)
    for (int y = 4; y < h - 4; y++)
      {
        if (progress && progress->cancel_requested ())
          continue;
        for (int x = 4; x < w - 4; x++)
          {
            if (GEOMETRY::demosaic_entry_color (x, y) != ah_green)
              continue;

            int idx = y * w + x;

            /* Refined VH directional discrimination (same as Step 3).  */
            luminosity_t vh_central = vh_dir[idx];
            luminosity_t vh_neighbourhood
                = (luminosity_t)0.25
                  * (vh_dir[(y - 1) * w + (x - 1)]
                     + vh_dir[(y - 1) * w + (x + 1)]
                     + vh_dir[(y + 1) * w + (x - 1)]
                     + vh_dir[(y + 1) * w + (x + 1)]);
            luminosity_t vh_disc
                = (my_abs ((luminosity_t)0.5 - vh_central)
                   < my_abs ((luminosity_t)0.5 - vh_neighbourhood))
                      ? vh_neighbourhood
                      : vh_central;
            vh_disc = std::clamp (vh_disc, (luminosity_t)0, (luminosity_t)1);

            luminosity_t g_here = d (x, y)[(int)ah_green];

            /* Green second-order differences for gradient computation.  */
            luminosity_t n_1
                = eps + my_abs (g_here - d (x, y - 2)[(int)ah_green]);
            luminosity_t s_1
                = eps + my_abs (g_here - d (x, y + 2)[(int)ah_green]);
            luminosity_t w_1
                = eps + my_abs (g_here - d (x - 2, y)[(int)ah_green]);
            luminosity_t e_1
                = eps + my_abs (g_here - d (x + 2, y)[(int)ah_green]);

            /* Green values at cardinal neighbors.  */
            luminosity_t g_n = d (x, y - 1)[(int)ah_green];
            luminosity_t g_s = d (x, y + 1)[(int)ah_green];
            luminosity_t g_w = d (x - 1, y)[(int)ah_green];
            luminosity_t g_e = d (x + 1, y)[(int)ah_green];

            /* Interpolate both non-dominating channels.  */
            for (int c = ah_red; c <= ah_blue; c += (ah_blue - ah_red))
              {
                /* Same-channel absolute differences for gradients.  */
                luminosity_t sn_abs
                    = fabs (dch (x, y - 1, c) - dch (x, y + 1, c));
                luminosity_t ew_abs
                    = fabs (dch (x - 1, y, c) - dch (x + 1, y, c));

                /* Cardinal gradients.  */
                luminosity_t n_grad
                    = n_1 + sn_abs
                      + fabs (dch (x, y - 1, c) - dch (x, y - 3, c));
                luminosity_t s_grad
                    = s_1 + sn_abs
                      + fabs (dch (x, y + 1, c) - dch (x, y + 3, c));
                luminosity_t w_grad
                    = w_1 + ew_abs
                      + fabs (dch (x - 1, y, c) - dch (x - 3, y, c));
                luminosity_t e_grad
                    = e_1 + ew_abs
                      + fabs (dch (x + 1, y, c) - dch (x + 3, y, c));

                /* Cardinal color differences (C - G at neighbors).  */
                luminosity_t n_est = dch (x, y - 1, c) - g_n;
                luminosity_t s_est = dch (x, y + 1, c) - g_s;
                luminosity_t w_est = dch (x - 1, y, c) - g_w;
                luminosity_t e_est = dch (x + 1, y, c) - g_e;

                /* V and H estimates with cross-gradient weighting.  */
                luminosity_t v_est
                    = (n_grad * s_est + s_grad * n_est) / (n_grad + s_grad);
                luminosity_t h_est
                    = (e_grad * w_est + w_grad * e_est) / (e_grad + w_grad);

                /* R@G or B@G: G + interpolated(C-G).  */
                d (x, y)[(int)c]
                    = g_here + vh_disc * h_est + (1 - vh_disc) * v_est;
              }
          }
        if (progress)
          progress->inc_progress ();
      }
    return !progress || !progress->cancelled ();
  }

  /* RCD (Ratio Corrected Demosaicing) variant for 4x4 sparse CFA.

     This variant is optimized for patterns where the dominating channel
     is a 2x4 grid and non-dominating channels are 4x4 grids, with empty
     pixels in between, such as in Dufaycolor reseau:
        R . B .
        . . . .
        . G . G
        . . . .

     The algorithm follows the same five steps as standard RCD but uses
     larger offsets and search kernels to handle the sparsity.
  */
  template <int ah_green, int ah_red, int ah_blue>
  bool
  rcd_interpolation_4x4 (progress_info *progress)
  {
    int w = m_area.width, h = m_area.height;
    constexpr luminosity_t eps = (luminosity_t)1e-5;
    constexpr luminosity_t epssq = (luminosity_t)1e-10;

    if (progress)
      progress->set_task ("demosaicing (rcd 4x4)", h * 6);

    /* ================================================================
       Step 1: Find vertical and horizontal interpolation directions.
       We use a search-based gradient to handle sparse patterns.
       ================================================================ */
    std::vector<luminosity_t> vh_dir (w * h, (luminosity_t)0.5);
    {
      std::vector<luminosity_t> v_grad (w * h, 0);
      std::vector<luminosity_t> h_grad (w * h, 0);

#pragma omp parallel for schedule(dynamic, 16)
      for (int y = 8; y < h - 8; y++)
        {
          if (progress && progress->cancel_requested ())
            continue;
          for (int x = 8; x < w - 8; x++)
            {
              auto get_grad = [&](int dx, int dy) {
                luminosity_t v1 = 0, v2 = 0;
                int d1 = 0, d2 = 0;
                /* Search for nearest dots of dominating channel.  */
                for (int i = 1; i <= 8; i++) {
                   if (!d1 && GEOMETRY::demosaic_entry_color(x + i*dx, y + i*dy) == ah_green) {
                      v1 = known(x + i*dx, y + i*dy); d1 = i;
                   }
                   if (!d2 && GEOMETRY::demosaic_entry_color(x - i*dx, y - i*dy) == ah_green) {
                      v2 = known(x - i*dx, y - i*dy); d2 = i;
                   }
                }
                /* Normalize gradient by the total distance to make cardinal directions comparable.  */
                return (d1 && d2) ? fabs(v1 - v2) / (luminosity_t)(d1 + d2) : (luminosity_t)0;
              };
              v_grad[y * w + x] = get_grad(0, 1);
              h_grad[y * w + x] = get_grad(1, 0);
            }
        }

#pragma omp parallel for schedule(dynamic, 16)
      for (int y = 10; y < h - 10; y++)
        {
          if (progress && progress->cancel_requested ())
            continue;
          for (int x = 10; x < w - 10; x++)
            {
              /* Use a 8x8 window (multiple of 4) to avoid phase beating.  */
              luminosity_t v_stat = eps;
              luminosity_t h_stat = eps;
              for (int dy = -4; dy <= 3; dy++)
                for (int dx = -4; dx <= 3; dx++)
                  {
                    v_stat += v_grad[(y + dy) * w + (x + dx)];
                    h_stat += h_grad[(y + dy) * w + (x + dx)];
                  }
              vh_dir[y * w + x] = v_stat / (v_stat + h_stat);
            }
          if (progress)
            progress->inc_progress ();
        }
    }

    /* ================================================================
       Step 2: Compute the low-pass filter (LPF).
       We use a weighted average over a 12x12 window (multiple of 4).
       ================================================================ */
    std::vector<luminosity_t> lpf (w * h, 0);
#pragma omp parallel for schedule(dynamic, 16)
    for (int y = 8; y < h - 8; y++)
      {
        if (progress && progress->cancel_requested ())
          continue;
        for (int x = 8; x < w - 8; x++)
          {
            luminosity_t r_sum = 0, g_sum = 0, b_sum = 0;
            int r_cnt = 0, g_cnt = 0, b_cnt = 0;
            /* 12x12 window is phase-invariant for 4x4 pattern.  */
            for (int dy = -6; dy <= 5; dy++)
              for (int dx = -6; dx <= 5; dx++)
                {
                  int color = GEOMETRY::demosaic_entry_color (x + dx, y + dy);
                  if (color == base_geometry::red) { r_sum += known(x + dx, y + dy); r_cnt++; }
                  else if (color == base_geometry::green) { g_sum += known(x + dx, y + dy); g_cnt++; }
                  else if (color == base_geometry::blue) { b_sum += known(x + dx, y + dy); b_cnt++; }
                }
            luminosity_t r_avg = r_cnt > 0 ? r_sum / r_cnt : 0;
            luminosity_t g_avg = g_cnt > 0 ? g_sum / g_cnt : 0;
            luminosity_t b_avg = b_cnt > 0 ? b_sum / b_cnt : 0;
            
            int active = (r_cnt > 0) + (g_cnt > 0) + (b_cnt > 0);
            lpf[y * w + x] = active > 0 ? (r_avg + g_avg + b_avg) / active : 0;
          }
        if (progress)
          progress->inc_progress ();
      }

    /* ================================================================
       Step 3: Populate the dominating channel (ah_green) at all sites.
       ================================================================ */
#pragma omp parallel for schedule(dynamic, 16)
    for (int y = 8; y < h - 8; y++)
      {
        if (progress && progress->cancel_requested ())
          continue;
        for (int x = 8; x < w - 8; x++)
          {
            if (GEOMETRY::demosaic_entry_color (x, y) == ah_green)
              {
                d (x, y)[(int)ah_green] = known (x, y);
                continue;
              }

            int idx = y * w + x;
            luminosity_t lpfi = lpf[idx];
            luminosity_t lpf2 = lpfi + lpfi;

            /* Inverse Distance Weighting (IDW) with gradients.  */
            auto get_est = [&](int dx, int dy) -> std::pair<luminosity_t, luminosity_t> {
              for (int i = 1; i <= 8; i++) {
                if (GEOMETRY::demosaic_entry_color (x + i*dx, y + i*dy) == ah_green) {
                  luminosity_t g = known (x + i*dx, y + i*dy);
                  /* Gradient normalized by distance.  */
                  luminosity_t grad = fabs (g - known (x + 2*i*dx, y + 2*i*dy)) / (luminosity_t)i;
                  luminosity_t est = g * lpf2 / (eps + lpfi + lpf[(y + i*dy) * w + (x + i*dx)]);
                  /* Inverse weight proportional to 1/(grad*dist).  */
                  return {est, (grad + eps) * (luminosity_t)i};
                }
              }
              return {0, 1e10};
            };

            auto n = get_est (0, -1);
            auto s = get_est (0, 1);
            auto w_ = get_est (-1, 0);
            auto e = get_est (1, 0);

            luminosity_t v_est = (n.second * s.first + s.second * n.first) / (n.second + s.second);
            luminosity_t h_est = (w_.second * e.first + e.second * w_.first) / (w_.second + e.second);

            /* Directional blend.  */
            luminosity_t vh_disc = vh_dir[idx];
            d (x, y)[(int)ah_green] = vh_disc * h_est + (1 - vh_disc) * v_est;
          }
        if (progress)
          progress->inc_progress ();
      }

    /* ================================================================
       Step 4: Populate non-dominating channels (R and B).
       ================================================================ */
#pragma omp parallel for schedule(dynamic, 16)
    for (int y = 8; y < h - 8; y++)
      {
        if (progress && progress->cancel_requested ())
          continue;
        for (int x = 8; x < w - 8; x++)
          {
            luminosity_t g_here = d (x, y)[(int)ah_green];

            for (int c : {ah_red, ah_blue})
              {
                if (GEOMETRY::demosaic_entry_color (x, y) == c)
                  {
                    d (x, y)[c] = known (x, y);
                    continue;
                  }

                auto get_cdiff_est = [&](int dx, int dy) -> std::pair<luminosity_t, luminosity_t> {
                  for (int i = 1; i <= 8; i++) {
                    if (GEOMETRY::demosaic_entry_color (x + i*dx, y + i*dy) == c) {
                      luminosity_t val = known (x + i*dx, y + i*dy);
                      luminosity_t cd = val - d (x + i*dx, y + i*dy)[(int)ah_green];
                      luminosity_t grad = fabs (val - known (x + 2*i*dx, y + 2*i*dy)) / (luminosity_t)i;
                      return {cd, (grad + eps) * (luminosity_t)i};
                    }
                  }
                  return {0, 1e10};
                };

                auto n = get_cdiff_est (0, -1);
                auto s = get_cdiff_est (0, 1);
                auto w_ = get_cdiff_est (-1, 0);
                auto e = get_cdiff_est (1, 0);

                luminosity_t v_cd = (n.second * s.first + s.second * n.first) / (n.second + s.second);
                luminosity_t h_cd = (w_.second * e.first + e.second * w_.first) / (w_.second + e.second);

                luminosity_t vh_disc = vh_dir[y * w + x];
                d (x, y)[c] = g_here + vh_disc * h_cd + (1 - vh_disc) * v_cd;
              }
          }
        if (progress)
          progress->inc_progress ();
      }

    return !progress || !progress->cancelled ();
  }

  /* LMMSE (Linear Minimum Mean Square Error) demosaicing.

     Based on the algorithm by Lei Zhang and Xiaolin Wu:
     "Color demosaicking via directional Linear Minimum Mean Square-error
     Estimation", IEEE Trans. on Image Processing, vol. 14, pp. 2167-2178,
     Dec. 2005.

     This implementation follows the darktable/rawtherapee/librtprocess
     reference, adapted to work with the GEOMETRY template and
     floating-point linear data.

     The algorithm:
       1. Convert CFA data to gamma-corrected (perceptual) space.
          This is critical because LMMSE variance estimation assumes
          approximately uniform noise, which holds in perceptual space
          but not in linear space (where shadows have far less variance).
       2. Compute horizontal and vertical G-R/B color differences at
          every pixel.  At non-dominating sites, these are direct
          estimates; at dominating sites, they are interpolated from
          neighbors (with opposite sign).
       3. Apply a 1D Gaussian low-pass filter along the respective
          direction to produce smoothed color difference estimates.
       4. At non-dominating sites, apply the LMMSE formula to optimally
          combine the raw and filtered estimates:
            x_lmmse = (x_raw * Var_filtered + x_filtered * Var_noise)
                      / (Var_filtered + Var_noise)
          This gives the minimum mean square error estimate under Gaussian
          assumptions.  The high-variance (textured) signal retains the
          raw estimate; the low-variance (smooth) signal converges to
          the filtered estimate.
       5. Reconstruct the dominating channel at non-dominating sites
          using the LMMSE-estimated color difference.
       6. Interpolate non-dominating channels using bilinear color
          differences.
       7. Optional median filtering of color differences and EECI
          (Edge Enhanced Color Interpolation) refinement.
       8. Convert back to linear space.

     Template parameters:
       AH_GREEN  - the dominating channel
       AH_RED    - first non-dominating channel
       AH_BLUE   - second non-dominating channel

     Possible improvements:
       - Tile-based processing for better cache locality
       - SIMD vectorization of inner loops
       - Noise-adaptive gamma curve
       - Adaptive number of median/refine iterations based on noise level
       - Separate treatment of highlight clipping regions  */
  template <int ah_green, int ah_red, int ah_blue>
  bool
  lmmse_interpolation (progress_info *progress)
  {
    int w = m_area.width, h = m_area.height;

    /* Gamma correction functions matching the darktable reference.
       These map linear [0,1] data to a perceptually uniform space
       using a power curve with gamma ~ 2.4.  The linear segment
       near zero avoids numerical issues at very small values.

       Possible improvement: use a noise-adaptive gamma that varies
       the toe slope based on estimated read noise.  */
    auto gamma_fwd = [] (luminosity_t x) -> luminosity_t
      {
        x = std::max (x, (luminosity_t)0);
        return (x <= (luminosity_t)0.001867)
                   ? x * (luminosity_t)17.0
                   : (luminosity_t)1.044445
                             * std::exp (std::log (x) / (luminosity_t)2.4)
                         - (luminosity_t)0.044445;
      };
    auto gamma_inv = [] (luminosity_t x) -> luminosity_t
      {
        x = std::max (x, (luminosity_t)0);
        return (x <= (luminosity_t)0.031746)
                   ? x / (luminosity_t)17.0
                   : std::exp (std::log ((x + (luminosity_t)0.044445)
                                         / (luminosity_t)1.044445)
                               * (luminosity_t)2.4);
      };

    /* Find robust max for normalization.  */
    luminosity_t range = find_robust_max (progress);
    if (progress && progress->cancelled ())
      return false;
    if (range <= 0)
      range = 1;
    luminosity_t inv_range = 1 / range;

    /* 9-tap Gaussian kernel weights for low-pass filtering.
       sigma^2 = 8, so weights are exp(-k^2 / (2*8)) = exp(-k^2/8).
       The kernel is applied in 1D along the interpolation direction.  */
    luminosity_t gh_0 = 1;
    luminosity_t gh_1 = std::exp ((luminosity_t)(-1.0 / 8.0));
    luminosity_t gh_2 = std::exp ((luminosity_t)(-4.0 / 8.0));
    luminosity_t gh_3 = std::exp ((luminosity_t)(-9.0 / 8.0));
    luminosity_t gh_4 = std::exp ((luminosity_t)(-16.0 / 8.0));
    luminosity_t gh_s = gh_0 + 2 * (gh_1 + gh_2 + gh_3 + gh_4);
    gh_0 /= gh_s;
    gh_1 /= gh_s;
    gh_2 /= gh_s;
    gh_3 /= gh_s;
    gh_4 /= gh_s;

    /* Total progress:
       gamma(h) + GR_diff(h) + LP(h) + LMMSE(h) + copy(h) +
       bilinear_G(h) + bilinear_RB(h) + median(3*h) + refine(3*h)
       + writeback(h) = ~10*h to ~16*h depending on passes.
       We use 12*h as a reasonable estimate.  */
    if (progress)
      progress->set_task ("demosaicing (lmmse)", h * 12);

    /* Allocate working buffers.
       cfa     - gamma-corrected CFA mosaic values
       h_diff  - horizontal G-R(B) color difference
       v_diff  - vertical G-R(B) color difference
       h_lp    - LP-filtered h_diff
       v_lp    - LP-filtered v_diff
       rgb[3]  - reconstructed channels in gamma space  */
    std::vector<luminosity_t> cfa (w * h, 0);
    std::vector<luminosity_t> h_diff (w * h, 0);
    std::vector<luminosity_t> v_diff (w * h, 0);

    /* ================================================================
       Step 1: Gamma correction.

       Convert the CFA data from linear to perceptual space.  The LMMSE
       variance estimation works best when noise is approximately uniform
       across the tonal range.  In linear space, shadow noise dominates
       and the LMMSE would under-weight shadows.
       ================================================================ */
    for (int y = 0; y < h; y++)
      {
        for (int x = 0; x < w; x++)
          cfa[y * w + x] = gamma_fwd (known (x, y) * inv_range);
        if (progress)
          progress->inc_progress ();
      }
    if (progress && progress->cancelled ())
      return false;

    /* ================================================================
       Step 2: Compute G-R(B) color differences.

       At non-dominating (R or B) sites:
         hdiff = horizontal interpolation of G-R(B)
               = -0.25*(cfa[x-2] + cfa[x+2]) + 0.5*(cfa[x-1] + cfa[x] +
       cfa[x+1]) This is a 5-tap filter that estimates the dominating channel
         from horizontal neighbors, then subtracts the non-dominating CFA.

         A clipping guard handles values near highlights where the
         interpolation can overshoot: if cfa > 1.75 * local_average,
         replace with median of three neighbors.

       At dominating (G) sites:
         hdiff = interpolation of R(B)-G from horizontal neighbors
               = 0.25*(cfa[x-2] + cfa[x+2]) - 0.5*(cfa[x-1] + cfa[x] +
       cfa[x+1]) Note the opposite sign convention.  This is clamped to [-1, 0]
         then added back to cfa to produce: cfa + clamp(R-G, -1, 0).

       Possible improvement: use a 7-tap or 9-tap interpolation filter
       for better frequency response.
       ================================================================ */
    for (int y = 2; y < h - 2; y++)
      {
        if (progress && progress->cancel_requested ())
          break;

        /* Non-dominating sites: G-R(B) color difference.  */
        for (int x = 2; x < w - 2; x++)
          {
            if (GEOMETRY::demosaic_entry_color (x, y) == ah_green)
              continue;
            int idx = y * w + x;
            luminosity_t c = cfa[idx];

            /* Local average incorporating diagonal and center values.  */
            luminosity_t v0 = (luminosity_t)0.0625
                                  * (cfa[(y - 1) * w + (x - 1)]
                                     + cfa[(y - 1) * w + (x + 1)]
                                     + cfa[(y + 1) * w + (x - 1)]
                                     + cfa[(y + 1) * w + (x + 1)])
                              + (luminosity_t)0.25 * c;

            /* Horizontal G-R(B) estimate.  */
            luminosity_t hd
                = (luminosity_t)(-0.25) * (cfa[idx - 2] + cfa[idx + 2])
                  + (luminosity_t)0.5 * (cfa[idx - 1] + c + cfa[idx + 1]);
            luminosity_t y_0 = v0 + (luminosity_t)0.5 * hd;
            /* Highlight guard: if CFA value is much brighter than
               expected, the linear interpolation overshoots.
               Fall back to median of three horizontal neighbors.  */
            hd = (c > (luminosity_t)1.75 * y_0)
                     ? median3 (hd, cfa[idx - 1], cfa[idx + 1])
                     : std::max (hd, (luminosity_t)0);
            h_diff[idx] = hd - c;

            /* Vertical G-R(B) estimate.  */
            luminosity_t vd
                = (luminosity_t)(-0.25)
                      * (cfa[(y - 2) * w + x] + cfa[(y + 2) * w + x])
                  + (luminosity_t)0.5
                        * (cfa[(y - 1) * w + x] + c + cfa[(y + 1) * w + x]);
            luminosity_t y_1 = v0 + (luminosity_t)0.5 * vd;
            vd = (c > (luminosity_t)1.75 * y_1)
                     ? median3 (vd, cfa[(y - 1) * w + x], cfa[(y + 1) * w + x])
                     : std::max (vd, (luminosity_t)0);
            v_diff[idx] = vd - c;
          }

        /* Dominating sites: -(R(B)-G) color difference.  */
        for (int x = 2; x < w - 2; x++)
          {
            if (GEOMETRY::demosaic_entry_color (x, y) != ah_green)
              continue;
            int idx = y * w + x;
            luminosity_t c = cfa[idx];

            luminosity_t hd
                = (luminosity_t)0.25 * (cfa[idx - 2] + cfa[idx + 2])
                  - (luminosity_t)0.5 * (cfa[idx - 1] + c + cfa[idx + 1]);
            luminosity_t vd
                = (luminosity_t)0.25
                      * (cfa[(y - 2) * w + x] + cfa[(y + 2) * w + x])
                  - (luminosity_t)0.5
                        * (cfa[(y - 1) * w + x] + c + cfa[(y + 1) * w + x]);
            h_diff[idx] = std::min (hd, (luminosity_t)0) + c;
            v_diff[idx] = std::min (vd, (luminosity_t)0) + c;
          }
        if (progress)
          progress->inc_progress ();
      }
    if (progress && progress->cancelled ())
      return false;

    /* ================================================================
       Step 3: Apply 1D Gaussian low-pass filter.

       The horizontal LP is applied along rows to hdiff.
       The vertical LP is applied along columns to vdiff.
       This produces smoothed color-difference estimates that retain
       low-frequency structure but remove high-frequency noise.

       Possible improvement: use a 2D separable filter for better
       isotropy, or an edge-preserving filter (e.g., bilateral).
       ================================================================ */
    std::vector<luminosity_t> h_lp (w * h, 0);
    std::vector<luminosity_t> v_lp (w * h, 0);

#pragma omp parallel for schedule(dynamic, 16)
    for (int y = 4; y < h - 4; y++)
      {
        if (progress && progress->cancel_requested ())
          continue;
        for (int x = 4; x < w - 4; x++)
          {
            int idx = y * w + x;
            /* Horizontal LP: 9-tap Gaussian along the row.  */
            h_lp[idx] = gh_0 * h_diff[idx]
                        + gh_1 * (h_diff[idx - 1] + h_diff[idx + 1])
                        + gh_2 * (h_diff[idx - 2] + h_diff[idx + 2])
                        + gh_3 * (h_diff[idx - 3] + h_diff[idx + 3])
                        + gh_4 * (h_diff[idx - 4] + h_diff[idx + 4]);
            /* Vertical LP: 9-tap Gaussian along the column.  */
            v_lp[idx] = gh_0 * v_diff[idx]
                        + gh_1 * (v_diff[idx - w] + v_diff[idx + w])
                        + gh_2 * (v_diff[idx - 2 * w] + v_diff[idx + 2 * w])
                        + gh_3 * (v_diff[idx - 3 * w] + v_diff[idx + 3 * w])
                        + gh_4 * (v_diff[idx - 4 * w] + v_diff[idx + 4 * w]);
          }
        if (progress)
          progress->inc_progress ();
      }
    if (progress && progress->cancelled ())
      return false;

    /* ================================================================
       Step 4: LMMSE estimation at non-dominating sites.

       For each non-dominating pixel, compute the optimal linear
       combination of the raw color difference and its LP-filtered
       version.

       The LMMSE formula is:
         x_est = (x_raw * Var_s + x_filtered * Var_n) / (Var_s + Var_n)
       where:
         Var_s = variance of LP-filtered signal in a 9-sample window
                 (measures "true" signal variance)
         Var_n = variance of (LP - raw) residual
                 (measures noise variance)

       When signal variance >> noise variance, x_est ≈ x_raw (keep detail).
       When noise variance >> signal variance, x_est ≈ x_filtered (smooth).

       The horizontal and vertical LMMSE estimates are combined using the
       same formula a second time:
         x_final = (x_h * Var_v + x_v * Var_h) / (Var_h + Var_v)
       This chooses the direction with lower variance (smoother interpolation).

       Possible improvement: use a larger window (e.g., 13 samples) for
       more robust variance estimation in very noisy images.
       ================================================================ */
    std::vector<luminosity_t> interp (w * h, 0);

#pragma omp parallel for schedule(dynamic, 16)
    for (int y = 4; y < h - 4; y++)
      {
        if (progress && progress->cancel_requested ())
          continue;
        for (int x = 4; x < w - 4; x++)
          {
            if (GEOMETRY::demosaic_entry_color (x, y) == ah_green)
              continue;
            int idx = y * w + x;

            /* Horizontal LMMSE.  */
            /* Collect 9 LP-filtered samples along the row.  */
            luminosity_t p_1 = h_lp[idx - 4];
            luminosity_t p_2 = h_lp[idx - 3];
            luminosity_t p_3 = h_lp[idx - 2];
            luminosity_t p_4 = h_lp[idx - 1];
            luminosity_t p_5 = h_lp[idx];
            luminosity_t p_6 = h_lp[idx + 1];
            luminosity_t p_7 = h_lp[idx + 2];
            luminosity_t p_8 = h_lp[idx + 3];
            luminosity_t p_9 = h_lp[idx + 4];

            /* Signal variance: Var(LP samples).  */
            luminosity_t mu
                = (p_1 + p_2 + p_3 + p_4 + p_5 + p_6 + p_7 + p_8 + p_9)
                  / (luminosity_t)9;
            luminosity_t vx
                = (luminosity_t)1e-7 + (p_1 - mu) * (p_1 - mu)
                  + (p_2 - mu) * (p_2 - mu) + (p_3 - mu) * (p_3 - mu)
                  + (p_4 - mu) * (p_4 - mu) + (p_5 - mu) * (p_5 - mu)
                  + (p_6 - mu) * (p_6 - mu) + (p_7 - mu) * (p_7 - mu)
                  + (p_8 - mu) * (p_8 - mu) + (p_9 - mu) * (p_9 - mu);

            /* Noise variance: Var(LP - raw).  */
            p_1 -= h_diff[idx - 4];
            p_2 -= h_diff[idx - 3];
            p_3 -= h_diff[idx - 2];
            p_4 -= h_diff[idx - 1];
            p_5 -= h_diff[idx];
            p_6 -= h_diff[idx + 1];
            p_7 -= h_diff[idx + 2];
            p_8 -= h_diff[idx + 3];
            p_9 -= h_diff[idx + 4];
            luminosity_t vn = (luminosity_t)1e-7 + p_1 * p_1 + p_2 * p_2
                              + p_3 * p_3 + p_4 * p_4 + p_5 * p_5 + p_6 * p_6
                              + p_7 * p_7 + p_8 * p_8 + p_9 * p_9;

            /* LMMSE horizontal estimate.  */
            luminosity_t x_h = (h_diff[idx] * vx + h_lp[idx] * vn) / (vx + vn);
            luminosity_t v_h = vx * vn / (vx + vn);

            /* Vertical LMMSE.  */
            p_1 = v_lp[idx - 4 * w];
            p_2 = v_lp[idx - 3 * w];
            p_3 = v_lp[idx - 2 * w];
            p_4 = v_lp[idx - w];
            p_5 = v_lp[idx];
            p_6 = v_lp[idx + w];
            p_7 = v_lp[idx + 2 * w];
            p_8 = v_lp[idx + 3 * w];
            p_9 = v_lp[idx + 4 * w];

            mu = (p_1 + p_2 + p_3 + p_4 + p_5 + p_6 + p_7 + p_8 + p_9)
                 / (luminosity_t)9;
            vx = (luminosity_t)1e-7 + (p_1 - mu) * (p_1 - mu)
                 + (p_2 - mu) * (p_2 - mu) + (p_3 - mu) * (p_3 - mu)
                 + (p_4 - mu) * (p_4 - mu) + (p_5 - mu) * (p_5 - mu)
                 + (p_6 - mu) * (p_6 - mu) + (p_7 - mu) * (p_7 - mu)
                 + (p_8 - mu) * (p_8 - mu) + (p_9 - mu) * (p_9 - mu);

            p_1 -= v_diff[idx - 4 * w];
            p_2 -= v_diff[idx - 3 * w];
            p_3 -= v_diff[idx - 2 * w];
            p_4 -= v_diff[idx - w];
            p_5 -= v_diff[idx];
            p_6 -= v_diff[idx + w];
            p_7 -= v_diff[idx + 2 * w];
            p_8 -= v_diff[idx + 3 * w];
            p_9 -= v_diff[idx + 4 * w];
            vn = (luminosity_t)1e-7 + p_1 * p_1 + p_2 * p_2 + p_3 * p_3
                 + p_4 * p_4 + p_5 * p_5 + p_6 * p_6 + p_7 * p_7 + p_8 * p_8
                 + p_9 * p_9;

            luminosity_t x_v = (v_diff[idx] * vx + v_lp[idx] * vn) / (vx + vn);
            luminosity_t v_v = vx * vn / (vx + vn);

            /* Combine H and V using the same LMMSE weighting:
               the direction with more "signal" (higher LP variance)
               gets more weight.  */
            interp[idx] = (x_h * v_v + x_v * v_h) / (v_h + v_v);
          }
        if (progress)
          progress->inc_progress ();
      }

    /* Free LP buffers no longer needed.  */
    h_lp.clear ();
    h_lp.shrink_to_fit ();
    v_lp.clear ();
    v_lp.shrink_to_fit ();
    h_diff.clear ();
    h_diff.shrink_to_fit ();
    v_diff.clear ();
    v_diff.shrink_to_fit ();

    if (progress && progress->cancelled ())
      return false;

    /* ================================================================
       Step 5: Reconstruct channel buffers.

       Copy CFA values to the appropriate channel buffer.  At non-
       dominating sites, reconstruct the dominating channel as:
         G = CFA_nondom + interp[LMMSE G-R(B)]

       We use three separate channel buffers (rgb[3]) in gamma space
       for the subsequent bilinear R/B interpolation.
       ================================================================ */
    std::vector<luminosity_t> rgb[3];
    rgb[ah_green].resize (w * h, 0);
    rgb[ah_red].resize (w * h, 0);
    rgb[ah_blue].resize (w * h, 0);

    for (int y = 0; y < h; y++)
      {
        for (int x = 0; x < w; x++)
          {
            int idx = y * w + x;
            int c = GEOMETRY::demosaic_entry_color (x, y);

            /* Store the known CFA value in its channel.  */
            rgb[c][idx] = cfa[idx];

            /* At non-dominating sites, also reconstruct the dominating
               channel from the color difference.  */
            if (c != ah_green)
              rgb[ah_green][idx] = cfa[idx] + interp[idx];
          }
        if (progress)
          progress->inc_progress ();
      }

    /* Free interp and cfa buffers.  */
    interp.clear ();
    interp.shrink_to_fit ();
    cfa.clear ();
    cfa.shrink_to_fit ();

    if (progress && progress->cancelled ())
      return false;

    /* ================================================================
       Step 6: Bilinear interpolation of non-dominating channels.

       Step 6a: R/B at dominating (G) positions.
       At each G pixel, one non-dominating color has cardinal neighbors
       and the other has the same cardinal direction neighbors (depending
       on row parity in Bayer).  Use color-difference bilinear:
         C@G = G + 0.5 * ((C-G)_neighbor1 + (C-G)_neighbor2)
       One channel uses horizontal neighbors, the other vertical.

       Step 6b: R at B positions and B at R positions.
       Use the average of all four cardinal color differences:
         C@opposite = G + 0.25 * sum_4_cardinal((C-G)_neighbor)

       Possible improvement: use the directional weights from the LMMSE
       step (VH discrimination) instead of simple bilinear for better
       edge preservation.
       ================================================================ */

    /* Step 6a: Non-dominating channels at dominating sites.  */
    for (int y = 1; y < h - 1; y++)
      {
        if (progress && progress->cancel_requested ())
          break;
        for (int x = 1; x < w - 1; x++)
          {
            if (GEOMETRY::demosaic_entry_color (x, y) != ah_green)
              continue;
            int idx = y * w + x;
            luminosity_t g = rgb[ah_green][idx];

            /* Determine which non-dominating channel has horizontal
               vs vertical neighbors by checking one adjacent pixel's
               color.  */
            int c_horiz = GEOMETRY::demosaic_entry_color (x + 1, y);
            int c_vert = GEOMETRY::demosaic_entry_color (x, y + 1);

            /* Skip if neighbors are also dominating (shouldn't happen
               in standard patterns, but be safe).  */
            if (c_horiz == ah_green && c_vert == ah_green)
              continue;

            /* Horizontal neighbor's channel: bilinear from E and W.  */
            if (c_horiz != ah_green)
              rgb[c_horiz][idx]
                  = g
                    + (luminosity_t)0.5
                          * (rgb[c_horiz][idx - 1] - rgb[ah_green][idx - 1]
                             + rgb[c_horiz][idx + 1] - rgb[ah_green][idx + 1]);

            /* Vertical neighbor's channel: bilinear from N and S.  */
            if (c_vert != ah_green)
              rgb[c_vert][idx]
                  = g
                    + (luminosity_t)0.5
                          * (rgb[c_vert][idx - w] - rgb[ah_green][idx - w]
                             + rgb[c_vert][idx + w] - rgb[ah_green][idx + w]);
          }
        if (progress)
          progress->inc_progress ();
      }
    if (progress && progress->cancelled ())
      return false;

    /* Step 6b: Cross non-dominating channel at non-dominating sites.
       R at B positions and B at R positions.  */
    for (int y = 1; y < h - 1; y++)
      {
        if (progress && progress->cancel_requested ())
          break;
        for (int x = 1; x < w - 1; x++)
          {
            int c = GEOMETRY::demosaic_entry_color (x, y);
            if (c == ah_green)
              continue;
            int idx = y * w + x;

            /* The "other" non-dominating channel.  */
            int c_other = (c == ah_red) ? ah_blue : ah_red;
            luminosity_t g = rgb[ah_green][idx];

            /* Average of four cardinal color differences.  */
            rgb[c_other][idx]
                = g
                  + (luminosity_t)0.25
                        * (rgb[c_other][(y - 1) * w + x]
                           - rgb[ah_green][(y - 1) * w + x]
                           + rgb[c_other][idx - 1] - rgb[ah_green][idx - 1]
                           + rgb[c_other][idx + 1] - rgb[ah_green][idx + 1]
                           + rgb[c_other][(y + 1) * w + x]
                           - rgb[ah_green][(y + 1) * w + x]);
          }
        if (progress)
          progress->inc_progress ();
      }
    if (progress && progress->cancelled ())
      return false;

    /* ================================================================
       Step 7: Median filtering on color differences (3 passes).

       Apply a 3x3 median filter to the R-G and B-G color differences.
       This suppresses outlier pixels (zipper artifacts, hot pixels)
       without blurring edges.  Each pass also reconstructs the
       dominating channel at non-dominating sites from the corrected
       color differences.

       Possible improvement: make the number of passes configurable.
        ================================================================ */
    for (int pass = 0; pass < 3; pass++)
      {
        /* Compute median(C-G) in temporary buffers.
           We reuse the original channel buffers as correction targets.
           corr_r stores median(R-G), corr_b stores median(B-G).  */
        std::vector<luminosity_t> corr_r (w * h, 0);
        std::vector<luminosity_t> corr_b (w * h, 0);

        for (int y = 1; y < h - 1; y++)
          {
            if (progress && progress->cancel_requested ())
              break;
            for (int x = 1; x < w - 1; x++)
              {
                int idx = y * w + x;

                /* Median of 3x3 (R-G) differences.  */
                luminosity_t p_r[9];
                luminosity_t p_b[9];
                int n = 0;
                for (int dy = -1; dy <= 1; dy++)
                  for (int dx = -1; dx <= 1; dx++)
                    {
                      int i = (y + dy) * w + (x + dx);
                      p_r[n] = rgb[ah_red][i] - rgb[ah_green][i];
                      p_b[n] = rgb[ah_blue][i] - rgb[ah_green][i];
                      n++;
                    }
                /* Sort to find median of 9 values.
                   Use a sorting network for efficiency.  */
                auto sort_2 = [] (luminosity_t &a, luminosity_t &b)
                  {
                    if (a > b)
                      std::swap (a, b);
                  };
                /* Bose-Nelson sorting network for 9 elements,
                   extracting only the median (element [4]).  */
                sort_2 (p_r[1], p_r[2]);
                sort_2 (p_r[4], p_r[5]);
                sort_2 (p_r[7], p_r[8]);
                sort_2 (p_r[0], p_r[1]);
                sort_2 (p_r[3], p_r[4]);
                sort_2 (p_r[6], p_r[7]);
                sort_2 (p_r[1], p_r[2]);
                sort_2 (p_r[4], p_r[5]);
                sort_2 (p_r[7], p_r[8]);
                sort_2 (p_r[0], p_r[3]);
                sort_2 (p_r[5], p_r[8]);
                sort_2 (p_r[4], p_r[7]);
                sort_2 (p_r[3], p_r[6]);
                sort_2 (p_r[1], p_r[4]);
                sort_2 (p_r[2], p_r[5]);
                sort_2 (p_r[4], p_r[7]);
                sort_2 (p_r[4], p_r[2]);
                sort_2 (p_r[6], p_r[4]);
                sort_2 (p_r[4], p_r[2]);
                corr_r[idx] = p_r[4];

                sort_2 (p_b[1], p_b[2]);
                sort_2 (p_b[4], p_b[5]);
                sort_2 (p_b[7], p_b[8]);
                sort_2 (p_b[0], p_b[1]);
                sort_2 (p_b[3], p_b[4]);
                sort_2 (p_b[6], p_b[7]);
                sort_2 (p_b[1], p_b[2]);
                sort_2 (p_b[4], p_b[5]);
                sort_2 (p_b[7], p_b[8]);
                sort_2 (p_b[0], p_b[3]);
                sort_2 (p_b[5], p_b[8]);
                sort_2 (p_b[4], p_b[7]);
                sort_2 (p_b[3], p_b[6]);
                sort_2 (p_b[1], p_b[4]);
                sort_2 (p_b[2], p_b[5]);
                sort_2 (p_b[4], p_b[7]);
                sort_2 (p_b[4], p_b[2]);
                sort_2 (p_b[6], p_b[4]);
                sort_2 (p_b[4], p_b[2]);
                corr_b[idx] = p_b[4];
              }
          }

        /* Apply corrections: reconstruct channels from median
           color differences at all positions.  */
        for (int y = 1; y < h - 1; y++)
          {
            for (int x = 1; x < w - 1; x++)
              {
                int idx = y * w + x;
                int c = GEOMETRY::demosaic_entry_color (x, y);
                if (c == ah_green)
                  {
                    /* At G sites: R = G + median(R-G),
                                   B = G + median(B-G).  */
                    rgb[ah_red][idx] = rgb[ah_green][idx] + corr_r[idx];
                    rgb[ah_blue][idx] = rgb[ah_green][idx] + corr_b[idx];
                  }
                else
                  {
                    /* At R/B sites: the "other" non-dominating channel
                       is updated from its median correction, and
                       G is reconstructed as average of the two
                       corrected estimates.  */
                    int c_other = (c == ah_red) ? ah_blue : ah_red;
                    rgb[c_other][idx]
                        = rgb[ah_green][idx]
                          + ((c_other == ah_red) ? corr_r[idx] : corr_b[idx]);
                    rgb[ah_green][idx] = (luminosity_t)0.5
                                         * (rgb[ah_red][idx] - corr_r[idx]
                                            + rgb[ah_blue][idx] - corr_b[idx]);
                  }
              }
            if (progress)
              progress->inc_progress ();
          }
        if (progress && progress->cancelled ())
          return false;
      }

    /* Restore CFA values in the known-channel slots, which may have
       been overwritten by the median correction.
       The gamma-corrected CFA is recomputed from the original data.  */
    for (int y = 4; y < h - 4; y++)
      {
        for (int x = 4; x < w - 4; x++)
          {
            int idx = y * w + x;
            int c = GEOMETRY::demosaic_entry_color (x, y);
            rgb[c][idx] = gamma_fwd (known (x, y) * inv_range);
          }
      }

    /* ================================================================
       Step 8: EECI refinement (0 passes).

       Edge Enhanced Color Interpolation reinforces each channel by
       using gradient-weighted directional color-difference averaging.

       NOTE: Disabled (0 passes) to strictly follow the RawTherapee /
       Darktable default behavior. While EECI can sharpen axial edges,
       it often slightly distorts 45-degree diagonal contours.
       ================================================================ */
    for (int step = 0; step < 0; step++)
      {
        /* Refine G at R/B sites.  */
        for (int y = 2; y < h - 2; y++)
          {
            if (progress && progress->cancel_requested ())
              break;
            for (int x = 2; x < w - 2; x++)
              {
                int c = GEOMETRY::demosaic_entry_color (x, y);
                if (c == ah_green)
                  continue;
                int idx = y * w + x;
                luminosity_t dL = 1
                                  / (1 + my_abs (rgb[c][idx - 2] - rgb[c][idx])
                                     + my_abs (rgb[ah_green][idx + 1]
                                               - rgb[ah_green][idx - 1]));
                luminosity_t dR = 1
                                  / (1 + my_abs (rgb[c][idx + 2] - rgb[c][idx])
                                     + my_abs (rgb[ah_green][idx + 1]
                                               - rgb[ah_green][idx - 1]));
                luminosity_t dU
                    = 1
                      / (1 + my_abs (rgb[c][idx - 2 * w] - rgb[c][idx])
                         + my_abs (rgb[ah_green][idx + w]
                                   - rgb[ah_green][idx - w]));
                luminosity_t dD
                    = 1
                      / (1 + my_abs (rgb[c][idx + 2 * w] - rgb[c][idx])
                         + my_abs (rgb[ah_green][idx + w]
                                   - rgb[ah_green][idx - w]));
                rgb[ah_green][idx]
                    = rgb[c][idx]
                      + ((rgb[ah_green][idx - 1] - rgb[c][idx - 1]) * dL
                         + (rgb[ah_green][idx + 1] - rgb[c][idx + 1]) * dR
                         + (rgb[ah_green][idx - w] - rgb[c][idx - w]) * dU
                         + (rgb[ah_green][idx + w] - rgb[c][idx + w]) * dD)
                            / (dL + dR + dU + dD);
              }
          }

        /* Refine R/B at G sites.  */
        for (int y = 2; y < h - 2; y++)
          {
            if (progress && progress->cancel_requested ())
              break;
            for (int x = 2; x < w - 2; x++)
              {
                if (GEOMETRY::demosaic_entry_color (x, y) != ah_green)
                  continue;
                int idx = y * w + x;
                for (int cc = 0; cc < 3; cc++)
                  {
                    if (cc == ah_green)
                      continue;
                    luminosity_t dL
                        = 1
                          / (1
                             + my_abs (rgb[ah_green][idx - 2]
                                       - rgb[ah_green][idx])
                             + my_abs (rgb[cc][idx + 1] - rgb[cc][idx - 1]));
                    luminosity_t dR
                        = 1
                          / (1
                             + my_abs (rgb[ah_green][idx + 2]
                                       - rgb[ah_green][idx])
                             + my_abs (rgb[cc][idx + 1] - rgb[cc][idx - 1]));
                    luminosity_t dU
                        = 1
                          / (1
                             + my_abs (rgb[ah_green][idx - 2 * w]
                                       - rgb[ah_green][idx])
                             + my_abs (rgb[cc][idx + w] - rgb[cc][idx - w]));
                    luminosity_t dD
                        = 1
                          / (1
                             + my_abs (rgb[ah_green][idx + 2 * w]
                                       - rgb[ah_green][idx])
                             + my_abs (rgb[cc][idx + w] - rgb[cc][idx - w]));
                    rgb[cc][idx]
                        = rgb[ah_green][idx]
                          - ((rgb[ah_green][idx - 1] - rgb[cc][idx - 1]) * dL
                             + (rgb[ah_green][idx + 1] - rgb[cc][idx + 1]) * dR
                             + (rgb[ah_green][idx - w] - rgb[cc][idx - w]) * dU
                             + (rgb[ah_green][idx + w] - rgb[cc][idx + w])
                                   * dD)
                                / (dL + dR + dU + dD);
                  }
              }
          }

        /* Refine cross non-dominating at R/B sites.  */
        for (int y = 2; y < h - 2; y++)
          {
            if (progress && progress->cancel_requested ())
              break;
            for (int x = 2; x < w - 2; x++)
              {
                int c = GEOMETRY::demosaic_entry_color (x, y);
                if (c == ah_green)
                  continue;
                int c_other = (c == ah_red) ? ah_blue : ah_red;
                int idx = y * w + x;
                luminosity_t d_l = 1
                                   / (1 + my_abs (rgb[c][idx - 2] - rgb[c][idx])
                                      + my_abs (rgb[ah_green][idx + 1]
                                                - rgb[ah_green][idx - 1]));
                luminosity_t d_r = 1
                                   / (1 + my_abs (rgb[c][idx + 2] - rgb[c][idx])
                                      + my_abs (rgb[ah_green][idx + 1]
                                                - rgb[ah_green][idx - 1]));
                luminosity_t d_u
                    = 1
                      / (1 + my_abs (rgb[c][idx - 2 * w] - rgb[c][idx])
                         + my_abs (rgb[ah_green][idx + w]
                                   - rgb[ah_green][idx - w]));
                luminosity_t d_d
                    = 1
                      / (1 + my_abs (rgb[c][idx + 2 * w] - rgb[c][idx])
                         + my_abs (rgb[ah_green][idx + w]
                                   - rgb[ah_green][idx - w]));
                rgb[c_other][idx]
                    = rgb[ah_green][idx]
                      - ((rgb[ah_green][idx - 1] - rgb[c_other][idx - 1]) * d_l
                         + (rgb[ah_green][idx + 1] - rgb[c_other][idx + 1])
                               * d_r
                         + (rgb[ah_green][idx - w] - rgb[c_other][idx - w])
                               * d_u
                         + (rgb[ah_green][idx + w] - rgb[c_other][idx + w])
                               * d_d)
                            / (d_l + d_r + d_u + d_d);
              }
          }
        if (progress)
          progress->inc_progress ();
        if (progress && progress->cancelled ())
          return false;
      }

    /* ================================================================
       Step 9: Convert back to linear space and write results.

       Apply inverse gamma, rescale to the original data range, and
       store the reconstructed RGB values into the m_demosaiced array.
       ================================================================ */
    for (int y = 0; y < h; y++)
      {
        for (int x = 0; x < w; x++)
          {
            int idx = y * w + x;
            d (x, y)[(int)ah_red] = std::max (
                (luminosity_t)0, range * gamma_inv (rgb[ah_red][idx]));
            d (x, y)[(int)ah_green] = std::max (
                (luminosity_t)0, range * gamma_inv (rgb[ah_green][idx]));
            d (x, y)[(int)ah_blue] = std::max (
                (luminosity_t)0, range * gamma_inv (rgb[ah_blue][idx]));
          }
        if (progress)
          progress->inc_progress ();
      }
    return !progress || !progress->cancelled ();
  }

  /* Internal helper to interpolate missing color channel CH using
     evaluated dominating channel G_DIR at position [X, Y], following
     the dcraw interpolation logic with clipping.
     LIMIT_MAX specifies the maximum allowed luminosity value.  */
  template <int ah_green>
  always_inline_attr luminosity_t
  interp_dcraw (int x, int y, int ch, const std::vector<luminosity_t> &G_dir,
                luminosity_t limit_max)
  {
    int w = m_area.width, h = m_area.height;
    if (GEOMETRY::demosaic_entry_color (x, y) == ch)
      return dch (x, y, ch);

    bool l_c = GEOMETRY::demosaic_entry_color (x - 1, y) == ch;
    bool r_c = GEOMETRY::demosaic_entry_color (x + 1, y) == ch;
    bool u_c = GEOMETRY::demosaic_entry_color (x, y - 1) == ch;
    bool d_c = GEOMETRY::demosaic_entry_color (x, y + 1) == ch;

    luminosity_t g_here = G_dir[y * w + x];
    luminosity_t val = g_here;

    if (l_c && r_c)
      {
        int xl = std::clamp (x - 1, 0, w - 1);
        int xr = std::clamp (x + 1, 0, w - 1);
        val = g_here
              + (dch (x - 1, y, ch) - G_dir[y * w + xl] + dch (x + 1, y, ch)
                 - G_dir[y * w + xr])
                    * (luminosity_t)0.5;
      }
    else if (u_c && d_c)
      {
        int yu = std::clamp (y - 1, 0, h - 1);
        int yd = std::clamp (y + 1, 0, h - 1);
        val = g_here
              + (dch (x, y - 1, ch) - G_dir[yu * w + x] + dch (x, y + 1, ch)
                 - G_dir[yd * w + x])
                    * (luminosity_t)0.5;
      }
    else if (l_c)
      val = g_here + dch (x - 1, y, ch)
            - G_dir[y * w + std::clamp (x - 1, 0, w - 1)];
    else if (r_c)
      val = g_here + dch (x + 1, y, ch)
            - G_dir[y * w + std::clamp (x + 1, 0, w - 1)];
    else if (u_c)
      val = g_here + dch (x, y - 1, ch)
            - G_dir[std::clamp (y - 1, 0, h - 1) * w + x];
    else if (d_c)
      val = g_here + dch (x, y + 1, ch)
            - G_dir[std::clamp (y + 1, 0, h - 1) * w + x];
    else
      {
        bool tl = GEOMETRY::demosaic_entry_color (x - 1, y - 1) == ch;
        bool tr = GEOMETRY::demosaic_entry_color (x + 1, y - 1) == ch;
        bool bl = GEOMETRY::demosaic_entry_color (x - 1, y + 1) == ch;
        bool br = GEOMETRY::demosaic_entry_color (x + 1, y + 1) == ch;
        int cnt = 0;
        luminosity_t sum = 0;
        if (tl)
          {
            int cx = std::clamp (x - 1, 0, w - 1);
            int cy = std::clamp (y - 1, 0, h - 1);
            sum += dch (x - 1, y - 1, ch) - G_dir[cy * w + cx];
            cnt++;
          }
        if (tr)
          {
            int cx = std::clamp (x + 1, 0, w - 1);
            int cy = std::clamp (y - 1, 0, h - 1);
            sum += dch (x + 1, y - 1, ch) - G_dir[cy * w + cx];
            cnt++;
          }
        if (bl)
          {
            int cx = std::clamp (x - 1, 0, w - 1);
            int cy = std::clamp (y + 1, 0, h - 1);
            sum += dch (x - 1, y + 1, ch) - G_dir[cy * w + cx];
            cnt++;
          }
        if (br)
          {
            int cx = std::clamp (x + 1, 0, w - 1);
            int cy = std::clamp (y + 1, 0, h - 1);
            sum += dch (x + 1, y + 1, ch) - G_dir[cy * w + cx];
            cnt++;
          }
        if (cnt > 0)
          val = g_here + sum / (luminosity_t)cnt;
      }

    return std::clamp (val, (luminosity_t)0.0, limit_max);
  }

  /* Adaptive Homogeneity-Directed (AHD) demosaicing algorithm (dcraw variant).
     This version follows the homogeneity assessment logic from dcraw,
     which uses a different metric than the standard AHD implementation.
     PROGRESS can be used to report progress and check for cancellation.
     AH_GREEN is the dominating channel.
     AH_RED is the red channel index.
     AH_BLUE is the blue channel index.  */
  template <int ah_green, int ah_red, int ah_blue>
  bool
  ahd_interpolation_dominating_channel_dcraw (progress_info *progress)
  {
    int w = m_area.width, h = m_area.height;
    if (progress)
      progress->set_task ("demosaicing dominating channel (ahd dcraw variant)",
                          h * 4);

    luminosity_t range = find_robust_max (progress);
    if (progress && progress->cancelled ())
      return false;
    if (range <= 0)
      range = 1.0f;

    std::vector<luminosity_t> green_h (w * h);
    std::vector<luminosity_t> green_v (w * h);

#pragma omp parallel shared(progress, h, w, range, green_h,                   \
                                green_v) default(none)
    for (int y = 0; y < h; y++)
      {
        if (!progress || !progress->cancel_requested ())
          for (int x = 0; x < w; x++)
            {
              int color = GEOMETRY::demosaic_entry_color (x, y);
              if (color == ah_green)
                {
                  green_h[y * w + x] = known (x, y);
                  green_v[y * w + x] = known (x, y);
                  continue;
                }

              luminosity_t g_10 = dch (x - 1, y, ah_green);
              luminosity_t g10 = dch (x + 1, y, ah_green);
              luminosity_t g0_1 = dch (x, y - 1, ah_green);
              luminosity_t g01 = dch (x, y + 1, ah_green);

              luminosity_t c00 = known (x, y);
              luminosity_t c_20 = known (x - 2, y);
              luminosity_t c20 = known (x + 2, y);
              luminosity_t c0_2 = known (x, y - 2);
              luminosity_t c02 = known (x, y + 2);

              luminosity_t gh = (g_10 + g10) * (luminosity_t)0.5
                                + (2 * c00 - c_20 - c20) * (luminosity_t)0.25;
              green_h[y * w + x] = std::clamp (gh, std::min (g_10, g10),
                                               std::max (g_10, g10));

              luminosity_t gv = (g0_1 + g01) * (luminosity_t)0.5
                                + (2 * c00 - c0_2 - c02) * (luminosity_t)0.25;
              green_v[y * w + x] = std::clamp (gv, std::min (g0_1, g01),
                                               std::max (g0_1, g01));
            }
        if (progress)
          progress->inc_progress ();
      }
    if (progress && progress->cancelled ())
      return false;

    std::vector<rgbdata> rgb_h (w * h);
    std::vector<rgbdata> rgb_v (w * h);
    std::vector<cie_lab> lab_h (w * h, cie_lab (xyz (0, 0, 0), srgb_white));
    std::vector<cie_lab> lab_v (w * h, cie_lab (xyz (0, 0, 0), srgb_white));

#pragma omp parallel shared(progress, h, w, range, rgb_h, rgb_v, lab_h,       \
                                lab_v, green_h, green_v,                      \
                                srgb_white) default(none)
    for (int y = 0; y < h; y++)
      {
        if (!progress || !progress->cancel_requested ())
          for (int x = 0; x < w; x++)
            {
              int idx = y * w + x;
              rgb_h[idx][ah_green] = green_h[idx];
              rgb_h[idx][ah_red]
                  = interp_dcraw<ah_green> (x, y, ah_red, green_h, range);
              rgb_h[idx][ah_blue]
                  = interp_dcraw<ah_green> (x, y, ah_blue, green_h, range);

              rgb_v[idx][ah_green] = green_v[idx];
              rgb_v[idx][ah_red]
                  = interp_dcraw<ah_green> (x, y, ah_red, green_v, range);
              rgb_v[idx][ah_blue]
                  = interp_dcraw<ah_green> (x, y, ah_blue, green_v, range);
            }
        if (progress)
          progress->inc_progress ();
      }
    if (progress && progress->cancelled ())
      return false;

#pragma omp parallel for shared(progress, h, w, range, rgb_h, rgb_v, lab_h,   \
                                    lab_v, srgb_white) default(none)
    for (int i = 0; i < h * w; i++)
      {
        xyz xyz_h = xyz::from_srgb (
            std::clamp ((float)(rgb_h[i][ah_red] / range), 0.0f, 1.0f),
            std::clamp ((float)(rgb_h[i][ah_green] / range), 0.0f, 1.0f),
            std::clamp ((float)(rgb_h[i][ah_blue] / range), 0.0f, 1.0f));
        lab_h[i] = cie_lab (xyz_h, srgb_white);

        xyz xyz_v = xyz::from_srgb (
            std::clamp ((float)(rgb_v[i][ah_red] / range), 0.0f, 1.0f),
            std::clamp ((float)(rgb_v[i][ah_green] / range), 0.0f, 1.0f),
            std::clamp ((float)(rgb_v[i][ah_blue] / range), 0.0f, 1.0f));
        lab_v[i] = cie_lab (xyz_v, srgb_white);
      }

    std::vector<int> homo_h (w * h, 0);
    std::vector<int> homo_v (w * h, 0);

#pragma omp parallel shared(progress, h, w, lab_h, lab_v, homo_h,             \
                                homo_v) default(none)
    for (int y = 0; y < h; y++)
      {
        if (!progress || !progress->cancel_requested ())
          for (int x = 0; x < w; x++)
            {
              int ldiff[2][4], abdiff[2][4];
              int dirs[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };

              for (int d = 0; d < 2; d++)
                {
                  const cie_lab &center
                      = (d == 0) ? lab_h[y * w + x] : lab_v[y * w + x];
                  for (int i = 0; i < 4; i++)
                    {
                      int nx = std::clamp ((int)x + dirs[i][0], 0, w - 1);
                      int ny = std::clamp ((int)y + dirs[i][1], 0, h - 1);
                      const cie_lab &neigh
                          = (d == 0) ? lab_h[ny * w + nx] : lab_v[ny * w + nx];

                      ldiff[d][i] = my_abs ((int)(center.l * 100.0f)
                                            - (int)(neigh.l * 100.0f));
                      abdiff[d][i] = ((int)(center.a * 100.0f)
                                      - (int)(neigh.a * 100.0f))
                                         * ((int)(center.a * 100.0f)
                                            - (int)(neigh.a * 100.0f))
                                     + ((int)(center.b * 100.0f)
                                        - (int)(neigh.b * 100.0f))
                                           * ((int)(center.b * 100.0f)
                                              - (int)(neigh.b * 100.0f));
                    }
                }

              int leps = std::min (std::max (ldiff[0][0], ldiff[0][1]),
                                   std::max (ldiff[1][2], ldiff[1][3]));
              int abeps = std::min (std::max (abdiff[0][0], abdiff[0][1]),
                                    std::max (abdiff[1][2], abdiff[1][3]));

              for (int d = 0; d < 2; d++)
                for (int i = 0; i < 4; i++)
                  if (ldiff[d][i] <= leps && abdiff[d][i] <= abeps)
                    {
                      if (d == 0)
                        homo_h[y * w + x]++;
                      else
                        homo_v[y * w + x]++;
                    }
            }
        if (progress)
          progress->inc_progress ();
      }

    if (progress && progress->cancelled ())
      return false;

#pragma omp parallel shared(progress, h, w, rgb_h, rgb_v, homo_h,             \
                                homo_v) default(none)
    for (int y = 0; y < h; y++)
      {
        if (!progress || !progress->cancel_requested ())
          for (int x = 0; x < w; x++)
            {
              int hm_h = 0;
              int hm_v = 0;

              for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                  {
                    int nx = std::clamp (x + dx, 0, w - 1);
                    int ny = std::clamp (y + dy, 0, h - 1);
                    hm_h += homo_h[ny * w + nx];
                    hm_v += homo_v[ny * w + nx];
                  }

              if (hm_v > hm_h)
                {
                  d (x, y)[ah_green] = rgb_v[y * w + x][ah_green];
                  d (x, y)[ah_red] = rgb_v[y * w + x][ah_red];
                  d (x, y)[ah_blue] = rgb_v[y * w + x][ah_blue];
                }
              else if (hm_h > hm_v)
                {
                  d (x, y)[ah_green] = rgb_h[y * w + x][ah_green];
                  d (x, y)[ah_red] = rgb_h[y * w + x][ah_red];
                  d (x, y)[ah_blue] = rgb_h[y * w + x][ah_blue];
                }
              else
                {
                  d (x, y)[ah_green] = (rgb_h[y * w + x][ah_green]
                                        + rgb_v[y * w + x][ah_green])
                                       * (luminosity_t)0.5;
                  d (x, y)[ah_red]
                      = (rgb_h[y * w + x][ah_red] + rgb_v[y * w + x][ah_red])
                        * (luminosity_t)0.5;
                  d (x, y)[ah_blue]
                      = (rgb_h[y * w + x][ah_blue] + rgb_v[y * w + x][ah_blue])
                        * (luminosity_t)0.5;
                }
            }
        if (progress)
          progress->inc_progress ();
      }

    return !progress || !progress->cancelled ();
  }

  /* Step 2 of the AHD algorithm (dcraw variant): interpolate red and blue
     channels at positions where they are missing.
     PROGRESS can be used to report progress and check for cancellation.
     AH_GREEN is the dominating channel.
     AH_RED is the red channel.
     AH_BLUE is the blue channel.  */
  template <int ah_green, int ah_red, int ah_blue>
  bool
  ahd_interpolation_remaining_channels_dcraw (progress_info *progress)
  {
    return !progress || !progress->cancelled ();
  }
};

/* Paget screen demosaicing implementation.  The Paget screen uses a
   rotated Bayer-like pattern where blue is the dominating channel.  */
template <typename ANALYZER = analyze_base_worker<paget_geometry>>
class demosaic_paget_base : public demosaic_base<paget_geometry, ANALYZER>
{
public:
  bool
  demosaic (ANALYZER *analyze, render *r,
            render_parameters::screen_demosaic_t alg,
            denoise_parameters denoise_params, progress_info *progress)
  {
    if (!this->initialize (analyze))
      return false;
    if (!analyze->populate_demosaiced_data (this->m_demosaiced, r, this->m_area, progress))
      return false;
    switch (alg)
      {
      case render_parameters::hamilton_adams_demosaic:
        if (!this->template hamilton_adams_interpolation_dominating_channel<
                base_geometry::blue, true> (progress))
          return false;
        if (!this->template hamilton_adams_interpolation_remaining_channels<
                base_geometry::blue, base_geometry::red,
                base_geometry::green> (progress))
          return false;
        break;
      case render_parameters::ahd_demosaic:
        // if (!ahd_interpolation_dominating_channel<base_geometry::blue,
        // base_geometry::red, base_geometry::green, false, false> (progress))
        // return false;
        if (!this->template ahd_interpolation_dominating_channel_dcraw<base_geometry::blue,
                                                        base_geometry::red,
                                                        base_geometry::green> (
                progress))
          return false;
        if (!this->template ahd_interpolation_remaining_channels<base_geometry::blue,
                                                  base_geometry::red,
                                                  base_geometry::green> (
                progress))
          return false;
        break;
      case render_parameters::amaze_demosaic:
        if (!this->template amaze_interpolation<base_geometry::blue, base_geometry::red,
                                 base_geometry::green, true> (progress))
          return false;
        break;
      case render_parameters::rcd_demosaic:
        if (!this->template rcd_interpolation<base_geometry::blue, base_geometry::red,
                               base_geometry::green> (progress))
          return false;
        break;
      case render_parameters::lmmse_demosaic:
        if (!this->template lmmse_interpolation<base_geometry::blue, base_geometry::red,
                                 base_geometry::green> (progress))
          return false;
        break;
      default:
        break;
      }
    if (denoise_params.get_mode () != denoise_parameters::none)
      {
        return denoise_rgb<luminosity_t> (
            this->m_area.width, this->m_area.height,
            [&] (int x, int y) {
              return this->m_demosaiced[y * this->m_area.width + x];
            },
            [&] (int x, int y, rgbdata pixel) {
              this->m_demosaiced[y * this->m_area.width + x] = pixel;
            },
            denoise_params, progress);
      }
    return true;
  }
};
class demosaic_paget : public demosaic_paget_base<> {};
/* Paget screen demosaicing implementation.  The Paget screen uses a
   rotated Bayer-like pattern where blue is the dominating channel.  */
/* Dufaycolor screen demosaicing implementation.  */
template <typename ANALYZER = analyze_base_worker<dufay_geometry>>
class demosaic_dufay_base : public demosaic_base<dufay_geometry, ANALYZER>
{
public:
  bool
  demosaic (ANALYZER *analyze, render *r,
            render_parameters::screen_demosaic_t alg,
            denoise_parameters denoise_params, progress_info *progress)
  {
    if (!this->initialize (analyze))
      return false;
    if (!analyze->populate_demosaiced_data (this->m_demosaiced, r, this->m_area, progress))
      return false;
    switch (alg)
      {
#if 0
      case render_parameters::hamilton_adams_demosaic:
        if (!hamilton_adams_interpolation_dominating_channel<
                base_geometry::red, true> (progress))
          return false;
        if (!hamilton_adams_interpolation_remaining_channels<
                base_geometry::red, base_geometry::green,
                base_geometry::blue> (progress))
          return false;
        break;
      case render_parameters::ahd_demosaic:
        if (!ahd_interpolation_dominating_channel_dcraw<base_geometry::red,
                                                        base_geometry::green,
                                                        base_geometry::blue> (
                progress))
          return false;
        if (!ahd_interpolation_remaining_channels<base_geometry::red,
                                                  base_geometry::green,
                                                  base_geometry::blue> (
                progress))
          return false;
        break;
      case render_parameters::amaze_demosaic:
        if (!amaze_interpolation<base_geometry::red, base_geometry::green,
                                 base_geometry::blue, true> (progress))
          return false;
        break;
#endif
      case render_parameters::rcd_demosaic:
        if (!this->template rcd_interpolation_4x4<base_geometry::red, base_geometry::green,
                               base_geometry::blue> (progress))
          return false;
        break;
#if 0
      case render_parameters::lmmse_demosaic:
        if (!lmmse_interpolation<base_geometry::red, base_geometry::green,
                                 base_geometry::blue> (progress))
          return false;
        break;
#endif
      default:
        break;
      }
    if (denoise_params.get_mode () != denoise_parameters::none)
      {
        return denoise_rgb<luminosity_t> (
            this->m_area.width, this->m_area.height,
            [&] (int x, int y) {
              return this->m_demosaiced[y * this->m_area.width + x];
            },
            [&] (int x, int y, rgbdata pixel) {
              this->m_demosaiced[y * this->m_area.width + x] = pixel;
            },
            denoise_params, progress);
      }
    return true;
  }
};
class demosaic_dufay : public demosaic_dufay_base<> {};

} // namespace colorscreen
#endif
