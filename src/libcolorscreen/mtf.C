/* Modulation transfer function and point spread function computation.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#include "include/tiff-writer.h"
#include "lru-cache.h"
#include "mtf.h"
#include "fft.h"
#include "nmsimplex.h"
#include "gsl-solver.h"
#include "include/colorscreen.h"
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <memory>
namespace colorscreen
{

namespace
{

/* Return the first-order Bessel function J1 evaluated at X.  */
double
get_j1 (double x)
{
#if defined(__cpp_lib_math_special_functions)                                 \
    || defined(_GLIBCXX_USE_STD_SPEC_FUNCS)
  return std::cyl_bessel_j (1, x);
#elif defined(_WIN32) && !defined(__cpp_lib_math_special_functions)
  return _j1 (x);
#else
  return j1 (x); // Fallback for macOS/libc++
#endif
}

/* Return MTF of a uniformly illuminated circular blur.
   FREQ is the spatial frequency.
   BLUR_CIRCLE_DIAMETER is the diameter of the blur circle.
   This geometric fallback is used only when the physical diffraction model
   cannot be constructed from capture metadata.  */
double
circular_blur_mtf (double freq, double blur_circle_diameter)
{
  /* If perfectly in focus or frequency is almost zero, return one.  */
  if (blur_circle_diameter < 1e-12 || my_fabs (freq) < 1e-12)
    return 1;

  /* The transfer function of a circular blur is 2*J1(X)/X.  */
  double arg = M_PI * freq * blur_circle_diameter;
  return my_fabs (2.0 * get_j1 (arg) / arg);
}

/* Return MTF of gaussian blur.
   FREQ is the spatial frequency.
   SIGMA is the standard deviation of the Gaussian.  */
double
gaussian_blur_mtf (double freq, double sigma)
{
  return std::exp (-2.0 * M_PI * M_PI * sigma * sigma * freq * freq);
}

/* Return normalized sinc sin (pi * X) / (pi * X).
   This is used to model the sensor pixel-aperture effect.  */
double
sinc (double x)
{
  double pi_x = M_PI * x;
  double pi_x2 = pi_x * pi_x;
  if (pi_x2 < 1.0e-8)
    return 1.0 - pi_x2 / 6.0 + pi_x2 * pi_x2 / 120.0;
  return my_sin (pi_x) / pi_x;
}

/* Return the diffraction-limited incoherent OTF of an unobstructed circular
   pupil at normalized spatial frequency Q.  Q is frequency divided by the
   diffraction cutoff.  */
double
circular_pupil_diffraction_otf (double q)
{
  if (q <= 0)
    return 1;
  if (q >= 1)
    return 0;

  /* Writing Q=cos (THETA) avoids the least stable form sqrt (1-Q*Q).
     Near the cutoff THETA and sin (THETA)*cos (THETA) nearly cancel, so use
     the series of THETA-sin(2*THETA)/2 in that small interval.  */
  double theta = my_acos (q);
  if (theta < 0.1)
    {
      double theta2 = theta * theta;
      double correction
          = 1.0 - theta2 / 5.0 + 2.0 * theta2 * theta2 / 105.0
            - theta2 * theta2 * theta2 / 945.0;
      return (4.0 / (3.0 * M_PI)) * theta * theta2 * correction;
    }
  return (2.0 / M_PI)
         * (theta - q * my_sqrt ((1.0 - q) * (1.0 + q)));
}

/* Nodes and weights of the positive half of the 16-point Gauss--Legendre
   quadrature rule.  Long double accumulation keeps the pupil integral more
   accurate than the float image FFT which consumes the resulting table.  */
constexpr std::array<long double, 8> gauss_legendre_16_nodes = {
  0.095012509837637440185319335424958063L,
  0.281603550779258913230460501460496106L,
  0.458016777657227386342419442983577574L,
  0.617876244402643748446671764048791019L,
  0.755404408355003033895101194847442268L,
  0.865631202387831743880467897712393132L,
  0.944575023073232576077988415534608345L,
  0.989400934991649932596154173450332627L
};

constexpr std::array<long double, 8> gauss_legendre_16_weights = {
  0.189450610455068496285396723208283105L,
  0.182603415044923588866763667969219939L,
  0.169156519395002538189312079030359962L,
  0.149595988816576732081501730547478549L,
  0.124628971255533872052476282192016420L,
  0.095158511682492784809925107602246226L,
  0.062253523938647892862843836994377694L,
  0.027152459411754094851780572456018104L
};

/* Evaluate the regularized circular-pupil overlap integral.
   OVERLAP is 1-Q, where Q is normalized spatial frequency.
   EDGE_PHASE is the defocus phase difference at the end of the overlap lens.

   The substitution x=(1-Q)*(1-u^2) removes the square-root endpoint from the
   direct pupil-autocorrelation integral.  Panels are added for oscillatory
   cases so one panel covers no more than approximately one phase half-cycle.  */
long double
defocused_pupil_integral (long double overlap, long double edge_phase)
{
  constexpr long double pi
      = 3.141592653589793238462643383279502884L;
  constexpr int max_panels = 4096;
  long double phase = std::fabs (edge_phase);

  /* Beyond this point the normalized integral is below the precision useful
     for an image MTF, while evaluating tens of thousands of oscillatory panels
     could make an accidental parameter value unreasonably expensive.  */
  if (phase > max_panels * pi)
    return 0;

  int panels = std::max (1, (int)std::ceil (phase / pi));
  long double sum = 0;
  for (int panel = 0; panel < panels; panel++)
    {
      long double left = panel / (long double)panels;
      long double right = (panel + 1) / (long double)panels;
      long double center = (left + right) * 0.5L;
      long double half_width = (right - left) * 0.5L;
      long double panel_sum = 0;
      for (size_t i = 0; i < gauss_legendre_16_nodes.size (); i++)
        for (int sign : {-1, 1})
          {
            long double u
                = center + sign * half_width * gauss_legendre_16_nodes[i];
            long double u2 = u * u;
            long double amplitude
                = u2 * std::sqrt (2.0L - overlap * u2);
            panel_sum += gauss_legendre_16_weights[i] * amplitude
                         * std::cos (edge_phase * (1.0L - u2));
          }
      sum += half_width * panel_sum;
    }
  return sum;
}

/* Return the signed exact defocus factor of an incoherent circular pupil.
   Q is normalized spatial frequency and EDGE_PHASE is the phase difference at
   the end of the pupil-overlap lens.  The diffraction-limited OTF is factored
   out, so the zero-defocus result and the cutoff limit are both one.  */
double
circular_pupil_defocus_factor (double q, double edge_phase)
{
  if (q <= 0 || q >= 1 || my_fabs (edge_phase) < 1.0e-12)
    return 1;
  if (!my_isfinite (q) || !my_isfinite (edge_phase))
    return 0;

  long double overlap = 1.0L - (long double)q;
  long double numerator
      = defocused_pupil_integral (overlap, (long double)edge_phase);
  long double denominator = defocused_pupil_integral (overlap, 0);
  if (!(denominator > 0))
    return 0;
  return (double)(numerator / denominator);
}

/* Return authoritative wavelength metadata for MEASUREMENT in PARAMS, or
   zero when neither the measurement, its labelled channel, nor the global
   narrow-band setting supplies a positive finite wavelength.  */
static double
measurement_wavelength (const mtf_parameters &params,
                        const mtf_measurement &measurement)
{
  if (my_isfinite (measurement.wavelength) && measurement.wavelength > 0)
    return measurement.wavelength;
  if (measurement.channel >= 0 && measurement.channel < 4
      && my_isfinite (params.wavelengths[measurement.channel])
      && params.wavelengths[measurement.channel] > 0)
    return params.wavelengths[measurement.channel];
  if (my_isfinite (params.wavelength) && params.wavelength > 0)
    return params.wavelength;
  return 0;
}

/* Return true when MODEL is one of the public MTF_MODEL enumerators.  This
   protects project files and front ends which pass the enum through an integer
   representation.  */
static bool
valid_mtf_model_p (mtf_model model)
{
  return model == mtf_model::automatic_legacy
         || model == mtf_model::physical_diffraction
         || model == mtf_model::empirical_fallback;
}

/* Lower and upper limits of a wavelength estimated from an MTF curve.  Fixed
   metadata may lie outside this interval, but the inverse problem is too weak
   to search an unrestricted spectral range safely.  */
constexpr double fitted_wavelength_min_nm = 380;
constexpr double fitted_wavelength_max_nm = 1000;

/* Historical wavelength ranges used by the compatibility fitting API.  The
   old interface fitted one shared coordinate for all unknown curves carrying
   the same channel label.  */
constexpr std::array<double, 8> legacy_channel_wavelength_ranges = {
  580, 750,  /* Red range.  */
  480, 580,  /* Green range.  */
  380, 480,  /* Blue range.  */
  750, 1000, /* Infrared range.  */
};

