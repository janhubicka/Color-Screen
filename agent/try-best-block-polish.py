#!/usr/bin/env python3
from pathlib import Path

p = Path("src/libcolorscreen/focus-analysis.C")
text = p.read_text()
old = """  if (best.success && (fparams.flags & finetune_position))
"""
new = """  /* Once direct coupled cold starts have found an interior physical basin,
     polish along the shallow sigma/defocus compensation valley one coordinate
     at a time before releasing both coordinates again.  Keep every step in
     competition by the same shared pixel objective.  */
  if (best.success && (fparams.flags & coupled) == coupled
      && rparam.sharpen.scanner_mtf.simulate_diffraction_p ())
    {
      render_parameters block_seed = rparam;
      if (freeze_focus_from_fit (&block_seed, fparams.flags, best))
        {
          finetune_parameters sigma_stage = fixed;
          sigma_stage.flags &= ~coupled;
          sigma_stage.flags |= finetune_scanner_mtf_sigma;
          sigma_stage.interpolate_scanner_mtf_defocus = false;
          finetune_result sigma_fit
              = finetune (block_seed, param, img, locations, &starts,
                          sigma_stage, progress);
          if (sigma_fit.success && my_isfinite (sigma_fit.badness))
            {
              render_parameters sigma_seed = block_seed;
              if (freeze_focus_from_fit (&sigma_seed, sigma_stage.flags,
                                         sigma_fit))
                {
                  sigma_fit.scanner_mtf_defocus
                      = sigma_seed.sharpen.scanner_mtf.defocus;
                  if (!best.success || sigma_fit.badness < best.badness)
                    {
                      best = sigma_fit;
                      if (best_seed_kind && *best_seed_kind < 0)
                        *best_seed_kind = 0;
                    }

                  finetune_parameters defocus_stage = fixed;
                  defocus_stage.flags &= ~coupled;
                  defocus_stage.flags |= finetune_scanner_mtf_defocus;
                  defocus_stage.interpolate_scanner_mtf_defocus = false;
                  finetune_result defocus_fit
                      = finetune (sigma_seed, param, img, locations, &starts,
                                  defocus_stage, progress);
                  if (defocus_fit.success && my_isfinite (defocus_fit.badness))
                    {
                      render_parameters polished = sigma_seed;
                      if (freeze_focus_from_fit (&polished,
                                                 defocus_stage.flags,
                                                 defocus_fit))
                        {
                          defocus_fit.scanner_mtf_sigma
                              = polished.sharpen.scanner_mtf.sigma;
                          if (!best.success
                              || defocus_fit.badness < best.badness)
                            best = defocus_fit;
                          consider_joint_fit (
                              polished, param, img, locations, starts, fixed,
                              progress, &best, best_seed_kind,
                              best_seed_kind ? *best_seed_kind : -1);
                        }
                    }
                }
            }
        }
    }

  if (best.success && (fparams.flags & finetune_position))
"""
if text.count(old) != 1:
    raise SystemExit(f"expected one final refinement gate, found {text.count(old)}")
p.write_text(text.replace(old, new, 1))
