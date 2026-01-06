#ifndef MTF_H
#define MTF_H
#include <fftw3.h>
#include <mutex>
#include "include/progress-info.h"
#include "include/base.h"
#include "include/color.h"
#include "include/precomputed-function.h"
#include "include/render-parameters.h"

namespace colorscreen
{
extern std::mutex fftw_lock;
class mtf
{
public:
  bool precompute (progress_info *progress = NULL);

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
  int init_psf (std::vector<double> &psf, luminosity_t scale);


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

  mtf (const mtf_parameters &params)
  : m_params (params)
  { }

  luminosity_t
  get_sigma ()
  {
    return m_params.sigma;
  }

  void
  print_psf (FILE *);

  void
  print_mtf (FILE *);

  static mtf *get_mtf (mtf_parameters &mtfp, progress_info *p);
  static void release_mtf (mtf *m);
private:
  mtf_parameters m_params;
  precomputed_function<luminosity_t> m_mtf;
  precomputed_function<luminosity_t> m_psf;
  luminosity_t m_psf_radius;
  bool m_precomputed;
  std::mutex m_lock;
  void compute_psf (int max_radius = 128, luminosity_t subsample = 1 / 32.0);
};
}
#endif