/* Fit the physical or fallback MTF model to one or more measured curves.  */
class mtf_solver
{
public:
  /* Construct a solver for PARAMS and MEASURED curves according to OPTIONS.
     PROGRESS receives progress updates and VERBOSE enables iteration
     diagnostics.  Numeric zeroes remain ordinary values; OPTIONS alone
     decides which coordinates are fitted.  */
  mtf_solver (const mtf_parameters &params,
              const std::vector<mtf_measurement> &measured,
              const mtf_estimation_options &options,
              progress_info *progress, bool verbose,
              bool legacy_channel_wavelengths)
      : m_measurements (measured), m_params (params), m_options (options),
        m_progress (progress), be_verbose (verbose), start_vec (),
        fit_weights (), start (nullptr), diffraction (false),
        m_legacy_channel_wavelengths (legacy_channel_wavelengths), nvalues (0),
        n_observations (0), sigma_index (-1), fill_factor_index (-1),
        wavelength_index (), channel_wavelength_index (), blur_index (),
        f_stop_index (-1), halo_fraction_index (-1), halo_sigma_index (-1)
  {
    channel_wavelength_index.fill (-1);
    m_params.measured_mtf_idx = -1;
    m_params.clear_data ();
    mtf_model requested_model = options.model == mtf_model::automatic_legacy
                                    ? params.model
                                    : options.model;
    diffraction = requested_model == mtf_model::physical_diffraction
                  || (requested_model == mtf_model::automatic_legacy
                      && my_isfinite (params.pixel_pitch)
                      && params.pixel_pitch > 0
                      && my_isfinite (params.scan_dpi)
                      && params.scan_dpi > 0);
    m_params.model = diffraction ? mtf_model::physical_diffraction
                                 : mtf_model::empirical_fallback;

    wavelength_index.assign (m_measurements.size (), -1);
    blur_index.assign (m_measurements.size (), -1);

    if (options.optimize_sigma)
      {
        start_vec.push_back (my_isfinite (m_params.sigma)
                                 && m_params.sigma >= 0
                             ? m_params.sigma
                             : 0.0);
        sigma_index = nvalues++;
      }

    if (diffraction)
      {
        if (options.optimize_f_stop)
          {
            start_vec.push_back (my_isfinite (m_params.f_stop)
                                     && m_params.f_stop > 0
                                 ? m_params.f_stop
                                 : 8.0);
            f_stop_index = nvalues++;
          }

        int current_defocus_index = -1;
        for (size_t measurement = 0; measurement < m_measurements.size ();
             measurement++)
          {
            if (!m_measurements[measurement].same_capture)
              current_defocus_index = -1;
            if (!m_options.include_measurement_p (measurement))
              continue;

            if (options.optimize_defocus)
              {
                if (current_defocus_index < 0)
                  {
                    start_vec.push_back (my_isfinite (m_params.defocus)
                                             && m_params.defocus >= 0
                                         ? m_params.defocus
                                         : 0.0);
                    current_defocus_index = nvalues++;
                  }
                blur_index[measurement] = current_defocus_index;
              }

            if (options.optimize_measurement_wavelength_p (measurement))
              {
                const int channel = m_measurements[measurement].channel;
                if (m_legacy_channel_wavelengths && channel >= 0
                    && channel < 4)
                  {
                    /* Preserve the historical API: one unknown wavelength was
                       shared by all measurements with the same channel label,
                       and each channel used its own conservative search range.  */
                    if (channel_wavelength_index[channel] < 0)
                      {
                        start_vec.push_back (0.5);
                        channel_wavelength_index[channel] = nvalues++;
                      }
                    wavelength_index[measurement]
                        = channel_wavelength_index[channel];
                  }
                else
                  {
                    double wavelength = fixed_wavelength (measurement);
                    if (!(my_isfinite (wavelength) && wavelength > 0))
                      wavelength = channel == 3 ? 850.0 : 550.0;
                    wavelength
                        = std::clamp (wavelength, fitted_wavelength_min_nm,
                                     fitted_wavelength_max_nm);
                    start_vec.push_back (
                        (wavelength - fitted_wavelength_min_nm)
                        / (fitted_wavelength_max_nm
                           - fitted_wavelength_min_nm));
                    wavelength_index[measurement] = nvalues++;
                  }
              }
          }

        if (options.optimize_sensor_fill_factor)
          {
            start_vec.push_back (my_isfinite (m_params.sensor_fill_factor)
                                     && m_params.sensor_fill_factor > 0
                                 ? m_params.sensor_fill_factor
                                 : 1.0);
            fill_factor_index = nvalues++;
          }

        if (options.optimize_halo_fraction)
          {
            start_vec.push_back (my_isfinite (m_params.halo_fraction)
                                     && m_params.halo_fraction >= 0
                                     && m_params.halo_fraction <= 0.95
                                 ? m_params.halo_fraction
                                 : 0.1);
            halo_fraction_index = nvalues++;
          }
        if (options.optimize_halo_sigma)
          {
            start_vec.push_back (my_isfinite (m_params.halo_sigma)
                                     && m_params.halo_sigma > 0
                                 ? m_params.halo_sigma
                                 : 5.0);
            halo_sigma_index = nvalues++;
          }
      }
    else
      {
        int current_blur_index = -1;
        for (size_t measurement = 0; measurement < m_measurements.size ();
             measurement++)
          {
            if (!m_measurements[measurement].same_capture)
              current_blur_index = -1;
            if (!m_options.include_measurement_p (measurement))
              continue;
            if (options.optimize_blur_diameter)
              {
                if (current_blur_index < 0)
                  {
                    start_vec.push_back (my_isfinite (m_params.blur_diameter)
                                             && m_params.blur_diameter >= 0
                                         ? m_params.blur_diameter
                                         : 0.0);
                    current_blur_index = nvalues++;
                  }
                blur_index[measurement] = current_blur_index;
              }
          }
      }

    fit_weights.resize (m_measurements.size ());
    std::vector<double> uncertainty_medians (m_measurements.size (), 0.0);
    std::vector<double> uncertainty_floors (m_measurements.size (), 0.0);
    std::vector<bool> has_uncertainty_weights (m_measurements.size (), false);
    for (size_t measurement = 0; measurement < m_measurements.size ();
         measurement++)
      {
        const mtf_measurement &curve = m_measurements[measurement];
        fit_weights[measurement].assign (curve.size (), 1.0);
        if (!m_options.include_measurement_p (measurement))
          continue;

        std::vector<double> uncertainties;
        for (size_t i = 0; i < curve.size (); i++)
          if (curve.get_freq (i) <= 0.5)
            {
              n_observations++;
              const double uncertainty = curve.get_uncertainty (i);
              if (my_isfinite (uncertainty) && uncertainty > 0)
                uncertainties.push_back (uncertainty);
            }

        /* New slanted-edge curves can carry a one-sigma uncertainty for each
           MTF sample.  Use it only when enough samples define a stable scale.
           A 0.25 percentage-point systematic floor prevents DC and accidental
           low-variance bins from dominating the physical fit.  Start with
           per-curve RMS normalization; this is the compatibility fallback for
           isolated curves and for capture groups which contain a legacy curve
           without usable uncertainty metadata.  A complete repeated-capture
           group is renormalized jointly below so absolute uncertainty can also
           redistribute influence between its curves.  */
        if (uncertainties.size () >= 3)
          {
            const size_t middle = uncertainties.size () / 2;
            std::nth_element (uncertainties.begin (),
                              uncertainties.begin () + middle,
                              uncertainties.end ());
            const double median_uncertainty = uncertainties[middle];
            uncertainty_medians[measurement] = median_uncertainty;
            const double uncertainty_floor
                = std::max (0.25, 0.25 * median_uncertainty);
            uncertainty_floors[measurement] = uncertainty_floor;
            has_uncertainty_weights[measurement] = true;
            long double sum_weights_squared = 0;
            size_t weighted_points = 0;
            for (size_t i = 0; i < curve.size (); i++)
              if (curve.get_freq (i) <= 0.5)
                {
                  double uncertainty = curve.get_uncertainty (i);
                  if (!(my_isfinite (uncertainty) && uncertainty > 0))
                    uncertainty = median_uncertainty;
                  const double weight
                      = 1.0 / std::max (uncertainty, uncertainty_floor);
                  fit_weights[measurement][i] = weight;
                  sum_weights_squared
                      += (long double)weight * (long double)weight;
                  weighted_points++;
                }
            if (sum_weights_squared > 0 && weighted_points)
              {
                const double normalization
                    = my_sqrt ((double)((long double)weighted_points
                                        / sum_weights_squared));
                for (size_t i = 0; i < curve.size (); i++)
                  if (curve.get_freq (i) <= 0.5)
                    fit_weights[measurement][i] *= normalization;
              }
          }
      }
    /* SAME_CAPTURE marks a curve as belonging to the same physical capture as
       the preceding curve.  When two or more included curves in such a group
       all have usable uncertainty metadata, retain their raw inverse-sigma
       ratio and apply one normalization to the whole capture.  In particular,
       constant 0.25- and 5-percentage-point uncertainties keep a 20:1 residual
       weight ratio instead of each curve being separately normalized back to
       unit RMS weight.  The common normalization keeps sum(w^2) equal to the
       number of included observations, so the capture's overall scale remains
       comparable with the historical uniformly weighted objective.  */
    for (size_t first = 0; first < m_measurements.size ();)
      {
        size_t last = first + 1;
        while (last < m_measurements.size ()
               && m_measurements[last].same_capture)
          last++;

        size_t included_curves = 0;
        bool complete_uncertainty = true;
        for (size_t measurement = first; measurement < last; measurement++)
          if (m_options.include_measurement_p (measurement))
            {
              included_curves++;
              complete_uncertainty
                  = complete_uncertainty
                    && has_uncertainty_weights[measurement];
            }

        if (included_curves >= 2 && complete_uncertainty)
          {
            long double sum_weights_squared = 0;
            size_t weighted_points = 0;
            for (size_t measurement = first; measurement < last; measurement++)
              if (m_options.include_measurement_p (measurement))
                {
                  const mtf_measurement &curve = m_measurements[measurement];
                  for (size_t i = 0; i < curve.size (); i++)
                    if (curve.get_freq (i) <= 0.5)
                      {
                        double uncertainty = curve.get_uncertainty (i);
                        if (!(my_isfinite (uncertainty) && uncertainty > 0))
                          uncertainty = uncertainty_medians[measurement];
                        const double weight
                            = 1.0 / std::max (
                                        uncertainty,
                                        uncertainty_floors[measurement]);
                        fit_weights[measurement][i] = weight;
                        sum_weights_squared
                            += (long double)weight * (long double)weight;
                        weighted_points++;
                      }
                }
            if (sum_weights_squared > 0 && weighted_points)
              {
                const double normalization
                    = my_sqrt ((double)((long double)weighted_points
                                        / sum_weights_squared));
                for (size_t measurement = first; measurement < last;
                     measurement++)
                  if (m_options.include_measurement_p (measurement))
                    {
                      const mtf_measurement &curve = m_measurements[measurement];
                      for (size_t i = 0; i < curve.size (); i++)
                        if (curve.get_freq (i) <= 0.5)
                          fit_weights[measurement][i] *= normalization;
                    }
              }
          }
        first = last;
      }

    start = start_vec.data ();
    sums.assign (m_measurements.size (), 0.0);
    assert (nvalues == (int)start_vec.size ());
  }

  /* Return the number of independently fitted scalar values.  */
  int
  num_values () const
  {
    return nvalues;
  }

  /* Return the objective-value tolerance used by the simplex solver.  */
  double
  epsilon () const
  {
    return 0.0000001;
  }

  /* Return the finite-difference step used by the least-squares solver.  */
  double
  derivative_perturbation () const
  {
    return 0.0001;
  }

  /* Return true when solver iteration diagnostics are requested.  */
  bool
  verbose () const
  {
    return be_verbose;
  }

  /* Return the common parameter scale expected by the generic solvers.  */
  double
  scale () const
  {
    return 1;
  }

