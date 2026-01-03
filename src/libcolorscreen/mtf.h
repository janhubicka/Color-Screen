#ifndef MTF_H
#define MTF_H
#include <fftw3.h>
#include <mutex>
#include "include/progress-info.h"
#include "include/base.h"
#include "include/color.h"
#include "include/precomputed-function.h"

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


  void
  add_value (luminosity_t freq, luminosity_t contrast)
  {
    assert (!m_precomputed);
    m_data.push_back ({freq, contrast});
  }

  size_t
  size () const
  {
    return m_data.size ();
  }
  luminosity_t
  get_freq (int i) const
  {
    return m_data[i].freq;
  }
  luminosity_t
  get_contrast (int i) const
  {
    return m_data[i].contrast;
  }

  mtf ()
  : m_sigma (0),
    m_precomputed (0)
  { }

  void
  set_sigma (luminosity_t sigma)
  {
    assert (!m_precomputed);
    m_sigma = sigma;
  }

  luminosity_t
  get_sigma ()
  {
    return m_sigma;
  }

  void
  print_psf (FILE *);

  void
  print_mtf (FILE *);
private:
  luminosity_t m_sigma;
  struct entry {luminosity_t freq, contrast;};
  std::vector <entry> m_data;
  precomputed_function<luminosity_t> m_mtf;
  precomputed_function<luminosity_t> m_psf;
  luminosity_t m_psf_radius;
  bool m_precomputed;
  std::mutex m_lock;
  void compute_psf ();
};
}
#endif
