/* Tiled scalar denoising utilities.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#include "denoise.h"
#include <cmath>
#include <algorithm>

namespace colorscreen
{

/* Return the support radius of the bilateral spatial Gaussian with standard
   deviation SIGMA_S.  Keep this in one place so tile-border allocation and
   filtering use exactly the same support.  */
static int
bilateral_radius (luminosity_t sigma_s)
{
  return std::max (0, (int)std::ceil (sigma_s * 3.0));
}

/* Set up denoising for given parameters.  */
template <typename T>
denoising<T>::denoising (const denoise_parameters &params, int max_threads,
                         bool use_support)
    : m_params (params), m_use_support (use_support)
{
  if (m_params.mode == denoise_parameters::bilateral)
    m_border_size = bilateral_radius (m_params.bilateral_sigma_s);
  else
    m_border_size = m_params.search_radius + m_params.patch_radius;
  /* Use a reasonable tile size.  */
  m_tile_size = 128;
  while (m_tile_size < m_border_size * 4)
    m_tile_size *= 2;

  m_data.resize (max_threads);
}

/* Destructor.  */
template <typename T>
denoising<T>::~denoising ()
{
}

/* Allocate memory for tiles for given THREAD_ID.  */
template <typename T>
void
denoising<T>::init (int thread_id)
{
  if (m_data[thread_id].initialized)
    return;
  m_data[thread_id].tile.resize (m_tile_size * m_tile_size);
  m_data[thread_id].out_tile.resize (m_tile_size * m_tile_size);
  if (m_use_support)
    m_data[thread_id].support.resize (m_tile_size * m_tile_size);
  if (m_params.mode == denoise_parameters::nl_fast)
    {
      m_data[thread_id].aux1.resize (m_tile_size * m_tile_size);
      m_data[thread_id].aux2.resize (m_tile_size * m_tile_size);
      m_data[thread_id].aux3.resize (m_tile_size * m_tile_size);
    }
  m_data[thread_id].initialized = true;
}

/* Return a usable collection support.  Analyze-* weights are accumulated from
   actual scan contributions and should normally be finite and nonnegative,
   but keep denoising robust against corrupt/partially initialized data.  */
template <typename T>
static inline T
usable_support (T support)
{
  return support > (T)0 && my_isfinite (support) ? support : (T)0;
}

/* Reliability of a difference between two samples with supports A and B.
   If support is proportional to the number/area of contributing scanner
   samples, noise variance is approximately inversely proportional to it.
   The harmonic mean therefore gives a useful inverse-variance-like weight
   for a difference.  It is normalized so equal unit supports give one and
   the confidence-aware filter is exactly the historical filter when every
   support value is one.  */
template <typename T>
static inline T
support_pair_reliability (T a, T b)
{
  a = usable_support (a);
  b = usable_support (b);
  if (a == (T)0 || b == (T)0)
    return (T)0;
  /* Keep the equal-support identity exact even with -ffast-math.  This is
     required for unit support to reproduce the historical filter bitwise.  */
  if (a == b)
    return a;
  return (T)2 * a * b / (a + b);
}

template <bool USE_SUPPORT, typename T>
static inline T
sample_support (const T *support, int idx)
{
  if constexpr (USE_SUPPORT)
    return usable_support (support[idx]);
  else
    {
      (void)support;
      (void)idx;
      return (T)1;
    }
}

template <bool USE_SUPPORT, typename T>
static inline T
sample_pair_reliability (const T *support, int idx1, int idx2)
{
  if constexpr (USE_SUPPORT)
    return support_pair_reliability (support[idx1], support[idx2]);
  else
    {
      (void)support;
      (void)idx1;
      (void)idx2;
      return (T)1;
    }
}