  /* Project optimization vector VALS onto the physically allowed ranges.  */
  void
  constrain (double *vals)
  {
    if (fill_factor_index >= 0 && vals[fill_factor_index] < 0.1)
      vals[fill_factor_index] = 0.1;
    if (fill_factor_index >= 0 && vals[fill_factor_index] > 32)
      vals[fill_factor_index] = 32;
    if (sigma_index >= 0 && vals[sigma_index] < 0)
      vals[sigma_index] = 0;
    if (sigma_index >= 0 && vals[sigma_index] > 20)
      vals[sigma_index] = 20;
    if (halo_fraction_index >= 0)
      vals[halo_fraction_index]
          = std::clamp (vals[halo_fraction_index], 0.0, 0.95);
    if (halo_sigma_index >= 0)
      {
        /* Keep the optional component identifiable as a broad halo rather
           than allowing it to exchange labels with the residual core
           Gaussian.  This constraint applies only while fitting a missing
           halo width; an explicitly supplied value is left untouched.  */
        const double core_sigma
            = sigma_index >= 0 ? vals[sigma_index] : m_params.sigma;
        const double minimum_halo_sigma
            = std::min (std::max (1.0, 2.0 * core_sigma), 256.0);
        vals[halo_sigma_index]
            = std::clamp (vals[halo_sigma_index], minimum_halo_sigma, 256.0);
      }
    for (int e : blur_index)
      if (e >= 0)
        vals[e] = std::clamp (vals[e], 0.0, diffraction ? 20.0 : 64.0);
    for (int e : wavelength_index)
      if (e >= 0)
	vals[e] = std::clamp (vals[e], 0.0, 1.0);
    if (f_stop_index >= 0)
      vals[f_stop_index] = std::clamp (vals[f_stop_index],
                                      0.5, 128.0);
  }

  /* Return sensor fill factor from optimization vector VALS.  */
  double
  get_fill_factor (const double *vals)
  {
    if (fill_factor_index < 0)
      return m_params.sensor_fill_factor;
    return vals[fill_factor_index];
  }

  /* Return the fixed wavelength metadata for MEASUREMENT, or zero when no
     authoritative per-measurement, channel, or global value is available.  */
  double
  fixed_wavelength (size_t measurement) const
  {
    return measurement_wavelength (m_params, m_measurements[measurement]);
  }

  /* Return true when wavelength of MEASUREMENT is represented by a free
     coordinate in the optimization vector.  */
  bool
  wavelength_estimated_p (size_t measurement) const
  {
    return wavelength_index[measurement] >= 0;
  }

  /* Return true when MEASUREMENT participates in the objective.  */
  bool
  measurement_included_p (size_t measurement) const
  {
    return m_options.include_measurement_p (measurement);
  }

  /* Return the lower wavelength search bound for MEASUREMENT.  */
  double
  wavelength_min (size_t measurement) const
  {
    const int channel = m_measurements[measurement].channel;
    if (m_legacy_channel_wavelengths && channel >= 0 && channel < 4)
      return legacy_channel_wavelength_ranges[2 * channel];
    return fitted_wavelength_min_nm;
  }

  /* Return the upper wavelength search bound for MEASUREMENT.  */
  double
  wavelength_max (size_t measurement) const
  {
    const int channel = m_measurements[measurement].channel;
    if (m_legacy_channel_wavelengths && channel >= 0 && channel < 4)
      return legacy_channel_wavelength_ranges[2 * channel + 1];
    return fitted_wavelength_max_nm;
  }

  /* Return wavelength for MEASUREMENT from optimization vector VALS.  */
  double
  get_wavelength (size_t measurement, const double *vals) const
  {
    if (wavelength_index[measurement] >= 0)
      {
        const double minimum = wavelength_min (measurement);
        const double maximum = wavelength_max (measurement);
        return vals[wavelength_index[measurement]] * (maximum - minimum)
               + minimum;
      }
    return fixed_wavelength (measurement);
  }

  /* Return true when legacy channel C has one shared wavelength coordinate.  */
  bool
  channel_wavelength_estimated_p (int c) const
  {
    return c >= 0 && c < 4 && channel_wavelength_index[c] >= 0;
  }

  /* Return fitted or fixed wavelength for legacy channel C.  */
  double
  get_channel_wavelength (int c, const double *vals) const
  {
    if (channel_wavelength_estimated_p (c))
      return vals[channel_wavelength_index[c]]
                 * (legacy_channel_wavelength_ranges[2 * c + 1]
                    - legacy_channel_wavelength_ranges[2 * c])
             + legacy_channel_wavelength_ranges[2 * c];
    if (m_params.wavelengths[c] > 0)
      return m_params.wavelengths[c];
    return m_params.wavelength;
  }

  /* Return a representative wavelength for the fitted physical model.  The
     first included curve is used because one scalar MTF_PARAMETERS instance
     can display only one current optical wavelength at a time.  */
  double
  first_wavelength (const double *vals) const
  {
    for (size_t measurement = 0; measurement < m_measurements.size ();
         measurement++)
      if (measurement_included_p (measurement))
        return get_wavelength (measurement, vals);
    return m_params.wavelength;
  }

  /* Return marked f-number from optimization vector VALS.  */
  double
  get_f_stop (const double *vals)
  {
    if (f_stop_index < 0)
      return m_params.f_stop;
    return vals[f_stop_index];
  }

  /* Return residual Gaussian sigma from optimization vector VALS.  */
  double
  get_sigma (const double *vals)
  {
    if (sigma_index < 0)
      return m_params.sigma;
    return vals[sigma_index];
  }
  /* Return halo energy fraction from optimization vector VALS.  */
  double
  get_halo_fraction (const double *vals)
  {
    if (halo_fraction_index < 0)
      return m_params.halo_fraction;
    return vals[halo_fraction_index];
  }
  /* Return halo Gaussian radius in pixels from optimization vector VALS.  */
  double
  get_halo_sigma (const double *vals)
  {
    if (halo_sigma_index < 0)
      return m_params.halo_sigma;
    return vals[halo_sigma_index];
  }

  /* Return image-plane defocus for MEASUREMENT from vector VALS.  */
  double
  get_defocus (int measurement, const double *vals)
  {
    if (!diffraction || blur_index[measurement] < 0)
      return m_params.defocus;
    return vals[blur_index[measurement]];
  }

  /* Return fallback blur diameter for MEASUREMENT from vector VALS.  */
  double
  get_blur_diameter (int measurement, const double *vals)
  {
    if (diffraction || blur_index[measurement] < 0)
      return m_params.blur_diameter;
    return vals[blur_index[measurement]];
  }

  /* Return sum of squared residuals for vector VALS.  If F_VEC is non-null,
     also store each residual for the least-squares solver.  */
  double
  objfunc (const double *vals, double *f_vec = nullptr)
  {
    /* Use double to avoid accumulation of an error */
    double sum = 0;
    mtf_parameters p = m_params;
    p.sigma = get_sigma (vals);
    p.halo_fraction = get_halo_fraction (vals);
    p.halo_sigma = get_halo_sigma (vals);
    p.f_stop = get_f_stop (vals);
    p.sensor_fill_factor = get_fill_factor (vals);
    int out_idx = 0;
    for (size_t m = 0; m < m_measurements.size (); m++)
      {
	if (!measurement_included_p (m))
	  {
	    sums[m] = 0;
	    continue;
	  }
	auto &measurement = m_measurements[m];
	p.wavelength = get_wavelength (m, vals);
	if (diffraction)
	  p.defocus = get_defocus (m, vals);
	else
	  p.blur_diameter = get_blur_diameter (m, vals);
	assert (diffraction == p.simulate_diffraction_p ());
	/* Use double to avoid accumulation error in long measured curves.  */
	double msum = 0;
	for (size_t i = 0; i < measurement.size (); i++)
	  {
	    double freq = measurement.get_freq (i);
	    /* Do not care about values above Nyquist.  */
	    if (freq > 0.5)
	      continue;
	    double contrast = measurement.get_contrast (i);
	    /* A slanted-edge measurement contains MTF magnitude only.  Fit it to
	       the magnitude of the complete signed physical OTF; the analytical
	       model, not the measurement, supplies the sign after a phase reversal.  */
	    const double predicted_otf = p.system_otf (freq);
	    double contrast2 = my_fabs (predicted_otf) * 100;
	    const double residual = contrast - contrast2;
	    const double weighted_residual = residual * fit_weights[m][i];
	    msum += weighted_residual * weighted_residual;
	    if (f_vec)
	      {
		assert (out_idx < n_observations);
		f_vec[out_idx++] = weighted_residual;
	      }
#if 0
	    if (be_verbose)
	      debug_data (freq, contrast, contrast2);
#endif
	  }
	if (be_verbose)
	  {
	    if (m_progress)
	      m_progress->pause_stdout ();
	    printf ("measurement %zu fill factor %f, f-stop %f (%f) "
	            "gaussian blur sigma %f, halo fraction %f, halo "
	            "sigma %f, wavelength %f, defocus %f, "
	            "blur_diameter %f, sqsum %f\n",
	            m, p.sensor_fill_factor, p.f_stop,
	            p.effective_f_stop (), p.sigma, p.halo_fraction,
	            p.halo_sigma, p.wavelength, p.defocus,
	            p.blur_diameter, msum);
	    if (m_progress)
	      m_progress->resume_stdout ();
	  }
	sum += msum;
	sums[m] = msum;
      }
    assert (!f_vec || out_idx == n_observations);

    if (be_verbose)
      {
	if (m_progress)
	  m_progress->pause_stdout ();
	printf ("Overall sum %f\n", sum);
	if (m_progress)
	  m_progress->resume_stdout ();
      }
    return sum;
  }
  int
  num_observations () const
  {
    return n_observations;
  }

  /* Store residual vector for VALS in F_VEC and return a GSL status.  */
  int
  residuals (const double *vals, double *f_vec)
  {
    objfunc (vals, f_vec);
    return GSL_SUCCESS;
  }
  const std::vector <mtf_measurement> &m_measurements;
  mtf_parameters m_params;
  mtf_estimation_options m_options;
  progress_info *m_progress;
  bool be_verbose;
  std::vector<double> start_vec;
  /* Per-sample inverse-uncertainty weights.  Complete repeated-capture groups
     share one RMS normalization so absolute uncertainty controls their relative
     influence.  Isolated and legacy/mixed groups retain per-curve
     normalization, and curves without uncertainty estimates contain unit
     weights.  */
  std::vector<std::vector<double>> fit_weights;
  std::vector<double> sums;
  double *start;
  bool diffraction;
  /* True only for the old zero-sentinel API, whose unknown channel-labelled
     measurements share one fitted wavelength per channel.  */
  bool m_legacy_channel_wavelengths;
  int nvalues;
  int n_observations;
  int sigma_index;
  int fill_factor_index;
  std::vector<int> wavelength_index;
  std::array<int, 4> channel_wavelength_index;
  std::vector<int> blur_index;
  int f_stop_index;
  int halo_fraction_index;
  int halo_sigma_index;
};

