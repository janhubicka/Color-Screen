/* Slanted edge MTF implementation.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#include "include/colorscreen.h"
#include "include/mtf-parameters.h"
#include "fft.h"
#include "render.h"
#include <cmath>
#include <vector>
#include <numeric>

namespace colorscreen {

slanted_edge_results
slanted_edge_mtf (render_parameters &rparam, const image_data &img, int_image_area roi,
                  const slanted_edge_parameters &params, progress_info *progress)
{
  slanted_edge_results res;
  res.edge_p1.x = 0;
  res.edge_p1.y = 0;
  res.edge_p2.x = 0;
  res.edge_p2.y = 0;

  render r(img, rparam, 65535);
  if (!r.precompute_all(true, false, {1, 1, 1}, progress))
    return res;

  // Make sure ROI is within image bounds
  roi = roi.intersect (int_image_area (0, 0, img.width, img.height));
  if (roi.empty_p () || roi.width < 10 || roi.height < 10)
    return res;

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

  std::vector<double> edge_pos;
  std::vector<double> line_coord;

  if (is_vertical)
    {
      for (int y = r_top; y < r_bottom; y++)
        {
          double centroid = 0;
          double sum_w = 0;
          for (int x = r_left + 1; x < r_right - 1; x++)
            {
              double w = r.get_unadjusted_data({x + 1, y}) - r.get_unadjusted_data({x - 1, y});
              w = std::abs(w);
              centroid += x * w;
              sum_w += w;
            }
          if (sum_w > 0)
            {
              edge_pos.push_back(centroid / sum_w);
              line_coord.push_back(y);
            }
        }
    }
  else
    {
      for (int x = r_left; x < r_right; x++)
        {
          double centroid = 0;
          double sum_w = 0;
          for (int y = r_top + 1; y < r_bottom - 1; y++)
            {
              double w = r.get_unadjusted_data({x, y + 1}) - r.get_unadjusted_data({x, y - 1});
              w = std::abs(w);
              centroid += y * w;
              sum_w += w;
            }
          if (sum_w > 0)
            {
              edge_pos.push_back(centroid / sum_w);
              line_coord.push_back(x);
            }
        }
    }

  if (edge_pos.size() < 5)
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

  // Set the actual detected edge coordinates
  if (is_vertical)
    {
      res.edge_p1.x = (coord_t)(A * r_top + B);
      res.edge_p1.y = (coord_t)r_top;
      res.edge_p2.x = (coord_t)(A * r_bottom + B);
      res.edge_p2.y = (coord_t)r_bottom;
    }
  else
    {
      res.edge_p1.x = (coord_t)r_left;
      res.edge_p1.y = (coord_t)(A * r_left + B);
      res.edge_p2.x = (coord_t)r_right;
      res.edge_p2.y = (coord_t)(A * r_right + B);
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

  int oversampling = params.oversampling > 0 ? params.oversampling : 4;
  int num_bins = std::ceil((max_d - min_d) * oversampling) + 1;
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

  for (int i = 0; i < num_bins; i++)
    res.edge_histogram.push_back(esf[i]);

  // Derivative to get LSF (Line Spread Function)
  // Use central difference [1, 0, -1] / 2
  std::vector<double> lsf(num_bins, 0);
  for (int i = 1; i < num_bins - 1; i++)
    lsf[i] = std::abs(esf[i+1] - esf[i-1]) / 2.0;
  lsf[0] = std::abs(esf[1] - esf[0]);
  lsf[num_bins - 1] = std::abs(esf[num_bins - 1] - esf[num_bins - 2]);

  // Find LSF peak
  int peak_idx = 0;
  double peak_val = -1;
  for (int i = 0; i < num_bins; i++)
    if (lsf[i] > peak_val)
      {
        peak_val = lsf[i];
        peak_idx = i;
      }

  // Apply Hamming window centered at peak
  int win_half = std::min(peak_idx, num_bins - 1 - peak_idx);
  int win_len = 2 * win_half + 1;
  
  // Create FFT input array
  int N = 1;
  while (N < num_bins) N <<= 1;
  N <<= 1; // zero padding for smoother MTF

  std::vector<double> in_vec(N, 0);
  for (int i = 0; i < win_len; i++)
    {
      double hamming = 0.54 - 0.46 * std::cos(2.0 * M_PI * i / (win_len - 1));
      in_vec[i] = lsf[peak_idx - win_half + i] * hamming;
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
  if (mtf_zero > 0)
    {
      for (int i = 0; i <= N / 2; i++)
        mtf[i] /= mtf_zero;
    }

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
      measurement.add_value(freq, mtf[i]);
    }

  rparam.sharpen.scanner_mtf.measurements.push_back(measurement);

  return res;
}

}
