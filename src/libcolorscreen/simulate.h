/* Finite-image simulation of a periodic historical color screen.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#ifndef SIMULATE_H
#define SIMULATE_H
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <vector>

#include "include/color.h"
#include "include/render-parameters.h"
#include "include/scr-detect-parameters.h"
#include "include/scr-to-img.h"
#include "screen.h"
#include "lru-cache.h"
#include "cubic-interpolate.h"

namespace colorscreen
{
/* Sampling operation applied when a periodic screen is converted to discrete
   capture pixels.  */
enum class screen_sampling
{
  /* Sample at the pixel centre.  Use this when the pre-sampling transfer
     already contains the sensor pixel aperture.  */
  point_sample,
  /* Integrate transmission over the complete capture-pixel footprint.  Use
     this when the preceding transfer excludes the sensor aperture.  */
  integrate_pixel
};

/* Return the sampling policy for a periodic screen filtered according to
   SHARPEN.  FORWARD_MTF_APPLIED is true only when the selected capture MTF was
   actually applied to that screen.  */
inline pure_attr screen_sampling
screen_sampling_for_capture_transfer (const sharpen_parameters &sharpen,
                                      bool forward_mtf_applied)
{
  if (forward_mtf_applied
      && sharpen.scanner_mtf.includes_sensor_aperture_p ())
    return screen_sampling::point_sample;
  return screen_sampling::integrate_pixel;
}

/* Finite RGB image containing the simulated capture of a periodic SCREEN.  */
struct simulated_screen;

/* Pixel type used by SIMULATED_SCREEN.  Keep this alias separate from RGBDATA
   so the storage type can be changed without changing the public interface.  */
typedef rgbdata simulated_screen_pixel;

/* Complete cache key for a finite simulated screen image.

   SCREEN_ID identifies SCR.  MESH_TRANS_ID identifies the optional mesh; when
   it is zero PARAMS contains the complete analytical mapping.  WIDTH and
   HEIGHT are part of the result and therefore must participate in equality.
   SAMPLING records whether the pixel aperture is already included.  SHARPEN
   is compared exactly because this cache stores a complete image, not an
   interactive approximation.  */
struct simulated_screen_params
{
  /* Unique identifier of SCR.  */
  uint64_t screen_id;
  /* Unique identifier of PARAMS.mesh_trans, or zero for an analytical map.  */
  uint64_t mesh_trans_id;
  /* Dimensions of the simulated image in capture pixels.  */
  int width, height;
  /* Operation converting the periodic screen to discrete capture samples.  */
  screen_sampling sampling = screen_sampling::integrate_pixel;
  /* Mapping from periodic screen coordinates to capture pixels.  */
  scr_to_img_parameters params;
  /* Digital sharpening applied after rendering the capture samples.  */
  sharpen_parameters sharpen;
  /* Periodic screen already carrying the requested pre-sampling blur.  */
  const screen *scr;

  /* Return true if THIS and O produce equivalent finite images.  */
  bool
  operator== (const simulated_screen_params &o) const
  {
    return screen_id == o.screen_id && mesh_trans_id == o.mesh_trans_id
           && width == o.width && height == o.height
           && sampling == o.sampling
           && (mesh_trans_id || params == o.params)
           && sharpen.equal_p (o.sharpen);
  }
};

/* Construct a new simulated screen for P.  Return null on failure or
   cancellation.  PROGRESS reports the operation.  */
std::unique_ptr<simulated_screen>
get_new_simulated_screen (simulated_screen_params &p,
                          progress_info *progress);

/* One-entry cache for the usually large finite simulated screen.  */
typedef lru_cache<simulated_screen_params, simulated_screen,
                  get_new_simulated_screen, 1>
    simulated_screen_cache_t;

/* Finite RGB image sampled at capture-pixel centers.

   Pixel (X, Y) has its center at image coordinate (X + 0.5, Y + 0.5).
   Integer access is bounds checked in checking builds.  Fractional access
   uses bicubic interpolation in the interior and clamped bilinear
   interpolation at the finite-image boundary.  */
