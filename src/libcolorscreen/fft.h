#ifndef FFT_H
#define FFT_H
#include "config.h"
#include <fftw3.h>
#include <mutex>
#include <memory>
#include <vector>
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

}
#endif
