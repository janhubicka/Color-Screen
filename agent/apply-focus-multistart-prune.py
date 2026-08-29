#!/usr/bin/env python3
from pathlib import Path

p = Path("src/libcolorscreen/focus-analysis.C")
s = p.read_text()

def once(old, new):
    global s
    n = s.count(old)
    if n != 1:
        raise SystemExit(f"expected one match, found {n}: {old[:80]!r}")
    s = s.replace(old, new, 1)

once(
"""  consider_joint_fit (rparam, param, img, locations, starts, fixed, progress,
                      &best, best_seed_kind, 0);

  struct scalar_seed
""",
"""  consider_joint_fit (rparam, param, img, locations, starts, fixed, progress,
                      &best, best_seed_kind, 0);
  const finetune_result loaded_fit = best;

  struct scalar_seed
""")

once(
"""  for (int which = 0; which < 2; which++)
    consider_scalar_seed (which, rparam);

  /* Physical defocus is even at exact focus, so a simplex initialized
""",
"""  for (int which = 0; which < 2; which++)
    consider_scalar_seed (which, rparam);

  auto same_loaded_focus = [&] (const scalar_seed &seed) {
    if (!loaded_fit.success || !seed.success)
      return false;
    auto close = [] (coord_t a, coord_t b) {
      if (!my_isfinite (a) || !my_isfinite (b))
        return false;
      const coord_t scale
          = std::max ((coord_t)1, std::max (std::fabs (a), std::fabs (b)));
      return std::fabs (a - b) <= (coord_t)1e-8 * scale;
    };
    return close (seed.fit.scanner_mtf_sigma, loaded_fit.scanner_mtf_sigma)
           && close (seed.fit.scanner_mtf_defocus,
                     loaded_fit.scanner_mtf_defocus);
  };

  constexpr coord_t cold_epsilon = (coord_t)1e-8;
  const bool cold_start
      = my_isfinite (rparam.sharpen.scanner_mtf.defocus)
        && rparam.sharpen.scanner_mtf.defocus >= 0
        && rparam.sharpen.scanner_mtf.defocus <= cold_epsilon;

  /* Physical defocus is even at exact focus, so a simplex initialized
""")

once(
"""  constexpr coord_t cold_epsilon = (coord_t)1e-8;
  if (my_isfinite (rparam.sharpen.scanner_mtf.defocus)
      && rparam.sharpen.scanner_mtf.defocus >= 0
      && rparam.sharpen.scanner_mtf.defocus <= cold_epsilon)
""",
"""  if (cold_start)
""")

old_release = """  /* Release both coordinates from the better scalar basin first.  Usually one
     of these already captures the process-screen attenuation and this avoids
     paying for two equally poor coupled searches.  If it does not beat the
     loaded start, try the other basin as a fallback.  */
  int first = 0, second = 1;
  if ((!scalar[first].success && scalar[second].success)
      || (scalar[first].success && scalar[second].success
          && scalar[second].badness < scalar[first].badness))
    std::swap (first, second);
  if (scalar[first].success)
    consider_joint_fit (scalar[first].rparam, param, img, locations, starts,
                        fixed, progress, &best, best_seed_kind,
                        scalar[first].kind);
  if (scalar[second].success)
    consider_joint_fit (scalar[second].rparam, param, img, locations, starts,
                        fixed, progress, &best, best_seed_kind,
                        scalar[second].kind);

  /* A cold defocus coordinate cannot leave zero by local first-order
"""
new_release = """  /* Release both coordinates from the better scalar basin first.  A cold
     physical start has a more reliable sigma-then-defocus continuation below,
     so defer these expensive coupled releases to fallback.  For a calibrated
     start, do not rerun a coupled solve when the scalar prefit returned the
     same focus state as the already evaluated loaded coupled solution.  */
  int first = 0, second = 1;
  if ((!scalar[first].success && scalar[second].success)
      || (scalar[first].success && scalar[second].success
          && scalar[second].badness < scalar[first].badness))
    std::swap (first, second);
  auto release_scalar_basin = [&] (int which) {
    if (!scalar[which].success || same_loaded_focus (scalar[which]))
      return;
    consider_joint_fit (scalar[which].rparam, param, img, locations, starts,
                        fixed, progress, &best, best_seed_kind,
                        scalar[which].kind);
  };
  if (!cold_start)
    {
      release_scalar_basin (first);
      release_scalar_basin (second);
    }

  bool cold_continuation_found = false;

  /* A cold defocus coordinate cannot leave zero by local first-order
"""
once(old_release, new_release)

once(
"""  constexpr coord_t cold_focus_epsilon = (coord_t)1e-8;
  if (scalar[0].success
      && my_isfinite (rparam.sharpen.scanner_mtf.defocus)
      && rparam.sharpen.scanner_mtf.defocus >= 0
      && rparam.sharpen.scanner_mtf.defocus <= cold_focus_epsilon)
""",
"""  constexpr coord_t cold_focus_epsilon = (coord_t)1e-8;
  if (scalar[0].success && cold_start)
""")

old_polish = """          if (staged.success)
            {
              if (!best.success || staged.fit.badness < best.badness)
                {
                  best = staged.fit;
                  if (best_seed_kind)
                    *best_seed_kind = scalar[0].kind;
                }

              /* Polish sigma with the recovered nonzero defocus fixed before
                 finally releasing both physical transfer coordinates.  */
              finetune_parameters sigma_stage = fixed;
              sigma_stage.flags &= ~coupled;
              sigma_stage.flags |= finetune_scanner_mtf_sigma;
              sigma_stage.interpolate_scanner_mtf_defocus = false;
              finetune_result fit
                  = finetune (staged.rparam, param, img, locations, &starts,
                              sigma_stage, progress);
              if (fit.success && my_isfinite (fit.badness))
                {
                  render_parameters polished = staged.rparam;
                  if (freeze_focus_from_fit (&polished, sigma_stage.flags, fit))
                    {
                      fit.scanner_mtf_defocus
                          = polished.sharpen.scanner_mtf.defocus;
                      if (!best.success || fit.badness < best.badness)
                        {
                          best = fit;
                          if (best_seed_kind)
                            *best_seed_kind = scalar[0].kind;
                        }
                      consider_joint_fit (
                          polished, param, img, locations, starts, fixed,
                          progress, &best, best_seed_kind, scalar[0].kind);
                    }
                }
            }
"""
new_polish = """          if (staged.success)
            {
              cold_continuation_found = true;
              if (!best.success || staged.fit.badness < best.badness)
                {
                  best = staged.fit;
                  if (best_seed_kind)
                    *best_seed_kind = scalar[0].kind;
                }

              /* The staged point has already been evaluated with a useful
                 nonzero defocus.  Release sigma and defocus together from
                 there; a separate sigma-only polish rebuilds the expensive
                 finite physical PSF and is immediately superseded by this
                 coupled solve.  */
              consider_joint_fit (staged.rparam, param, img, locations, starts,
                                  fixed, progress, &best, best_seed_kind,
                                  scalar[0].kind);
            }
"""
once(old_polish, new_polish)

once(
"""    }

  if (best.success && (fparams.flags & finetune_position))
""",
"""    }

  /* If the cold sigma-then-defocus continuation could not produce a usable
     seed, retain the old scalar-basin releases as a robustness fallback.  */
  if (cold_start && !cold_continuation_found)
    {
      release_scalar_basin (first);
      release_scalar_basin (second);
    }

  if (best.success && (fparams.flags & finetune_position))
""")

p.write_text(s)
