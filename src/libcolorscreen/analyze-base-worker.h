/* Template implementations for color screen analysis workers.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#include "analyze-base.h"

namespace colorscreen
{

/* Collect luminosity of individual color patches.

   SCR_TO_IMG is the map from image to screen.
   RENDER is the renderer.
   SCREEN is the screen geometry.
   SIMULATED_SCREEN is the simulated screen if any.
   COLLECTION_THRESHOLD is the threshold for luminosity collection.
   W_RED, W_GREEN, W_BLUE are the weights for the channels.
   AREA is the region to analyze.
   PROGRESS is the progress info object.  */
template <typename GEOMETRY>
bool
analyze_base_worker<GEOMETRY>::analyze_precise (
    scr_to_img *scr_to_img, render_to_scr *render, const screen *screen,
    const simulated_screen *simulated_screen,
    luminosity_t collection_threshold, luminosity_t *w_red,
    luminosity_t *w_green, luminosity_t *w_blue, int_image_area area,
    progress_info *progress)
{
  int size = (openmp_min_size + area.width - 1) / area.width;
  int size2 = (openmp_min_size + m_area.width - 1) / m_area.width;
#pragma omp parallel shared(                                                  \
        progress, render, scr_to_img, screen, collection_threshold, w_blue,   \
            w_red, w_green, area, simulated_screen) default(none) \
	    if (area.height > size || this->m_area.height > size2)
  {
#pragma omp for
    for (int y = area.y; y < area.y + area.height; y++)
      {
        if (!progress || !progress->cancel_requested ())
          for (int x = area.x; x < area.x + area.width; x++)
            {
              point_t scr = scr_to_img->to_scr (
                  { x + (coord_t) 0.5, y + (coord_t) 0.5 });
              scr += { (coord_t)m_area.xshift (), (coord_t)m_area.yshift () };
              /* Dufay analyzer shifts red strip and some pixels gets accounted
                 to neighbouring screen tile; add extra bffer of 1 screen tile
                 to be sure we do not access uninitialized memory.  */
              if (!GEOMETRY::check_range
                  && (scr.x <= (coord_t) 0
                      || scr.x >= (coord_t) m_area.width - (coord_t) 1
                      || scr.y <= (coord_t) 0
                      || scr.y >= (coord_t) m_area.height - (coord_t) 1))
                continue;

              luminosity_t l = render->get_unadjusted_data ({ x, y });
              rgbdata screen_color;
              if (!simulated_screen)
                screen_color = screen->noninterpolated_mult (scr);
              else
                screen_color = simulated_screen->get_pixel (x, y);
              if (screen_color.red > collection_threshold)
                {
                  data_entry e = GEOMETRY::red_scr_to_entry (scr);
                  if (!GEOMETRY::check_range
                      || (e.x >= 0
                          && e.x < m_area.width * GEOMETRY::red_width_scale
                          && e.y >= 0
                          && e.y < m_area.height
                                       * GEOMETRY::red_height_scale))
                    {
                      if constexpr (debug)
                        assert (e.x >= 0 && e.y >= 0
                                && e.x < m_area.width
                                             * GEOMETRY::red_width_scale
                                && e.y < m_area.height
                                             * GEOMETRY::red_height_scale);
                      luminosity_t val
                          = (screen_color.red - collection_threshold);
                      int idx = e.y * m_area.width * GEOMETRY::red_width_scale
                                + e.x;
                      luminosity_t &c = m_red[idx];
                      luminosity_t vall = val * l;
#pragma omp atomic
                      c += vall;
                      luminosity_t &w = w_red[idx];
#pragma omp atomic
                      w += val;
                    }
                }
              if (screen_color.green > collection_threshold)
                {
                  data_entry e = GEOMETRY::green_scr_to_entry (scr);
                  if (!GEOMETRY::check_range
                      || (e.x >= 0
                          && e.x < m_area.width * GEOMETRY::green_width_scale
                          && e.y >= 0
                          && e.y < m_area.height
                                       * GEOMETRY::green_height_scale))
                    {
                      if constexpr (debug)
                        assert (e.x >= 0 && e.y >= 0
                                && e.x < m_area.width
                                             * GEOMETRY::green_width_scale
                                && e.y < m_area.height
                                             * GEOMETRY::green_height_scale);
                      luminosity_t val
                          = (screen_color.green - collection_threshold);
                      int idx
                          = e.y * m_area.width * GEOMETRY::green_width_scale
                            + e.x;
                      luminosity_t vall = val * l;
                      luminosity_t &c = m_green[idx];
#pragma omp atomic
                      c += vall;
                      luminosity_t &w = w_green[idx];
#pragma omp atomic
                      w += val;
                    }
                }
              if (screen_color.blue > collection_threshold)
                {
                  data_entry e = GEOMETRY::blue_scr_to_entry (scr);
                  if (!GEOMETRY::check_range
                      || (e.x >= 0
                          && e.x < m_area.width * GEOMETRY::blue_width_scale
                          && e.y >= 0
                          && e.y < m_area.height
                                       * GEOMETRY::blue_height_scale))
                    {
                      if constexpr (debug)
                        assert (e.x >= 0 && e.y >= 0
                                && e.x < m_area.width
                                             * GEOMETRY::blue_width_scale
                                && e.y < m_area.height
                                             * GEOMETRY::blue_height_scale);
                      luminosity_t val
                          = (screen_color.blue - collection_threshold);
                      int idx
                          = e.y * m_area.width * GEOMETRY::blue_width_scale
                            + e.x;
                      luminosity_t vall = val * l;
                      luminosity_t &c = m_blue[idx];
#pragma omp atomic
                      c += vall;
                      luminosity_t &w = w_blue[idx];
#pragma omp atomic
                      w += val;
                    }
                }
            }
        if (progress)
          progress->inc_progress ();
      }
    if (!progress || !progress->cancel_requested ())
      {
#pragma omp for nowait
        for (int y = 0; y < m_area.height; y++)
          {
            if (!progress || !progress->cancel_requested ())
              for (int yy = 0; yy < GEOMETRY::red_height_scale; yy++)
                for (int x = 0; x < m_area.width * GEOMETRY::red_width_scale;
                     x++)
                  {
                    data_entry e = { x, y * GEOMETRY::red_height_scale + yy };
                    int idx = e.y * m_area.width * GEOMETRY::red_width_scale
                              + e.x;
                    if (w_red[idx] != (luminosity_t) 0)
                      m_red[idx] /= w_red[idx];
                    else
                      {
                        point_t scr = GEOMETRY::red_entry_to_scr (e);
                        m_red[idx]
                            = render->get_unadjusted_img_pixel_scr (
                                { scr.x - (coord_t)m_area.xshift (),
                                  scr.y - (coord_t)m_area.yshift () });
                      }
                  }
            if (progress)
              progress->inc_progress ();
          }
#pragma omp for nowait
        for (int y = 0; y < m_area.height; y++)
          {
            if (!progress || !progress->cancel_requested ())
              for (int yy = 0; yy < GEOMETRY::green_height_scale; yy++)
                for (int x = 0;
                     x < m_area.width * GEOMETRY::green_width_scale; x++)
                  {
                    data_entry e
                        = { x, y * GEOMETRY::green_height_scale + yy };
                    int idx = e.y * m_area.width * GEOMETRY::green_width_scale
                              + e.x;
                    if (w_green[idx] != (luminosity_t) 0)
                      m_green[idx] /= w_green[idx];
                    else
                      {
                        point_t scr = GEOMETRY::green_entry_to_scr (e);
                        m_green[idx]
                            = render->get_unadjusted_img_pixel_scr (
                                { scr.x - (coord_t)m_area.xshift (),
                                  scr.y - (coord_t)m_area.yshift () });
                      }
                  }
            if (progress)
              progress->inc_progress ();
          }
#pragma omp for nowait
        for (int y = 0; y < m_area.height; y++)
          {
            if (!progress || !progress->cancel_requested ())
              for (int yy = 0; yy < GEOMETRY::blue_height_scale; yy++)
                for (int x = 0; x < m_area.width * GEOMETRY::blue_width_scale;
                     x++)
                  {
                    data_entry e = { x, y * GEOMETRY::blue_height_scale + yy };
                    int idx = e.y * m_area.width * GEOMETRY::blue_width_scale
                              + e.x;
                    if (w_blue[idx] != (luminosity_t) 0)
                      m_blue[idx] /= w_blue[idx];
                    else
                      {
                        point_t scr = GEOMETRY::blue_entry_to_scr (e);
                        m_blue[idx]
                            = render->get_unadjusted_img_pixel_scr (
                                { scr.x - (coord_t)m_area.xshift (),
                                  scr.y - (coord_t)m_area.yshift () });
                      }
                  }
            if (progress)
              progress->inc_progress ();
          }
      }
  }
  return !progress || !progress->cancelled ();
}
/* Collect RGB colors of individual color patches (in scanner color space).
   This is useful i.e. to determine scanner response to the dye colors and set
   mixing weights.

   SCR_TO_IMG is the map from image to screen.
   RENDER is the renderer.
   SCREEN is the screen geometry.
   SIMULATED_SCREEN is the simulated screen if any.
   COLLECTION_THRESHOLD is the threshold for luminosity collection.
   W_RED, W_GREEN, W_BLUE are the weights for the channels.
   AREA is the region to analyze.
   PROGRESS is the progress info object.  */
