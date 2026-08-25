#!/usr/bin/env python3
from pathlib import Path


def replace_once(path, old, new, label):
    p = Path(path)
    s = p.read_text()
    count = s.count(old)
    if count != 1:
        raise SystemExit(f"{path}: {label} count {count}")
    p.write_text(s.replace(old, new, 1))


# The weighted transfer implementation uses a scale-aware zero-sum guard.
replace_once(
    "src/libcolorscreen/screen.C",
    '#include <math.h>\n#include <memory>\n',
    '#include <math.h>\n#include <memory>\n#include <limits>\n',
    "include insertion",
)

# Keep the API explicitly in scanner/capture-channel terminology.  The three
# spectra inside screen_filter_source remain process-primary spectra.
old = '''  nodiscard_attr bool
  initialize_with_sharpen_parameters (
      const screen_filter_source &source,
      sharpen_parameters *sharpen[3], bool anticipate_sharpening,
      bool parallel = true, screen_filter_profile *profile = nullptr);
'''
new = old + '''  /* Apply one scalar transfer formed as the normalized weighted sum of
     three native scanner/capture-channel transfers to every process-primary
     spectrum in SOURCE.  CAPTURE_WEIGHTS are effective scalar coefficients
     (for example w_c * a_c for a monochrome RGB capture), not process-primary
     weights.  This is the exact linear model for RGB capture of one
     monochrome image layer; it deliberately does not perform viewing-filter
     process-primary/scanner-response mixing.  Anticipated Richardson-Lucy is
     nonlinear and therefore cannot be collapsed into one Fourier transfer.  */
  nodiscard_attr bool
  initialize_with_weighted_capture_transfer (
      const screen_filter_source &source,
      sharpen_parameters *capture[3], rgbdata capture_weights,
      bool anticipate_sharpening, bool parallel = true,
      screen_filter_profile *profile = nullptr);
'''
replace_once("src/libcolorscreen/screen.h", old, new, "weighted API declaration")

implementation = r'''
/* Apply a normalized weighted sum of native scanner-channel transfers to all
   three process-primary spectra in SOURCE.  This models an RGB capture of a
   single underlying monochrome image layer:

     H_eff(f) = sum_c q_c H_c(f) / sum_c q_c,

   where Q contains the effective scalar capture coefficients (for example
   mix weight times native-channel gain).  Combining the transfers before the
   inverse FFT is exactly equivalent to filtering the same scalar signal in
   each native channel and mixing the results afterwards, while requiring only
   three inverse FFTs for the process-primary basis.

   This is intentionally not the viewing-filter colour model: no scanner
   response/process-primary matrix is applied here.  Richardson-Lucy is
   nonlinear when anticipated and cannot be represented by one H_EFF.  */
bool
screen::initialize_with_weighted_capture_transfer (
    const screen_filter_source &source, sharpen_parameters *capture[3],
    rgbdata capture_weights, bool anticipate_sharpening, bool parallel,
    screen_filter_profile *profile)
{
  if (!source.m_impl)
    return false;

  double weight_sum = 0;
  double weight_scale = 0;
  for (int c = 0; c < 3; c++)
    {
      const double weight = capture_weights[c];
      if (!my_isfinite (weight))
        return false;
      weight_sum += weight;
      weight_scale += my_fabs (weight);
      if (weight != 0
          && (!capture[c]
              || (anticipate_sharpening
                  && capture[c]->get_mode ()
                         == sharpen_parameters::richardson_lucy_deconvolution)))
        return false;
    }

  /* A scalar transfer with zero DC response has no meaningful unit-DC
     normalization.  Reject near-cancellation as well as the exact all-zero
     case so arbitrary signed mix weights cannot amplify roundoff.  */
  const double threshold
      = std::numeric_limits<double>::epsilon ()
        * std::max (1.0, weight_scale) * 32;
  if (!my_isfinite (weight_sum) || weight_scale == 0
      || my_fabs (weight_sum) <= threshold)
    return false;

  memcpy (add, source.m_impl->add, sizeof (add));
  auto combined = fft_alloc_complex<screen_fft_t> (
      screen::size * fft_size);
  auto channel_filter = fft_alloc_complex<screen_fft_t> (
      screen::size * fft_size);
  for (int i = 0; i < screen::size * fft_size; i++)
    {
      combined[i][0] = 0;
      combined[i][1] = 0;
    }

  for (int c = 0; c < 3; c++)
    {
      const screen_fft_t weight
          = (screen_fft_t)((double)capture_weights[c] / weight_sum);
      if (weight == 0)
        continue;
      sharpen_parameters::sharpen_mode mode;
      if (!build_periodic_filter (
              channel_filter.get (), *capture[c], anticipate_sharpening,
              parallel, true, profile, &mode))
        return false;
      assert (mode != sharpen_parameters::richardson_lucy_deconvolution);
      for (int i = 0; i < screen::size * fft_size; i++)
        {
          combined[i][0] += weight * channel_filter[i][0];
          combined[i][1] += weight * channel_filter[i][1];
        }
    }

  /* BUILD_PERIODIC_FILTER gives every contributing channel exact unit DC
     before FFT normalization.  Renormalize the weighted sum once more to make
     the same invariant exact after finite-precision accumulation.  */
  const screen_fft_t data_scale
      = 1.0 / ((screen_fft_t)screen::size * (screen_fft_t)screen::size);
  if (!my_isfinite (combined[0][0])
      || my_fabs (combined[0][0]) < (screen_fft_t)1e-30)
    return false;
  const screen_fft_t dc_scale = data_scale / combined[0][0];
  if (!my_isfinite (dc_scale))
    return false;
  if (dc_scale != 1)
    for (int i = 0; i < screen::size * fft_size; i++)
      {
        combined[i][0] *= dc_scale;
        combined[i][1] *= dc_scale;
      }

  initialize_with_2D_fft_precomputed<screen_fft_t> (
      *this, source.m_impl->spectrum, combined.get (), 0, 2);
  if (profile)
    profile->screen_inverse_ffts += 3;
  return true;
}

'''
marker = "void\nscreen::initialize_with_point_spread (\n"
p = Path("src/libcolorscreen/screen.C")
s = p.read_text()
if s.count(marker) != 1:
    raise SystemExit(f"src/libcolorscreen/screen.C: point-spread marker count {s.count(marker)}")
