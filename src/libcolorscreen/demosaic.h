#ifndef DEMOSAIC_H
#define DEMOSAIC_H
#include "bitmap.h"
#include "include/color.h"
#include "include/progress-info.h"

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
        double wx[6];
        for (int i = 0; i < 6; i++)
          wx[i] = lanczos3_kernel (i - 2 - rx);
        double wy[6];
        for (int j = 0; j < 6; j++)
          wy[j] = lanczos3_kernel (j - 2 - ry);

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
    return { (luminosity_t)1, (luminosity_t)0, (luminosity_t)0 };
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
    return { (luminosity_t)1, (luminosity_t)0, (luminosity_t)0 };
  }
public:
  inline pure_attr rgbdata
  interpolate (point_t scr, rgbdata patch_proportions,
               render_parameters::demosaiced_scaling_t scaling_mode)
  {
    if (scaling_mode == render_parameters::lanczos3_scaling)
      return lanczos3_demosaiced_interpolate (scr);
    else
      return bicubic_demosaiced_interpolate (scr);
  }

protected:
  bool
  initialize (analyze_base_worker<GEOMETRY> *analysis)
  {
    analysis->demosaiced_dimensions (&m_width, &m_height, &m_xshift, &m_yshift);
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

              luminosity_t TL = 0.1 /** 2*/;
              luminosity_t TH = 0.8 /** 2*/;

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
  template<int ah_green, int ah_chn>
  always_inline_attr void
  hamilton_adams_interpolate_color(luminosity_t g_here, int x, int y)
  {
    /* Check cardinal neighbors for red.  */
    bool l_r = GEOMETRY::demosaic_entry_color (x - 1, y)
	       == ah_chn;
    bool r_r = GEOMETRY::demosaic_entry_color (x + 1, y)
	       == ah_chn;
    bool u_r = GEOMETRY::demosaic_entry_color (x, y - 1)
	       == ah_chn;
    bool d_r = GEOMETRY::demosaic_entry_color (x, y + 1)
	       == ah_chn;
    bool h_r = l_r && r_r;
    bool v_r = u_r && d_r;

    assert (!colorscreen_checking || (!h_r || !v_r));

    if (h_r)
      {
	luminosity_t rdl = dch (x - 1, y, ah_chn);
	luminosity_t rdr = dch (x + 1, y, ah_chn);
	luminosity_t gnl = d (x - 1, y)[ah_green];
	luminosity_t gnr = d (x + 1, y)[ah_green];
	d (x, y)[(int)ah_chn]
	    = ((2 * g_here - gnl - gnr) + (rdl + rdr)) * 0.5;
      }
    else if (v_r)
      {
	luminosity_t rdu = dch (x, y - 1, ah_chn);
	luminosity_t rdd = dch (x, y + 1, ah_chn);
	luminosity_t gnu = d (x, y - 1)[ah_green];
	luminosity_t gnd = d (x, y + 1)[ah_green];
	d (x, y)[(int)ah_chn]
	    = ((2 * g_here - gnu - gnd) + (rdu + rdd)) * 0.5;
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

	luminosity_t dn = fabs (2 * g00 - g_1_1 - g11)
			  + fabs (r_1_1 - r11);
	luminosity_t dp = fabs (2 * g00 - g_11 - g1_1)
			  + fabs (r_11 - r1_1);
	if (dn < dp)
	  d (x, y)[(int)ah_chn]
	      = (r_1_1 + r11 + 2 * g00 - g_1_1 - g11)
		* (luminosity_t)0.5;
	if (dp < dn)
	  d (x, y)[(int)ah_chn]
	      = (r_11 + r1_1 + 2 * g00 - g_11 - g1_1)
		* (luminosity_t)0.5;
	else
	  d (x, y)[(int)ah_chn]
	      = (r_1_1 + r11 + r_11 + r1_1 + 4 * g00 - g_1_1
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
#pragma omp parallel shared(progress, h, w) default(none)
    for (int y = 0; y < h; y++)
      {
        if (!progress || !progress->cancel_requested ())
          for (int x = 0; x < w; x++)
            {
              int color = GEOMETRY::demosaic_entry_color (x, y);
              luminosity_t g_here = d (x, y)[(int)ah_green];

	      if (color != ah_red)
                hamilton_adams_interpolate_color <ah_green, ah_red> (g_here, x, y);
	      if (color != ah_blue)
                hamilton_adams_interpolate_color <ah_green, ah_blue> (g_here, x, y);
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
  demosaic (analyze_paget *analyze, render *r, progress_info *progress)
  {
    if (!initialize (analyze))
      return false;
    if (!analyze->populate_demosaiced_data (m_demosaiced, r, m_width, m_height, m_xshift, m_yshift, progress))
      return false;
    if (!hamiltom_adams_interpolation_dominating_channel<base_geometry::blue,
                                                         true> (progress))
      return false;
    if (!hamiltom_adams_interpolation_remaining_channels<
            base_geometry::blue, base_geometry::red, base_geometry::green> (
            progress))
      return false;
    printf ("Demosaiced\n");
    return true;
  }
};

}
#endif