/* Bilateral filter implementation.  */
template <bool USE_SUPPORT, typename T>
static void
process_bilateral (int tile_size, int border, int basic_size, const T *in, T *out,
                   luminosity_t sigma_s, luminosity_t sigma_r,
                   const T *support)
{
  const T inv_s2 = (T)1.0 / ((T)2.0 * sigma_s * sigma_s);
  const T inv_r2 = (T)1.0 / ((T)2.0 * sigma_r * sigma_r);
  const int r = bilateral_radius (sigma_s);

  for (int y = border; y < border + basic_size; ++y)
    for (int x = border; x < border + basic_size; ++x)
      {
        T center_val = in[y * tile_size + x];
        T sum_w = 0;
        T sum_v = 0;
        for (int ky = -r; ky <= r; ++ky)
          for (int kx = -r; kx <= r; ++kx)
            {
              T val = in[(y + ky) * tile_size + (x + kx)];
              T dist_s2 = (T)(kx * kx + ky * ky);
              T dist_r2 = (val - center_val) * (val - center_val);
              int center_idx = y * tile_size + x;
              int candidate_idx = (y + ky) * tile_size + (x + kx);
              T reliability = sample_pair_reliability<USE_SUPPORT> (
                  support, center_idx, candidate_idx);
              T candidate_support
                  = sample_support<USE_SUPPORT> (support, candidate_idx);
              T weight
                  = std::exp (-dist_s2 * inv_s2
                              - dist_r2 * reliability * inv_r2)
                    * candidate_support;
              sum_w += weight;
              sum_v += weight * val;
            }
        out[y * tile_size + x]
            = sum_w > (T)0 ? sum_v / sum_w : center_val;
      }
}

/* Fast NL-means using integral images.  */
template <bool USE_SUPPORT, typename T>
static void
process_nl_fast (int tile_size, int border, int basic_size, const T *in, T *out,
                 int patch_r, int search_r, const denoise_parameters &params,
                 T *integral, T *diff, T *total_weight, const T *support)
{
  const T strength_sq = (T)params.strength * (T)params.strength;
  const T inv_strength_sq = (strength_sq > (T)0) ? (T)1.0 / strength_sq : (T)0.0;
  const int size = tile_size;
  const int patch_diam = 2 * patch_r + 1;
  const T inv_patch_size = (T)1.0 / (T)(patch_diam * patch_diam);

  std::fill (total_weight, total_weight + (size_t)size * size, (T)0);
  std::fill (out, out + (size_t)size * size, (T)0);

  for (int sy = -search_r; sy <= search_r; ++sy)
    for (int sx = -search_r; sx <= search_r; ++sx)
      {
        if (sx == 0 && sy == 0)
          {
            for (int y = border; y < border + basic_size; ++y)
              for (int x = border; x < border + basic_size; ++x)
                {
                  const int idx = y * size + x;
                  T weight = sample_support<USE_SUPPORT> (support, idx);
                  total_weight[idx] += weight;
                  out[idx] += weight * in[idx];
                }
            continue;
          }

        /* Calculate squared differences.  */
        for (int y = border - patch_r; y < border + basic_size + patch_r; ++y)
          for (int x = border - patch_r; x < border + basic_size + patch_r; ++x)
            {
              int idx1 = y * size + x;
              int idx2 = (y + sy) * size + (x + sx);
              diff[idx1]
                  = denoise_nl_square_distance (in[idx1], in[idx2], params)
                    * sample_pair_reliability<USE_SUPPORT> (support, idx1, idx2);
            }

        /* Calculate integral image of squared differences.  */
        for (int y = border - patch_r; y < border + basic_size + patch_r; ++y)
          {
            T row_sum = 0;
            for (int x = border - patch_r; x < border + basic_size + patch_r; ++x)
              {
                row_sum += diff[y * size + x];
                integral[y * size + x] = (y > border - patch_r) 
                                         ? integral[(y - 1) * size + x] + row_sum 
                                         : row_sum;
              }
          }

        /* Use integral image to get patch distances.  */
        for (int y = border; y < border + basic_size; ++y)
          for (int x = border; x < border + basic_size; ++x)
            {
              T dist_sq = integral[(y + patch_r) * size + (x + patch_r)]
                        - integral[(y - patch_r - 1) * size + (x + patch_r)]
                        - integral[(y + patch_r) * size + (x - patch_r - 1)]
                        + integral[(y - patch_r - 1) * size + (x - patch_r - 1)];
              
              int candidate_idx = (y + sy) * size + (x + sx);
              T weight
                  = std::exp (-dist_sq * inv_patch_size * inv_strength_sq)
                    * sample_support<USE_SUPPORT> (support, candidate_idx);
              total_weight[y * size + x] += weight;
              out[y * size + x] += weight * in[candidate_idx];
            }
      }

  for (int y = border; y < border + basic_size; ++y)
    for (int x = border; x < border + basic_size; ++x)
      {
        const int idx = y * size + x;
        if (total_weight[idx] > (T)0)
          out[idx] /= total_weight[idx];
        else
          out[idx] = in[idx];
      }
}

