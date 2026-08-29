#!/usr/bin/env python3
from pathlib import Path

p = Path("src/libcolorscreen/focus-analysis.C")
s = p.read_text()
old = '''  if (best.success && (fparams.flags & finetune_position))
    {
      render_parameters final_seed = rparam;
      if (freeze_focus_from_fit (&final_seed, fparams.flags, best))
        {
          finetune_result refined
              = finetune (final_seed, param, img, locations, &starts, fparams,
                          progress);
          if (refined.success && my_isfinite (refined.badness)
              && refined.badness <= best.badness)
            best = std::move (refined);
        }
    }
'''
new = '''  if (best.success && (fparams.flags & finetune_position))
    {
      /* Position contributes two independent simplex coordinates per tile on
         two-dimensional screens.  Optimizing all local phases together with
         the shared MTF therefore scales poorly with the number of areas.
         Refine the separable local phases one tile at a time at fixed focus,
         then clean up only the shared focus coordinates with all phases fixed.
         This keeps the exact finite-PSF forward model while bounding each
         simplex block independently of the number of samples.  */
      render_parameters phase_rparam = rparam;
      if (freeze_focus_from_fit (&phase_rparam, fparams.flags, best))
        {
          finetune_parameters phase_params = fparams;
          phase_params.flags
              &= ~(finetune_scanner_mtf_sigma
                   | finetune_scanner_mtf_defocus
                   | finetune_scanner_mtf_channel_defocus);
          phase_params.flags |= finetune_position;
          phase_params.interpolate_scanner_mtf_defocus = false;
          if (phase_params.flags & finetune_uniform_image_layer)
            phase_params.flags |= finetune_fixed_screen_colors;

          std::vector<finetune_result> phase_starts = starts;
          bool phase_ok = phase_starts.size () == locations.size ();
          for (size_t i = 0; phase_ok && i < locations.size (); i++)
            {
              std::vector<point_t> one_location = { locations[i] };
              std::vector<finetune_result> one_start = { phase_starts[i] };
              if (phase_params.flags & finetune_fixed_screen_colors)
                {
                  one_start[0].screen_red = best.screen_red;
                  one_start[0].screen_green = best.screen_green;
                  one_start[0].screen_blue = best.screen_blue;
                }
              finetune_result phase_fit
                  = finetune (phase_rparam, param, img, one_location,
                              &one_start, phase_params, progress);
              if (!phase_fit.success || !my_isfinite (phase_fit.badness))
                phase_ok = false;
              else
                phase_starts[i] = std::move (phase_fit);
            }

          if (phase_ok)
            {
              finetune_result refined
                  = finetune (phase_rparam, param, img, locations,
                              &phase_starts, fixed, progress);
              if (refined.success && my_isfinite (refined.badness)
                  && refined.badness <= best.badness)
                best = std::move (refined);
              else
                phase_ok = false;
            }

          /* Preserve the old simultaneous refinement as a robustness fallback
             if a local phase block or the shared cleanup fails to improve the
             already valid fixed-phase solution.  */
          if (!phase_ok)
            {
              finetune_result refined
                  = finetune (phase_rparam, param, img, locations, &starts,
                              fparams, progress);
              if (refined.success && my_isfinite (refined.badness)
                  && refined.badness <= best.badness)
                best = std::move (refined);
            }
        }
    }
'''
if s.count(old) != 1:
    raise SystemExit(f"expected one final-refinement block, found {s.count(old)}")
p.write_text(s.replace(old, new, 1))