p.write_text(s.replace(marker, implementation + marker, 1))

# Add a direct regression against the deliberately slow reference: build each
# native scanner transfer separately, apply it to the whole process-primary
# basis, mix those three results, and compare to the one-combined-transfer path.
test = r'''bool
test_weighted_image_layer_capture_transfer ()
{
  std::array<sharpen_parameters, 3> capture;
  for (int c = 0; c < 3; c++)
    {
      capture[c].mode = sharpen_parameters::wiener_deconvolution;
      capture[c].scanner_snr = (luminosity_t)(80 + 40 * c);
      capture[c].scanner_mtf.model = mtf_model::empirical_fallback;
      capture[c].scanner_mtf.sensor_fill_factor = 0;
      capture[c].scanner_mtf.sigma = (luminosity_t)(0.55 + 0.45 * c);
      capture[c].scanner_mtf.blur_diameter
          = (luminosity_t)(0.25 + 0.55 * c);
      capture[c].scanner_mtf_scale = (luminosity_t)0.012345;
    }
  sharpen_parameters *channels[3]
      = { &capture[0], &capture[1], &capture[2] };

  screen source;
  source.initialize (Paget);
  screen_filter_source prepared;
  if (!source.prepare_filter_source (prepared))
    return false;

  const rgbdata weights = { (luminosity_t)0.19,
                            (luminosity_t)0.33,
                            (luminosity_t)0.48 };
  const double weight_sum
      = (double)weights.red + (double)weights.green + (double)weights.blue;

  /* Check both the pure forward capture transfer and the linear
     sharpen-before-mix case used by Wiener deconvolution.  */
  for (bool anticipate_sharpening : { false, true })
    {
      screen combined;
      screen_filter_profile profile;
      if (!combined.initialize_with_weighted_capture_transfer (
              prepared, channels, weights, anticipate_sharpening, false,
              &profile))
        {
          fprintf (stderr,
                   "Weighted monochrome capture-transfer construction failed\n");
          return false;
        }
      if (profile.screen_forward_ffts != 0
          || profile.screen_inverse_ffts != 3
          || profile.empirical_focus_transfer_builds != 3)
        {
          fprintf (stderr,
                   "Weighted transfer used %llu source forward, %llu inverse, "
                   "and %llu empirical transfer builds\n",
                   (unsigned long long)profile.screen_forward_ffts,
                   (unsigned long long)profile.screen_inverse_ffts,
                   (unsigned long long)profile.empirical_focus_transfer_builds);
          return false;
        }

      std::array<std::unique_ptr<screen>, 3> separate;
      for (int capture_channel = 0; capture_channel < 3; capture_channel++)
        {
          std::array<sharpen_parameters, 3> same
              = { capture[capture_channel], capture[capture_channel],
                  capture[capture_channel] };
          sharpen_parameters *same_channels[3]
              = { &same[0], &same[1], &same[2] };
          separate[capture_channel] = std::make_unique<screen> ();
          if (!separate[capture_channel]->initialize_with_sharpen_parameters (
                  prepared, same_channels, anticipate_sharpening, false))
            return false;
        }

      double max_delta = 0;
      for (int y = 0; y < screen::size; y++)
        for (int x = 0; x < screen::size; x++)
          for (int primary = 0; primary < 3; primary++)
            {
              double reference = 0;
              for (int capture_channel = 0; capture_channel < 3;
                   capture_channel++)
                reference
                    += (double)weights[capture_channel]
                       * separate[capture_channel]->mult[y][x][primary];
              reference /= weight_sum;
              max_delta
                  = std::max (max_delta,
                              fabs ((double)combined.mult[y][x][primary]
                                    - reference));
            }
      if (max_delta > 2e-6)
        {
          fprintf (stderr,
                   "Weighted Fourier transfer differs from slow per-channel "
                   "reference by %.12g\n",
                   max_delta);
          return false;
        }

      /* Scaling a one-hot coefficient must not change the transfer.  This
         connects the general reference directly to the exact one-channel
         specialization merged in the previous step.  */
      screen one_hot;
      const rgbdata green_only = { 0, (luminosity_t)7.25, 0 };
      if (!one_hot.initialize_with_weighted_capture_transfer (
              prepared, channels, green_only, anticipate_sharpening, false))
        return false;
      luminosity_t delta = 0;
      if (!one_hot.almost_equal_p (*separate[1], &delta,
                                   (luminosity_t)2e-7))
        {
          fprintf (stderr,
                   "Weighted one-hot transfer differs from native green by %g\n",
                   (double)delta);
          return false;
        }
    }

  /* Normalizing the scalar transfer is undefined for no response or a
     cancelling signed mix, and non-finite weights must never enter FFT state.  */
  screen invalid;
  if (invalid.initialize_with_weighted_capture_transfer (
          prepared, channels, { 0, 0, 0 }, false, false)
      || invalid.initialize_with_weighted_capture_transfer (
          prepared, channels, { 1, -1, 0 }, false, false)
      || invalid.initialize_with_weighted_capture_transfer (
          prepared, channels, { test_runtime_nan_luminosity (), 0, 1 },
          false, false))
    {
      fprintf (stderr, "Invalid weighted capture coefficients were accepted\n");
      return false;
    }

  /* Richardson-Lucy is nonlinear, so only its forward-capture part may use
     the combined Fourier reference.  */
  std::array<sharpen_parameters, 3> rl = capture;
  rl[0].mode = sharpen_parameters::richardson_lucy_deconvolution;
  rl[0].richardson_lucy_iterations = 3;
  sharpen_parameters *rl_channels[3] = { &rl[0], &rl[1], &rl[2] };
  if (invalid.initialize_with_weighted_capture_transfer (
          prepared, rl_channels, { 1, 0, 0 }, true, false)
      || !invalid.initialize_with_weighted_capture_transfer (
          prepared, rl_channels, { 1, 0, 0 }, false, false))
    {
      fprintf (stderr, "Weighted transfer mishandled Richardson-Lucy mode\n");
      return false;
    }

  return true;
}

'''
marker = "bool\ntest_finetune_focus_screen_cache ()\n"
p = Path("src/libcolorscreen/unittests.C")
s = p.read_text()
if s.count(marker) != 1:
    raise SystemExit(f"src/libcolorscreen/unittests.C: focus-cache marker count {s.count(marker)}")