/* Apply denoising (Non-Local Means) to the tile for given THREAD_ID.  */
template <typename T>
void
denoising<T>::process_tile (int thread_id, progress_info *progress)
{
  if (progress && progress->cancelled ())
    return;

  const int border = m_border_size;
  const int basic_size = get_basic_tile_size ();
  
  const T *in = m_data[thread_id].tile.data ();
  T *out = m_data[thread_id].out_tile.data ();
  const T *support
      = m_use_support ? m_data[thread_id].support.data () : nullptr;

  /* Unit support is defined to reproduce the historical unweighted filter
     exactly.  Do not rely on algebraically redundant multiplications by one
     compiling to bit-identical code under -ffast-math: Apple Clang can select
     a different arithmetic/vectorization path for the support-enabled
     template.
     Dispatch an all-unit tile through the same unweighted implementation.  */
  bool use_support = m_use_support;
  if (use_support)
    {
      use_support = false;
      const size_t n = (size_t)m_tile_size * m_tile_size;
      for (size_t i = 0; i < n; i++)
        if (usable_support (support[i]) != (T)1)
          {
            use_support = true;
            break;
          }
    }

  if (m_params.mode == denoise_parameters::bilateral)
    {
      if (use_support)
        process_bilateral<true> (
            m_tile_size, border, basic_size, in, out,
            (T)m_params.bilateral_sigma_s, (T)m_params.bilateral_sigma_r,
            support);
      else
        process_bilateral<false> (
            m_tile_size, border, basic_size, in, out,
            (T)m_params.bilateral_sigma_s, (T)m_params.bilateral_sigma_r,
            support);
    }
  else if (m_params.mode == denoise_parameters::nl_fast)
    {
      if (use_support)
        process_nl_fast<true> (
            m_tile_size, border, basic_size, in, out, m_params.patch_radius,
            m_params.search_radius, m_params, m_data[thread_id].aux1.data (),
            m_data[thread_id].aux2.data (),
            m_data[thread_id].aux3.data (), support);
      else
        process_nl_fast<false> (
            m_tile_size, border, basic_size, in, out, m_params.patch_radius,
            m_params.search_radius, m_params, m_data[thread_id].aux1.data (),
            m_data[thread_id].aux2.data (),
            m_data[thread_id].aux3.data (), support);
    }
  else
    {
      /* Original NL-Means implementation.  */
      const int patch_r = m_params.patch_radius;
      const int search_r = m_params.search_radius;
      const T strength_sq = m_params.strength * m_params.strength;
      const T inv_strength_sq
          = (strength_sq > (T)0) ? (T)1.0 / strength_sq : (T)0.0;
      const int patch_diam = 2 * patch_r + 1;
      const T inv_patch_size = (T)1.0 / (T)(patch_diam * patch_diam);

      /* Process each pixel in the basic tile (excluding borders).  */
      for (int y = border; y < border + basic_size; ++y)
        {
          for (int x = border; x < border + basic_size; ++x)
            {
              T total_weight = 0;
              T weighted_sum = 0;
              
              /* Search window.  */
              for (int sy = -search_r; sy <= search_r; ++sy)
                {
                  for (int sx = -search_r; sx <= search_r; ++sx)
                    {
                      /* Center of the candidate patch.  */
                      int cx = x + sx;
                      int cy = y + sy;
                      
                      T dist_sq = 0;
                      /* Compare patches.  */
                      for (int py = -patch_r; py <= patch_r; ++py)
                        {
#pragma omp simd reduction(+:dist_sq)
                          for (int px = -patch_r; px <= patch_r; ++px)
                            {
                              T v1 = in[(y + py) * m_tile_size + (x + px)];
                              T v2 = in[(cy + py) * m_tile_size + (cx + px)];
                              T d2 = denoise_nl_square_distance (v1, v2,
                                                                  m_params);
                              if (use_support)
                                {
                                  int idx1 = (y + py) * m_tile_size + (x + px);
                                  int idx2 = (cy + py) * m_tile_size + (cx + px);
                                  dist_sq += d2
                                             * support_pair_reliability (
                                                 support[idx1], support[idx2]);
                                }
                              else
                                dist_sq += d2;
                            }
                        }
                      
                      /* Weighting function.  */
                      T weight
                          = std::exp (-dist_sq * inv_patch_size
                                      * inv_strength_sq);
                      if (use_support)
                        weight *= usable_support (
                            support[cy * m_tile_size + cx]);
                      weighted_sum += weight * in[cy * m_tile_size + cx];
                      total_weight += weight;
                    }
                }
              
              if (total_weight > (T)0)
                out[y * m_tile_size + x] = weighted_sum / total_weight;
              else
                out[y * m_tile_size + x] = in[y * m_tile_size + x];
            }
        }
    }
  
}


