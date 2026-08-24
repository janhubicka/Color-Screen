# Focus analysis from solid-colour areas

Focus analysis uses several locally uniform scene regions to constrain one
capture blur/focus model.  The physical factorization is deliberately narrow:
the scanner RGB responses to the additive-screen primaries are shared by the
whole image, while each solid scene region has only three scalar image-layer
transmissions that dim those primaries before the common capture transfer.
Giving every region independent primary chromaticities would recreate the
blur-versus-saturation ambiguity this workflow is intended to remove.

## Candidate discovery

`finetune_find_focus_area_candidates_in_image()` is the convenience entry point
used by the Qt workflow.  It constructs a bounded-resolution interpolated RGB
analysis image directly from the scan, disables sharpening and output colour
adjustments, and asks the existing flat-window detector for candidates.  A
zero window size currently chooses a window spanning eight screen periods.  The
returned rectangles and centres are in scan/image coordinates suitable for
`finetune()` and for an `ImageWidget` overlay.

The lower-level `finetune_find_focus_area_candidates()` remains useful when a
caller already owns a suitable linear RGB analysis image.  Each window is
fitted by shallow independent RGB planes.  Gentle fading or illumination drift
is therefore allowed, while texture, edges, and strong gradients are rejected.
Non-maximum suppression limits overlapping candidates before expensive solver
work.

Candidate discovery is intentionally only a cheap first pass.  Every promising
centre is subsequently verified by an ordinary one-tile `finetune()` call.
`finetune_select_focus_areas()` rejects failed/weak fits, compares objective
quality relative to the observed mean-colour magnitude, and greedily chooses a
D-optimal colour-diverse subset.  It does not rank by the historical
contrast-scaled `uncertainty`, because excessive fitted screen contrast is
itself one way an over-blurred model can compensate for the wrong focus.

## Joint and validation fits

`finetune_analyze_focus_areas()` first fits the selected centres jointly.  For
RGB scans of differently coloured uniform regions this normally uses
`finetune_uniform_image_layer`: focus/blur and screen-primary scanner responses
are shared while each tile keeps its local phase and three image-layer
transmissions.

With `leave_one_out` enabled, the selected set is then refitted once per omitted
area.  Entry `i` of `leave_one_out_fits` is the N-1 fit with `selected[i]`
omitted.  For scalar focus models the result reports the span of these N-1
estimates and their maximum displacement from the all-area joint estimate.
These diagnostics measure how strongly any one region controls the fitted
focus.

With `held_out` enabled, RGB uniform-image-layer analysis additionally evaluates
the omitted tile against the corresponding N-1 model.  The N-1 focus transfer
and scanner responses to the red, green, and blue screen primaries are frozen;
only the omitted tile's local phase and three image-layer transmission factors
may move.  The per-tile `held_out_relative_badness` is the raw held-out
objective divided by the observed mean RGB norm, and
`held_out_max_relative_badness` is its maximum over the selected set.  Unlike a
leave-one-out refit, this can expose a faded, damaged, textured, or otherwise
model-incompatible region which the shared model cannot predict.

The primary amplitudes learned by a joint fit retain the normalization gauge of
the training tiles.  Consequently held-out transmission coefficients are
relative nuisance factors and are not constrained to be at most one; fixing the
primary responses removes the optimization gauge but does not turn their scale
into an absolute optical transmission calibration.

Held-out evaluation is currently defined for the scalar RGB uniform-image-layer
models.  Per-channel focus, fog, emulsion-blur, and sharpening fits return the
joint/leave-one-out information without inventing a scalar held-out reduction.
No universal numerical acceptance threshold is imposed: physical defocus,
empirical blur diameter, residual sigma, and legacy screen blur have different
units, and useful residual thresholds still need calibration on real scans.

## Qt workflow

The Sharpness panel exposes **Find focus analysis areas** and **Analyze
sharpness in areas**.  Candidate rectangles, independent-fit state, the selected
subset, and held-out residual labels are transient state owned by the current
`MainWindow`; they are never stored globally and follow whichever ordinary view
is currently presenting that document's inspector.  Changing crop or screen
geometry invalidates the candidates.

Area discovery and fitting run off the GUI thread with cancellable progress.
After analysis the GUI reports the selected count, leave-one-out stability, and
maximum held-out relative residual.  The fitted focus is deliberately not
applied automatically.  The user must explicitly choose **Apply focus**, which
uses the normal undoable parameter path.

The synthetic regression constructs three Paget regions with different uniform
image-layer colours and one known common blur.  It checks recovery of the shared
blur, leave-one-out stability, held-out evaluation, candidate-order invariance,
and rejection of geometry-discovery flags that are incompatible with explicit
area locations.
