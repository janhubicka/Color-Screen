#ifndef SIMULATE_H
#define SIMULATE_H
#include "include/color.h"
#include "include/render-parameters.h"
#include "include/scr-detect-parameters.h"
#include "include/scr-to-img.h"
#include "screen.h"

namespace colorscreen
{
// typedef mem_rgbdata simulated_screen_pixel;
typedef rgbdata simulated_screen_pixel;

struct simulated_screen
{
  simulated_screen (int width, int height)
      : m_data (width * height), m_width (width), m_height (height)
  {
  }

  rgbdata
  get_pixel (int x, int y) const
  {
    return m_data[y * m_width + x];
  }

  void
  put_pixel (int x, int y, rgbdata color)
  {
    m_data[y * m_width + x] = color;
  }
  inline rgbdata get_interpolated_pixel (coord_t xp, coord_t yp);

  simulated_screen_pixel *data ()
  {
    return m_data.data ();
  }

protected:
  std::vector<simulated_screen_pixel> m_data;
  int m_width, m_height;
};
simulated_screen *get_simulated_screen (const scr_to_img_parameters &param,
                                        const screen *scr, uint64_t screen_id,
                                        const sharpen_parameters sharpen,
                                        int width, int height,
                                        progress_info *progress, uint64_t *id);
void release_simulated_screen (simulated_screen *s);

rgbdata
simulated_screen::get_interpolated_pixel (coord_t xp, coord_t yp)
{
  rgbdata val;

  /* Center of pixel [0,0] is [0.5,0.5].  */
  xp -= (coord_t)0.5;
  yp -= (coord_t)0.5;
  int sx, sy;
  coord_t rx = my_modf (xp, &sx);
  coord_t ry = my_modf (yp, &sy);

  if (sx >= 1 && sx < m_width - 2 && sy >= 1 && sy < m_height - 2)
    {
      vec_luminosity_t v1
          = { get_pixel (sx - 1, sy - 1).red, get_pixel (sx, sy - 1).red,
              get_pixel (sx + 1, sy - 1).red, get_pixel (sx + 2, sy - 1).red };
      vec_luminosity_t v2
          = { get_pixel (sx - 1, sy - 0).red, get_pixel (sx, sy - 0).red,
              get_pixel (sx + 1, sy - 0).red, get_pixel (sx + 2, sy - 0).red };
      vec_luminosity_t v3
          = { get_pixel (sx - 1, sy + 1).red, get_pixel (sx, sy + 1).red,
              get_pixel (sx + 1, sy + 1).red, get_pixel (sx + 2, sy + 1).red };
      vec_luminosity_t v4
          = { get_pixel (sx - 1, sy + 2).red, get_pixel (sx, sy + 2).red,
              get_pixel (sx + 1, sy + 2).red, get_pixel (sx + 2, sy + 2).red };
      vec_luminosity_t v = vec_cubic_interpolate (v1, v2, v3, v4, ry);
      val.red = cubic_interpolate (v[0], v[1], v[2], v[3], rx);

      vec_luminosity_t gv1
          = { get_pixel (sx - 1, sy - 1).green, get_pixel (sx, sy - 1).green,
              get_pixel (sx + 1, sy - 1).green,
              get_pixel (sx + 2, sy - 1).green };
      vec_luminosity_t gv2
          = { get_pixel (sx - 1, sy - 0).green, get_pixel (sx, sy - 0).green,
              get_pixel (sx + 1, sy - 0).green,
              get_pixel (sx + 2, sy - 0).green };
      vec_luminosity_t gv3
          = { get_pixel (sx - 1, sy + 1).green, get_pixel (sx, sy + 1).green,
              get_pixel (sx + 1, sy + 1).green,
              get_pixel (sx + 2, sy + 1).green };
      vec_luminosity_t gv4
          = { get_pixel (sx - 1, sy + 2).green, get_pixel (sx, sy + 2).green,
              get_pixel (sx + 1, sy + 2).green,
              get_pixel (sx + 2, sy + 2).green };
      v = vec_cubic_interpolate (gv1, gv2, gv3, gv4, ry);
      val.green = cubic_interpolate (v[0], v[1], v[2], v[3], rx);

      vec_luminosity_t bv1
          = { get_pixel (sx - 1, sy - 1).blue, get_pixel (sx, sy - 1).blue,
              get_pixel (sx + 1, sy - 1).blue,
              get_pixel (sx + 2, sy - 1).blue };
      vec_luminosity_t bv2
          = { get_pixel (sx - 1, sy - 0).blue, get_pixel (sx, sy - 0).blue,
              get_pixel (sx + 1, sy - 0).blue,
              get_pixel (sx + 2, sy - 0).blue };
      vec_luminosity_t bv3
          = { get_pixel (sx - 1, sy + 1).blue, get_pixel (sx, sy + 1).blue,
              get_pixel (sx + 1, sy + 1).blue,
              get_pixel (sx + 2, sy + 1).blue };
      vec_luminosity_t bv4
          = { get_pixel (sx - 1, sy + 2).blue, get_pixel (sx, sy + 2).blue,
              get_pixel (sx + 1, sy + 2).blue,
              get_pixel (sx + 2, sy + 2).blue };
      v = vec_cubic_interpolate (bv1, bv2, bv3, bv4, ry);
      val.blue = cubic_interpolate (v[0], v[1], v[2], v[3], rx);
    }
  return val;
}
}

#endif