/* Return mean squared distance of two RGB samples.  Using the channel mean
   keeps the range parameter on the same approximate scale as scalar filtering.  */
static inline luminosity_t
rgb_mean_square_distance (rgbdata a, rgbdata b)
{
  luminosity_t dr = a.red - b.red;
  luminosity_t dg = a.green - b.green;
  luminosity_t db = a.blue - b.blue;
  return (dr * dr + dg * dg + db * db) / (luminosity_t)3;
}

rgb_denoising::rgb_denoising (const denoise_parameters &params,
                              int max_threads)
    : m_params (params)
{
  if (m_params.mode == denoise_parameters::bilateral)
    m_border_size = bilateral_radius (m_params.bilateral_sigma_s);
  else
    m_border_size = m_params.search_radius + m_params.patch_radius;
  m_tile_size = 128;
  while (m_tile_size < m_border_size * 4)
    m_tile_size *= 2;
  m_data.resize (max_threads);
}

void
rgb_denoising::init (int thread_id)
{
  if (m_data[thread_id].initialized)
    return;
  const size_t n = (size_t)m_tile_size * m_tile_size;
  m_data[thread_id].tile.resize (n);
  m_data[thread_id].out_tile.resize (n);
  if (m_params.mode == denoise_parameters::nl_fast)
    {
      m_data[thread_id].integral.resize (n);
      m_data[thread_id].diff.resize (n);
      m_data[thread_id].total_weight.resize (n);
    }
  m_data[thread_id].initialized = true;
}

