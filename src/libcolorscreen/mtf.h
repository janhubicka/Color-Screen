/* Modulation transfer function and point spread function computation.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#ifndef MTF_H
#define MTF_H
#include "config.h"
#include <algorithm>
#include <cmath>
#include <fftw3.h>
#include <memory>
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

/* Immutable defocus-independent state of the analytical physical capture
   transfer.  The expensive fixed diffraction, sensor, halo, and pupil-overlap
   terms are prepared once and shared through an LRU cache.  PRECOMPUTE builds
   the same signed 512-sample radial transfer table as MTF::PRECOMPUTE for the
   requested image-plane DEFOCUS, but evaluates only the varying pupil phase.

   This helper is intentionally limited to the metadata-driven physical model.
   Measured curves and the empirical fallback retain the ordinary MTF/PSF
   path.  Instances are immutable after construction and safe to share between
   adaptive-focus worker threads.  */
class mtf_focus_transfer
{
public:
  mtf_focus_transfer ();
  explicit mtf_focus_transfer (const mtf_parameters &params);
  ~mtf_focus_transfer ();
  mtf_focus_transfer (mtf_focus_transfer &&) noexcept;
  mtf_focus_transfer &operator= (mtf_focus_transfer &&) noexcept;
  mtf_focus_transfer (const mtf_focus_transfer &) = delete;
  mtf_focus_transfer &operator= (const mtf_focus_transfer &) = delete;

  /* Return a cached transfer source for PARAMS with DEFOCUS excluded from the
     key.  Return null when PARAMS does not select the analytical physical
     model.  CACHE_HIT, when nonnull, reports whether the immutable state was
     already present.  */
  static std::shared_ptr<const mtf_focus_transfer>
  get (const mtf_parameters &params, bool *cache_hit = nullptr);

  /* Build the signed radial transfer table for DEFOCUS.  Return false if the
     prepared state or resulting coefficients are invalid.  */
  nodiscard_attr bool
  precompute (double defocus, precomputed_function<double> &transfer) const;

  /* Remove unreferenced cached states.  This is primarily useful to make
     profiling and unit-test cache accounting deterministic.  */
  static void prune_cache ();

  /* Return true when construction from physical capture metadata succeeded.  */
  bool valid_p () const { return (bool)m_impl; }

private:
  struct impl;
  std::unique_ptr<impl> m_impl;
};

/* Radially symmetric optical transfer model.  For the analytical physical
   model the table stores the signed, real zero-phase OTF so known defocus
   reversals survive forward blur and deconvolution.  A measured slanted-edge
   curve supplies magnitudes only, so measured entries remain nonnegative and
   assume an unknown phase of zero.  Directional or phase information absent
   from a measurement cannot be reconstructed.  */
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
  /* Return one-dimensional signed transfer coefficient at VAL.  Analytical
     physical models may be negative after a known defocus reversal; measured
     MTFs are magnitude-only and therefore remain nonnegative.  */
  inline double
  get_transfer (double val) const
  {
    return m_mtf.apply (val);
  }

  /* Return two-dimensional signed transfer coefficient at point P.  SCALE
     converts the caller's frequency units to cycles per modeled pixel.  */
  inline double
  get_transfer (point_t p, double scale = 1) const
  {
    return m_mtf.apply (p.length () * scale);
  }

  /* Return two-dimensional signed transfer coefficient at coordinates X and
     Y.  SCALE converts the caller's frequency units to cycles per modeled
     pixel.  */
  inline double
  get_transfer (double x, double y, double scale = 1) const
  {
    return m_mtf.apply (std::hypot (x, y) * scale);
  }

  /* Return one-dimensional MTF magnitude at VAL.  This accessor is intended
     for charts, fitting diagnostics and comparison with measured MTF data.  */
  inline double
  get_mtf (double val) const
  {
    return my_fabs (get_transfer (val));
  }

  /* Return two-dimensional MTF magnitude at point P.  SCALE converts the
     caller's frequency units to cycles per modeled pixel.  */
  inline double
  get_mtf (point_t p, double scale = 1) const
  {
    return my_fabs (get_transfer (p, scale));
  }

  /* Return two-dimensional MTF magnitude at coordinates X and Y.  SCALE
     converts the caller's frequency units to cycles per modeled pixel.  */
  inline double
  get_mtf (double x, double y, double scale = 1) const
  {
    return my_fabs (get_transfer (x, y, scale));
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
