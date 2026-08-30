#!/usr/bin/env python3
from pathlib import Path

p = Path("src/libcolorscreen/focus-analysis.C")
text = p.read_text()
old = """  if (best.success && (fparams.flags & finetune_position))
"""
new = """  /* A fresh simplex around the best coupled physical point can continue
     along the shallow sigma/defocus compensation valley after a basin-search
     simplex has contracted.  This preserves the same pixel objective and is
     cheap on the prepared physical-transfer path.  */
  if (best.success && (fparams.flags & coupled) == coupled
      && rparam.sharpen.scanner_mtf.simulate_diffraction_p ())
    {
      render_parameters restart = rparam;
      if (freeze_focus_from_fit (&restart, fparams.flags, best))
        consider_joint_fit (restart, param, img, locations, starts, fixed,
                            progress, &best, best_seed_kind,
                            best_seed_kind ? *best_seed_kind : -1);
    }

  if (best.success && (fparams.flags & finetune_position))
"""
if text.count(old) != 1:
    raise SystemExit(f"expected one final refinement gate, found {text.count(old)}")
p.write_text(text.replace(old, new, 1))