struct simulated_screen
{
  /* Allocate an image of dimensions WIDTH by HEIGHT.  */
  simulated_screen (int width, int height)
      : m_data (width > 0 && height > 0
                    ? (std::size_t)width * (std::size_t)height
                    : (std::size_t)0),
        m_width (width), m_height (height)
  {
    assert (width > 0 && height > 0);
  }

  /* Return pixel (X, Y).  */
  rgbdata
  get_pixel (int x, int y) const
  {
    assert (!colorscreen_checking
            || (x >= 0 && x < m_width && y >= 0 && y < m_height));
    return m_data[y * m_width + x];
  }

  /* Store COLOR in pixel (X, Y).  */
  void
  put_pixel (int x, int y, rgbdata color)
  {
    assert (!colorscreen_checking
            || (x >= 0 && x < m_width && y >= 0 && y < m_height));
    m_data[y * m_width + x] = color;
  }

  /* Return the interpolated pixel at image coordinate (XP, YP).  Clamp the
     coordinate to the finite image instead of introducing a black border.  */
  inline rgbdata get_interpolated_pixel (coord_t xp, coord_t yp) const noexcept;

  /* Return writable contiguous pixel storage.  */
  simulated_screen_pixel *data ()
  {
    return m_data.data ();
  }

protected:
  /* Row-major pixel storage.  */
  std::vector<simulated_screen_pixel> m_data;
  /* Dimensions of M_DATA.  */
  int m_width, m_height;
};

/* Return a cached finite simulation for PARAM and SCR.  SCREEN_ID identifies
   SCR, SAMPLING specifies pixel-aperture ownership, WIDTH and HEIGHT specify
   output dimensions, SHARPEN specifies the post-sampling digital filter, and
   ID receives the cache-entry identifier.  Update PROGRESS and return null on
   failure or cancellation.  */
std::shared_ptr<simulated_screen>
get_simulated_screen (const scr_to_img_parameters &param, const screen *scr,
                      uint64_t screen_id, screen_sampling sampling,
                      const sharpen_parameters sharpen, int width, int height,
                      progress_info *progress, uint64_t *id);

rgbdata
simulated_screen::get_interpolated_pixel (coord_t xp, coord_t yp) const noexcept
{
  if (m_width <= 0 || m_height <= 0)
    return {};

  /* The center of pixel [0,0] is [0.5,0.5].  Clamp before subtracting the
     half-pixel offset so the fallback interpolation replicates the nearest
     boundary pixel rather than extrapolating or returning black.  */
  xp = std::clamp (xp, (coord_t)0.5, (coord_t)m_width - (coord_t)0.5);
  yp = std::clamp (yp, (coord_t)0.5, (coord_t)m_height - (coord_t)0.5);
  xp -= (coord_t)0.5;
  yp -= (coord_t)0.5;
  int sx, sy;
  coord_t rx = my_modf (xp, &sx);
  coord_t ry = my_modf (yp, &sy);

  if (sx >= 1 && sx < m_width - 2 && sy >= 1 && sy < m_height - 2)
    {
      rgbdata val;
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
      return val;
    }

  /* Bicubic interpolation needs a complete 4 by 4 neighbourhood.  Near the
     finite boundary use clamped bilinear interpolation.  This is continuous
     at pixel centers and avoids the historical zero-valued border.  */
  int sx1 = std::min (sx + 1, m_width - 1);
  int sy1 = std::min (sy + 1, m_height - 1);
  rgbdata top = get_pixel (sx, sy) * ((coord_t)1 - rx)
                + get_pixel (sx1, sy) * rx;
  rgbdata bottom = get_pixel (sx, sy1) * ((coord_t)1 - rx)
                   + get_pixel (sx1, sy1) * rx;
  return top * ((coord_t)1 - ry) + bottom * ry;
}
}

#endif
