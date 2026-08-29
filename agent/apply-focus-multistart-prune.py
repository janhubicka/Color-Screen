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

# Let callers distinguish a successful released solve from a failed one.  The
# existing callers may ignore the return value.
once(
"""static void
consider_joint_fit (const render_parameters &rparam,
""",
"""static bool
consider_joint_fit (const render_parameters &rparam,
""")
once(
"""  if (!fit.success || !my_isfinite (fit.badness))
    return;
  if (!best->success || fit.badness < best->badness)
""",
"""  if (!fit.success || !my_isfinite (fit.badness))
    return false;
  if (!best->success || fit.badness < best->badness)
""")
once(
"""      if (best_seed_kind)
        *best_seed_kind = seed_kind;
    }
}

/* Coupled physical sigma/defocus fitting""",
"""      if (best_seed_kind)
        *best_seed_kind = seed_kind;
    }
  return true;
}

/* Coupled physical sigma/defocus fitting""")

# Classify a cold physical start before deciding whether the loaded coupled
# solve is worth running.
once(
"""  finetune_result best;
  if (best_seed_kind)
    *best_seed_kind = -1;
  consider_joint_fit (rparam, param, img, locations, starts, fixed, progress,
                      &best, best_seed_kind, 0);

  struct scalar_seed
""",
"""  constexpr coord_t cold_epsilon = (coord_t)1e-8;
  const bool cold_start
      = my_isfinite (rparam.sharpen.scanner_mtf.defocus)
        && rparam.sharpen.scanner_mtf.defocus >= 0
        && rparam.sharpen.scanner_mtf.defocus <= cold_epsilon;

  finetune_result best;
  finetune_result loaded_fit;
  if (best_seed_kind)
    *best_seed_kind = -1;
  /* At exact physical focus the coupled simplex cannot obtain a useful
     first-order defocus direction.  Defer that expensive loaded solve until
     fallback; calibrated starts still evaluate it immediately.  */
  if (!cold_start)
    {
      consider_joint_fit (rparam, param, img, locations, starts, fixed,
                          progress, &best, best_seed_kind, 0);
      loaded_fit = best;
    }

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

# Keep scalar feasible points, but defer their expensive coupled releases on a
# cold start.  On a warm start skip releases that exactly reproduce the already
# evaluated loaded focus state.
once(
"""  /* Release both coordinates from the better scalar basin first.  Usually one
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
""",
"""  /* Release both coordinates from scalar basins only when they add a distinct
     warm start.  The cold sigma-then-defocus continuation below is both more
     reliable and cheaper than first releasing the boundary scalar basin.  */
  int first = 0, second = 1;
  if ((!scalar[first].success && scalar[second].success)
      || (scalar[first].success && scalar[second].success
          && scalar[second].badness < scalar[first].badness))
    std::swap (first, second);
  auto release_scalar_basin = [&] (int which) {
    if (!scalar[which].success || same_loaded_focus (scalar[which]))
      return false;
    return consider_joint_fit (scalar[which].rparam, param, img, locations,
                               starts, fixed, progress, &best, best_seed_kind,
                               scalar[which].kind);
  };
  if (!cold_start)
    {
      release_scalar_basin (first);
      release_scalar_basin (second);
    }

  bool cold_coupled_release_succeeded = false;

  /* A cold defocus coordinate cannot leave zero by local first-order
""")

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

# Preserve the sigma polish: although it can report the same scalar sigma, the
# exact finite-PSF solver uses it to establish the useful interior basin before
# releasing both coordinates.  Only record success when that released coupled
# solve itself succeeds.
once(
"""                      consider_joint_fit (
                          polished, param, img, locations, starts, fixed,
                          progress, &best, best_seed_kind, scalar[0].kind);
""",
"""                      cold_coupled_release_succeeded
                          = consider_joint_fit (
                              polished, param, img, locations, starts, fixed,
                              progress, &best, best_seed_kind,
                              scalar[0].kind);
""")

# If the preferred cold continuation fails at any point, restore the complete
# old loaded/scalar coupled search as robustness fallback.
once(
"""    }

  if (best.success && (fparams.flags & finetune_position))
""",
"""    }

  if (cold_start && !cold_coupled_release_succeeded)
    {
      consider_joint_fit (rparam, param, img, locations, starts, fixed,
                          progress, &best, best_seed_kind, 0);
      if (scalar[first].success)
        consider_joint_fit (scalar[first].rparam, param, img, locations,
                            starts, fixed, progress, &best, best_seed_kind,
                            scalar[first].kind);
      if (scalar[second].success)
        consider_joint_fit (scalar[second].rparam, param, img, locations,
                            starts, fixed, progress, &best, best_seed_kind,
                            scalar[second].kind);
    }

  if (best.success && (fparams.flags & finetune_position))
""")

p.write_text(s)
