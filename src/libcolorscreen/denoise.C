/* High-quality image denoising.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#include "denoise.h"
#include <cmath>
#include <algorithm>

namespace colorscreen
{

/* Set up denoising for given parameters.  */
template <typename T>
denoising<T>::denoising (const denoise_parameters &params, int max_threads)
    : m_params (params)
{
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
  if (m_params.mode == denoise_parameters::nl_fast)
    {
      m_data[thread_id].aux1.resize (m_tile_size * m_tile_size);
      m_data[thread_id].aux2.resize (m_tile_size * m_tile_size);
    }
  m_data[thread_id].initialized = true;
}

/* Bilateral filter implementation.  */
template <typename T>
static void
process_bilateral (int tile_size, int border, int basic_size, const T *in, T *out,
                   luminosity_t sigma_s, luminosity_t sigma_r)
{
  const T inv_s2 = (T)1.0 / ((T)2.0 * sigma_s * sigma_s);
  const T inv_r2 = (T)1.0 / ((T)2.0 * sigma_r * sigma_r);
  const int r = (int)std::ceil (sigma_s * 3.0);

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
              T weight = std::exp (-dist_s2 * inv_s2 - dist_r2 * inv_r2);
              sum_w += weight;
              sum_v += weight * val;
            }
        out[y * tile_size + x] = sum_v / sum_w;
      }
}

/* Fast NL-means using integral images.  */
template <typename T>
static void
process_nl_fast (int tile_size, int border, int basic_size, const T *in, T *out,
                 int patch_r, int search_r, T strength, T *integral, T *diff)
{
  const T strength_sq = strength * strength;
  const T inv_strength_sq = (strength_sq > (T)0) ? (T)1.0 / strength_sq : (T)0.0;
  const int size = tile_size;
  const int patch_diam = 2 * patch_r + 1;
  const T inv_patch_size = (T)1.0 / (T)(patch_diam * patch_diam);

  std::vector<T> total_weight (size * size, 0);
  std::vector<T> weighted_sum (size * size, 0);

  for (int sy = -search_r; sy <= search_r; ++sy)
    for (int sx = -search_r; sx <= search_r; ++sx)
      {
        if (sx == 0 && sy == 0)
          {
            for (int y = border; y < border + basic_size; ++y)
              for (int x = border; x < border + basic_size; ++x)
                {
                  total_weight[y * size + x] += 1;
                  weighted_sum[y * size + x] += in[y * size + x];
                }
            continue;
          }

        /* Calculate squared differences.  */
        for (int y = border - patch_r; y < border + basic_size + patch_r; ++y)
          for (int x = border - patch_r; x < border + basic_size + patch_r; ++x)
            {
              T d = in[y * size + x] - in[(y + sy) * size + (x + sx)];
              diff[y * size + x] = d * d;
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
              
              T weight = std::exp (-dist_sq * inv_patch_size * inv_strength_sq);
              total_weight[y * size + x] += weight;
              weighted_sum[y * size + x] += weight * in[(y + sy) * size + (x + sx)];
            }
      }

  for (int y = border; y < border + basic_size; ++y)
    for (int x = border; x < border + basic_size; ++x)
      out[y * size + x] = weighted_sum[y * size + x] / total_weight[y * size + x];
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

  if (m_params.mode == denoise_parameters::bilateral)
    {
      process_bilateral (m_tile_size, border, basic_size, in, out,
                         (T)m_params.bilateral_sigma_s, (T)m_params.bilateral_sigma_r);
    }
  else if (m_params.mode == denoise_parameters::nl_fast)
    {
      process_nl_fast (m_tile_size, border, basic_size, in, out,
                       m_params.patch_radius, m_params.search_radius,
                       (T)m_params.strength, m_data[thread_id].aux1.data (),
                       m_data[thread_id].aux2.data ());
    }
  else
    {
      /* Original NL-Means implementation.  */
      const int patch_r = m_params.patch_radius;
      const int search_r = m_params.search_radius;
      const T strength_sq = m_params.strength * m_params.strength;
      const T inv_strength_sq = (strength_sq > (T)0) ? (T)1.0 / strength_sq : (T)0.0;

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
                              T d = v1 - v2;
                              dist_sq += d * d;
                            }
                        }
                      
                      /* Weighting function.  */
                      T weight = std::exp (-dist_sq * inv_strength_sq);
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
  
  /* Copy result back to tile buffer so get_pixel works correctly.  */
  std::copy (m_data[thread_id].out_tile.begin (), m_data[thread_id].out_tile.end (),
             m_data[thread_id].tile.begin ());
}

template class denoising<float>;
template class denoising<double>;

}