template <typename GEOMETRY>
bool
analyze_base_worker<GEOMETRY>::analyze_precise_rgb (
    scr_to_img *scr_to_img, render_to_scr *render, const screen *screen,
    const simulated_screen *simulated_screen,
    luminosity_t collection_threshold, luminosity_t *w_red,
    luminosity_t *w_green, luminosity_t *w_blue, int_image_area area,
    progress_info *progress)
{
  int size = (openmp_min_size + area.width - 1) / area.width;
  int size2 = (openmp_min_size + m_area.width - 1) / m_area.width;
#pragma omp parallel shared(                                                  \
        progress, render, scr_to_img, screen, collection_threshold, w_blue,   \
            w_red, w_green, area, simulated_screen) default(none) \
	    if (area.height > size || this->m_area.height > size2)
  {
#pragma omp for
    for (int y = area.y; y < area.y + area.height; y++)
      {
        if (!progress || !progress->cancel_requested ())
          for (int x = area.x; x < area.x + area.width; x++)
            {
              point_t scr = scr_to_img->to_scr (
                  { x + (coord_t) 0.5, y + (coord_t) 0.5 });
              scr += { (coord_t)m_area.xshift (), (coord_t)m_area.yshift () };
              if (!GEOMETRY::check_range
                  && (scr.x < (coord_t) 0 || scr.x > (coord_t)m_area.width - 1
                      || scr.y < (coord_t) 0
                      || scr.y > (coord_t)m_area.height - 1))
                continue;

              rgbdata l = render->get_unadjusted_rgb_pixel ({ x, y });
              rgbdata screen_color;
              if (!simulated_screen)
                screen_color = screen->noninterpolated_mult (scr);
              else
                screen_color = simulated_screen->get_pixel (x, y);
              if (screen_color.red > collection_threshold)
                {
                  data_entry e = GEOMETRY::red_scr_to_entry (scr);
                  if (!GEOMETRY::check_range
                      || (e.x >= 0
                          && e.x < m_area.width * GEOMETRY::red_width_scale
                          && e.y >= 0
                          && e.y < m_area.height
                                       * GEOMETRY::red_height_scale))
                    {
                      if constexpr (debug)
                        assert (e.x >= 0 && e.y >= 0
                                && e.x < m_area.width
                                             * GEOMETRY::red_width_scale
                                && e.y < m_area.height
                                             * GEOMETRY::red_height_scale);
                      luminosity_t val
                          = (screen_color.red - collection_threshold);
                      int idx = e.y * m_area.width * GEOMETRY::red_width_scale
                                + e.x;
                      rgbdata vall = l * val;
                      rgbdata &c = m_rgb_red[idx];
#pragma omp atomic
                      c.red += vall.red;
#pragma omp atomic
                      c.green += vall.green;
#pragma omp atomic
                      c.blue += vall.blue;
                      luminosity_t &w = w_red[idx];
#pragma omp atomic
                      w += val;
                    }
                }
              if (screen_color.green > collection_threshold)
                {
                  data_entry e = GEOMETRY::green_scr_to_entry (scr);
                  if (!GEOMETRY::check_range
                      || (e.x >= 0
                          && e.x < m_area.width * GEOMETRY::green_width_scale
                          && e.y >= 0
                          && e.y < m_area.height
                                       * GEOMETRY::green_height_scale))
                    {
                      if constexpr (debug)
                        assert (e.x >= 0 && e.y >= 0
                                && e.x < m_area.width
                                             * GEOMETRY::green_width_scale
                                && e.y < m_area.height
                                             * GEOMETRY::green_height_scale);
                      luminosity_t val
                          = (screen_color.green - collection_threshold);
                      int idx
                          = e.y * m_area.width * GEOMETRY::green_width_scale
                            + e.x;
                      rgbdata vall = l * val;
                      rgbdata &c = m_rgb_green[idx];
#pragma omp atomic
                      c.red += vall.red;
#pragma omp atomic
                      c.green += vall.green;
#pragma omp atomic
                      c.blue += vall.blue;
                      luminosity_t &w = w_green[idx];
#pragma omp atomic
                      w += val;
                    }
                }
              if (screen_color.blue > collection_threshold)
                {
                  data_entry e = GEOMETRY::blue_scr_to_entry (scr);
                  if (!GEOMETRY::check_range
                      || (e.x >= 0
                          && e.x < m_area.width * GEOMETRY::blue_width_scale
                          && e.y >= 0
                          && e.y < m_area.height
                                       * GEOMETRY::blue_height_scale))
                    {
                      if constexpr (debug)
                        assert (e.x >= 0 && e.y >= 0
                                && e.x < m_area.width
                                             * GEOMETRY::blue_width_scale
                                && e.y < m_area.height
                                             * GEOMETRY::blue_height_scale);
                      luminosity_t val
                          = (screen_color.blue - collection_threshold);
                      int idx
                          = e.y * m_area.width * GEOMETRY::blue_width_scale
                            + e.x;
                      rgbdata vall = l * val;
                      rgbdata &c = m_rgb_blue[idx];
#pragma omp atomic
                      c.red += vall.red;
#pragma omp atomic
                      c.green += vall.green;
#pragma omp atomic
                      c.blue += vall.blue;
                      luminosity_t &w = w_blue[idx];
#pragma omp atomic
                      w += val;
                    }
                }
            }
        if (progress)
          progress->inc_progress ();
      }
    if (!progress || !progress->cancel_requested ())
      {
#pragma omp for nowait
        for (int y = 0; y < m_area.height; y++)
          {
            if (!progress || !progress->cancel_requested ())
              for (int yy = 0; yy < GEOMETRY::red_height_scale; yy++)
                for (int x = 0; x < m_area.width * GEOMETRY::red_width_scale;
                     x++)
                  {
                    data_entry e = { x, y * GEOMETRY::red_height_scale + yy };
                    int idx = e.y * m_area.width * GEOMETRY::red_width_scale
                              + e.x;
                    if (w_red[idx] != (luminosity_t) 0)
                      m_rgb_red[idx] /= w_red[idx];
                    else
                      {
                        point_t scr = GEOMETRY::red_entry_to_scr (e);
                        m_rgb_red[idx]
                            = render->get_unadjusted_rgb_pixel_scr (
                                { scr.x - (coord_t)m_area.xshift (),
                                  scr.y - (coord_t)m_area.yshift () });
                      }
                  }
            if (progress)
              progress->inc_progress ();
          }
#pragma omp for nowait
        for (int y = 0; y < m_area.height; y++)
          {
            if (!progress || !progress->cancel_requested ())
              for (int yy = 0; yy < GEOMETRY::green_height_scale; yy++)
                for (int x = 0;
                     x < m_area.width * GEOMETRY::green_width_scale; x++)
                  {
                    data_entry e
                        = { x, y * GEOMETRY::green_height_scale + yy };
                    int idx = e.y * m_area.width * GEOMETRY::green_width_scale
                              + e.x;
                    if (w_green[idx] != (luminosity_t) 0)
                      m_rgb_green[idx] /= w_green[idx];
                    else
                      {
                        point_t scr = GEOMETRY::green_entry_to_scr (e);
                        m_rgb_green[idx]
                            = render->get_unadjusted_rgb_pixel_scr (
                                { scr.x - (coord_t)m_area.xshift (),
                                  scr.y - (coord_t)m_area.yshift () });
                      }
                  }
            if (progress)
              progress->inc_progress ();
          }
#pragma omp for nowait
        for (int y = 0; y < m_area.height; y++)
          {
            if (!progress || !progress->cancel_requested ())
              for (int yy = 0; yy < GEOMETRY::blue_height_scale; yy++)
                for (int x = 0; x < m_area.width * GEOMETRY::blue_width_scale;
                     x++)
                  {
                    data_entry e = { x, y * GEOMETRY::blue_height_scale + yy };
                    int idx = e.y * m_area.width * GEOMETRY::blue_width_scale
                              + e.x;
                    if (w_blue[idx] != (luminosity_t) 0)
                      m_rgb_blue[idx] /= w_blue[idx];
                    else
                      {
                        point_t scr = GEOMETRY::blue_entry_to_scr (e);
                        m_rgb_blue[idx]
                            = render->get_unadjusted_rgb_pixel_scr (
                                { scr.x - (coord_t)m_area.xshift (),
                                  scr.y - (coord_t)m_area.yshift () });
                      }
                  }
            if (progress)
              progress->inc_progress ();
          }
      }
  }
  return !progress || !progress->cancelled ();
}
/* Collect data for deinterlaced rendering in original or profiled colors.
   TODO: This does not make much sense.  We should apply profile first or
   collect in RGB colors.

   SCR_TO_IMG is the map from image to screen.
   RENDER is the renderer.
   W_RED, W_GREEN, W_BLUE are the weights for the channels.
   AREA is the region to analyze.
   PROGRESS is the progress info object.  */
