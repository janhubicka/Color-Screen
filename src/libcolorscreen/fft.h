#ifndef FFT_H
#define FFT_H
#include "config.h"
#include <fftw3.h>
#include <mutex>
#include <memory>
#include <vector>
#include <map>
#include <tuple>
#include <cstdlib>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

namespace colorscreen
{
extern std::mutex fft_lock;

/* Allocate aligned memory so vectorized loops works fast.
   FFTW knows alignent, but it does not pass proper attributes
   to GCC.  */
template <class T> struct fft_allocator
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
  void
  deallocate (T *p, std::size_t)
  {
#ifdef HAVE_MEMALIGN
    free (p);
#else
    fftw_free (p);
#endif
  }
};

template <typename T> struct fft_complex_t;
template <> struct fft_complex_t<double> { typedef fftw_complex type; };
template <> struct fft_complex_t<float> { typedef fftwf_complex type; };

template <typename T> struct fft_plan_t;
template <> struct fft_plan_t<double> { typedef fftw_plan type; };
template <> struct fft_plan_t<float> { typedef fftwf_plan type; };

template <typename T> struct fft_deleter
{
  void operator() (T *p) const
  {
#ifdef HAVE_MEMALIGN
    free (p);
#else
    fftw_free (p);
#endif
  }
};

template <typename T>
using fft_unique_ptr = std::unique_ptr<typename fft_complex_t<T>::type[], fft_deleter<typename fft_complex_t<T>::type>>;

template <typename T>
fft_unique_ptr<T>
fft_alloc_complex (size_t n)
{
  fft_allocator<typename fft_complex_t<T>::type> alloc;
  return fft_unique_ptr<T> (alloc.allocate (n));
}

template <typename T>
class fft_plan
{
  typename fft_plan_t<T>::type m_plan;
public:
  fft_plan () : m_plan (NULL) {}
  fft_plan (typename fft_plan_t<T>::type p) : m_plan (p) {}
  operator typename fft_plan_t<T>::type () { return m_plan; }

  /* Execute real-to-complex DFT (1D or 2D)  */
  void execute_r2c (T *in, typename fft_complex_t<T>::type *out)
  {
    if (!m_plan) return;
    if constexpr (std::is_same_v<T, double>)
      fftw_execute_dft_r2c (m_plan, in, out);
    else if constexpr (std::is_same_v<T, float>)
      fftwf_execute_dft_r2c (m_plan, in, out);
    else
      abort ();
  }

  /* Execute complex-to-real DFT (1D or 2D)  */
  void execute_c2r (typename fft_complex_t<T>::type *in, T *out)
  {
    if (!m_plan) return;
    if constexpr (std::is_same_v<T, double>)
      fftw_execute_dft_c2r (m_plan, in, out);
    else if constexpr (std::is_same_v<T, float>)
      fftwf_execute_dft_c2r (m_plan, in, out);
    else
      abort ();
  }

  /* Execute real-to-real transform (1D or 2D)  */
  void execute_r2r (T *in, T *out)
  {
    if (!m_plan) return;
    if constexpr (std::is_same_v<T, double>)
      fftw_execute_r2r (m_plan, in, out);
    else if constexpr (std::is_same_v<T, float>)
      fftwf_execute_r2r (m_plan, in, out);
    else
      abort ();
  }
};

/* Factory functions for creating/retrieving plans.
   These handle locking and caching.  */

template <typename T>
fft_plan<T> fft_plan_r2c_2d (int n0, int n1, T *in = NULL,
                              typename fft_complex_t<T>::type *out = NULL,
                              unsigned flags = FFTW_ESTIMATE);

template <typename T>
fft_plan<T> fft_plan_c2r_2d (int n0, int n1,
                              typename fft_complex_t<T>::type *in = NULL,
                              T *out = NULL, unsigned flags = FFTW_ESTIMATE);

template <typename T>
fft_plan<T> fft_plan_r2c_1d (int n, T *in = NULL,
                              typename fft_complex_t<T>::type *out = NULL,
                              unsigned flags = FFTW_ESTIMATE);

template <typename T>
fft_plan<T> fft_plan_r2r_1d (int n, fftw_r2r_kind kind, T *in = NULL,
                              T *out = NULL, unsigned flags = FFTW_ESTIMATE);

}
#endif
