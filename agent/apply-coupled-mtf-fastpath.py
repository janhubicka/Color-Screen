#!/usr/bin/env python3
from pathlib import Path


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    "src/libcolorscreen/mtf.h",
    """/* Immutable defocus-independent state of the analytical physical capture
   transfer.  The expensive fixed diffraction, sensor, halo, and pupil-overlap
   terms are prepared once and shared through an LRU cache.  PRECOMPUTE builds
   the same signed 512-sample radial transfer table as MTF::PRECOMPUTE for the
   requested image-plane DEFOCUS, but evaluates only the varying pupil phase.
""",
    """/* Immutable defocus- and residual-sigma-independent state of the
   analytical physical capture transfer.  The expensive fixed diffraction,
   sensor, halo, and pupil-overlap terms are prepared once and shared through
   an LRU cache.  PRECOMPUTE builds the same signed 512-sample radial transfer
   table as MTF::PRECOMPUTE for the requested image-plane DEFOCUS and residual
   Gaussian SIGMA, evaluating only those varying compact-core terms.
""",
)
replace_once(
    "src/libcolorscreen/mtf.h",
    """  /* Return a cached transfer source for PARAMS with DEFOCUS excluded from the
     key.  Return null when PARAMS does not select the analytical physical
     model.  CACHE_HIT, when nonnull, reports whether the immutable state was
     already present.  */
""",
    """  /* Return a cached transfer source for PARAMS with DEFOCUS and residual
     SIGMA excluded from the key.  Return null when PARAMS does not select the
     analytical physical model.  CACHE_HIT, when nonnull, reports whether the
     immutable state was already present.  */
""",
)
replace_once(
    "src/libcolorscreen/mtf.h",
    """  /* Build the signed radial transfer table for DEFOCUS.  Return false if the
     prepared state or resulting coefficients are invalid.  */
  nodiscard_attr bool
  precompute (double defocus, precomputed_function<double> &transfer) const;
""",
    """  /* Build the signed radial transfer table for DEFOCUS and residual SIGMA.
     Return false if the prepared state or resulting coefficients are invalid.  */
  nodiscard_attr bool
  precompute (double defocus, double sigma,
              precomputed_function<double> &transfer) const;
""",
)

replace_once(
    "src/libcolorscreen/mtf.C",
    """/* One defocus-independent sample of the physical system transfer.  COMPACT
   already contains sensor aperture, diffraction, residual Gaussian blur, and
   the compact-core energy fraction.  HALO is the complete fixed broad-halo
   contribution.  Only the normalized pupil integral in DEFOCUS_FACTOR varies
   between focus states.  */
""",
    """/* One defocus- and residual-sigma-independent sample of the physical system
   transfer.  COMPACT contains sensor aperture, diffraction, and the compact-
   core energy fraction.  HALO is the complete fixed broad-halo contribution.
   Residual Gaussian blur and the normalized pupil integral in DEFOCUS_FACTOR
   vary between focus states.  */
""",
)
replace_once(
    "src/libcolorscreen/mtf.C",
    """/* Prepare all physical-transfer terms that do not depend on DEFOCUS.  */
""",
    """/* Prepare all physical-transfer terms that do not depend on DEFOCUS or
   residual Gaussian SIGMA.  */
""",
)
replace_once(
    "src/libcolorscreen/mtf.C",
    """      sample.compact
          = sensor * compact_fraction
            * params.lens_diffraction_otf (frequency)
            * gaussian_blur_mtf (frequency, params.sigma);
""",
    """      sample.compact
          = sensor * compact_fraction
            * params.lens_diffraction_otf (frequency);
""",
)
replace_once(
    "src/libcolorscreen/mtf.C",
    """/* Construct the same equidistant signed system-transfer table used by the
   analytical MTF implementation, evaluating only the defocus-dependent pupil
   numerator for every new focus state.  */
bool
mtf_focus_transfer::precompute (
    double defocus, precomputed_function<double> &transfer) const
{
  if (!m_impl)
    return false;
""",
    """/* Construct the same equidistant signed system-transfer table used by the
   analytical MTF implementation, evaluating the varying residual Gaussian and
   defocus-dependent pupil terms for every new focus state.  */
bool
mtf_focus_transfer::precompute (
    double defocus, double sigma, precomputed_function<double> &transfer) const
{
  if (!m_impl || !my_isfinite (sigma) || sigma < 0)
    return false;
""",
)
replace_once(
    "src/libcolorscreen/mtf.C",
    """      values[i] = sample.compact * defocus_factor + sample.halo;
""",
    """      const double sigma_factor
          = gaussian_blur_mtf (i * focus_transfer_step, sigma);
      values[i]
          = sample.compact * sigma_factor * defocus_factor + sample.halo;
""",
)
replace_once(
    "src/libcolorscreen/mtf.C",
    """/* LRU key for immutable physical-transfer state.  Defocus is normalized to
   zero before lookup, while every other active physical-model parameter is
   compared by MTF_PARAMETERS::OPERATOR==.  */
""",
    """/* LRU key for immutable physical-transfer state.  Defocus and residual
   Gaussian sigma are normalized to zero before lookup, while every other
   active physical-model parameter is compared by MTF_PARAMETERS::OPERATOR==.  */
""",
)
replace_once(
    "src/libcolorscreen/mtf.C",
    """  key.params = params;
  key.params.defocus = 0;
  return mtf_focus_transfer_cache.get (key, nullptr, nullptr, cache_hit);
""",
    """  key.params = params;
  key.params.defocus = 0;
  key.params.sigma = 0;
  return mtf_focus_transfer_cache.get (key, nullptr, nullptr, cache_hit);
""",
)