s = s.replace(marker, test + marker, 1)

registry = '''    { "finetune_focus_cache", "exact finetune focus-screen cache tests",
      [] () { return test_finetune_focus_screen_cache (); } },
'''
registry_new = '''    { "weighted_capture_transfer",
      "weighted monochrome RGB capture-transfer tests",
      [] () { return test_weighted_image_layer_capture_transfer (); } },
''' + registry
if s.count(registry) != 1:
    raise SystemExit(f"src/libcolorscreen/unittests.C: test registry count {s.count(registry)}")
p.write_text(s.replace(registry, registry_new, 1))

old = '''**Status:** partially fixed; exact one-channel mixes are handled, while general
weighted multi-channel forward models remain open.'''
new = '''**Status:** exact one-channel production handling and an exact weighted Fourier
reference are implemented; automatic production selection for general mixtures
remains open.'''
replace_once("doc/mtf-channel-domain-tracking.md", old, new, "DOM-005 status")

old = '''Where periodic screen simulation or BW finetune currently uses one
representative analytical transfer for an RGB-derived monochrome layer, add an
exact weighted-channel-transfer reference and compare it with the shortcut.
This should be substantially cheaper than the viewing-filter model below and
may often reduce to one combined Fourier transfer before the inverse FFT.'''
new = '''`screen::initialize_with_weighted_capture_transfer()` now provides the exact
Fourier-domain reference for this case.  Its input coefficients are effective
native scanner-channel weights (for example `w_c a_c` above).  It constructs
each contributing native-channel transfer, forms their normalized weighted sum
in Fourier space, and applies that one scalar transfer to all three
process-primary basis spectra before the inverse FFT.  A regression compares
this path with the deliberately slow reference that filters the whole basis
three times and mixes the spatial results afterwards; it also verifies exact
reduction to the previous one-channel case.  Anticipated Richardson-Lucy is
rejected because that iteration is nonlinear and cannot be collapsed into one
Fourier transfer.

Production periodic simulation/BW finetune still uses the representative
scalar specialization for genuine multi-channel RGB-derived layers.  Wiring the
new reference into those callers requires reliable capture provenance and the
effective native-channel gains: it must not become an automatic shortcut for a
viewing-filter transparency, whose model is MTF-DOM-006.'''
replace_once("doc/mtf-channel-domain-tracking.md", old, new, "DOM-005 reference text")
