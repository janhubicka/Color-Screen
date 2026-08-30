#!/usr/bin/env python3
from pathlib import Path

p = Path("src/libcolorscreen/focus-analysis.C")
text = p.read_text()
old = """              render_parameters seed = scalar[0].rparam;
              seed.sharpen.scanner_mtf.defocus = useful_limit * fraction;
              finetune_result fit
                  = finetune (seed, param, img, locations, &starts,
                              defocus_stage, progress);
"""
new = """              render_parameters seed = scalar[0].rparam;
              seed.sharpen.scanner_mtf.defocus = useful_limit * fraction;
              /* With the prepared physical-transfer path, coupled trials are
                 cheap enough to retain the raw nonzero starts before the
                 fixed-sigma stage follows the compensation valley back toward
                 small defocus.  This gives the released two-dimensional
                 simplex direct access to the interior physical basin.  */
              consider_joint_fit (seed, param, img, locations, starts, fixed,
                                  progress, &best, best_seed_kind,
                                  scalar[0].kind);
              finetune_result fit
                  = finetune (seed, param, img, locations, &starts,
                              defocus_stage, progress);
"""
if text.count(old) != 1:
    raise SystemExit(f"expected one staged seed loop, found {text.count(old)}")
p.write_text(text.replace(old, new, 1))
