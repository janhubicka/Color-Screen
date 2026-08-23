# DG-009: classified-component and flood-fill diagnostics

This note records the measurements used to investigate DG-009 in the regular
screen geometry detector.  It complements `scr-detect-geometry-tracking.md` and
deliberately does not change detection thresholds or flood-fill decisions.

## Report-only counters

When a detector report is requested, `detect_stats:` now includes aggregate
outcomes from the fast classified-component flood fill:

- `fast_patch_success`: a square/diamond component passed size, centroid and
  displacement checks;
- `fast_patch_zero`: the predicted pixel did not belong to the expected class;
- `fast_patch_too_small`: the expected-class component was smaller than the
  existing geometry-derived minimum;
- `fast_patch_too_large`: the component exceeded the existing maximum;
- `fast_patch_center_reject`: the component centroid was not inside the
  component;
- `fast_patch_distance_reject`: the centroid was farther from the prediction
  than the existing tolerance;
- `fast_strip_success`: a Dufay-like intervening strip was found; and
- `fast_strip_fail`: the strip component did not reach the existing minimum.

The counters aggregate all fast flood-fill attempts in one detector call.  They
are disabled when no report file is supplied, so normal detection does not
perform the counter updates.  `testsuite/benchmark-screen-detection.py` copies
the fields to its CSV output.

## NGS00428 Tile05

Tile05 is an interior Capture One EIP with screen across essentially the whole
image.  It is useful because explicit 0/0 detector sharpening already finds the
initial Dufay grid immediately; the remaining loss is therefore flood-fill
continuity rather than seed discovery.  The runs below use fast-only flood fill,
Dufay, fixed-lens geometry, gamma 1, colour optimisation, no mesh, and five
threads.  The telemetry patch leaves the historical coverage and patch counts
unchanged.

| outcome | legacy 2/3 | explicit 0/0 |
| --- | ---: | ---: |
| screen coverage | 92.07% | 73.98% |
| accepted patches | 3,525,563 | 1,615,660 |
| zero-size patch lookup | 174,664 | 182,402 |
| too-small patch component | 22,616 | 155,532 |
| too-large patch component | 187,659 | 3,709 |
| centroid rejected | 127,303 | 11,661 |
| displacement rejected | 58,572 | 5,155 |
| accepted strips | 1,928,164 | 844,086 |
| rejected strips | 41,208 | 145,499 |

The strongest unsharpened-specific signal is not centroid displacement or
oversized merging.  It is a roughly seven-fold increase in sub-threshold square
components together with about 3.5 times as many strip failures.  A local size
histogram from the diagnostic run also showed most rejected non-empty square
components immediately below the existing nine-pixel minimum: sizes 6, 7 and 8
accounted for approximately 24k, 37k and 54k rejections respectively.

## Rejected exploratory changes

Several deliberately local experiments were measured and rejected rather than
included in the implementation:

- accepting a same-colour component within the existing centroid tolerance;
- bridging a one-pixel `unknown` gap between same-colour pixels;
- filling isolated `unknown` class-map pixels from agreeing opposite neighbours;
- reducing the square-component minimum size; and
- globally lowering the colour-dominance ratio.

Each recovered only part of Tile05's missing coverage, and the threshold changes
would weaken false-positive protection.  A horizontal one-element Dufay skip
could cross the visible propagation barrier but still left low patch density and
made the combined slow path substantially more expensive.

The real Nikon Dufay compatibility fixture also still fails its 99% coverage
requirement when the historical 2/3 mask is globally disabled, even after the
DG-024 predicted-center rounding fix.  Thus the old mask cannot yet be removed.

## Next algorithmic experiment

The measurements point toward separating strict seed classification from
geometry-guided flood classification.  Initial-grid discovery should retain its
strict hard class map.  Once a coherent lattice is known, a future experimental
flood layer can evaluate colour confidence at predicted element locations and
recover weak/fragmented elements without globally relabelling the image or
lowering seed thresholds.  The counters in this patch provide the acceptance
measure for that work: improvements should reduce `fast_patch_too_small` and
`fast_strip_fail` on the unsharpened corpus without increasing false detections
on negative images.