/* Determine PSF kernel radius.  */
static int
get_psf_radius (const mtf::psf_t *psf, int size, bool *ok = nullptr)
{
  double peak = 0;
  for (int i = 0; i < size; i++)
    {
      // iprintf ("%i %f\n", i, psf[i]);
      peak = std::max (peak, (double)my_fabs (psf[i]));
    }
  int this_psf_radius = 1;
  /* Center of the PSF kernel is at 0  */
  for (int i = 1; i < size / 2 - 1; i++)
    if (my_fabs (psf[i]) > peak * 0.0001f)
      this_psf_radius = i + 1;

  /* This may be solved by iteratively reducing subsampling.  */
  if (this_psf_radius >= size / 2 - 2)
    {
      if (ok)
        *ok = false;
    }
  else if (ok)
    *ok = true;
  return this_psf_radius;
}

}

/* Return signed radial sensor-aperture OTF at PIXEL_FREQ cycles per pixel.
   The first-cut model is radial, as requested; SENSOR_FILL_FACTOR is
   interpreted as active pixel area, so its square root is the active linear
   aperture.  A scanner aperture wider than one pixel may legitimately cross a
   sinc zero, so keep the sign here.  */
double
mtf_parameters::sensor_otf (double pixel_freq) const
{
  if (!(sensor_fill_factor > 0) || !my_isfinite (sensor_fill_factor))
    return 1;
  return sinc (pixel_freq * my_sqrt (sensor_fill_factor));
}

/* Return radial sensor-aperture MTF magnitude at PIXEL_FREQ cycles per pixel.  */
double
mtf_parameters::sensor_mtf (double pixel_freq) const
{
  return my_fabs (sensor_otf (pixel_freq));
}

/* Return the capture magnification inferred from sensor pitch and scan DPI.
   The result assumes one output image pixel corresponds to one sensor pixel;
   callers must adjust SCAN_DPI when the camera image was resampled.  */
double
mtf_parameters::magnification () const
{
  if (!(scan_dpi > 0) || !(pixel_pitch > 0) || !my_isfinite (scan_dpi)
      || !my_isfinite (pixel_pitch))
    return 0;
  double object_pixel_pitch_um = 25400.0 / scan_dpi;
  return pixel_pitch / object_pixel_pitch_um;
}

/* Return the image-side working f-number for macro capture.
   The relation N*(1+M) assumes pupil magnification one.  It is exact for the
   thin symmetric-lens convention and is the documented approximation used
   until lens-specific pupil magnification is available.  */
double
mtf_parameters::effective_f_stop () const
{
  if (!(f_stop > 0) || !my_isfinite (f_stop))
    return f_stop;
  return f_stop * (1.0 + magnification ());
}

/* Return normalized optical frequency NU.
   PIXEL_FREQ is spatial frequency in cycles per sensor/output pixel.  NU is
   zero at DC and one at the incoherent diffraction cutoff.  */
double
mtf_parameters::nu (double pixel_freq) const
{
  if (!can_simulate_diffraction_p () || !my_isfinite (pixel_freq))
    return 0;
  double frequency_per_mm = my_fabs (pixel_freq) / (pixel_pitch * 0.001);
  double wavelength_mm = wavelength * 1.0e-6;
  double cutoff_per_mm = 1.0 / (wavelength_mm * effective_f_stop ());
  return std::clamp (frequency_per_mm / cutoff_per_mm, 0.0, 1.0);
}

/* Return diffraction-limited circular-pupil OTF at PIXEL_FREQ cycles per
   pixel.  The unaberrated circular-pupil OTF is real and nonnegative.  */
double
mtf_parameters::lens_diffraction_otf (double pixel_freq) const
{
  return circular_pupil_diffraction_otf (nu (pixel_freq));
}

/* Return diffraction-limited circular-pupil MTF magnitude at PIXEL_FREQ
   cycles per pixel.  */
double
mtf_parameters::lens_diffraction_mtf (double pixel_freq) const
{
  return my_fabs (lens_diffraction_otf (pixel_freq));
}

/* Return the exact signed defocus OTF factor for the diffraction model.
   PIXEL_FREQ is spatial frequency in cycles per pixel.  DEFOCUS is image-plane
   displacement in millimeters.  Negative values are physical phase reversals
   of the known circular-pupil transfer and must survive forward blur and
   deconvolution.  */
double
mtf_parameters::lens_defocus_otf (double pixel_freq) const
{
  double q = nu (pixel_freq);
  if (q <= 0 || q >= 1 || my_fabs (defocus) < 1.0e-15)
    return 1;

  double wavelength_mm = wavelength * 1.0e-6;
  double working_f_stop = effective_f_stop ();
  double edge_phase
      = M_PI * defocus * q * (1.0 - q)
        / (wavelength_mm * working_f_stop * working_f_stop);
  /* By the triangle inequality the magnitude cannot exceed the in-focus OTF.
     Clamp only roundoff outside the physical bound and preserve the sign.  */
  return std::clamp (circular_pupil_defocus_factor (q, edge_phase), -1.0,
                     1.0);
}

/* Return magnitude of the exact circular-pupil defocus factor at PIXEL_FREQ.
   This is the quantity comparable with a measured slanted-edge MTF.  */
double
mtf_parameters::lens_defocus_mtf (double pixel_freq) const
{
  return my_fabs (lens_defocus_otf (pixel_freq));
}

/* Return the historical Stokseth/Bessel defocus approximation.
   PIXEL_FREQ is spatial frequency in cycles per pixel.  This function is kept
   only for diagnostics and regression comparison; LENS_MTF uses the exact
   pupil-autocorrelation factor returned by LENS_DEFOCUS_MTF.  */
double
mtf_parameters::stokseth_defocus_mtf (double pixel_freq) const
{
  double q = nu (pixel_freq);
  if (q <= 0 || q >= 1 || my_fabs (defocus) < 1.0e-15)
    return 1;

  double wavelength_mm = wavelength * 1.0e-6;
  double working_f_stop = effective_f_stop ();
  double edge_phase
      = M_PI * defocus * q * (1.0 - q)
        / (wavelength_mm * working_f_stop * working_f_stop);
  if (my_fabs (edge_phase) < 1.0e-8)
    return 1;
  return my_fabs (2.0 * get_j1 (edge_phase) / edge_phase);
}

/* Return the legacy approximate defocus factor.
   PIXEL_FREQ is spatial frequency in cycles per pixel.  */
double
mtf_parameters::hopkins_defocus_mtf (double pixel_freq) const
{
  return stokseth_defocus_mtf (pixel_freq);
}

/* Return the normalized MTF of the broad scattering halo component.
   PIXEL_FREQ is spatial frequency in cycles per output pixel.  The halo is a
   separate broad Gaussian PSF component; LENS_OTF mixes it with the signed
   compact optical core before taking any magnitude.  */
double
mtf_parameters::halo_mtf (double pixel_freq) const
{
  if (!(my_isfinite (halo_sigma) && halo_sigma > 0))
    return 1;
  return gaussian_blur_mtf (pixel_freq, halo_sigma);
}

/* Return signed complete lens OTF at PIXEL_FREQ cycles per pixel.  The
   empirical fallback has no independently known phase model and therefore
   remains nonnegative.  For the physical model the broad halo is an additive
   PSF component: mix the signed compact-core OTF with the positive broad-halo
   OTF first, and take the magnitude only later in LENS_MTF/SYSTEM_MTF.  This
   preserves and correctly shifts physical phase reversals.  */
double
mtf_parameters::lens_otf (double pixel_freq) const
{
  if (simulate_diffraction_p ())
    {
      const double core_otf
          = lens_diffraction_otf (pixel_freq)
            * lens_defocus_otf (pixel_freq)
            * gaussian_blur_mtf (pixel_freq, sigma);
      if (!(my_isfinite (halo_fraction) && halo_fraction > 0
            && my_isfinite (halo_sigma) && halo_sigma > 0))
        return core_otf;
      const double fraction = std::clamp (halo_fraction, 0.0, 1.0);
      return (1.0 - fraction) * core_otf
             + fraction * halo_mtf (pixel_freq);
    }
  return gaussian_blur_mtf (pixel_freq, sigma)
         * circular_blur_mtf (pixel_freq, blur_diameter);
}

/* Return complete lens MTF magnitude at PIXEL_FREQ cycles per pixel.  */
double
mtf_parameters::lens_mtf (double pixel_freq) const
{
  return my_fabs (lens_otf (pixel_freq));
}

/* Return an optional correction applied on top of a measured MTF.
   PIXEL_FREQ is spatial frequency in cycles per pixel.  A measurement already
   contains diffraction and capture defocus, so only the metadata-free residual
   Gaussian/circular adjustment is applied here.  */
double
mtf_parameters::measured_mtf_correction (double pixel_freq) const
{
  return gaussian_blur_mtf (pixel_freq, sigma)
         * circular_blur_mtf (pixel_freq, blur_diameter);
}

/* Return signed complete radial system OTF at PIXEL_FREQ cycles per pixel.
   This is meaningful for the analytical physical model, where the pupil phase
   is known.  Measured slanted-edge data is magnitude-only and is handled
   separately by MTF::PRECOMPUTE.  */
double
mtf_parameters::system_otf (double pixel_freq) const
{
  return sensor_otf (pixel_freq) * lens_otf (pixel_freq);
}

/* Return complete radial system MTF magnitude at PIXEL_FREQ cycles per pixel.  */
double
mtf_parameters::system_mtf (double pixel_freq) const
{
  return my_fabs (system_otf (pixel_freq));
}

/* Compute right half of LSF.
   LSF is vector to store the result.
   SUBSAMPLE is the spatial sampling step.  */

void
mtf::compute_lsf (std::vector<psf_t, fft_allocator<psf_t>> &lsf,
                  luminosity_t subsample) const
{
  int size = lsf.size ();
  if (size & 1)
    {
      lsf[size - 1] = 0;
      size--;
    }
  std::vector<psf_t, fft_allocator<psf_t>> mtf_half (size);
  auto plan = fft_plan_r2r_1d<psf_t> (size, FFTW_REDFT00, mtf_half.data (), lsf.data ());
  double scale = 1.0 / ((double)size * subsample * 2.0);

  /* Mirror mtf.  */
  for (int i = 0; i < size; i++)
    mtf_half[i] = get_transfer (i * scale);

  plan.execute_r2r (mtf_half.data (), lsf.data ());

  /* Use double to avoid accumulation of error.  */
  double sum = 0;
  for (int i = 0; i < size; i++)
    sum += lsf[i];
  double fin_scale = 1.0 / sum;
  for (int i = 0; i < size; i++)
    lsf[i] *= fin_scale;
}