template <typename GEOMETRY>
bool
analyze_base_worker<GEOMETRY>::analyze_color (
    scr_to_img *scr_to_img, render_to_scr *render, luminosity_t *w_red,
    luminosity_t *w_green, luminosity_t *w_blue, int_image_area area,
    progress_info *progress)
{
  luminosity_t weights[256];
  luminosity_t half_weights[256];
  int size = (openmp_min_size + area.width - 1) / area.width;
  int size2 = (openmp_min_size + m_area.width - 1) / m_area.width;

  /* FIXME: technically not right for paget where diagonal coordinates are not
     of same size as normal ones.  */
  coord_t pixel_size = render->pixel_size ();
  coord_t left = (coord_t) 128 - (pixel_size / (coord_t) 2) * (coord_t) 256;
  coord_t right = (coord_t) 128 + (pixel_size / (coord_t) 2) * (coord_t) 256;
  coord_t half_left = (coord_t) 128 - (pixel_size / (coord_t) 4) * (coord_t) 256;
  coord_t half_right = (coord_t) 128 + (pixel_size / (coord_t) 4) * (coord_t) 256;

  for (int i = 0; i < 256; i++)
    {
      if ((coord_t)i <= left)
        weights[i] = (luminosity_t) 0;
      else if ((coord_t)i >= right)
        weights[i] = (luminosity_t)1;
      else
        weights[i]
            = (luminosity_t)((coord_t)i - left) / (luminosity_t)(right - left);

      if ((coord_t)i <= half_left)
        half_weights[i] = (luminosity_t) 0;
      else if ((coord_t)i >= half_right)
        half_weights[i] = (luminosity_t)1;
      else
        half_weights[i] = (luminosity_t)((coord_t)i - half_left)
                          / (luminosity_t)(half_right - half_left);
    }
#pragma omp parallel shared(                                                  \
        progress, render, scr_to_img, w_blue, w_red, w_green, area,           \
            half_weights,                                                     \
            weights) default(none) if (area.height > size                     \
                                           || this->m_area.height > size2)
  {
#pragma omp for
    for (int y = area.y; y < area.y + area.height; y++)
      {
        int64_t red_minx = -2, red_miny = -2, green_minx = -2, green_miny = -2,
                blue_minx = -2, blue_miny = -2;
        int64_t red_maxx = 2, red_maxy = 2, green_maxx = 2, green_maxy = 2,
                blue_maxx = 2, blue_maxy = 2;
        if (!progress || !progress->cancel_requested ())
          for (int x = area.x; x < area.x + area.width; x++)
            {
              point_t scr = scr_to_img->to_scr (
                  { x + (coord_t) 0.5, y + (coord_t) 0.5 });
              scr += { (coord_t)m_area.xshift (), (coord_t)m_area.yshift () };
              if (!GEOMETRY::check_range
                  && (scr.x < (coord_t) 0 || scr.x > (coord_t)m_area.width - 1
                      || scr.y < (coord_t) 0
                      || scr.y > (coord_t)m_area.height - 1))
                continue;
              rgbdata d = render->get_unadjusted_rgb_pixel ({ x, y });

              point_t off;
              analyze_base::data_entry e
                  = GEOMETRY::red_scr_to_entry (scr, &off);
              if (e.x + red_minx >= 0
                  && e.x + red_maxx < m_area.width * GEOMETRY::red_width_scale
                  && e.y + red_miny >= 0
                  && e.y + red_maxy
                         < m_area.height * GEOMETRY::red_height_scale)
                {
                  off.x = half_weights[(int)(off.x * (coord_t)255.5)];
                  off.y = weights[(int)(off.y * (coord_t)255.5)];
                  luminosity_t &l1
                      = w_red[(e.y) * m_area.width * GEOMETRY::red_width_scale
                              + e.x];
                  luminosity_t &v1
                      = m_red[(e.y) * m_area.width
                                        * GEOMETRY::red_width_scale
                                    + e.x];
                  luminosity_t val1
                      = ((luminosity_t)1 - (luminosity_t)off.x)
                        * ((luminosity_t)1 - (luminosity_t)off.y);
#pragma omp atomic
                  l1 += val1;
#pragma omp atomic
                  v1 += d.red * val1;
                  data_entry o
                      = GEOMETRY::offset_for_interpolation_red (e, { 1, 0 });
                  luminosity_t &l2
                      = w_red[o.y * m_area.width * GEOMETRY::red_width_scale
                              + o.x];
                  luminosity_t &v2
                      = m_red[o.y * m_area.width
                                        * GEOMETRY::red_width_scale
                                    + o.x];
                  luminosity_t val2
                      = ((luminosity_t)off.x)
                        * ((luminosity_t)1 - (luminosity_t)off.y);
#pragma omp atomic
                  l2 += val2;
#pragma omp atomic
                  v2 += d.red * val2;
                  o = GEOMETRY::offset_for_interpolation_red (e, { 0, 1 });
                  luminosity_t &l3
                      = w_red[o.y * m_area.width * GEOMETRY::red_width_scale
                              + o.x];
                  luminosity_t &v3
                      = m_red[o.y * m_area.width
                                        * GEOMETRY::red_width_scale
                                    + o.x];
                  luminosity_t val3 = ((luminosity_t)1 - (luminosity_t)off.x)
                                      * ((luminosity_t)off.y);
#pragma omp atomic
                  l3 += val3;
#pragma omp atomic
                  v3 += d.red * val3;
                  o = GEOMETRY::offset_for_interpolation_red (e, { 1, 1 });
                  luminosity_t &l4
                      = w_red[o.y * m_area.width * GEOMETRY::red_width_scale
                              + o.x];
                  luminosity_t &v4
                      = m_red[o.y * m_area.width
                                        * GEOMETRY::red_width_scale
                                    + o.x];
                  luminosity_t val4
                      = ((luminosity_t)off.x) * ((luminosity_t)off.y);
#pragma omp atomic
                  l4 += val4;
#pragma omp atomic
                  v4 += d.red * val4;
                }
              e = GEOMETRY::green_scr_to_entry (scr, &off);
              if (e.x + green_minx >= 0
                  && e.x + green_maxx
                         < m_area.width * GEOMETRY::green_width_scale
                  && e.y + green_miny >= 0
                  && e.y + green_maxy
                         < m_area.height * GEOMETRY::green_height_scale)
                {
                  off.x = half_weights[(int)(off.x * (coord_t)255.5)];
                  off.y = weights[(int)(off.y * (coord_t)255.5)];
                  luminosity_t &l1 = w_green[(e.y) * m_area.width
                                                 * GEOMETRY::green_width_scale
                                             + e.x];
                  luminosity_t &v1
                      = m_green[(e.y) * m_area.width
                                          * GEOMETRY::green_width_scale
                                      + e.x];
                  luminosity_t val1
                      = ((luminosity_t)1 - (luminosity_t)off.x)
                        * ((luminosity_t)1 - (luminosity_t)off.y);
#pragma omp atomic
                  l1 += val1;
#pragma omp atomic
                  v1 += d.green * val1;
                  data_entry o
                      = GEOMETRY::offset_for_interpolation_green (e, { 1, 0 });
                  luminosity_t &l2 = w_green[o.y * m_area.width
                                                 * GEOMETRY::green_width_scale
                                             + o.x];
                  luminosity_t &v2
                      = m_green[o.y * m_area.width
                                          * GEOMETRY::green_width_scale
                                      + o.x];
                  luminosity_t val2
                      = ((luminosity_t)off.x)
                        * ((luminosity_t)1 - (luminosity_t)off.y);
#pragma omp atomic
                  l2 += val2;
#pragma omp atomic
                  v2 += d.green * val2;
                  o = GEOMETRY::offset_for_interpolation_green (e, { 0, 1 });
                  luminosity_t &l3 = w_green[o.y * m_area.width
                                                 * GEOMETRY::green_width_scale
                                             + o.x];
                  luminosity_t &v3
                      = m_green[o.y * m_area.width
                                          * GEOMETRY::green_width_scale
                                      + o.x];
                  luminosity_t val3 = ((luminosity_t)1 - (luminosity_t)off.x)
                                      * ((luminosity_t)off.y);
#pragma omp atomic
                  l3 += val3;
#pragma omp atomic
                  v3 += d.green * val3;
                  o = GEOMETRY::offset_for_interpolation_green (e, { 1, 1 });
                  luminosity_t &l4 = w_green[o.y * m_area.width
                                                 * GEOMETRY::green_width_scale
                                             + o.x];
                  luminosity_t &v4
                      = m_green[o.y * m_area.width
                                          * GEOMETRY::green_width_scale
                                      + o.x];
                  luminosity_t val4
                      = ((luminosity_t)off.x) * ((luminosity_t)off.y);
#pragma omp atomic
                  l4 += val4;
#pragma omp atomic
                  v4 += d.green * val4;
                }
              e = GEOMETRY::blue_scr_to_entry (scr, &off);
              if (e.x + blue_minx >= 0
                  && e.x + blue_maxx
                         < m_area.width * GEOMETRY::blue_width_scale
                  && e.y + blue_miny >= 0
                  && e.y + blue_maxy
                         < m_area.height * GEOMETRY::blue_height_scale)
                {
                  off.x = half_weights[(int)(off.x * (coord_t)255.5)];
                  off.y = weights[(int)(off.y * (coord_t)255.5)];
                  luminosity_t &l1 = w_blue[(e.y) * m_area.width
                                                * GEOMETRY::blue_width_scale
                                            + e.x];
                  luminosity_t &v1
                      = m_blue[(e.y) * m_area.width
                                         * GEOMETRY::blue_width_scale
                                     + e.x];
                  luminosity_t val1
                      = ((luminosity_t)1 - (luminosity_t)off.x)
                        * ((luminosity_t)1 - (luminosity_t)off.y);
#pragma omp atomic
                  l1 += val1;
#pragma omp atomic
                  v1 += d.blue * val1;
                  data_entry o
                      = GEOMETRY::offset_for_interpolation_blue (e, { 1, 0 });
                  luminosity_t &l2
                      = w_blue[o.y * m_area.width * GEOMETRY::blue_width_scale
                               + o.x];
                  luminosity_t &v2
                      = m_blue[o.y * m_area.width
                                         * GEOMETRY::blue_width_scale
                                     + o.x];
                  luminosity_t val2
                      = ((luminosity_t)off.x)
                        * ((luminosity_t)1 - (luminosity_t)off.y);
#pragma omp atomic
                  l2 += val2;
#pragma omp atomic
                  v2 += d.blue * val2;
                  o = GEOMETRY::offset_for_interpolation_blue (e, { 0, 1 });
                  luminosity_t &l3
                      = w_blue[o.y * m_area.width * GEOMETRY::blue_width_scale
                               + o.x];
                  luminosity_t &v3
                      = m_blue[o.y * m_area.width
                                         * GEOMETRY::blue_width_scale
                                     + o.x];
                  luminosity_t val3 = ((luminosity_t)1 - (luminosity_t)off.x)
                                      * ((luminosity_t)off.y);
#pragma omp atomic
                  l3 += val3;
#pragma omp atomic
                  v3 += d.blue * val3;
                  o = GEOMETRY::offset_for_interpolation_blue (e, { 1, 1 });
                  luminosity_t &l4
                      = w_blue[o.y * m_area.width * GEOMETRY::blue_width_scale
                               + o.x];
                  luminosity_t &v4
                      = m_blue[o.y * m_area.width
                                         * GEOMETRY::blue_width_scale
                                     + o.x];
                  luminosity_t val4
                      = ((luminosity_t)off.x) * ((luminosity_t)off.y);
#pragma omp atomic
                  l4 += val4;
#pragma omp atomic
                  v4 += d.blue * val4;
                }
            }
        if (progress)
          progress->inc_progress ();
      }
    if (!progress || !progress->cancel_requested ())
      {
#pragma omp for nowait
        for (int y = 0; y < m_area.height; y++)
          {
            if (!progress || !progress->cancel_requested ())
              for (int yy = 0; yy < GEOMETRY::red_height_scale; yy++)
                for (int x = 0; x < m_area.width * GEOMETRY::red_width_scale;
                     x++)
                  {
                    data_entry e = { x, y * GEOMETRY::red_height_scale + yy };
                    int idx = e.y * m_area.width * GEOMETRY::red_width_scale
                              + e.x;
                    if (w_red[idx] != (luminosity_t) 0)
                      m_red[idx] /= w_red[idx];
                  }
            if (progress)
              progress->inc_progress ();
          }
#pragma omp for nowait
        for (int y = 0; y < m_area.height; y++)
          {
            if (!progress || !progress->cancel_requested ())
              for (int yy = 0; yy < GEOMETRY::green_height_scale; yy++)
                for (int x = 0;
                     x < m_area.width * GEOMETRY::green_width_scale; x++)
                  {
                    data_entry e
                        = { x, y * GEOMETRY::green_height_scale + yy };
                    int idx = e.y * m_area.width * GEOMETRY::green_width_scale
                              + e.x;
                    if (w_green[idx] != (luminosity_t) 0)
                      m_green[idx] /= w_green[idx];
                  }
            if (progress)
              progress->inc_progress ();
          }
#pragma omp for nowait
        for (int y = 0; y < m_area.height; y++)
          {
            if (!progress || !progress->cancel_requested ())
              for (int yy = 0; yy < GEOMETRY::blue_height_scale; yy++)
                for (int x = 0; x < m_area.width * GEOMETRY::blue_width_scale;
                     x++)
                  {
                    data_entry e = { x, y * GEOMETRY::blue_height_scale + yy };
                    int idx = e.y * m_area.width * GEOMETRY::blue_width_scale
                              + e.x;
                    if (w_blue[idx] != (luminosity_t) 0)
                      m_blue[idx] /= w_blue[idx];
                  }
            if (progress)
              progress->inc_progress ();
          }
      }
  }
  return !progress || !progress->cancelled ();
}

