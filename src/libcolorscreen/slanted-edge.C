/* Slanted edge MTF implementation.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#include "include/colorscreen.h"
#include "include/mtf-parameters.h"
#include "fft.h"
#include "render.h"
#include <climits>
#include <cmath>
#include <vector>
#include <numeric>

namespace colorscreen {
namespace {

constexpr int default_slanted_edge_oversampling = 10;
constexpr int max_slanted_edge_oversampling = 64;
constexpr int max_slanted_edge_bins = 8 * 1024 * 1024;

/* Return the reciprocal of sin (X) / X without losing accuracy near zero.  */
static inline double
inverse_sinc (double x)
{
  double x2 = x * x;
  if (x2 < 1.0e-12)
    return 1.0 + x2 / 6.0 + 7.0 * x2 * x2 / 360.0;
  return x / std::sin (x);
}

} // anonymous namespace


slanted_edge_results
slanted_edge_mtf (render_parameters &rparam, const image_data &img, int_image_area roi,
                  const slanted_edge_parameters &params, progress_info *progress)
{
  slanted_edge_results res;
  res.success = false;
  res.edge_p1.x = 0;
  res.edge_p1.y = 0;
  res.edge_p2.x = 0;
  res.edge_p2.y = 0;

  int oversampling = params.oversampling > 0
                     ? params.oversampling
                     : default_slanted_edge_oversampling;
  if (oversampling < 2 || oversampling > max_slanted_edge_oversampling)
    {
      if (progress)
        fprintf (stderr,
                 "Slanted-edge failed: oversampling %d is outside "
                 "the supported range 2..%d\n",
                 oversampling, max_slanted_edge_oversampling);
      return res;
    }

  // Make sure ROI is within image bounds before expensive precomputation.
  roi = roi.intersect (int_image_area (0, 0, img.width, img.height));
  if (roi.empty_p () || roi.width < 10 || roi.height < 10)
    return res;

  /* Measure the input image, not an image already modified by unsharp masking
     or deconvolution.  Preserve linearization and backlight correction while
     disabling sharpening only in the private render used for measurement.  */
  render_parameters measurement_rparam = rparam;
  measurement_rparam.sharpen.mode = sharpen_parameters::none;
  render r (img, measurement_rparam, 65535);
  if (!r.precompute_all (true, false, {1, 1, 1}, progress))
    {
      if (progress)
        fprintf (stderr, "Slanted-edge failed: precompute_all failed\n");
      return res;
    }

  int r_top = roi.y;
  int r_bottom = roi.y + roi.height;
  int r_left = roi.x;
  int r_right = roi.x + roi.width;

  // 1. Determine edge orientation and compute centroids
  // We'll compute horizontal gradient (for vertical edge) and vertical gradient (for horizontal edge)
  double sum_gx = 0, sum_gy = 0;
  for (int y = r_top + 1; y < r_bottom - 1; y++)
    for (int x = r_left + 1; x < r_right - 1; x++)
      {
        double dx = r.get_unadjusted_data({x + 1, y}) - r.get_unadjusted_data({x - 1, y});
        double dy = r.get_unadjusted_data({x, y + 1}) - r.get_unadjusted_data({x, y - 1});
        sum_gx += std::abs(dx);
        sum_gy += std::abs(dy);
      }

  bool is_vertical = sum_gx > sum_gy;
  if (progress)
    fprintf(stderr, "Slanted-edge: orientation %s, gx=%.4f, gy=%.4f\n", 
            is_vertical ? "vertical" : "horizontal", sum_gx, sum_gy);

  std::vector<double> edge_pos;
  std::vector<double> line_coord;

  if (is_vertical)
    {
      for (int y = r_top; y < r_bottom; y++)
        {
          double max_w = -1;
          int max_w_x = -1;
          for (int x = r_left + 1; x < r_right - 1; x++)
            {
              double w = std::abs(r.get_unadjusted_data({x + 1, y}) - r.get_unadjusted_data({x - 1, y}));
              if (w > max_w)
                {
                  max_w = w;
                  max_w_x = x;
                }
            }
          if (max_w > 0.0001)
            {
              double sum_w = 0;
              double sum_pos_w = 0;
              for (int x = std::max (r_left + 1, max_w_x - 10);
                   x <= std::min (r_right - 2, max_w_x + 10); x++)
                {
                  double w = std::abs(r.get_unadjusted_data({x + 1, y}) - r.get_unadjusted_data({x - 1, y}));
                  sum_w += w;
                  sum_pos_w += w * x;
                }
              if (sum_w > 0)
                {
                  double edge_p = sum_pos_w / sum_w;
                  edge_pos.push_back(edge_p);
                  line_coord.push_back(y);
                }
            }
        }
    }
  else
    {
      for (int x = r_left; x < r_right; x++)
        {
          double max_w = -1;
          int max_w_y = -1;
          for (int y = r_top + 1; y < r_bottom - 1; y++)
            {
              double w = std::abs(r.get_unadjusted_data({x, y + 1}) - r.get_unadjusted_data({x, y - 1}));
              if (w > max_w)
                {
                  max_w = w;
                  max_w_y = y;
                }
            }
          if (max_w > 0.0001)
            {
              double sum_w = 0;
              double sum_pos_w = 0;
              for (int y = std::max (r_top + 1, max_w_y - 10);
                   y <= std::min (r_bottom - 2, max_w_y + 10); y++)
                {
                  double w = std::abs(r.get_unadjusted_data({x, y + 1}) - r.get_unadjusted_data({x, y - 1}));
                  sum_w += w;
                  sum_pos_w += w * y;
                }
              if (sum_w > 0)
                {
                  double edge_p = sum_pos_w / sum_w;
                  edge_pos.push_back(edge_p);
                  line_coord.push_back(x);
                }
            }
        }
    }

  if (edge_pos.size() < 10)
    return res;

  // Linear regression: pos = A * coord + B
  double sum_c = 0, sum_p = 0, sum_cc = 0, sum_cp = 0;
  int n = edge_pos.size();
  for (int i = 0; i < n; i++)
    {
      sum_c += line_coord[i];
      sum_p += edge_pos[i];
      sum_cc += line_coord[i] * line_coord[i];
      sum_cp += line_coord[i] * edge_pos[i];
    }

  double det = n * sum_cc - sum_c * sum_c;
  if (std::abs(det) < 1e-6)
    return res;

  double A = (n * sum_cp - sum_c * sum_p) / det;
  double B = (sum_cc * sum_p - sum_c * sum_cp) / det;

  // Compute R-squared to check fit quality
  double ss_res = 0, ss_tot = 0;
  double mean_p = sum_p / n;
  for (int i = 0; i < n; i++)
    {
      double pred = A * line_coord[i] + B;
      ss_res += (edge_pos[i] - pred) * (edge_pos[i] - pred);
      ss_tot += (edge_pos[i] - mean_p) * (edge_pos[i] - mean_p);
    }
  if (ss_tot > 0 && ss_res / ss_tot > 0.1) // Too much noise in edge detection
    {
      if (progress) fprintf(stderr, "Slanted-edge failed: too much noise in edge fit (res/tot = %.4f)\n", ss_res / ss_tot);
      return res;
    }

  // Set the actual detected edge coordinates
  if (is_vertical)
    {
      res.edge_p1.x = (coord_t)(A * r_top + B);
      res.edge_p1.y = (coord_t)r_top;
      res.edge_p2.x = (coord_t)(A * (r_bottom - 1) + B);
      res.edge_p2.y = (coord_t)(r_bottom - 1);
    }
  else
    {
      res.edge_p1.x = (coord_t)r_left;
      res.edge_p1.y = (coord_t)(A * r_left + B);
      res.edge_p2.x = (coord_t)(r_right - 1);
      res.edge_p2.y = (coord_t)(A * (r_right - 1) + B);
    }

  // Calculate ESF using super-sampling
  // Distance of a point (x,y) to the line x - A*y - B = 0 (if vertical)
  // or y - A*x - B = 0 (if horizontal).
  double a, b, c;
  if (is_vertical)
    {
      a = 1.0;
      b = -A;
      c = -B;
    }
  else
    {
      a = -A;
      b = 1.0;
      c = -B;
    }
  double norm = std::sqrt(a*a + b*b);
  a /= norm;
  b /= norm;
  c /= norm;

  // Find min and max distance to define bin range
  double min_d = 1e9, max_d = -1e9;
  for (int y = r_top; y < r_bottom; y++)
    for (int x = r_left; x < r_right; x++)
      {
        double d = a * x + b * y + c;
        min_d = std::min(min_d, d);
        max_d = std::max(max_d, d);
      }

  double num_bins_d = std::ceil ((max_d - min_d) * oversampling) + 1.0;
  if (!my_isfinite (num_bins_d) || num_bins_d < 4
      || num_bins_d > max_slanted_edge_bins)
    {
      if (progress)
        fprintf (stderr,
                 "Slanted-edge failed: invalid or excessive ESF size %.0f\n",
                 num_bins_d);
      return res;
    }
  int num_bins = (int)num_bins_d;
  std::vector<double> esf_sum(num_bins, 0);
  std::vector<int> esf_count(num_bins, 0);

  for (int y = r_top; y < r_bottom; y++)
    for (int x = r_left; x < r_right; x++)
      {
        double d = a * x + b * y + c;
        int bin = std::round((d - min_d) * oversampling);
        if (bin >= 0 && bin < num_bins)
          {
            esf_sum[bin] += r.get_unadjusted_data({x, y});
            esf_count[bin]++;
          }
      }

  // Fill empty bins by interpolation
  std::vector<double> esf(num_bins, 0);
  for (int i = 0; i < num_bins; i++)
    {
      if (esf_count[i] > 0)
        esf[i] = esf_sum[i] / esf_count[i];
      else
        {
          // Find nearest valid neighbors
          int left = i - 1;
          while (left >= 0 && esf_count[left] == 0) left--;
          int right = i + 1;
          while (right < num_bins && esf_count[right] == 0) right++;
          
          if (left >= 0 && right < num_bins)
            esf[i] = esf_sum[left] / esf_count[left] + 
                     (double)(i - left) / (right - left) * 
                     (esf_sum[right] / esf_count[right] - esf_sum[left] / esf_count[left]);
          else if (left >= 0)
            esf[i] = esf_sum[left] / esf_count[left];
          else if (right < num_bins)
            esf[i] = esf_sum[right] / esf_count[right];
        }
    }

  double min_esf = 1e9, max_esf = -1e9;
  for (int i = 0; i < num_bins; i++)
    {
      res.edge_histogram.push_back(esf[i]);
      if (esf[i] < min_esf) min_esf = (double)esf[i];
      if (esf[i] > max_esf) max_esf = (double)esf[i];
    }

  double contrast = max_esf - min_esf;
  if (progress)
    {
      fprintf (stderr,
               "Slanted-edge: ESF range [%.4f, %.4f], contrast %.4f\n",
               min_esf, max_esf, contrast);
      fflush (stderr);
    }

  if (!my_isfinite (contrast) || contrast <= 1.0e-9)
    {
      if (progress)
        fprintf (stderr,
                 "Slanted-edge failed: zero or non-finite edge contrast\n");
      return res;
    }

  // Create FFT input array
  int N = 1;
  while (N < num_bins)
    {
      if (N > INT_MAX / 2)
        return res;
      N <<= 1;
    }
  if (N > INT_MAX / 2)
    return res;
  N <<= 1; // zero padding for smoother MTF

  if (progress)
    fprintf(stderr, "Slanted-edge: num_bins=%d, oversampling=%d, N=%d\n", num_bins, oversampling, N);

  std::vector<double> lsf(num_bins, 0);
  int peak_idx = 0;
  double max_lsf = -1;
  // Use [-1, 0, 1] central difference as in QuickMTF
  for (int i = 1; i < num_bins - 1; i++)
    lsf[i] = (esf[i+1] - esf[i-1]) / 2.0;
  lsf[0] = esf[1] - esf[0];
  lsf[num_bins - 1] = esf[num_bins - 1] - esf[num_bins - 2];

  for (int i = 0; i < num_bins; i++)
    if (std::abs (lsf[i]) > max_lsf)
      {
        max_lsf = std::abs (lsf[i]);
        peak_idx = i;
      }
  if (!my_isfinite (max_lsf) || max_lsf <= 1.0e-12)
    {
      if (progress)
        fprintf (stderr, "Slanted-edge failed: zero or non-finite LSF\n");
      return res;
    }

  std::vector<double, fft_allocator<double>> in_vec(N, 0.0);
  for (int i = 0; i < N; i++)
    {
      // Apply Hann window centered at the peak
      int dist = i - peak_idx;
      int window_idx = dist + num_bins / 2;
      double w = 0;
      if (window_idx >= 0 && window_idx < num_bins)
        w = 0.5 * (1.0 - std::cos(2.0 * M_PI * window_idx / (num_bins - 1)));
      
      if (i < num_bins)
        in_vec[i] = lsf[i] * w;
    }

  auto out = fft_alloc_complex<double>(N / 2 + 1);
  fft_plan<double> plan = fft_plan_r2c_1d<double>(N, in_vec.data(), out.get());
  plan.execute_r2c(in_vec.data(), out.get());

  // Calculate MTF
  std::vector<double> mtf(N / 2 + 1);
  for (int i = 0; i <= N / 2; i++)
    {
      double re = out.get()[i][0];
      double im = out.get()[i][1];
      mtf[i] = std::sqrt(re * re + im * im);
    }

  // Normalize MTF
  double mtf_zero = mtf[0];
  if (!my_isfinite (mtf_zero) || mtf_zero < 1e-9)
    return res;

  for (int i = 0; i <= N / 2; i++)
    mtf[i] /= mtf_zero;

  double Fs = oversampling;
  
  mtf_measurement measurement;
  measurement.name = "Slanted edge MTF";
  measurement.channel = -1; // grayscale
  measurement.wavelength = 0;
  
  for (int i = 0; i <= N / 2; i++)
    {
      double freq = i * Fs / N;
      if (freq > 0.5) // Usually only care up to Nyquist
        break;
      
      double mtf_val = mtf[i];
      if (freq > 0.0)
        {
          double w_bin = M_PI * freq / Fs;
          double w_deriv = 2.0 * M_PI * freq / Fs;
          
          double correction = 1.0;
          // Binning correction (rect convolution)
          correction *= inverse_sinc (w_bin);
          // Derivative correction (central difference)
          correction *= inverse_sinc (w_deriv);
          
          mtf_val *= correction;
        }

      if (!my_isfinite (mtf_val))
        {
          if (progress)
            fprintf (stderr, "Slanted-edge failed: non-finite MTF value\n");
          return res;
        }
      measurement.add_value(freq, mtf_val * 100.0);
    }

  rparam.sharpen.scanner_mtf.measurements.push_back(measurement);

  res.success = true;
  return res;
}

}