std::vector<mtf::psf_t, fft_allocator<mtf::psf_t>>
mtf::compute_2d_psf (int psf_size, luminosity_t subscale,
                     progress_info *progress, bool parallel)
{
  int fft_size = psf_size / 2 + 1;
  const double psf_step = 1.0 / ((double)psf_size * subscale);
  // Use unique_ptr with FFTW allocator for fftw_complex array
  auto mtf_kernel = fft_alloc_complex<psf_t> (psf_size * fft_size);
  std::vector<psf_t, fft_allocator<psf_t>> psf_data (psf_size * psf_size);
  auto plan = fft_plan_c2r_2d<psf_t> (psf_size, psf_size, mtf_kernel.get (), psf_data.data ());
  for (int x = 0; x < fft_size; x++)
    {
      std::complex ker (std::clamp ((double)get_transfer (x, 0, psf_step),
				    -1.0, 1.0),
			0.0);
      mtf_kernel.get ()[x][0] = std::real (ker);
      mtf_kernel.get ()[x][1] = std::imag (ker);
    }
  //printf ("%i %i\n",fft_size, parallel);
  // This loop is performance critical for focus finetuning.
  // Bring it inline when parallelism is disabled or kernel is too small
  if (!parallel || fft_size < 256)
    {
      for (int y = 1; y < fft_size; y++)
	for (int x = 0; x < fft_size; x++)
	  {
	    std::complex ker (std::clamp ((double)get_transfer (x, y, psf_step),
					  -1.0, 1.0),
			      0.0);
	    mtf_kernel.get ()[y * fft_size + x][0] = std::real (ker);
	    mtf_kernel.get ()[y * fft_size + x][1] = std::imag (ker);
	    mtf_kernel.get ()[(psf_size - y) * fft_size + x][0] = std::real (ker);
	    mtf_kernel.get ()[(psf_size - y) * fft_size + x][1] = std::imag (ker);
	  }
    }
  else
    {
#pragma omp parallel for default(none) schedule(dynamic) collapse(2)          \
      shared(fft_size, psf_step, mtf_kernel, psf_size) //if (parallel && fft_size > 256)
    for (int y = 1; y < fft_size; y++)
      for (int x = 0; x < fft_size; x++)
	{
	  std::complex ker (std::clamp ((double)get_transfer (x, y, psf_step),
					0.0, 1.0),
			    0.0);
	  mtf_kernel.get ()[y * fft_size + x][0] = std::real (ker);
	  mtf_kernel.get ()[y * fft_size + x][1] = std::imag (ker);
	  mtf_kernel.get ()[(psf_size - y) * fft_size + x][0] = std::real (ker);
	  mtf_kernel.get ()[(psf_size - y) * fft_size + x][1] = std::imag (ker);
	}
    }
  plan.execute_c2r (mtf_kernel.get (), psf_data.data ());

  return psf_data;
}

/* Determine radius of PSF to be sure that the minimal value is at most MAX *
   MIN_THRESHOLD. If SUM_THRESHOLD is non-zero reduce it then so the sum of the
   kernel up to radius is 1-sum_threshold of the overall kernel.  */
double
mtf::estimate_psf_size (luminosity_t min_threshold,
                        luminosity_t sum_threshold) const
{
  /* Make a guess that PSF does not spread over 4 pixels.  */
  double subscale = 1 / 32.0;
  int lsf_size = 256;
  while (true)
    {
      std::vector<psf_t, fft_allocator<psf_t>> lsf (lsf_size);
      compute_lsf (lsf, subscale);
      psf_t max = 0;
      for (auto v : lsf)
        max = std::max (max, my_fabs (v));
      /* Not good enough.  Include negative ringing introduced by a measured
         or sharply truncated MTF when determining the required support.  */
      if (my_fabs (lsf.back ()) > max * min_threshold)
        {
          if (lsf_size < 4096)
            lsf_size *= 2;
          else
            subscale *= 2;
          continue;
        }
      int radius = 1;
      for (int v = 2; v < lsf_size; v++)
        if (my_fabs (lsf[v]) > max * min_threshold)
          radius = v + 1;
#if 0
      if (sum_threshold)
	{
	  double sum = 0;
	  for (int radius = 0; radius < lsf_size && sum < 1 - sum_threshold; radius++)
	    sum += lsf[radius];
	  if (radius == lsf_size)
	    {
	      printf ("Upscaling 2 %i %f %f\n", lsf_size, subscale, sum);
	      if (lsf_size < 4096)
		lsf_size *= 2;
	      else
		subscale *= 2;
	      continue;
	    }
	}
#endif
      assert (radius);
      return radius * subscale;
    }
}

/* Compute PSF as 2D FFT of circular MTF.
   MAX_RADIUS is an estimate of radius.  SUBSCALE is a size of
   pixel we compute at (smaller pixel means more precise PSF)  */
bool
mtf::compute_psf (luminosity_t max_radius, luminosity_t subscale, const char *filename,
                  const char **error, bool parallel)
{
  bool verbose = false;
  /* Cap size of FFT to solve.  */
  while (my_ceil (max_radius / subscale) * 2 + 1 > 1024)
    subscale *= 2;
  int psf_size = my_ceil (max_radius / subscale) * 2 + 1;
  int iterations = 0;

  while (true)
    {
      /* Determine PSF radius.  */

      bool ok;
      auto psf_data = mtf::compute_2d_psf (psf_size, subscale, nullptr, parallel);
      if (psf_data.empty ())
        return false;
      int radius = get_psf_radius (psf_data.data (), psf_size, &ok);
      /* If FFT size was to small for the PSF, increase it and restart.  */
      if (!ok && iterations < 10)
        {
          if (psf_size < 1024)
            psf_size = std::min (psf_size * 2, 1024);
          else
            /* At the FFT size cap, increase the physical extent at lower
               spatial resolution.  Dividing SUBSCALE made the support smaller
               and could never resolve a PSF that already reached the edge.  */
            subscale *= 2;
	  //printf ("Iterating PSF computation %i %i %f\n", iterations, psf_size, subscale);
          iterations++;
          continue;
        }
      if (!ok)
        {
          if (error)
            *error = "Failed to determine finite PSF support";
          return false;
        }
      m_psf_radius = radius * subscale;

      /* Compute LSF. Circular LSF is PSF.  */
      /* Make sure PSF also trails by 0.  */
      luminosity_t d1 = psf_data[radius];
      luminosity_t d2 = psf_data[radius + 1];
      psf_data[radius] = 0;
      psf_data[radius + 1] = 0;
      m_psf.set_range (0, (radius + 2) * subscale);
      m_psf.init_by_y_values (psf_data.data (), radius + 2);
      psf_data[radius] = d1;
      psf_data[radius + 1] = d2;
      if (filename)
        {
          tiff_writer_params pp;
          int width = 2 * radius;
          int height = 2 * radius;
          pp.width = width;
          const int lsf_size = 100;
          pp.height = height + lsf_size;
          pp.depth = 16;
          const char *tiff_error = nullptr;
          pp.filename = filename;
          tiff_writer renderedu (pp, &tiff_error);
          if (tiff_error)
            {
              if (error)
                *error = tiff_error;
              return false;
            }
          double err = 0, m = 0;
          for (int y = 0; y < psf_size / 2; y++)
            for (int x = 0; x < psf_size / 2; x++)
              {
                luminosity_t val = get_psf (x, y, 1 / subscale);
                luminosity_t diff = my_fabs (val - psf_data[y * psf_size + x]);
                if (val > m)
                  m = val;
                if (diff > err)
                  err = diff;
                // psf_data[y * psf_size + x] = val;
              }
          for (int y = 0; y < height; y++)
            {
              for (int x = 0; x < width; x++)
                {
                  int xp = nearest_int (x * subscale);
                  int yp = nearest_int (y * subscale);
                  int xx = ((x + psf_size / 2 - radius) + psf_size / 2)
                           % psf_size;
                  int yy = ((y + psf_size / 2 - radius) + psf_size / 2)
                           % psf_size;
                  int v = std::clamp (
                      (int)(invert_gamma (psf_data[yy * psf_size + xx] / m, -1)
                                * (65535)
                            + 0.5),
                      0, 65535);
                  int vv
                      = std::clamp (v + 100 * 256 * ((xp + yp) % 2), 0, 65535);
                  renderedu.put_pixel (x, v, v, vv);
                }
              if (!renderedu.write_row ())
                return false;
            }
          std::vector<psf_t, fft_allocator<psf_t>> lsf (radius);
          compute_lsf (lsf, subscale);
          psf_t lsf_max = lsf[0], collected_lsf_max = 0;
          std::vector<psf_t, fft_allocator<psf_t>> collected_lsf (width);
          for (int x = 0; x < width; x++)
            {
              int xx = (x < radius ? radius - x - 1 : x - radius);
              for (int y = 0; y < psf_size; y++)
                collected_lsf[x] += psf_data[xx * psf_size + y];
              if (collected_lsf[x] > collected_lsf_max)
                collected_lsf_max = collected_lsf[x];
            }
          for (int y = 0; y < lsf_size; y++)
            {
              for (int x = 0; x < width; x++)
                {
                  int idx = lsf_size
                            - nearest_int (
                                lsf[(x < radius ? radius - x - 1 : x - radius)]
                                * lsf_size / lsf_max);
                  int idx2
                      = lsf_size
                        - nearest_int (psf_data[((x + psf_size / 2 - radius)
                                                 + psf_size / 2)
                                                % psf_size]
                                       * lsf_size / m);
                  int idx3 = lsf_size
                             - nearest_int (collected_lsf[x] * lsf_size
                                            / collected_lsf_max);
                  int r = (idx == y) * 65535;
                  int g = (idx2 == y) * 65535;
                  int b = (idx3 == y) * 65535;
                  renderedu.put_pixel (x, r, g, b);
                }
              if (!renderedu.write_row ())
                return false;
            }
          if (verbose)
            printf ("Max %f, err %f normalized %f\n", m, err, err / m);
        }
      return true;
    }
}