/* Fast data collection from centers of individual patches.

   RENDER is the renderer.
   PROGRESS is the progress info object.  */
template <typename GEOMETRY>
bool
analyze_base_worker<GEOMETRY>::analyze_fast (render_to_scr *render,
                                             progress_info *progress)
{
  int size2 = (openmp_min_size + m_area.width - 1) / m_area.width;
#pragma omp parallel for default(none)                                        \
    shared(progress, render, size2) if (m_area.height > size2)
  for (int y = 0; y < m_area.height; y++)
    {
      if (!progress || !progress->cancel_requested ())
        for (int x = 0; x < m_area.width; x++)
          {
            for (int yy = 0; yy < GEOMETRY::red_height_scale; yy++)
              for (int xx = 0; xx < GEOMETRY::red_width_scale; xx++)
                {
                  data_entry e = { x * GEOMETRY::red_width_scale + xx,
                                   y * GEOMETRY::red_height_scale + yy };
                  int idx
                      = e.y * m_area.width * GEOMETRY::red_width_scale + e.x;
                  point_t scr = GEOMETRY::red_entry_to_scr (e);
                  m_red[idx] = render->get_unadjusted_img_pixel_scr (
                      { scr.x - (coord_t)m_area.xshift (),
                        scr.y - (coord_t)m_area.yshift () });
                }
            for (int yy = 0; yy < GEOMETRY::green_height_scale; yy++)
              for (int xx = 0; xx < GEOMETRY::green_width_scale; xx++)
                {
                  data_entry e = { x * GEOMETRY::green_width_scale + xx,
                                   y * GEOMETRY::green_height_scale + yy };
                  int idx = e.y * m_area.width * GEOMETRY::green_width_scale
                            + e.x;
                  point_t scr = GEOMETRY::green_entry_to_scr (e);
                  m_green[idx] = render->get_unadjusted_img_pixel_scr (
                      { scr.x - (coord_t)m_area.xshift (),
                        scr.y - (coord_t)m_area.yshift () });
                }
            for (int yy = 0; yy < GEOMETRY::blue_height_scale; yy++)
              for (int xx = 0; xx < GEOMETRY::blue_width_scale; xx++)
                {
                  data_entry e = { x * GEOMETRY::blue_width_scale + xx,
                                   y * GEOMETRY::blue_height_scale + yy };
                  int idx
                      = e.y * m_area.width * GEOMETRY::blue_width_scale + e.x;
                  point_t scr = GEOMETRY::blue_entry_to_scr (e);
                  m_blue[idx] = render->get_unadjusted_img_pixel_scr (
                      { scr.x - (coord_t)m_area.xshift (),
                        scr.y - (coord_t)m_area.yshift () });
                }
          }
      if (progress)
        progress->inc_progress ();
    }
  return !progress || !progress->cancelled ();
}

