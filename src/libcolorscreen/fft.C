#include "fft.h"
#include <map>
#include <tuple>

namespace colorscreen
{
/* FFTW execute is thread safe. Everything else is not.  */
std::mutex fft_lock;

namespace
{
enum plan_kind
{
  r2c_2d,
  c2r_2d,
  r2c_1d,
  r2r_1d
};

/* Key for caching plans.  */
typedef std::tuple<plan_kind, int, int, int, unsigned> plan_key;

template <typename T>
std::map<plan_key, typename fft_plan_t<T>::type> &
get_plan_cache ()
{
  static std::map<plan_key, typename fft_plan_t<T>::type> cache;
  return cache;
}
}

template <typename T>
fft_plan<T>
fft_plan_r2c_2d (int n0, int n1, T *in,
                 typename fft_complex_t<T>::type *out, unsigned flags)
{
  std::lock_guard<std::mutex> lock (fft_lock);
  plan_key key = { r2c_2d, n0, n1, 0, flags };
  auto &cache = get_plan_cache<T> ();
  if (cache.count (key))
    return fft_plan<T> (cache[key]);

  T *tmp_in = NULL;
  typename fft_complex_t<T>::type *tmp_out = NULL;
  if (!in)
    in = tmp_in = (T *)fftw_malloc (sizeof (T) * n0 * n1);
  if (!out)
    out = tmp_out = (typename fft_complex_t<T>::type *)fftw_malloc (
        sizeof (typename fft_complex_t<T>::type) * n0 * (n1 / 2 + 1));

  typename fft_plan_t<T>::type p;
  if constexpr (std::is_same_v<T, double>)
    p = fftw_plan_dft_r2c_2d (n0, n1, in, out, flags);
  else
    p = fftwf_plan_dft_r2c_2d (n0, n1, (float *)in, (fftwf_complex *)out,
                                flags);
  if (tmp_in)
    fftw_free (tmp_in);
  if (tmp_out)
    fftw_free (tmp_out);
  cache[key] = p;
  return fft_plan<T> (p);
}

template <typename T>
fft_plan<T>
fft_plan_c2r_2d (int n0, int n1, typename fft_complex_t<T>::type *in,
                 T *out, unsigned flags)
{
  std::lock_guard<std::mutex> lock (fft_lock);
  plan_key key = { c2r_2d, n0, n1, 0, flags };
  auto &cache = get_plan_cache<T> ();
  if (cache.count (key))
    return fft_plan<T> (cache[key]);

  typename fft_complex_t<T>::type *tmp_in = NULL;
  T *tmp_out = NULL;
  if (!in)
    in = tmp_in = (typename fft_complex_t<T>::type *)fftw_malloc (
        sizeof (typename fft_complex_t<T>::type) * n0 * (n1 / 2 + 1));
  if (!out)
    out = tmp_out = (T *)fftw_malloc (sizeof (T) * n0 * n1);

  typename fft_plan_t<T>::type p;
  if constexpr (std::is_same_v<T, double>)
    p = fftw_plan_dft_c2r_2d (n0, n1, in, out, flags);
  else
    p = fftwf_plan_dft_c2r_2d (n0, n1, (fftwf_complex *)in, (float *)out,
                                flags);
  if (tmp_in)
    fftw_free (tmp_in);
  if (tmp_out)
    fftw_free (tmp_out);
  cache[key] = p;
  return fft_plan<T> (p);
}

template <typename T>
fft_plan<T>
fft_plan_r2c_1d (int n, T *in, typename fft_complex_t<T>::type *out,
                 unsigned flags)
{
  std::lock_guard<std::mutex> lock (fft_lock);
  plan_key key = { r2c_1d, n, 0, 0, flags };
  auto &cache = get_plan_cache<T> ();
  if (cache.count (key))
    return fft_plan<T> (cache[key]);

  T *tmp_in = NULL;
  typename fft_complex_t<T>::type *tmp_out = NULL;
  if (!in)
    in = tmp_in = (T *)fftw_malloc (sizeof (T) * n);
  if (!out)
    out = tmp_out = (typename fft_complex_t<T>::type *)fftw_malloc (
        sizeof (typename fft_complex_t<T>::type) * (n / 2 + 1));

  typename fft_plan_t<T>::type p;
  if constexpr (std::is_same_v<T, double>)
    p = fftw_plan_dft_r2c_1d (n, in, out, flags);
  else
    p = fftwf_plan_dft_r2c_1d (n, (float *)in, (fftwf_complex *)out, flags);
  if (tmp_in)
    fftw_free (tmp_in);
  if (tmp_out)
    fftw_free (tmp_out);
  cache[key] = p;
  return fft_plan<T> (p);
}

template <typename T>
fft_plan<T>
fft_plan_r2r_1d (int n, fftw_r2r_kind kind, T *in, T *out, unsigned flags)
{
  std::lock_guard<std::mutex> lock (fft_lock);
  plan_key key = { r2r_1d, n, (int)kind, 0, flags };
  auto &cache = get_plan_cache<T> ();
  if (cache.count (key))
    return fft_plan<T> (cache[key]);

  T *tmp_in = NULL;
  T *tmp_out = NULL;
  if (!in)
    in = tmp_in = (T *)fftw_malloc (sizeof (T) * n);
  if (!out)
    out = tmp_out = (T *)fftw_malloc (sizeof (T) * n);

  typename fft_plan_t<T>::type p;
  if constexpr (std::is_same_v<T, double>)
    p = fftw_plan_r2r_1d (n, in, out, kind, flags);
  else
    p = fftwf_plan_r2r_1d (n, (float *)in, (float *)out, kind, flags);
  if (tmp_in)
    fftw_free (tmp_in);
  if (tmp_out)
    fftw_free (tmp_out);
  cache[key] = p;
  return fft_plan<T> (p);
}

/* Explicit instantiations for double and float.  */
template fft_plan<double> fft_plan_r2c_2d<double> (int, int, double *, fftw_complex *, unsigned);
template fft_plan<double> fft_plan_c2r_2d<double> (int, int, fftw_complex *, double *, unsigned);
template fft_plan<double> fft_plan_r2c_1d<double> (int, double *, fftw_complex *, unsigned);
template fft_plan<double> fft_plan_r2r_1d<double> (int, fftw_r2r_kind, double *, double *, unsigned);

template fft_plan<float> fft_plan_r2c_2d<float> (int, int, float *, fftwf_complex *, unsigned);
template fft_plan<float> fft_plan_c2r_2d<float> (int, int, fftwf_complex *, float *, unsigned);
template fft_plan<float> fft_plan_r2c_1d<float> (int, float *, fftwf_complex *, unsigned);
template fft_plan<float> fft_plan_r2r_1d<float> (int, fftw_r2r_kind, float *, float *, unsigned);

}