bool
mtf::precompute (progress_info *progress, bool parallel)
{
  std::lock_guard<std::mutex> lock (m_lock);
  if (m_precomputed)
    {
      return true;
    }

  /* Use actual MTF data.  */
  if (m_params.use_measured_mtf ())
    {
      const mtf_measurement &measurement
          = m_params.measurements[m_params.measured_mtf_idx];
      const size_t measured_size = measurement.size ();
      if (measured_size < 3)
        return false;

      for (size_t i = 0; i < measured_size; i++)
        {
          const double frequency = measurement.get_freq (i);
          const double contrast = measurement.get_contrast (i);
          if (!my_isfinite (frequency) || !my_isfinite (contrast)
              || frequency < 0 || contrast < 0
              || (i && !(measurement.get_freq (i - 1) < frequency)))
            {
              fprintf (stderr,
                       "Invalid measured MTF: frequencies must be finite, "
                       "nonnegative and strictly increasing, and contrasts "
                       "must be finite and nonnegative.\n");
              return false;
            }
        }

      std::vector<double> frequencies;
      std::vector<double> contrasts;
      frequencies.reserve (measured_size + 3);
      contrasts.reserve (measured_size + 3);

      /* MTF is a relative transfer function and therefore has unit response
         at DC.  Normalize a measured DC value; if the table starts above DC,
         add the physically required point (0, 1).  */
      constexpr double dc_frequency_epsilon = 1e-9;
      const bool has_measured_dc
          = measurement.get_freq (0) <= dc_frequency_epsilon;
      double dc = 1;
      if (has_measured_dc)
        {
          dc = measurement.get_contrast (0) * 0.01
               * m_params.measured_mtf_correction (0);
          if (!my_isfinite (dc) || dc <= 0)
            {
              fprintf (stderr,
                       "Invalid measured MTF: the DC response must be "
                       "positive and finite.\n");
              return false;
            }
        }
      else
        {
          frequencies.push_back (0);
          contrasts.push_back (1);
        }

      for (size_t i = 0; i < measured_size; i++)
        {
          double frequency = measurement.get_freq (i);
          if (i == 0 && has_measured_dc)
            frequency = 0;
          double value = measurement.get_contrast (i) * 0.01
                         * m_params.measured_mtf_correction (frequency) / dc;
          if (!my_isfinite (value))
            {
              fprintf (stderr,
                       "Invalid measured MTF: corrected contrast is not "
                       "finite.\n");
              return false;
            }
          frequencies.push_back (frequency);
          contrasts.push_back (std::clamp (value, 0.0, 1.0));
        }

      /* Extend the curve by two zero points.  Frequencies outside the table
         then remain zero because precomputed_function clamps to its final
         (zero-to-zero) segment.  */
      const size_t curve_size = frequencies.size ();
      double step = (frequencies.back () - frequencies.front ())
                    / (curve_size - 1);
      if (!(step > 0) || !my_isfinite (step))
        return false;
      /* A two-dimensional sampled image has valid Fourier samples out to
         diagonal Nyquist, sqrt (2) / 2 cycles per pixel.  Slanted-edge tables
         conventionally stop at the axial Nyquist frequency 0.5.  Dropping to
         zero one tiny table step later would erase all diagonal frequencies
         with |f| > 0.5 and create a sharply ringing PSF.  When the measurement
         reaches axial Nyquist, conservatively taper its final value to zero at
         diagonal Nyquist instead.  */
      constexpr double axial_nyquist = 0.5;
      const double diagonal_nyquist = std::sqrt (0.5);
      const double regular_zero_frequency = frequencies.back () + step;
      double zero_frequency = regular_zero_frequency;
      if (frequencies.back () >= axial_nyquist - 0.01)
        zero_frequency = std::max (zero_frequency, diagonal_nyquist);
      frequencies.push_back (zero_frequency);
      contrasts.push_back (0);
      frequencies.push_back (zero_frequency + step);
      contrasts.push_back (0);

      /* Use the exact equidistant representation when every interior point is
         regular.  The old loop skipped the final interior points and therefore
         misinterpreted short or partly irregular measured tables.  */
      bool regular_steps = true;
      const double regular_step
          = (frequencies[curve_size - 1] - frequencies.front ())
            / (curve_size - 1);
      for (size_t i = 1; i + 1 < curve_size && regular_steps; i++)
        if (my_fabs (frequencies[i] - frequencies.front ()
                     - i * regular_step)
            > 0.0006)
          regular_steps = false;

      m_mtf.set_range (frequencies.front (), frequencies.back ());
      if (regular_steps
          && my_fabs (zero_frequency - regular_zero_frequency) <= 0.0006)
        m_mtf.init_by_y_values (contrasts.data (), contrasts.size ());
      else
        m_mtf.init_by_x_y_values (frequencies.data (), contrasts.data (),
                                  frequencies.size (), 4096);

      if (colorscreen_checking)
        for (size_t i = 0; i < frequencies.size () - 2; i++)
          if (my_fabs (contrasts[i] - get_transfer (frequencies[i])) > 0.01)
            {
              printf ("Mismatch (measured) %i freq %f table %f "
                      "precomputed %f\n",
                      (int)i, (double)frequencies[i], (double)contrasts[i],
                      (double)m_mtf.apply (frequencies[i]));
              abort ();
            }

      if (progress)
        progress->set_task ("computing point spread function", 1);
    }
  /* Use lens model.  */
  else
    {
      const int entries = 512;
      std::vector<double> contrasts (entries);
      double step = 1.0 / (entries - 2);
      for (int i = 0; i < entries - 2; i++)
        contrasts[i] = m_params.system_otf (i * step);
      contrasts[entries - 2] = contrasts[entries - 1] = 0;
      m_mtf.set_range (0, 1 + step);
      m_mtf.init_by_y_values (contrasts.data (), entries);

      if (colorscreen_checking)
        for (int i = 0; i < entries - 1; i++)
          if (my_fabs (m_params.system_otf (i * step)
                    - m_mtf.apply (i * step))
              > 0.0001)
            {
              printf ("Mismatch (model) %f %f %f\n",
                      m_params.system_otf (i * step),
                      m_mtf.apply (i * step), step);
              abort ();
            }

      if (progress)
        progress->set_task ("computing point spread function", 1);
    }
  m_psf_radius = estimate_psf_size ();
  // print_lsf (stdout);

  // m_mtf.plot (0, 1);
  // m_psf.plot (0, 5);
  m_precomputed = true;
  return true;
}
bool
mtf::precompute_psf (progress_info *progress, bool parallel, const char *filename, const char **error)
{
  if (!precompute (progress, parallel))
    return false;
  std::lock_guard<std::mutex> lock (m_lock);
  if (m_precomputed_psf)
    {
      return true;
    }
  if (!compute_psf (psf_size (1), 1 / 32.0, filename, error, parallel))
    return false;
  m_precomputed_psf = true;
  return true;
}

std::unique_ptr<mtf>
mtf::get_new_mtf (struct mtf_parameters &p, progress_info *)
{
  return std::make_unique<mtf> (p);
}

static mtf::mtf_cache_t
    mtf_cache ("Modulation transfer functions");

std::shared_ptr<mtf>
mtf::get_mtf (const mtf_parameters &mtfp, progress_info *p)
{
  return mtf_cache.get (const_cast<mtf_parameters &> (mtfp), p);
}

bool
mtf_parameters::save_psf (progress_info *progress, const char *write_table,
                          const char **error) const
{
  mtf mtf (*this);
  return mtf.precompute_psf (progress, true, write_table, error);
}

/* Write the shared component-curve header to F.  */
bool
mtf_parameters::print_csv_header (FILE *f) const
{
  return fprintf (
             f,
             "diffraction f-stop 0/%.8g (effective 0/%.8g) wavelength %.8gnm "
             "magnification %.10g pixel pitch %.8gum\texact defocus "
             "%.10gmm\tlegacy Bessel defocus\tcore sigma=%.8gpx\thalo "
             "component fraction %.8g sigma %.8gpx\tlens\tsensor fill factor "
             "%.8g\tsystem\n",
             f_stop, effective_f_stop (), wavelength, magnification (),
             pixel_pitch, defocus, sigma, halo_fraction, halo_sigma,
             sensor_fill_factor)
         >= 0;
}

/* Write model component curves to WRITE_TABLE and report errors through
   ERROR.  */
bool
mtf_parameters::write_table (const char *write_table, const char **error) const
{
  if (write_table)
    {
      FILE *f = fopen (write_table, "wt");
      if (!f)
        {
          if (error)
            *error = "failed to open output file";
          return false;
        }
      if (fprintf (f, "frequency\t") < 0 || !print_csv_header (f))
        {
          if (error)
            *error = "write error";
          fclose (f);
          return false;
        }
      for (size_t i = 0; i < 400; i++)
        {
          double freq = i / 400.0;
          if (fprintf (f,
                       "%.17g\t%.12g\t%.12g\t%.12g\t%.12g\t%.12g\t"
                       "%.12g\t%.12g\t%.12g\n",
                       freq, lens_diffraction_mtf (freq) * 100,
                       lens_defocus_mtf (freq) * 100,
                       stokseth_defocus_mtf (freq) * 100,
                       gaussian_blur_mtf (freq, sigma) * 100,
                       halo_mtf (freq) * 100, lens_mtf (freq) * 100,
                       sensor_mtf (freq) * 100,
                       system_mtf (freq) * 100)
              < 0)
            {
              if (error)
                *error = "write error";
              fclose (f);
              return false;
            }
        }
      if (fclose (f))
        {
          if (error)
            *error = "error closing output file";
          return false;
        }
    }
  return true;
}

/* Return STEPS uniformly sampled component curves over zero to one cycle per
   pixel.  An empty result is returned for nonpositive STEPS.  */
mtf_parameters::computed_mtf
mtf_parameters::compute_curves (int steps) const
{
  computed_mtf result;
  if (steps <= 0)
    return result;
  result.system_otf.reserve (steps);
  result.system_mtf.reserve (steps);
  result.sensor_mtf.reserve (steps);
  result.gaussian_blur_mtf.reserve (steps);
  result.halo_mtf.reserve (steps);
  result.lens_defocus_mtf.reserve (steps);
  result.stokseth_defocus_mtf.reserve (steps);
  result.lens_diffraction_mtf.reserve (steps);
  result.lens_mtf.reserve (steps);
  result.hopkins_blur_mtf.reserve (steps);

  for (int i = 0; i < steps; i++)
    {
      double freq = steps == 1 ? 0.0 : i / (double)(steps - 1);
      result.lens_diffraction_mtf.push_back (lens_diffraction_mtf (freq));
      result.lens_defocus_mtf.push_back (lens_defocus_mtf (freq));
      result.stokseth_defocus_mtf.push_back (
          stokseth_defocus_mtf (freq));
      result.gaussian_blur_mtf.push_back (gaussian_blur_mtf (freq, sigma));
      result.halo_mtf.push_back (halo_mtf (freq));
      result.lens_mtf.push_back (lens_mtf (freq));
      result.sensor_mtf.push_back (sensor_mtf (freq));
      result.system_otf.push_back (system_otf (freq));
      result.system_mtf.push_back (system_mtf (freq));
      result.hopkins_blur_mtf.push_back (
          circular_blur_mtf (freq, blur_diameter));
    }

  return result;
}

