# Focus analysis from solid-colour areas

This library layer sits after the solid-area search and colour-diverse subset
selection described in `finetune-solver.md`.  It deliberately does not choose
pixels itself and it does not contain Qt state.  The caller first finds cheap
flat RGB candidates, verifies every promising centre with an ordinary one-tile
`finetune()` call, and stores that local result in the candidate.  The joint
stage can then use those local registration results as starting states rather
than rediscovering screen phase from scratch.

`finetune_analyze_focus_areas()` performs three operations:

1. `finetune_select_focus_areas()` filters the individually verified candidates
   and chooses a quality-controlled, colour-diverse subset.
2. The selected centres are passed together to `finetune()`.  Focus/blur and
   screen response are therefore shared by all tiles while the model-specific
   local degrees of freedom remain local.  For RGB scans of differently
   coloured solid regions this normally means using
   `finetune_uniform_image_layer`; BW/IR matching keeps using its existing
   uniform-tile model and must not enable the RGB-only flag.
3. Unless disabled, the selected set is refitted once per omitted area.  Entry
   `i` of `leave_one_out_fits` is the fit with `selected[i]` omitted.  A single
   bad region can therefore be identified before the joint estimate is treated
   as authoritative.

For scalar focus models the result also reports the span of the leave-one-out
estimates and their maximum absolute displacement from the joint estimate.  The
active scalar is legacy screen blur, scanner residual sigma, physical MTF
defocus, or empirical compact blur diameter as appropriate.  The library does
not impose one numerical acceptance threshold because those quantities use
different physical units.  Per-channel focus and other multidimensional models
leave the scalar stability fields negative; callers can inspect the returned
fits directly.

The leave-one-out pass is a stability test, not a true held-out residual: every
leave-one-out fit is allowed to re-optimise on the remaining tiles.  A future
validation layer can evaluate an omitted tile with the shared joint parameters
frozen if that proves useful.  Keeping this distinction explicit avoids giving
the current diagnostic stronger statistical meaning than it has.

The focused synthetic regression constructs three Paget regions with different
uniform image-layer colours and one known common blur.  It checks recovery of
the shared blur, successful leave-one-out fits, bounded focus sensitivity,
candidate-order invariance, the option to disable leave-one-out work, and
rejection of geometry-discovery flags that are incompatible with explicit area
locations.
