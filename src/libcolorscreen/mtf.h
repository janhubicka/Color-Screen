#ifndef MTF_H
#define MTF_H
#include "config.h"
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

namespace colorscreen
{
extern std::mutex fftw_lock;

/* Allocate aligned memory so vectorized loops works fast.
   FFTW knows alignent, but it does not pass proper attributes
   to GCC.  */
template <class T> struct fftw_allocator
{
  typedef T value_type;
  T *
  allocate (std::size_t n)
  {
#ifdef HAVE_MEMALIGN
    void *ptr = memalign (128, n * sizeof (T));
#else
    void *ptr = fftw_malloc (n * sizeof (T));
#endif
    if (!ptr)
      throw std::bad_alloc ();
    return static_cast<T *> (ptr);
  }
#ifdef HAVE_MEMALIGN
  void
  deallocate (T *p, std::size_t)
  {
    free (p);
  }
#else
  void
  deallocate (T *p, std::size_t)
  {
    fftw_free (p);
  }
#endif
};

class mtf
{
public:
  bool precompute (progress_info *progress = NULL);
  /* Precompute psf, psf_radius and psf_size estimate may be revisited.  */
  bool precompute_psf (progress_info *progress = NULL, const char *filename = NULL,
                   const char **error = NULL);

  /* Reutrn 1d MTF value.  */
  inline luminosity_t
  get_mtf (luminosity_t val) const
  {
    return m_mtf.apply (val);
  }

  /* Return 2d MTF value.  */
  inline luminosity_t
  get_mtf (luminosity_t x, luminosity_t y, luminosity_t scale = 1) const
  {
    return m_mtf.apply (my_sqrt (x * x + y * y) * scale);
  }

  inline luminosity_t
  get_psf (luminosity_t x, luminosity_t scale = 1) const
  {
    return m_psf.apply (fabs (x) * (1 / scale));
  }

  /* Return PSF (point spread function) value.
     This is 2D function created as rotation of LSF which is
     determined at precomputation time.  */
  inline luminosity_t
  get_psf (luminosity_t x, luminosity_t y, luminosity_t scale = 1) const
  {
    return m_psf.apply (my_sqrt (x * x + y * y) * (1 / scale));
  }
  inline int
  psf_radius (luminosity_t scale) const
  {
    return ceil (m_psf_radius * scale);
  }
  inline int
  psf_size (luminosity_t scale) const
  {
    return psf_radius (scale) * 2 - 1;
  }

  size_t
  size () const
  {
    return m_params.size ();
  }
  luminosity_t
  get_freq (int i) const
  {
    return m_params.get_freq (i);
  }
  luminosity_t
  get_contrast (int i) const
  {
    return m_params.get_contrast (i);
  }

  mtf (const mtf_parameters &params) : m_params (params), m_precomputed (false), m_precomputed_psf (false)
  {
  }

  luminosity_t
  get_sigma ()
  {
    return m_params.sigma;
  }

  void print_psf (FILE *);

  void print_mtf (FILE *);

  static mtf * get_new_mtf (struct mtf_parameters &, progress_info *);
  typedef lru_cache<mtf_parameters, mtf, mtf *, get_new_mtf, 10> mtf_cache_t;

  static mtf_cache_t::cached_ptr get_mtf (const mtf_parameters &mtfp, progress_info *p);
  std::vector<double, fftw_allocator<double>>
  compute_2d_psf (int psf_size, luminosity_t subscale,
		  progress_info *progress = NULL);

private:
  mtf_parameters m_params;
  precomputed_function<luminosity_t> m_mtf;
  precomputed_function<luminosity_t> m_psf;
  luminosity_t m_psf_radius;
  bool m_precomputed;
  bool m_precomputed_psf;
  std::mutex m_lock;
  luminosity_t estimate_psf_size (luminosity_t min_threshold = 0.001, luminosity_t sum_threshold = 1.0 / 65535) const;
  bool compute_psf (luminosity_t max_radius, luminosity_t subsample,
                    const char *filename, const char **error);
  void compute_lsf (std::vector<double, fftw_allocator<double>> &lsf,
                    luminosity_t subsample) const;
};
}
#endif