replace_once(
    "src/libcolorscreen/screen.C",
    """     coefficients by the capture OTF at the corresponding harmonics.  Reuse
     the defocus-independent physical state when available.  The empirical
""",
    """     coefficients by the capture OTF at the corresponding harmonics.  Reuse
     the defocus- and residual-sigma-independent physical state when available.
     The empirical
""",
)
replace_once(
    "src/libcolorscreen/screen.C",
    """      if (!physical_focus->precompute (
              sharpen.scanner_mtf.defocus, transfer))
""",
    """      if (!physical_focus->precompute (
              sharpen.scanner_mtf.defocus, sharpen.scanner_mtf.sigma, transfer))
""",
)

replace_once(
    "src/libcolorscreen/finetune.C",
    """  /* Source spectra can be shared while the ideal periodic screen stays
     fixed.  Scalar physical defocus and compact fallback blur diameter both
     change only the capture transfer.  Strip-width fitting is intentionally
     left on the ordinary exact path because it changes the source screen
     itself.  */
  bool
  focus_source_cache_eligible_p () const
  {
    return optimize_scanner_mtf_defocus && !optimize_scanner_mtf_sigma
           && !optimize_scanner_mtf_channel_defocus && !optimize_strips
""",
    """  /* Source spectra can be shared while the ideal periodic screen stays
     fixed.  Scalar residual sigma, physical defocus, and compact fallback blur
     diameter change only the capture transfer.  Strip-width fitting is
     intentionally left on the ordinary exact path because it changes the
     source screen itself.  */
  bool
  focus_source_cache_eligible_p () const
  {
    return (optimize_scanner_mtf_defocus || optimize_scanner_mtf_sigma)
           && !optimize_scanner_mtf_channel_defocus && !optimize_strips
""",
)

# The internal regression-test helper already exists specifically to obtain an
# exact prepared-source finetune screen.  Give that narrow test hook default
# visibility so shared-library test executables can call it without exporting
# the lower-level screen_filter_source implementation.
replace_once(
    "src/libcolorscreen/finetune-int.h",
    """std::shared_ptr<screen> finetune_get_cached_screen_for_test (
""",
    """DLL_PUBLIC std::shared_ptr<screen> finetune_get_cached_screen_for_test (
""",
)

