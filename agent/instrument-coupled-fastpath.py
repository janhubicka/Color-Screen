#!/usr/bin/env python3
from pathlib import Path


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text()
    n = text.count(old)
    if n != 1:
        raise SystemExit(f"{path}: expected one match, found {n}")
    p.write_text(text.replace(old, new, 1))

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
           "FAST_PROFILE %s flags=0x%llx in-sigma=%.9f in-defocus=%.9f "
           "ok=%d bad=%.12g out-sigma=%.9f out-defocus=%.9f mtf=%.9f "
           "eval=%llu obj-ms=%.3f filter-ms=%.3f exact=%llu "
           "physical=%llu direct=%llu mtf-pre=%llu psf-pre=%llu "
           "wrapped=%llu kernel-fft=%llu source-cache=%llu/%llu\\n",
           label, (unsigned long long)fparams.flags,
           (double)rparam.sharpen.scanner_mtf.sigma,
           (double)rparam.sharpen.scanner_mtf.defocus, fit.success ? 1 : 0,
           (double)fit.badness, (double)fit.scanner_mtf_sigma,
           (double)fit.scanner_mtf_defocus, (double)carrier,
           (unsigned long long)p.objective_evaluations,
           p.objective_nanoseconds / 1e6, p.screen_filter_nanoseconds / 1e6,
           (unsigned long long)p.exact_screen_builds,
           (unsigned long long)p.physical_focus_transfer_builds,
           (unsigned long long)p.direct_transfer_builds,
           (unsigned long long)p.mtf_precompute_calls,
           (unsigned long long)p.mtf_psf_precompute_calls,
           (unsigned long long)p.wrapped_psf_builds,
           (unsigned long long)p.kernel_forward_ffts,
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