void
rgb_denoising::process_tile (int thread_id, progress_info *progress)
{
  if (progress && progress->cancelled ())
    return;

  const int border = m_border_size;
  const int basic_size = get_basic_tile_size ();
  const int size = m_tile_size;
  const rgbdata *in = m_data[thread_id].tile.data ();
  rgbdata *out = m_data[thread_id].out_tile.data ();

  if (m_params.mode == denoise_parameters::bilateral)
    {
      const luminosity_t inv_s2
          = (luminosity_t)1
            / ((luminosity_t)2 * m_params.bilateral_sigma_s
               * m_params.bilateral_sigma_s);
      const luminosity_t inv_r2
          = (luminosity_t)1
            / ((luminosity_t)2 * m_params.bilateral_sigma_r
               * m_params.bilateral_sigma_r);
      const int r = bilateral_radius (m_params.bilateral_sigma_s);
      for (int y = border; y < border + basic_size; y++)
        for (int x = border; x < border + basic_size; x++)
          {
            const rgbdata center = in[y * size + x];
            luminosity_t sum_w = 0;
            rgbdata sum = { 0, 0, 0 };
            for (int ky = -r; ky <= r; ky++)
              for (int kx = -r; kx <= r; kx++)
                {
                  const rgbdata val = in[(y + ky) * size + x + kx];
                  const luminosity_t dist_s2
                      = (luminosity_t)(kx * kx + ky * ky);
                  const luminosity_t dist_r2
                      = rgb_mean_square_distance (center, val);
                  const luminosity_t w
                      = std::exp (-dist_s2 * inv_s2 - dist_r2 * inv_r2);
                  sum_w += w;
                  sum.red += w * val.red;
                  sum.green += w * val.green;
                  sum.blue += w * val.blue;
                }
            if (sum_w > 0)
              out[y * size + x]
                  = { sum.red / sum_w, sum.green / sum_w, sum.blue / sum_w };
            else
              out[y * size + x] = center;
          }
      return;
    }

  const int patch_r = m_params.patch_radius;
  const int search_r = m_params.search_radius;
  const luminosity_t strength_sq = m_params.strength * m_params.strength;
  const luminosity_t inv_strength_sq
      = strength_sq > 0 ? (luminosity_t)1 / strength_sq : 0;
  const int patch_diam = patch_r * 2 + 1;
  const luminosity_t inv_patch_size
      = (luminosity_t)1 / (luminosity_t)(patch_diam * patch_diam);

  if (m_params.mode == denoise_parameters::nl_fast)
    {
      luminosity_t *integral = m_data[thread_id].integral.data ();
      luminosity_t *diff = m_data[thread_id].diff.data ();
      luminosity_t *total_weight = m_data[thread_id].total_weight.data ();
      const size_t n = (size_t)size * size;
      std::fill (total_weight, total_weight + n, (luminosity_t)0);
      std::fill (out, out + n, rgbdata { 0, 0, 0 });

      for (int sy = -search_r; sy <= search_r; sy++)
        for (int sx = -search_r; sx <= search_r; sx++)
          {
            if (!sx && !sy)
              {
                for (int y = border; y < border + basic_size; y++)
                  for (int x = border; x < border + basic_size; x++)
                    {
                      const int idx = y * size + x;
                      total_weight[idx] += 1;
                      out[idx].red += in[idx].red;
                      out[idx].green += in[idx].green;
                      out[idx].blue += in[idx].blue;
                    }
                continue;
              }

            for (int y = border - patch_r;
                 y < border + basic_size + patch_r; y++)
              for (int x = border - patch_r;
                   x < border + basic_size + patch_r; x++)
                {
                  const int idx1 = y * size + x;
                  const int idx2 = (y + sy) * size + x + sx;
                  diff[idx1]
                      = denoise_nl_rgb_square_distance (in[idx1], in[idx2],
                                                        m_params);
                }

            for (int y = border - patch_r;
                 y < border + basic_size + patch_r; y++)
              {
                luminosity_t row_sum = 0;
                for (int x = border - patch_r;
                     x < border + basic_size + patch_r; x++)
                  {
                    row_sum += diff[y * size + x];
                    integral[y * size + x]
                        = y > border - patch_r
                              ? integral[(y - 1) * size + x] + row_sum
                              : row_sum;
                  }
              }

            for (int y = border; y < border + basic_size; y++)
              for (int x = border; x < border + basic_size; x++)
                {
                  luminosity_t dist_sq
                      = integral[(y + patch_r) * size + x + patch_r]
                        - integral[(y - patch_r - 1) * size + x + patch_r]
                        - integral[(y + patch_r) * size + x - patch_r - 1]
                        + integral[(y - patch_r - 1) * size + x - patch_r - 1];
                  luminosity_t w
                      = std::exp (-dist_sq * inv_patch_size * inv_strength_sq);
                  const int idx = y * size + x;
                  const rgbdata candidate = in[(y + sy) * size + x + sx];
                  total_weight[idx] += w;
                  out[idx].red += w * candidate.red;
                  out[idx].green += w * candidate.green;
                  out[idx].blue += w * candidate.blue;
                }
          }

      for (int y = border; y < border + basic_size; y++)
        for (int x = border; x < border + basic_size; x++)
          {
            const int idx = y * size + x;
            if (total_weight[idx] > 0)
              {
                out[idx].red /= total_weight[idx];
                out[idx].green /= total_weight[idx];
                out[idx].blue /= total_weight[idx];
              }
            else
              out[idx] = in[idx];
          }
      return;
    }

  /* Reference vector NL-means.  */
  for (int y = border; y < border + basic_size; y++)
    for (int x = border; x < border + basic_size; x++)
      {
        luminosity_t total_weight = 0;
        rgbdata sum = { 0, 0, 0 };
        for (int sy = -search_r; sy <= search_r; sy++)
          for (int sx = -search_r; sx <= search_r; sx++)
            {
              luminosity_t dist_sq = 0;
              for (int py = -patch_r; py <= patch_r; py++)
                for (int px = -patch_r; px <= patch_r; px++)
                  dist_sq += denoise_nl_rgb_square_distance (
                      in[(y + py) * size + x + px],
                      in[(y + sy + py) * size + x + sx + px], m_params);
              luminosity_t w
                  = std::exp (-dist_sq * inv_patch_size * inv_strength_sq);
              rgbdata candidate = in[(y + sy) * size + x + sx];
              total_weight += w;
              sum.red += w * candidate.red;
              sum.green += w * candidate.green;
              sum.blue += w * candidate.blue;
            }
        if (total_weight > 0)
          out[y * size + x] = { sum.red / total_weight,
                                sum.green / total_weight,
                                sum.blue / total_weight };
        else
          out[y * size + x] = in[y * size + x];
      }
}

template class denoising<float>;
template class denoising<double>;

}
