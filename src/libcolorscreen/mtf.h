/* Modulation transfer function and point spread function computation.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#ifndef MTF_H
#define MTF_H
#include "config.h"
#include <algorithm>
#include <cmath>
#include <fftw3.h>
#include <mutex>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
#include "include/base.h"
#include "include/color.h"
#include "include/precomputed-function.h"
#include "include/progress-info.h"
#include "include/render-parameters.h"
#include "lru-cache.h"
#include "fft.h"

namespace colorscreen
{

/* Radially symmetric, zero-phase optical transfer model.  A measured MTF
   supplies only transfer magnitudes; consequently this class assumes that the
   corresponding point-spread function is centered and rotationally symmetric.
   It cannot reconstruct directional or phase information absent from the
   measurement.  */
class mtf
{
public:
  nodiscard_attr bool precompute (progress_info *progress = nullptr,
                                   bool parallel = true);
  /* Precompute psf, psf_radius and psf_size estimate may be revisited.  */
  nodiscard_attr bool precompute_psf (progress_info *progress = nullptr,
                                       bool parallel = true,
                                       const char *filename = nullptr,
                                       const char **error = nullptr);
  /* Return 1d MTF value.  */
  inline double
  get_mtf (double val) const
  {
    return m_mtf.apply (val);
  }

  /* Return 2d MTF value at point P.  */
  inline double
  get_mtf (point_t p, double scale = 1) const
  {
    return m_mtf.apply (p.length () * scale);
  }

  /* Return 2d MTF value at coordinates X and Y, multiplied by SCALE.  */
  inline double
  get_mtf (double x, double y, double scale = 1) const
  {
    return m_mtf.apply (std::hypot (x, y) * scale);
  }

  /* Return PSF value.  */
  inline double
  get_psf (luminosity_t x, luminosity_t scale = 1) const
  {
    return m_psf.apply (my_fabs (x) * (1 / scale));
  }

  /* Return PSF value at point P.  */
  inline double
  get_psf (point_t p, luminosity_t scale = 1) const
  {
    return m_psf.apply (p.length () * (1 / scale));
  }

  /* Return PSF (point spread function) value.
     This is 2D function created as rotation of LSF which is
     determined at precomputation time.  */
  inline double
  get_psf (luminosity_t x, luminosity_t y, luminosity_t scale = 1) const
  {
    return m_psf.apply (my_sqrt (x * x + y * y) * (1 / scale));
  }
  inline int
  psf_radius (luminosity_t scale) const
  {
    return my_isfinite ((double)scale) && scale > 0
               ? std::max ((int)my_ceil (m_psf_radius * scale), 0)
               : 0;
  }
  inline int
  psf_size (luminosity_t scale) const
  {
    return std::max (psf_radius (scale) * 2 - 1, 1);
  }

  mtf (const mtf_parameters &params) : m_params (params)
  {
  }

  luminosity_t
  get_sigma () const
  {
    return m_params.sigma;
  }

  static std::unique_ptr<mtf> get_new_mtf (struct mtf_parameters &,
                                           progress_info *);
  typedef lru_cache<mtf_parameters, mtf, get_new_mtf, 10> mtf_cache_t;

  static std::shared_ptr<mtf> get_mtf (const mtf_parameters &mtfp,
                                       progress_info *p);
  typedef float psf_t;
  std::vector<psf_t, fft_allocator<psf_t>>
  compute_2d_psf (int psf_size, luminosity_t subscale,
		  progress_info *progress = nullptr, bool parallel = true);

  bool render_dot_spread_tile (tile_parameters &tile, progress_info *p);

private:
  mtf_parameters m_params;
  /* The MTF table is small, while an interpolation error is multiplied by an
     inverse filter.  Keep it in double even though large image FFTs use float.  */
  precomputed_function<double> m_mtf;
  precomputed_function<psf_t> m_psf;
  double m_psf_radius = 0;
  bool m_precomputed = false;
  bool m_precomputed_psf = false;
  std::mutex m_lock;
  double estimate_psf_size (luminosity_t min_threshold = 0.001,
                            luminosity_t sum_threshold = 1.0 / 65535) const;
  bool compute_psf (luminosity_t max_radius, luminosity_t subsample,
                    const char *filename, const char **error, bool parallel = true);
  void compute_lsf (std::vector<psf_t, fft_allocator<psf_t>> &lsf,
                    luminosity_t subsample) const;
};
}
#endif
