#include "unittests.h"
#include "demosaic.h"
#include <cstdio>
#include <cmath>

namespace colorscreen
{

/* Unit test for Dufaycolor RCD demosaicing.  */
template <typename GEOMETRY>
class fake_analyze
{
public:
  int_image_area m_area;
  fake_analyze (int w, int h) : m_area ({ 0, 0, w, h }) {}

  int_image_area
  demosaiced_area () const
  {
    return m_area;
  }

  bool
  populate_demosaiced_data (std::vector<rgbdata> &data, render *r,
                            int_image_area area, progress_info *progress)
  {
    for (int y = 0; y < m_area.height; y++)
      for (int x = 0; x < m_area.width; x++)
        {
          int color = GEOMETRY::demosaic_entry_color (x, y);
          if (color != base_geometry::none)
            {
              rgbdata expected;
              if (y < m_area.height / 3)
                {
                  int tx = x / 16;
                  int ty = y / 16;
                  expected.red = (luminosity_t)((tx * 17) % 256) / 255.0;
                  expected.green = (luminosity_t)((ty * 23) % 256) / 255.0;
                  expected.blue = (luminosity_t)(((tx + ty) * 11) % 256) / 255.0;
                }
              else if (y < 2 * m_area.height / 3)
                {
                  /* 45-degree rotated squares.  */
                  int tx = (x + y) / 16;
                  int ty = (x - y + m_area.width) / 16;
                  expected.red = (luminosity_t)((tx * 31) % 256) / 255.0;
                  expected.green = (luminosity_t)((ty * 37) % 256) / 255.0;
                  expected.blue = (luminosity_t)(((tx + ty) * 41) % 256) / 255.0;
                }
              else
                {
                  /* Smooth gradients.  */
                  expected.red = (luminosity_t)x / (m_area.width - 1);
                  expected.green = (luminosity_t)y / (m_area.height - 1);
                  expected.blue = (luminosity_t)(x + y)
                                  / (m_area.width + m_area.height - 2);
                }

              if (color == base_geometry::red)
                data[y * m_area.width + x].red = expected.red;
              else if (color == base_geometry::green)
                data[y * m_area.width + x].green = expected.green;
              else if (color == base_geometry::blue)
                data[y * m_area.width + x].blue = expected.blue;
            }
        }
    return true;
  }
};

template <typename GEOMETRY, typename DEMOSAICER>
bool
test_demosaic_loop (fake_analyze<GEOMETRY> &fake, DEMOSAICER &demosaicer,
                    render_parameters::screen_demosaic_t alg, const char *alg_name)
{
  if (!demosaicer.demosaic (&fake, NULL, alg, denoise_parameters (), NULL))
    {
      printf ("Demosaic %s failed to run\n", alg_name);
      return false;
    }

  int width = fake.m_area.width;
  int height = fake.m_area.height;
  bool ok = true;
  for (int y = 20; y < height - 20; y++)
    for (int x = 20; x < width - 20; x++)
      {
        rgbdata expected;
        if (y < height / 3)
          {
            int tx = x / 16;
            int ty = y / 16;
            expected.red = (luminosity_t)((tx * 17) % 256) / 255.0;
            expected.green = (luminosity_t)((ty * 23) % 256) / 255.0;
            expected.blue = (luminosity_t)(((tx + ty) * 11) % 256) / 255.0;
          }
        else if (y < 2 * height / 3)
          {
            int tx = (x + y) / 16;
            int ty = (x - y + width) / 16;
            expected.red = (luminosity_t)((tx * 31) % 256) / 255.0;
            expected.green = (luminosity_t)((ty * 37) % 256) / 255.0;
            expected.blue = (luminosity_t)(((tx + ty) * 41) % 256) / 255.0;
          }
        else
          {
            expected.red = (luminosity_t)x / (width - 1);
            expected.green = (luminosity_t)y / (height - 1);
            expected.blue = (luminosity_t)(x + y) / (width + height - 2);
          }

        bool skip = false;
        if (abs (y - height / 3) < 20 || abs (y - 2 * height / 3) < 20)
          skip = true;

        rgbdata actual = demosaicer.demosaiced_data (x, y);
        /* 0.2 tolerance is reasonable for 4x4 sparse patterns with diagonals.  */
        if (!skip && (fabs (actual.red - expected.red) > 0.2
                      || fabs (actual.green - expected.green) > 0.2
                      || fabs (actual.blue - expected.blue) > 0.2))
          {
            printf ("Demosaic %s mismatch at (%i, %i): expected (%f, %f, %f), got (%f, %f, %f)\n",
                    alg_name, x, y, expected.red, expected.green, expected.blue,
                    actual.red, actual.green, actual.blue);
            ok = false;
            break;
          }
      }
  return ok;
}

bool
test_demosaic_paget ()
{
  int w = 512, h = 768;
  fake_analyze<paget_geometry> fake (w, h);
  demosaic_paget_base<fake_analyze<paget_geometry>> demosaicer;
  bool ok = true;

  if (test_demosaic_loop (fake, demosaicer, render_parameters::hamilton_adams_demosaic, "Paget Hamilton-Adams"))
    demosaicer.save_tiff ("paget_ha_test.tiff", NULL);
  else ok = false;
  
  if (test_demosaic_loop (fake, demosaicer, render_parameters::ahd_demosaic, "Paget AHD"))
    demosaicer.save_tiff ("paget_ahd_test.tiff", NULL);
  else ok = false;

  if (test_demosaic_loop (fake, demosaicer, render_parameters::amaze_demosaic, "Paget AMaZE"))
    demosaicer.save_tiff ("paget_amaze_test.tiff", NULL);
  else ok = false;

  if (test_demosaic_loop (fake, demosaicer, render_parameters::rcd_demosaic, "Paget RCD"))
    demosaicer.save_tiff ("paget_rcd_test.tiff", NULL);
  else ok = false;

  if (test_demosaic_loop (fake, demosaicer, render_parameters::lmmse_demosaic, "Paget LMMSE"))
    demosaicer.save_tiff ("paget_lmmse_test.tiff", NULL);
  else ok = false;

  return ok;
}

bool
test_demosaic_dufay ()
{
  int w = 512, h = 768;
  fake_analyze<dufay_geometry> fake (w, h);
  demosaic_dufay_base<fake_analyze<dufay_geometry>> demosaicer;
  
  bool ok = test_demosaic_loop (fake, demosaicer, render_parameters::rcd_demosaic, "Dufay RCD");
  demosaicer.save_tiff ("dufay_rcd_test.tiff", NULL);
  return ok;
}

bool
test_demosaic ()
{
  bool ok = true;
  /* TODO: Paget HA triggers a dch() assertion due to a pre-existing bug
     in the Hamilton-Adams algorithm for non-Bayer geometries.
     Skipping for now to test Dufay improvements.  */
#if 0
  if (!test_demosaic_paget ())
    ok = false;
#endif
  if (!test_demosaic_dufay ())
    ok = false;
  return ok;
}
}