/* Main entry point for screen analysis.

   RENDER is the renderer.
   IMG is the image data.
   SCR_TO_IMG is the map from image to screen.
   SCREEN is the screen geometry.
   SIMULATED_SCR is the simulated screen if any.
   WIDTH, HEIGHT are the dimensions.
   XSHIFT, YSHIFT are the shifts.
   MODE is the analysis mode.
   COLLECTION_THRESHOLD is the threshold for luminosity collection.
   PROGRESS is the progress info object.  */
template <typename GEOMETRY>
bool
analyze_base_worker<GEOMETRY>::analyze (
    render_to_scr *render, const image_data *img, scr_to_img *scr_to_img,
    const screen *screen, const simulated_screen *simulated_scr, int_image_area area,
    mode mode, luminosity_t collection_threshold, progress_info *progress)
{
  assert (!m_red);
  m_area = area;
  /* G B .
     R R .
     . . .  */
  if (mode != precise_rgb)
    {
      m_red = std::make_unique<luminosity_t[]> (
          (size_t)m_area.width * m_area.height
          * (GEOMETRY::red_width_scale * GEOMETRY::red_height_scale));
      m_green = std::make_unique<luminosity_t[]> (
          (size_t)m_area.width * m_area.height
          * (GEOMETRY::green_width_scale * GEOMETRY::green_height_scale));
      m_blue = std::make_unique<luminosity_t[]> (
          (size_t)m_area.width * m_area.height
          * (GEOMETRY::blue_width_scale * GEOMETRY::blue_height_scale));
      if (!m_red || !m_green || !m_blue)
        return false;
      std::fill (m_red.get (),
                 m_red.get () + (size_t) m_area.width * m_area.height
                                    * (GEOMETRY::red_width_scale
                                       * GEOMETRY::red_height_scale),
                 (luminosity_t) 0);
      std::fill (m_green.get (),
                 m_green.get () + (size_t) m_area.width * m_area.height
                                      * (GEOMETRY::green_width_scale
                                         * GEOMETRY::green_height_scale),
                 (luminosity_t) 0);
      std::fill (m_blue.get (),
                 m_blue.get () + (size_t) m_area.width * m_area.height
                                     * (GEOMETRY::blue_width_scale
                                        * GEOMETRY::blue_height_scale),
                 (luminosity_t) 0);
    }
  else
    {
      m_rgb_red = std::make_unique<rgbdata[]> (
          (size_t) m_area.width * m_area.height
          * (GEOMETRY::red_width_scale * GEOMETRY::red_height_scale));
      m_rgb_green = std::make_unique<rgbdata[]> (
          (size_t) m_area.width * m_area.height
          * (GEOMETRY::green_width_scale * GEOMETRY::green_height_scale));
      m_rgb_blue = std::make_unique<rgbdata[]> (
          (size_t) m_area.width * m_area.height
          * (GEOMETRY::blue_width_scale * GEOMETRY::blue_height_scale));
      if (!m_rgb_red || !m_rgb_green || !m_rgb_blue)
        return false;
      std::fill (m_rgb_red.get (),
                 m_rgb_red.get () + (size_t) m_area.width * m_area.height
                                        * (GEOMETRY::red_width_scale
                                           * GEOMETRY::red_height_scale),
                 rgbdata{ 0, 0, 0 });
      std::fill (m_rgb_green.get (),
                 m_rgb_green.get () + (size_t) m_area.width * m_area.height
                                          * (GEOMETRY::green_width_scale
                                             * GEOMETRY::green_height_scale),
                 rgbdata{ 0, 0, 0 });
      std::fill (m_rgb_blue.get (),
                 m_rgb_blue.get () + (size_t) m_area.width * m_area.height
                                         * (GEOMETRY::blue_width_scale
                                            * GEOMETRY::blue_height_scale),
                 rgbdata{ 0, 0, 0 });
    }
  bool ok = false;
  if (mode == precise || mode == precise_rgb || mode == color)
    {
      std::unique_ptr<luminosity_t[]> w_red_ptr = std::make_unique<
          luminosity_t[]> ((size_t) m_area.width * m_area.height
                           * (GEOMETRY::red_width_scale
                              * GEOMETRY::red_height_scale));
      std::unique_ptr<luminosity_t[]> w_green_ptr = std::make_unique<
          luminosity_t[]> ((size_t) m_area.width * m_area.height
                           * (GEOMETRY::green_width_scale
                              * GEOMETRY::green_height_scale));
      std::unique_ptr<luminosity_t[]> w_blue_ptr = std::make_unique<
          luminosity_t[]> ((size_t) m_area.width * m_area.height
                           * (GEOMETRY::blue_width_scale
                              * GEOMETRY::blue_height_scale));
      if (!w_red_ptr || !w_green_ptr || !w_blue_ptr)
        return false;

      luminosity_t *w_red = w_red_ptr.get ();
      luminosity_t *w_green = w_green_ptr.get ();
      luminosity_t *w_blue = w_blue_ptr.get ();

      std::fill (w_red,
                 w_red + (size_t) m_area.width * m_area.height
                             * (GEOMETRY::red_width_scale
                                * GEOMETRY::red_height_scale),
                 (luminosity_t) 0);
      std::fill (w_green,
                 w_green + (size_t) m_area.width * m_area.height
                               * (GEOMETRY::green_width_scale
                                  * GEOMETRY::green_height_scale),
                 (luminosity_t) 0);
      std::fill (w_blue,
                 w_blue + (size_t) m_area.width * m_area.height
                              * (GEOMETRY::blue_width_scale
                                 * GEOMETRY::blue_height_scale),
                 (luminosity_t) 0);

      int_image_area img_area = scr_to_img->get_img_range (area).intersect ({ 0, 0, img->width, img->height });
      if (img_area.empty_p ())
        return true;

      if (progress)
        {
          if (mode == precise)
            progress->set_task ("determining intensities of color screen "
                                "patches (precise mode)",
                                img_area.height + m_area.height * 3);
          else if (mode == color)
            progress->set_task ("determining intensities of color screen "
                                "patches (original color mode)",
                                img_area.height + m_area.height * 3);
          else
            progress->set_task ("determining intensities of color screen "
                                "patches (precise rgb mode)",
                                img_area.height + m_area.height * 3);
        }

      if (mode == precise)
        ok = analyze_precise (scr_to_img, render, screen, simulated_scr,
                               collection_threshold, w_red, w_green, w_blue,
                               img_area, progress);
      else if (mode == precise_rgb)
        ok = analyze_precise_rgb (scr_to_img, render, screen,
                                   simulated_scr, collection_threshold,
                                   w_red, w_green, w_blue, img_area, progress);
      else
        ok = analyze_color (scr_to_img, render, w_red, w_green, w_blue,
                            img_area, progress);
    }
  else
    {
      if (progress)
        progress->set_task (
            "determining intensities of color screen patches (fast mode)",
            m_area.height);
      ok = analyze_fast (render, progress);
    }
  return ok && (!progress || !progress->cancelled ());
}
} // namespace colorscreen