/* Return true when OPTIONS selects the physical diffraction model for PAR.
   AUTOMATIC_LEGACY preserves the former geometry-based model selection used by
   the compatibility estimate_parameters overload.  */
static bool
physical_estimation_model_p (const mtf_parameters &par,
                             const mtf_estimation_options &options)
{
  const mtf_model model = options.model == mtf_model::automatic_legacy
                              ? par.model
                              : options.model;
  if (model == mtf_model::physical_diffraction)
    return true;
  if (model == mtf_model::empirical_fallback)
    return false;
  return my_isfinite (par.pixel_pitch) && par.pixel_pitch > 0
         && my_isfinite (par.scan_dpi) && par.scan_dpi > 0;
}

/* Validate an explicit fitting request OPTIONS for parameter values PAR.
   ERROR receives a static diagnostic on failure.  The library owns these
   rules so command-line and graphical front ends cannot silently disagree.  */
bool
mtf_parameters::validate_estimation_options (
    const mtf_parameters &par, const mtf_estimation_options &options,
    const char **error)
{
  if (error)
    *error = nullptr;
  auto fail = [error] (const char *message) {
    if (error)
      *error = message;
    return false;
  };

  if (!valid_mtf_model_p (options.model)
      || (options.model == mtf_model::automatic_legacy
          && !valid_mtf_model_p (par.model)))
    return fail ("invalid MTF model selection");

  if (!options.include_measurements.empty ()
      && options.include_measurements.size () != par.measurements.size ())
    return fail ("MTF measurement-selection vector has the wrong size");
  if (options.optimize_measurement_wavelengths.size ()
      > par.measurements.size ())
    return fail ("MTF wavelength-selection vector has the wrong size");

  size_t observations = 0;
  size_t included = 0;
  size_t variables = 0;
  size_t fitted_wavelengths = 0;
  size_t fixed_wavelengths = 0;
  bool any_wavelength_fitted = false;
  bool capture_has_variable = false;
  const bool physical = physical_estimation_model_p (par, options);

  for (size_t measurement = 0; measurement < par.measurements.size ();
       measurement++)
    {
      const mtf_measurement &value = par.measurements[measurement];
      if (!value.same_capture)
        capture_has_variable = false;
      if (!options.include_measurement_p (measurement))
        continue;

      if (options.optimize_measurement_wavelength_p (measurement))
        any_wavelength_fitted = true;

      included++;
      size_t measurement_observations = 0;
      double previous_frequency = -1;
      for (size_t i = 0; i < value.size (); i++)
        {
          const double frequency = value.get_freq (i);
          const double contrast = value.get_contrast (i);
          if (!my_isfinite (frequency) || !my_isfinite (contrast)
              || frequency < 0 || contrast < 0
              || (i && !(frequency > previous_frequency)))
            return fail ("MTF measurements must be finite, nonnegative, and "
                         "strictly increasing in frequency");
          previous_frequency = frequency;
          if (frequency <= 0.5)
            measurement_observations++;
        }
      if (measurement_observations < 3)
        return fail ("every selected MTF measurement needs at least three "
                     "samples through Nyquist");
      observations += measurement_observations;

      if (physical)
        {
          if (options.optimize_measurement_wavelength_p (measurement))
            {
              const double initial_wavelength
                  = measurement_wavelength (par, value);
              if (initial_wavelength > 0
                  && (initial_wavelength < fitted_wavelength_min_nm
                      || initial_wavelength > fitted_wavelength_max_nm))
                return fail ("an optimized wavelength starting value must be "
                             "between 380 and 1000 nm");
              variables++;
              fitted_wavelengths++;
            }
          else if (measurement_wavelength (par, value) > 0)
            fixed_wavelengths++;
          else
            return fail ("every selected physical MTF measurement needs a "
                         "positive wavelength or wavelength optimization");

          if (options.optimize_defocus && !capture_has_variable)
            {
              variables++;
              capture_has_variable = true;
            }
        }
      else if (options.optimize_blur_diameter && !capture_has_variable)
        {
          variables++;
          capture_has_variable = true;
        }
    }

  if (!included)
    return fail ("no MTF measurement is selected for fitting");

  if (!my_isfinite (par.sigma) || par.sigma < 0)
    {
      if (!options.optimize_sigma)
        return fail ("Gaussian sigma must be finite and nonnegative or "
                     "enabled for optimization");
    }
  if (options.optimize_sigma)
    variables++;

  if (physical)
    {
      if (options.optimize_blur_diameter)
        return fail ("fallback blur diameter cannot be optimized by the "
                     "physical diffraction model");
      if (!(my_isfinite (par.pixel_pitch) && par.pixel_pitch > 0))
        return fail ("physical MTF fitting requires a positive sensor pixel "
                     "pitch");
      if (!(my_isfinite (par.scan_dpi) && par.scan_dpi > 0))
        return fail ("physical MTF fitting requires a positive scan "
                     "resolution");

      if (options.optimize_f_stop)
        variables++;
      else if (!(my_isfinite (par.f_stop) && par.f_stop > 0))
        return fail ("f-number must be positive or enabled for optimization");

      if (!options.optimize_defocus
          && !(my_isfinite (par.defocus) && par.defocus >= 0))
        return fail ("defocus must be finite and nonnegative or enabled for "
                     "optimization");

      if (options.optimize_sensor_fill_factor)
        variables++;
      else if (!(my_isfinite (par.sensor_fill_factor)
                 && par.sensor_fill_factor >= 0
                 && par.sensor_fill_factor <= 32))
        return fail ("sensor fill factor must be between zero and 32 or "
                     "enabled for optimization");

      if (options.optimize_halo_fraction)
        variables++;
      else if (!(my_isfinite (par.halo_fraction)
                 && par.halo_fraction >= 0 && par.halo_fraction <= 0.95))
        return fail ("halo fraction must be between zero and 0.95 or enabled "
                     "for optimization");

      const bool halo_can_be_present
          = options.optimize_halo_fraction || par.halo_fraction > 0;
      if (options.optimize_halo_sigma)
        {
          if (!halo_can_be_present)
            return fail ("halo radius cannot be optimized while halo fraction "
                         "is fixed at zero");
          variables++;
        }
      else if (halo_can_be_present
               && !(my_isfinite (par.halo_sigma) && par.halo_sigma > 0))
        return fail ("halo radius must be positive or enabled for "
                     "optimization when a halo is fitted");

      /* Diffraction cutoff mainly constrains WAVELENGTH*F_STOP.  Fitting the
         f-number and every wavelength simultaneously leaves no scale anchor.  */
      if (options.optimize_f_stop && fitted_wavelengths
          && !fixed_wavelengths)
        return fail ("fix the f-number or at least one measurement wavelength; "
                     "optimizing both is underdetermined");
    }
  else
    {
      if (options.optimize_f_stop || options.optimize_defocus
          || options.optimize_sensor_fill_factor
          || options.optimize_halo_fraction || options.optimize_halo_sigma
          || any_wavelength_fitted)
        return fail ("physical parameters cannot be optimized by the "
                     "empirical fallback model");
      if (!options.optimize_blur_diameter
          && !(my_isfinite (par.blur_diameter)
               && par.blur_diameter >= 0))
        return fail ("fallback blur diameter must be finite and nonnegative "
                     "or enabled for optimization");
    }

  if (variables >= observations)
    return fail ("the selected MTF curves contain too few observations for "
                 "the requested number of free parameters");
  return true;
}

/* Construct explicit OPTIONS which reproduce the historical zero-sentinel
   interface.  This helper exists only for source compatibility; new callers,
   especially the GUI, should construct MTF_ESTIMATION_OPTIONS directly.  */
static mtf_estimation_options
legacy_estimation_options (const mtf_parameters &par, int flags)
{
  mtf_estimation_options options;
  options.model = mtf_model::automatic_legacy;
  options.optimize_sigma = !(my_isfinite (par.sigma) && par.sigma != 0);
  options.optimize_measurement_wavelengths.assign (par.measurements.size (),
                                                    false);

  const bool physical = physical_estimation_model_p (par, options);
  if (physical)
    {
      options.optimize_f_stop
          = !(my_isfinite (par.f_stop) && par.f_stop != 0);
      options.optimize_defocus
          = !(my_isfinite (par.defocus) && par.defocus != 0);
      options.optimize_sensor_fill_factor
          = !(my_isfinite (par.sensor_fill_factor)
              && par.sensor_fill_factor != 0);
      options.optimize_halo_fraction
          = (flags & mtf_parameters::estimate_halo)
            && !(my_isfinite (par.halo_fraction) && par.halo_fraction > 0);
      options.optimize_halo_sigma
          = (flags & mtf_parameters::estimate_halo)
            && !(my_isfinite (par.halo_sigma) && par.halo_sigma > 0);
      for (size_t measurement = 0; measurement < par.measurements.size ();
           measurement++)
        options.optimize_measurement_wavelengths[measurement]
            = measurement_wavelength (par, par.measurements[measurement]) <= 0;
    }
  else
    options.optimize_blur_diameter
        = !(my_isfinite (par.blur_diameter) && par.blur_diameter != 0);
  return options;
}

/* Fit the physical or fallback model using the historical zero-sentinel
   convention.  New code should call the explicit overload below.  */
double
mtf_parameters::estimate_parameters (mtf_parameters &par,
                                     const char *write_table,
                                     progress_info *progress,
                                     const char **error, int flags)
{
  return estimate_parameters_internal (par, nullptr, write_table, progress,
                                       error, flags);
}

/* Fit this object to measurements in PAR using explicit free-variable
   OPTIONS.  Numeric zeroes remain ordinary fixed values unless their matching
   option is enabled.  */
double
mtf_parameters::estimate_parameters (mtf_parameters &par,
                                     const mtf_estimation_options &options,
                                     const char *write_table,
                                     progress_info *progress,
                                     const char **error, int flags)
{
  return estimate_parameters_internal (par, &options, write_table, progress,
                                       error, flags);
}

/* Fit this object to measurements in PAR.  EXPLICIT_OPTIONS is null only for
   the compatibility interface, where zero retains its historical request-to-
   optimize meaning.  WRITE_TABLE optionally receives component curves,
   PROGRESS reports solver work, ERROR receives a static diagnostic, and FLAGS
   selects numerical solvers.  */
