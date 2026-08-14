#pragma once

#include "../libcolorscreen/include/finetune.h"
#include <cstdint>

/** User-selectable settings for one adaptive blur/focus analysis run.
    Defaults preserve the historical GUI behaviour: a 25-column dense grid,
    an aspect-ratio-derived height, a matching 25-column coarse prepass and
    scalar scanner-MTF defocus with local position refinement.  Zero values for
    dependent dimensions and sub-sampling request the library defaults.  */
struct AdaptiveSharpeningParameters {
  int stripXSteps = 25;
  int stripYSteps = 0;
  int xSteps = 25;
  int ySteps = 0;
  int xSubsteps = 0;
  int ySubsteps = 0;
  uint64_t flags = colorscreen::finetune_position |
                   colorscreen::finetune_scanner_mtf_defocus;
  bool optimizeStripWidthsInPrepass = true;
  bool reoptimizeStripWidths = false;
  double skipMin = 25.0;
  double skipMax = 25.0;
  double tolerance = -1.0;
  double minimumContrast = colorscreen::finetune_default_min_contrast;
  bool reportProfile = false;
  bool interpolateFocus = false;
  double focusMtfThreshold = 0.05;
  int focusInterpolationNodes = 49;
};
