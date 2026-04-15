#ifndef DEMOSAIC_H
#define DEMOSAIC_H
#include "bitmap.h"
#include "include/color.h"
#include "include/progress-info.h"
#include "bspline.h"

namespace colorscreen
{
class demosaic_generic_base
{
protected:
  static constexpr const bool debug = colorscreen_checking;

public:
  /* Basic access to demosaiced data.  */
  rgbdata &
  demosaiced_data (int x, int y)
  {
    x = std::clamp (x, 0, m_width - 1);
    y = std::clamp (y, 0, m_height - 1);
    return m_demosaiced[y * m_width + x];
  };

  /* Basic access to demosaiced data; no clamping.  */
  rgbdata &
  fast_demosaiced_data (int x, int y)
  {
    return m_demosaiced[y * m_width + x];
  };

  luminosity_t
  find_robust_max (progress_info *progress)
  {
    histogram h;
    if (progress)
      progress->set_task ("Determining demosaiced value range", m_height * 2);
    for (int y = 0; y < m_height; y++)
      {
        if (!progress || !progress->cancel_requested ())
	  for (int x = 0; x < m_height; x++)
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
    for (int y = 0; y < m_height; y++)
      {
        if (!progress || !progress->cancel_requested ())
	  for (int x = 0; x < m_height; x++)
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
  int m_width, m_height, m_xshift, m_yshift;

  /* Accessor shorcut used in the demosaicing algorithm
     implementation.  */
  always_inline_attr rgbdata &
  d (int x, int y)
  {
    x = std::clamp (x, 0, m_width - 1);
    y = std::clamp (y, 0, m_height - 1);
    return m_demosaiced[y * m_width + x];
  };
};

template <typename GEOMETRY> class demosaic_base : public demosaic_generic_base
{
  inline pure_attr rgbdata
  lanczos3_demosaiced_interpolate (point_t scr)
  {
    point_t p = GEOMETRY::to_demosaiced_coordinates (scr);
    int sx, sy;
    coord_t rx = my_modf (p.x, &sx);
    coord_t ry = my_modf (p.y, &sy);
    sx += m_xshift;
    sy += m_yshift;
    if (sx >= 2 && sx < m_width - 3 && sy >= 2 && sy < m_height - 3)
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
    sx += m_xshift;
    sy += m_yshift;
    if (sx >= 0 && sx < m_width && sy >= 0 && sy < m_height)
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
    sx += m_xshift;
    sy += m_yshift;
    if (sx >= 0 && sx < m_width - 1 && sy >= 0 && sy < m_height - 1)
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
    sx += m_xshift;
    sy += m_yshift;
    if (sx >= 1 && sx < m_width - 2 && sy >= 1 && sy < m_height - 2)
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
    sx += m_xshift;
    sy += m_yshift;
    if (sx >= 1 && sx < m_width - 2 && sy >= 1 && sy < m_height - 2)
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
  initialize (analyze_base_worker<GEOMETRY> *analysis)
  {
    analysis->demosaiced_dimensions (&m_width, &m_height, &m_xshift,
                                     &m_yshift);
    m_demosaiced.resize (m_width * m_height);
    return true;
  }

  /* Accessor for mosaiced channel data with sanity check that channel is
     defined and access is in bounds.  */
  always_inline_attr luminosity_t &
  dch (int x, int y, int ch)
  {
    int cx = std::clamp (x, 0, m_width - 1);
    int cy = std::clamp (y, 0, m_height - 1);
    assert (!debug || cx != x || cy != y
            || (int)GEOMETRY::demosaic_entry_color (cx, cy) == ch);
    return m_demosaiced[cy * m_width + cx][ch];
  };
  /* Return the known (mosaiced) channel value at position (x,y).
     The color is determined by GEOMETRY.  */
  always_inline_attr luminosity_t
  known (int x, int y)
  {
    int cx = std::clamp (x, 0, m_width - 1);
    int cy = std::clamp (y, 0, m_height - 1);
    switch (GEOMETRY::demosaic_entry_color (x, y))
      {
      case base_geometry::red:
        return m_demosaiced[cy * m_width + cx].red;
      case base_geometry::green:
        return m_demosaiced[cy * m_height + cx].green;
      default:
        return m_demosaiced[cy * m_width + cx].blue;
      }
  };
  /* Formely AI generation experiment for demosaicing of non-bayer filters.
     Step 1 - interpolation of dominating pattern.

     For each non-green pixel, check which cardinal and diagonal
     neighbors are green and interpolate accordingly.
     The Laplacian correction uses the known channel at the current
     pixel and same-color pixels at distance 2.  */
  template <int ah_green, bool smoothen>
  bool
  generic_dominating_channel (progress_info *progress)
  {
    int w = m_width, h = m_height;
#pragma omp parallel shared(progress, h, w) default(none)
    for (int y = 0; y < h; y++)
      {
        /* Accessor for the demosaiced array with bounds clamping (read/write).
         */
        const auto d = [&] (int x, int y) -> rgbdata &
          {
            x = std::clamp (x, 0, w - 1);
            y = std::clamp (y, 0, h - 1);
            return m_demosaiced[y * w + x];
          };
        /* Read-only accessor for mosaiced channel data.  Use bounds clamping
           (border replication) for out-of-bounds coordinates — returning 0
           would bias the Laplacian correction at image boundaries.
           Assert that the pixel at the given position is the expected color
           (only when coordinates are in-bounds).  */
        auto dch = [&] (int x, int y, int ch) -> luminosity_t
          {
            int cx = std::clamp (x, 0, w - 1);
            int cy = std::clamp (y, 0, h - 1);
            assert (!debug || cx != x || cy != y
                    || (int)GEOMETRY::demosaic_entry_color (x, y) == ch);
            return m_demosaiced[cy * w + cx][ch];
          };
        /* Return the known (mosaiced) channel value at position (x,y).
           The color is determined by GEOMETRY.  Uses clamping for
           out-of-bounds to provide smooth boundary behavior.  */
        auto known = [&] (int x, int y) -> luminosity_t
          {
            int cx = std::clamp (x, 0, w - 1);
            int cy = std::clamp (y, 0, h - 1);
            switch (GEOMETRY::demosaic_entry_color (x, y))
              {
              case base_geometry::red:
                return m_demosaiced[cy * w + cx].red;
              case base_geometry::green:
                return m_demosaiced[cy * w + cx].green;
              default:
                return m_demosaiced[cy * w + cx].blue;
              }
          };

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

                  luminosity_t grad_h = fabs (g_left - g_right)
                                        + fabs (2 * known_val - kl2 - kr2);
                  luminosity_t grad_v = fabs (g_up - g_down)
                                        + fabs (2 * known_val - ku2 - kd2);
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

  /* Basic interpolation for non-dominating color formely AI experiment.

     Now green is fully populated.  We use green-channel-guided
     color-difference interpolation: interpolate (C-G) from
     positions where C is known, then add local green.  */
  template <int ah_green, int ah_red, int ah_blue>
  bool
  generic_interpolation_remaining_channels (progress_info *progress)
  {
    int w = m_width, h = m_height;
#pragma omp parallel shared(progress, h, w) default(none)
    for (int y = 0; y < h; y++)
      {
        /* Accessor for the demosaiced array with bounds clamping (read/write).
         */
        const auto d = [&] (int x, int y) -> rgbdata &
          {
            x = std::clamp (x, 0, w - 1);
            y = std::clamp (y, 0, h - 1);
            return m_demosaiced[y * w + x];
          };
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
                      luminosity_t rgh = fabs (rdl - rdr);
                      luminosity_t rgv = fabs (rdu - rdd);
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
                      luminosity_t bgh = fabs (bdl - bdr);
                      luminosity_t bgv = fabs (bdu - bdd);
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

  /* Step 1 on hamiltom-adams algorithm; the dominating channel (green) is
     assumed to form a chess board and is interpolated first.

     For each non-green pixel, check which cardinal and diagonal
     neighbors are green and interpolate accordingly.
     The Laplacian correction uses the known channel at the current
     pixel and same-color pixels at distance 2.

     Optional step 2: smoothening of the dominating channel.  */
  template <int ah_green, bool smoothen>
  bool
  hamiltom_adams_interpolation_dominating_channel (progress_info *progress)
  {
    int w = m_width, h = m_height;
    luminosity_t range = find_robust_max (progress);
    if (progress && progress->cancelled ())
      return false;
    luminosity_t TL = 0.1 * range;
    luminosity_t TH = 0.8 * range;

    bitmap_2d predA (smoothen ? w : 0, smoothen ? h : 0);
    if (progress)
      progress->set_task ("Demosaicing dominating channel (Hamilton-Adams)",
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

              luminosity_t h = fabs (2 * c00 - c0_2 - c02)
                               + 2 * fabs (g0_1 - g01)
                               + fabs (2 * (c00 + bh) - g0_1 - g01);

              luminosity_t v = fabs (2 * c00 - c_20 - c20)
                               + 2 * fabs (g_10 - g10)
                               + fabs (2 * (c00 + bv) - g_10 - g10);


              if (h < TL && v < TL)
                {
                  d (x, y)[(int)ah_green]
                      = (g_10 + g10 + g0_1 + g01) * (luminosity_t)0.25;
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
          progress->set_task ("Smoothening noisy areas in the dominating "
                              "channel (Hamilton-Adams)",
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

  /* Basic interpolation logic of Hamilton-Adams for non-dominating color.  */
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

        luminosity_t dn = fabs (2 * g00 - g_1_1 - g11) + fabs (r_1_1 - r11);
        luminosity_t dp = fabs (2 * g00 - g_11 - g1_1) + fabs (r_11 - r1_1);
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
  /* Step 3: Interpolate red and blue at positions where they are missing.

     Now green is fully populated.  We use green-channel-guided
     color-difference interpolation: interpolate (C-G) from
     positions where C is known, then add local green.  */
  template <int ah_green, int ah_red, int ah_blue>
  bool
  hamiltom_adams_interpolation_remaining_channels (progress_info *progress)
  {
    int h = m_height, w = m_width;
    if (progress)
      progress->set_task ("Demosaicing remaining chanels (Hamilton-Adams)", h);
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
  luminosity_t
  interp (int x, int y, int ch, const std::vector<luminosity_t> &G_dir) 
  {
    int w = m_width, h = m_height;
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
	       + (dch (x - 1, y, ch) - G_dir[y * w + xl]
		  + dch (x + 1, y, ch) - G_dir[y * w + xr])
		     * (luminosity_t)0.5;
      }
    else if (u_c && d_c)
      {
	int yu = std::clamp (y - 1, 0, h - 1);
	int yd = std::clamp (y + 1, 0, h - 1);
	return g_here
	       + (dch (x, y - 1, ch) - G_dir[yu * w + x]
		  + dch (x, y + 1, ch) - G_dir[yd * w + x])
		     * (luminosity_t)0.5;
      }
    else if (l_c)
      return g_here + dch (x - 1, y, ch) - G_dir[y * w + std::clamp (x - 1, 0, w - 1)];
    else if (r_c)
      return g_here + dch (x + 1, y, ch) - G_dir[y * w + std::clamp (x + 1, 0, w - 1)];
    else if (u_c)
      return g_here + dch (x, y - 1, ch) - G_dir[std::clamp (y - 1, 0, h - 1) * w + x];
    else if (d_c)
      return g_here + dch (x, y + 1, ch) - G_dir[std::clamp (y + 1, 0, h - 1) * w + x];
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
  /* AHD (Adaptive Homogeneity-Directed) interpolation for the dominating channel.
     This algorithm performs directional interpolations (horizontal and vertical) for the
     dominating channel. It interpolates the remaining channels in those directions if
     using the slow mode. It evaluates a homogeneity metric and selects the direction
     with the greatest homogeneity. The fast version uses a simple color difference
     variance metric. The slow version computes true CIELAB homogeneity mapped across
     a 3x3 window using the full color delta metrics.
     Template parameters allow its application on arbitrary geometries where color channels
     can be exchanged.
   */
  template <int ah_green, int ah_red, int ah_blue, bool fast, bool smoothen>
  bool
  ahd_interpolation_dominating_channel (progress_info *progress)
  {
    int w = m_width, h = m_height;
    if (progress)
      progress->set_task (fast ? "Demosaicing dominating channel (AHD fast)" :
                                 "Demosaicing dominating channel (AHD slow)",
                          fast ? (smoothen ? h * 2 : h) : (smoothen ? h * 5 : h * 4));

    std::vector<luminosity_t> Green_H (w * h);
    std::vector<luminosity_t> Green_V (w * h);

    /* Real AHD requires filling Green_H and Green_V fully first.  */
#pragma omp parallel shared(progress, h, w, Green_H, Green_V) default(none)
    for (int y = 0; y < h; y++)
      {
        if (!progress || !progress->cancel_requested ())
          for (int x = 0; x < w; x++)
            {
              int color = GEOMETRY::demosaic_entry_color (x, y);
              if (color == ah_green)
                {
                  Green_H[y * w + x] = known (x, y);
                  Green_V[y * w + x] = known (x, y);
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

              Green_H[y * w + x]
                  = (g_10 + g10) * (luminosity_t)0.5
                    + (2 * c00 - c_20 - c20) * (luminosity_t)0.25;
              Green_V[y * w + x]
                  = (g0_1 + g01) * (luminosity_t)0.5
                    + (2 * c00 - c0_2 - c02) * (luminosity_t)0.25;
            }
        if (progress)
          progress->inc_progress ();
      }
    if (progress && progress->cancelled ())
      return false;

    std::vector<rgbdata> RGB_H;
    std::vector<rgbdata> RGB_V;
    std::vector<cie_lab> LAB_H;
    std::vector<cie_lab> LAB_V;

    if (!fast)
      {
        luminosity_t range = find_robust_max (progress);
        if (progress && progress->cancelled ())
          return false;
        if (range <= 0)
          range = 1.0f;

        RGB_H.resize (w * h);
        RGB_V.resize (w * h);
        

#pragma omp parallel shared(progress, h, w, RGB_H, RGB_V, LAB_H, LAB_V, Green_H, Green_V) default(none)
        for (int y = 0; y < h; y++)
          {
            if (!progress || !progress->cancel_requested ())
              for (int x = 0; x < w; x++)
                {
                  int idx = y * w + x;
                  RGB_H[idx][ah_green] = Green_H[idx];
                  RGB_H[idx][ah_red] = interp (x, y, ah_red, Green_H);
                  RGB_H[idx][ah_blue] = interp (x, y, ah_blue, Green_H);

                  RGB_V[idx][ah_green] = Green_V[idx];
                  RGB_V[idx][ah_red] = interp (x, y, ah_red, Green_V);
                  RGB_V[idx][ah_blue] = interp (x, y, ah_blue, Green_V);
                }
            if (progress)
              progress->inc_progress ();
          }
        if (progress && progress->cancelled ())
          return false;

        // Convert to Lab sequentially to avoid omp initialization/sizing overhead
        LAB_H.resize (w * h, cie_lab(xyz(0,0,0), srgb_white));
        LAB_V.resize (w * h, cie_lab(xyz(0,0,0), srgb_white));
#pragma omp parallel for shared(progress, h, w, range, RGB_H, RGB_V, LAB_H, LAB_V, srgb_white) default(none)
        for (int i = 0; i < h * w; i++)
          {
            xyz xyz_h = xyz::from_srgb (std::clamp(RGB_H[i].red / range, 0.0f, 1.0f),
                                        std::clamp(RGB_H[i].green / range, 0.0f, 1.0f),
                                        std::clamp(RGB_H[i].blue / range, 0.0f, 1.0f));
            LAB_H[i] = cie_lab (xyz_h, srgb_white);

            xyz xyz_v = xyz::from_srgb (std::clamp(RGB_V[i].red / range, 0.0f, 1.0f),
                                        std::clamp(RGB_V[i].green / range, 0.0f, 1.0f),
                                        std::clamp(RGB_V[i].blue / range, 0.0f, 1.0f));
            LAB_V[i] = cie_lab (xyz_v, srgb_white);
          }
      }

    bitmap_2d predA (smoothen ? w : 0, smoothen ? h : 0);

#pragma omp parallel shared(progress, h, w, predA, Green_H, Green_V, LAB_H, LAB_V) default(none)
    for (int y = 0; y < h; y++)
      {
        if (!progress || !progress->cancel_requested ())
          for (int x = 0; x < w; x++)
            {
              int color = GEOMETRY::demosaic_entry_color (x, y);
              if (color == ah_green)
                continue;

              bool choose_h = true;

              if (fast)
                {
                  // Fast variance metric based on simple color difference
                  luminosity_t h_var = 0;
                  luminosity_t v_var = 0;
                  luminosity_t gh = Green_H[y * w + x];
                  luminosity_t gv = Green_V[y * w + x];

                  luminosity_t c00 = known (x, y);
                  luminosity_t diff_h = fabs (c00 - gh);
                  luminosity_t diff_v = fabs (c00 - gv);

                  luminosity_t g_10 = dch (x - 1, y, ah_green);
                  luminosity_t g10 = dch (x + 1, y, ah_green);
                  luminosity_t g0_1 = dch (x, y - 1, ah_green);
                  luminosity_t g01 = dch (x, y + 1, ah_green);

                  h_var = diff_h + fabs(g_10 - g10);
                  v_var = diff_v + fabs(g0_1 - g01);

                  choose_h = (h_var < v_var);
                }
              else
                {
                  // Slow CIELAB homogeneity metric
                  int h_homogeneity = 0;
                  int v_homogeneity = 0;

                  // 3x3 window homogeneity check against the center pixel
                  for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++)
                      {
                        int nx = std::clamp (x + dx, 0, w - 1);
                        int ny = std::clamp (y + dy, 0, h - 1);
                        
                        if (deltaE (LAB_H[y * w + x], LAB_H[ny * w + nx]) < 2.0f)
                          h_homogeneity++;
                        if (deltaE (LAB_V[y * w + x], LAB_V[ny * w + nx]) < 2.0f)
                          v_homogeneity++;
                      }
                  
                  choose_h = (h_homogeneity >= v_homogeneity);
                }

              if (choose_h)
                {
                  d (x, y)[ah_green] = Green_H[y * w + x];
                  predA.set_bit (x, y);
                }
              else
                {
                  d (x, y)[ah_green] = Green_V[y * w + x];
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
          progress->set_task ("Smoothening noisy areas in the dominating "
                              "channel (AHD)",
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

  /* Step 2: Interpolate red and blue at positions where they are missing.

     Now green is fully populated by AHD. We use a parallel color difference strategy
     that mirrors Hamilton-Adams's residual calculation for remaining channels. 
   */
  template <int ah_green, int ah_red, int ah_blue>
  bool
  ahd_interpolation_remaining_channels (progress_info *progress)
  {
    int h = m_height, w = m_width;
    if (progress)
      progress->set_task ("Demosaicing remaining chanels (AHD)", h);
#pragma omp parallel shared(progress, h, w) default(none)
    for (int y = 0; y < h; y++)
      {
        if (!progress || !progress->cancel_requested ())
          for (int x = 0; x < w; x++)
            {
              int color = GEOMETRY::demosaic_entry_color (x, y);
              luminosity_t g_here = d (x, y)[(int)ah_green];

              if (color != ah_red)
                hamilton_adams_interpolate_color<ah_green, ah_red> (g_here, x, y);
              if (color != ah_blue)
                hamilton_adams_interpolate_color<ah_green, ah_blue> (g_here, x, y);
            }
        if (progress)
          progress->inc_progress ();
      }
    return !progress || !progress->cancelled ();
  }
};

class demosaic_paget : public demosaic_base<paget_geometry>
{
public:
  bool
  demosaic (analyze_paget *analyze, render *r, render_parameters::screen_demosaic_t alg, progress_info *progress)
  {
    if (!initialize (analyze))
      return false;
    if (!analyze->populate_demosaiced_data (m_demosaiced, r, m_width, m_height,
                                            m_xshift, m_yshift, progress))
      return false;
    switch (alg)
      {
      case render_parameters::hamilton_adams_demosaic:
	if (!hamiltom_adams_interpolation_dominating_channel<base_geometry::blue,
							     true> (progress))
	  return false;
	if (!hamiltom_adams_interpolation_remaining_channels<
		base_geometry::blue, base_geometry::red, base_geometry::green> (
		progress))
	  return false;
	break;
      case render_parameters::ahd_demosaic:
	if (!ahd_interpolation_dominating_channel<base_geometry::blue, base_geometry::red, base_geometry::green, true, true> (progress))
	  return false;
	if (!ahd_interpolation_remaining_channels<
		base_geometry::blue, base_geometry::red, base_geometry::green> (
		progress))
	  return false;
	break;
      default:
	break;
      }
    return true;
  }
};

}
#endif