double
mtf_parameters::estimate_parameters_internal (
    mtf_parameters &par, const mtf_estimation_options *explicit_options,
    const char *write_table, progress_info *progress, const char **error,
    int flags)
{
  if (error)
    *error = nullptr;

  const mtf_estimation_options options
      = explicit_options ? *explicit_options
                         : legacy_estimation_options (par, flags);
  if (explicit_options
      && !validate_estimation_options (par, options, error))
    return -1;

  /* Retain the useful historical diagnostic for compatibility callers.  The
     explicit interface performs the more detailed per-curve validation above.  */
  if (!explicit_options
      && (par.measurements.empty () || par.measurements[0].size () < 3))
    {
      if (error)
        *error = "no measured MTF curve to fit";
      return -1;
    }

  *this = par;
  mtf_solver solver (par, par.measurements, options, progress,
                     flags & estimate_verbose_solving,
                     explicit_options == nullptr);
  if (solver.num_values () > 0)
    {
      /* Pure defocus is even around the in-focus starting point, so its first
         derivative vanishes there.  Find the correct basin with a
         derivative-free simplex before local least-squares refinement.  */
      if (flags & estimate_use_nmsimplex)
        simplex<double, mtf_solver> (solver,
                                    "optimizing system mtf (simplex)",
                                    progress, true, 1000);
      if (flags & estimate_use_multifit)
        gsl_multifit<double, mtf_solver> (solver,
                                         "optimizing system mtf (multifit)",
                                         progress);
    }

  const bool physical = physical_estimation_model_p (par, options);
  if (explicit_options)
    {
      /* A successful explicit fit selects the fitted analytical model for
         sharpening.  Leaving a measured curve selected here would make all
         newly fitted physical parameters appear to have no effect.  */
      model = physical ? mtf_model::physical_diffraction
                       : mtf_model::empirical_fallback;
      measured_mtf_idx = -1;
    }
  else if (options.model != mtf_model::automatic_legacy)
    model = options.model;
  f_stop = solver.get_f_stop (solver.start);
  sigma = solver.get_sigma (solver.start);
  halo_fraction = solver.get_halo_fraction (solver.start);
  halo_sigma = solver.get_halo_sigma (solver.start);
  sensor_fill_factor = solver.get_fill_factor (solver.start);
  measurements = par.measurements;

  int first_measurement = -1;
  for (size_t measurement = 0; measurement < par.measurements.size ();
       measurement++)
    if (solver.measurement_included_p (measurement))
      {
        if (first_measurement < 0)
          first_measurement = (int)measurement;
        /* Explicit fits make every participating physical curve
           self-contained.  The compatibility API instead retains its former
           global and per-channel storage convention.  */
        if (physical && explicit_options)
          measurements[measurement].wavelength
              = solver.get_wavelength (measurement, solver.start);
      }

  if (physical && !explicit_options)
    {
      wavelength = par.wavelength > 0
                       ? par.wavelength
                       : solver.get_wavelength (0, solver.start);
      wavelengths = par.wavelengths;
      for (int channel = 0; channel < 4; channel++)
        if (solver.channel_wavelength_estimated_p (channel))
          wavelengths[channel]
              = solver.get_channel_wavelength (channel, solver.start);
    }

  if (first_measurement >= 0)
    {
      defocus = solver.get_defocus (first_measurement, solver.start);
      blur_diameter
          = solver.get_blur_diameter (first_measurement, solver.start);
      if (physical && explicit_options)
        wavelength = solver.first_wavelength (solver.start);
    }
  const double final_objective = solver.objfunc (solver.start);

  if (flags & estimate_verbose)
    {
      if (progress)
        progress->pause_stdout ();
      for (size_t measurement = 0; measurement < par.measurements.size ();
           measurement++)
        if (solver.measurement_included_p (measurement))
          printf ("Measurement %s defocus %.12g sum %.12g\n",
                  par.measurements[measurement].name.c_str (),
                  solver.get_defocus (measurement, solver.start),
                  solver.sums[measurement]);
      if (progress)
        progress->resume_stdout ();
    }

  if (write_table)
    {
      FILE *f = fopen (write_table, "wt");
      if (!f)
        {
          if (error)
            *error = "failed to open CSV file for writing";
          return -1;
        }
      if (fprintf (f, "frequency\tmeasured MTF\t") < 0
          || !print_csv_header (f))
        {
          if (error)
            *error = "write error in CSV file";
          fclose (f);
          return -1;
        }
      for (size_t measurement_index = 0;
           measurement_index < par.measurements.size (); measurement_index++)
        {
          if (!solver.measurement_included_p (measurement_index))
            continue;
          const mtf_measurement &measurement
              = par.measurements[measurement_index];
          mtf_parameters fitted_curve = *this;
          fitted_curve.measured_mtf_idx = -1;
          fitted_curve.model = physical ? mtf_model::physical_diffraction
                                        : mtf_model::empirical_fallback;
          if (physical)
            {
              fitted_curve.wavelength
                  = solver.get_wavelength (measurement_index, solver.start);
              fitted_curve.defocus
                  = solver.get_defocus (measurement_index, solver.start);
            }
          else
            fitted_curve.blur_diameter
                = solver.get_blur_diameter (measurement_index, solver.start);

          for (size_t i = 0; i < measurement.size (); i++)
            {
              const double freq = measurement.get_freq (i);
              const double contrast = measurement.get_contrast (i);
              if (fprintf (f,
                           "%.17g\t%.12g\t%.12g\t%.12g\t%.12g\t%.12g\t"
                           "%.12g\t%.12g\t%.12g\t%.12g\n",
                           freq, contrast,
                           fitted_curve.lens_diffraction_mtf (freq) * 100,
                           fitted_curve.lens_defocus_mtf (freq) * 100,
                           fitted_curve.stokseth_defocus_mtf (freq) * 100,
                           gaussian_blur_mtf (freq, fitted_curve.sigma) * 100,
                           fitted_curve.halo_mtf (freq) * 100,
                           fitted_curve.lens_mtf (freq) * 100,
                           fitted_curve.sensor_mtf (freq) * 100,
                           fitted_curve.system_mtf (freq) * 100)
                  < 0)
                {
                  if (error)
                    *error = "write error in CSV file";
                  fclose (f);
                  return -1;
                }
            }
          if (fputc ('\n', f) == EOF)
            {
              if (error)
                *error = "write error in CSV file";
              fclose (f);
              return -1;
            }
        }
      if (fclose (f))
        {
          if (error)
            *error = "error closing CSV file";
          return -1;
        }
    }

  return final_objective;
}

/* Load a QuickMTF five-column table from IN, label it NAME, append the parsed
   measurements, and report parse errors through ERROR.  */
int
mtf_parameters::load_csv (FILE *in, std::string name, const char **error)
{
  /* QuickMTF stores frequency, blue, green, red and combined contrast.  */
  struct row
  {
    double freq;
    std::array<double, 4> contrast;
  };

  if (!in)
    {
      if (error)
        *error = "invalid QuickMTF input file";
      return -1;
    }

  bool rgb = false;
  std::vector<row> data;
  char line[1024];
  while (fgets (line, sizeof (line), in))
    {
      double freq;
      double blue;
      double green;
      double red;
      double combined;
      int items_found = sscanf (line, "%lf%lf%lf%lf%lf", &freq, &blue,
                                &green, &red, &combined);
      if (items_found != 5)
        continue;
      if (!my_isfinite (freq) || !my_isfinite (blue)
          || !my_isfinite (green) || !my_isfinite (red)
          || !my_isfinite (combined) || freq < 0 || blue < 0 || green < 0
          || red < 0 || combined < 0
          || (!data.empty () && !(data.back ().freq < freq)))
        {
          if (error)
            *error = "invalid or non-increasing QuickMTF data";
          return -1;
        }
      if (blue != green || green != red || red != combined)
        rgb = true;
      data.push_back ({ freq, { blue, green, red, combined } });
    }

  if (data.empty ())
    {
      if (error)
        *error = "QuickMTF output should contain five whitespace-separated "
                 "values on every data line:\n"
                 "pixel_frequency blue_contrast green_contrast "
                 "red_contrast combined_contrast\n"
                 "Contrasts are percentages.";
      return -1;
    }

  std::vector<mtf_measurement> loaded;
  if (rgb)
    {
      static constexpr std::array<int, 3> channels = { 2, 1, 0 };
      static constexpr std::array<const char *, 3> names
          = { "blue", "green", "red" };
      for (size_t column = 0; column < channels.size (); column++)
        {
          mtf_measurement measurement;
          measurement.name = name + " " + names[column];
          measurement.channel = channels[column];
          measurement.same_capture = column != 0;
          for (const row &value : data)
            measurement.add_value (value.freq, value.contrast[column]);
          loaded.push_back (std::move (measurement));
        }
    }
  else
    {
      mtf_measurement measurement;
      measurement.name = std::move (name);
      for (const row &value : data)
        measurement.add_value (value.freq, value.contrast[3]);
      loaded.push_back (std::move (measurement));
    }

  measurements.insert (measurements.end (), loaded.begin (), loaded.end ());
  return rgb ? 3 : 1;
}

bool
mtf::render_dot_spread_tile (tile_parameters &tile, progress_info *p)
{
  if (!precompute_psf (p))
    return false;
  double maxp = 1;
  double m = 0;
  for (int p = 0; p < 1024; p++)
    m = std::max (m, (double)get_psf (p));
  for (double p = 1; p < 1024; p*= 1 + 1.0 / tile.width)
    if (get_psf (p) > m / 100)
      maxp = p;
  if (m > 0)
    m = 1 / m;
  luminosity_t step = maxp / (tile.width / (luminosity_t)2);
  for (int y = 0; y < tile.height; y++)
    for (int x = 0; x < tile.width; x++)
      {
	coord_t posx = (x - tile.width / 2) * step;
	coord_t posy = (y - tile.height / 2) * step;
	luminosity_t val = get_psf (posx , posy, 1);
	int i = (nearest_int (posx) + nearest_int (posy)) & 1;
	luminosity_t lum = std::clamp ((luminosity_t)(val * m), (luminosity_t)0.0, (luminosity_t)1.0);
	luminosity_t lum2 = i ? std::clamp ((luminosity_t)(val * m + i * (double)0.6), (luminosity_t)0.0, (luminosity_t)1.0) : lum;
	tile.pixels[x * 3 + y * tile.rowstride] = tile.pixels[x * 3 + y * tile.rowstride + 1] = invert_gamma (lum, -1) * 255 + 0.5;
	tile.pixels[x * 3 + y * tile.rowstride + 2] = invert_gamma (lum2, -1) * 255 + 0.5;
      }
  return true;
}

}
