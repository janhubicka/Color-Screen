#!/usr/bin/env python3
from pathlib import Path


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text()
    n = text.count(old)
    if n != 1:
        raise SystemExit(f"{path}: expected one match, found {n}")
    p.write_text(text.replace(old, new, 1))

# Enable existing FINETUNE profiling for the synthetic coupled regression.
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

# Add a compact per-FINETUNE report in the multistart wrapper.
replace_once(
    "src/libcolorscreen/focus-analysis.C",
    """/* Run one shared all-area fit from RPARAM and keep the better successful
   result.  */
static void
consider_joint_fit""",
    """static void
report_focus_profile (const char *label, const render_parameters &rparam,
                      const scr_to_img_parameters &param, const image_data &img,
                      const finetune_parameters &fparams,
                      const finetune_result &fit)
{
  if (!fparams.collect_profile)
    return;
  const finetune_profile &p = fit.profile;
  const coord_t frequency = process_screen_frequency (param, img);
  const coord_t carrier = fit_system_mtf (rparam, img, fparams.flags, fit,
                                          frequency);
  fprintf (stderr,
           "FOCUS_PROFILE %s flags=0x%llx in-sigma=%.6f in-defocus=%.6f "
           "ok=%d bad=%.12g out-sigma=%.6f out-defocus=%.6f mtf=%.6f "
           "eval=%llu obj-ms=%.3f filter-ms=%.3f cache-ms=%.3f "
           "sim-ms=%.3f color-ms=%.3f residual-ms=%.3f exact=%llu "
           "mtf-pre=%llu psf-pre=%llu wrapped=%llu kernel-fft=%llu "
           "screen-fft=%llu/%llu focus-cache=%llu/%llu source-cache=%llu/%llu\\n",
           label, (unsigned long long)fparams.flags,
           (double)rparam.sharpen.scanner_mtf.sigma,
           (double)rparam.sharpen.scanner_mtf.defocus, fit.success ? 1 : 0,
           (double)fit.badness, (double)fit.scanner_mtf_sigma,
           (double)fit.scanner_mtf_defocus, (double)carrier,
           (unsigned long long)p.objective_evaluations,
           p.objective_nanoseconds / 1e6, p.screen_filter_nanoseconds / 1e6,
           p.screen_cache_nanoseconds / 1e6,
           p.screen_simulation_nanoseconds / 1e6,
           p.color_estimation_nanoseconds / 1e6,
           p.residual_nanoseconds / 1e6,
           (unsigned long long)p.exact_screen_builds,
           (unsigned long long)p.mtf_precompute_calls,
           (unsigned long long)p.mtf_psf_precompute_calls,
           (unsigned long long)p.wrapped_psf_builds,
           (unsigned long long)p.kernel_forward_ffts,
           (unsigned long long)p.screen_forward_ffts,
           (unsigned long long)p.screen_inverse_ffts,
           (unsigned long long)p.focus_screen_cache_hits,
           (unsigned long long)p.focus_screen_cache_misses,
           (unsigned long long)p.focus_source_cache_hits,
           (unsigned long long)p.focus_source_cache_misses);
}

/* Run one shared all-area fit from RPARAM and keep the better successful
   result.  */
static void
consider_joint_fit""",
)
replace_once(
    "src/libcolorscreen/focus-analysis.C",
    """  finetune_result fit
      = finetune (rparam, param, img, locations, &starts, fparams, progress);
  if (!fit.success || !my_isfinite (fit.badness))
""",
    """  finetune_result fit
      = finetune (rparam, param, img, locations, &starts, fparams, progress);
  char profile_label[64];
  snprintf (profile_label, sizeof (profile_label), "joint-seed-%d", seed_kind);
  report_focus_profile (profile_label, rparam, param, img, fparams, fit);
  if (!fit.success || !my_isfinite (fit.badness))
""",
)
replace_once(
    "src/libcolorscreen/focus-analysis.C",
    """    finetune_result fit
        = finetune (seed, param, img, locations, &starts, one, progress);
    if (!fit.success || !my_isfinite (fit.badness))
""",
    """    finetune_result fit
        = finetune (seed, param, img, locations, &starts, one, progress);
    report_focus_profile (which == 0 ? "scalar-sigma" : "scalar-defocus",
                          seed, param, img, one, fit);
    if (!fit.success || !my_isfinite (fit.badness))
""",
)
replace_once(
    "src/libcolorscreen/focus-analysis.C",
    """              finetune_result fit
                  = finetune (seed, param, img, locations, &starts,
                              defocus_stage, progress);
              if (!fit.success || !my_isfinite (fit.badness))
""",
    """              finetune_result fit
                  = finetune (seed, param, img, locations, &starts,
                              defocus_stage, progress);
              report_focus_profile ("staged-defocus", seed, param, img,
                                    defocus_stage, fit);
              if (!fit.success || !my_isfinite (fit.badness))
""",
)
replace_once(
    "src/libcolorscreen/focus-analysis.C",
    """              finetune_result fit
                  = finetune (staged.rparam, param, img, locations, &starts,
                              sigma_stage, progress);
              if (fit.success && my_isfinite (fit.badness))
""",
    """              finetune_result fit
                  = finetune (staged.rparam, param, img, locations, &starts,
                              sigma_stage, progress);
              report_focus_profile ("staged-sigma", staged.rparam, param, img,
                                    sigma_stage, fit);
              if (fit.success && my_isfinite (fit.badness))
""",
)
replace_once(
    "src/libcolorscreen/focus-analysis.C",
    """          finetune_result refined
              = finetune (final_seed, param, img, locations, &starts, fparams,
                          progress);
          if (refined.success && my_isfinite (refined.badness)
""",
    """          finetune_result refined
              = finetune (final_seed, param, img, locations, &starts, fparams,
                          progress);
          report_focus_profile ("final-phase", final_seed, param, img,
                                fparams, refined);
          if (refined.success && my_isfinite (refined.badness)
""",
)