replace_once(
    "src/libcolorscreen/unittests.C",
    """  mtf_parameters comparison_parameters = physical[0].scanner_mtf;
  comparison_parameters.defocus = (coord_t)0.5;
  std::shared_ptr<const mtf_focus_transfer> reused_transfer
""",
    """  mtf_parameters comparison_parameters = physical[0].scanner_mtf;
  comparison_parameters.defocus = (coord_t)0.5;
  comparison_parameters.sigma = (coord_t)1.17;
  std::shared_ptr<const mtf_focus_transfer> reused_transfer
""",
)
replace_once(
    "src/libcolorscreen/unittests.C",
    """  precomputed_function<double> prepared_table;
  if (!reused_transfer->precompute (comparison_parameters.defocus,
                                    prepared_table))
""",
    """  precomputed_function<double> prepared_table;
  if (!reused_transfer->precompute (comparison_parameters.defocus,
                                    comparison_parameters.sigma,
                                    prepared_table))
""",
)

# Generate the synthetic coupled-focus fixture through the existing exact
# prepared-source finetune test hook.  The ordinary screen overload retains its
# historical sampled-PSF numerical path, so mixing the two implementations
# would test discretization differences rather than cold/warm optimizer
# stability.
replace_once(
    "src/libcolorscreen/focus-analysis-unittests.C",
    """  screen source, filtered;
  source.initialize (Paget);
  sharpen_parameters *sp[3] = { &capture, &capture, &capture };
  if (!filtered.initialize_with_sharpen_parameters (source, sp, false, false))
    return false;
""",
    """  std::array<sharpen_parameters, 3> exact_capture
      = { capture, capture, capture };
  bool fixture_cache_hit = false;
  std::shared_ptr<screen> filtered = finetune_get_cached_screen_for_test (
      Paget, 0, 0, false, exact_capture, false, &fixture_cache_hit);
  if (!filtered)
    return false;
""",
)
replace_once(
    "src/libcolorscreen/focus-analysis-unittests.C",
    """        const rgbdata screen_value = filtered.interpolated_mult (
""",
    """        const rgbdata screen_value = filtered->interpolated_mult (
""",
)
replace_once(
    "src/libcolorscreen/focus-analysis-unittests.C",
    """  fparam.flags = finetune_scanner_mtf_sigma | finetune_scanner_mtf_defocus
                 | finetune_bw | finetune_no_normalize
                 | finetune_no_data_collection;
  finetune_focus_analysis_parameters analysis;
""",
    """  fparam.flags = finetune_scanner_mtf_sigma | finetune_scanner_mtf_defocus
                 | finetune_bw | finetune_no_normalize
                 | finetune_no_data_collection;
  fparam.collect_profile = true;
  finetune_focus_analysis_parameters analysis;
""",
)
replace_once(
    "src/libcolorscreen/focus-analysis-unittests.C",
    """      return false;
    }
  return true;
}

static bool
test_grayscale_image_search ()
""",
    """      return false;
    }

  const finetune_profile &profile = cold.joint_fit.profile;
  if (!profile.physical_focus_transfer_builds
      || !profile.direct_transfer_builds
      || profile.mtf_precompute_calls
      || profile.mtf_psf_precompute_calls
      || profile.wrapped_psf_builds
      || profile.kernel_forward_ffts)
    {
      fprintf (
          stderr,
          "Coupled physical focus missed prepared transfer path: "
          "physical %llu direct %llu mtf %llu psf %llu wrapped %llu "
          "kernel-fft %llu\\n",
          (unsigned long long)profile.physical_focus_transfer_builds,
          (unsigned long long)profile.direct_transfer_builds,
          (unsigned long long)profile.mtf_precompute_calls,
          (unsigned long long)profile.mtf_psf_precompute_calls,
          (unsigned long long)profile.wrapped_psf_builds,
          (unsigned long long)profile.kernel_forward_ffts);
      return false;
    }
  return true;
}

static bool
test_grayscale_image_search ()
""",
)
