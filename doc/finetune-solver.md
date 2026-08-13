# Additive-screen finetuning solver

## Purpose

`src/libcolorscreen/finetune.C` matches a small region of a scan against a
forward simulation of the historical additive process.  It is used for three
related tasks:

1. refine the phase of the periodic colour screen, and optionally local scale
   and rotation, so the centres of the red, green, and blue elements are known
   accurately;
2. estimate scanner/camera transfer parameters, especially defocus or the
   legacy periodic-screen blur;
3. repeat the focus estimate across a scan and reduce the local fits into a
   `scanner_blur_correction_parameters` table.

The solver is not a generic image-registration routine.  Its objective assumes
that the visible high-frequency pattern is the known additive screen after
historical emulsion effects, capture blur, sampling, and possibly digital
sharpening.

This document records the current implementation contract.  Open correctness
and performance work is tracked in `doc/finetune-tracking.md`.

## Public entry points

### `finetune()`

`finetune()` fits one or more rectangular scan tiles.  Its inputs are:

- `render_parameters`, including the scanner MTF and RGB-to-neutral mixing;
- a `scr_to_img_parameters` geometry;
- the source `image_data`;
- tile locations, or no locations for broad coordinate discovery;
- optional previous `finetune_result` objects used to initialize tile offsets;
- `finetune_parameters`, whose flags select fitted variables and diagnostics.

A successful call returns fitted geometry, blur/MTF values, screen colours,
contrast, a historical fit-quality score, and optional diagnostic images.  A
failed call leaves `success == false` and normally sets `err`.

### Geometry-grid helpers

`finetune_area()` samples a regular grid, keeps a configured fraction of the
most reliable successful fits, rejects low-contrast points, and adds the
surviving exact patch centres to `solver_parameters`.
`finetune_misregistered_area()` grows an existing sparse registration by a
conservative flood fill: it proposes points near known cells, rejects large
screen-space displacements, and tightens its fit-quality cutoff over successive
waves.

`finetune_area_parameters::uncertainty_ratio` is the fraction of the most
reliable successful fits retained.  Failed and non-finite fits do not
participate in the quantile.  At the endpoints, zero keeps the best score (and
ties), while one keeps every valid score.  The area entry points reject invalid
or overflowing grid sizes, ratios, contrasts, displacement limits, empty scan
crops, and null solver objects.  The conservative flood fill also requires at
least one trusted solver point in the requested area.

### `analyze_scanner_blur_worker`

Adaptive focus is deliberately staged so GUI and CLI callers can schedule the
expensive local fits themselves:

1. `step1()` validates the grid and allocates a coarse prepass;
2. `analyze_strips()` fits each coarse location;
3. `step2()` rejects the least reliable prepass fits by fit score, computes
   robust global strip widths and focus, updates the worker's private rendering
   parameters, and allocates the dense pass;
4. `analyze_blur()` fits each dense sub-sample;
5. `step3()` rejects the least reliable local fits by fit score and robustly
   reduces every group of sub-samples to one correction-table entry.

The worker is single-use.  It stores one scalar correction model: legacy blur
radius, physical MTF defocus, or compact MTF blur diameter.  Scanner MTF sigma
may be fitted jointly with defocus, but sigma is not the spatial correction
stored in the table.

## Which scan data is fitted

### Normal RGB mode

When `finetune_bw` is not selected and RGB data is available, each tile stores
linearized RGB values.  The forward model predicts the RGB response of the
three historical screen primaries.  Depending on the flags, those response
colours are estimated by patch collection, linear least squares, or the
nonlinear simplex.

RGB normalization is enabled by default.  It divides each measured pixel by
its RGB sum to suppress an approximately neutral silver-image layer.  This is
useful for registration, but it is only an approximation because the image
layer and the colour screen have both passed through the capture transfer.
`finetune_no_normalize` disables it.

### Normal grayscale/IR mode

With `finetune_bw`, the solver asks the renderer for its grayscale buffer.
When the source has a usable grayscale or infrared channel and
`ignore_infrared` is false, that buffer is the measured channel.  This is the
normal and best-constrained path for RGB+IR scans: the approximately neutral
silver image is measured directly rather than inferred from RGB.

When no usable grayscale/IR channel exists, the renderer constructs a neutral
layer from RGB using the current mixing weights and dark values.  The solver
marks this internally as simulated grayscale so result interpretation and
future validation can distinguish it from measured IR.  A multi-tile BW fit
must use the same source kind for every tile: mixing measured IR and
RGB-derived grayscale in one solver is rejected because colour constraints and
result conversion are shared across tiles.

The current adaptive-focus CLI and Qt worker do not select `finetune_bw` and
therefore use RGB even when measured IR is present.  Exposing an explicit
measured-IR choice, or safely preferring it by default, is tracked as FT-046.

### Experimental `finetune_simulate_infrared` mode

`finetune_simulate_infrared` is separate from ordinary RGB+IR fitting.  It works
from RGB tiles, estimates RGB mixing weights and a scalar post-mixing dark
term, and constrains the fitted screen colours to have equal neutral-layer
response.  The least-squares formulation divides by the blue mixing weight,
so its parameterization and conditioning need additional work; see FT-023.

## Forward model

For each nonlinear parameter vector, the solver performs the following logical
steps.

### 1. Geometry and tile extraction

Every scan pixel centre in the tile is mapped to periodic screen coordinates
and cached in `tile_data::pos`.  Position refinement adds a small screen-space
offset.  Coordinate refinement additionally constructs a scale/rotation
matrix around the tile centre.  Broad coordinate discovery searches many scale
and angle intervals and keeps the solution with the lowest fit-quality score.

The current result geometry is translated back correctly for a single image.
Stitched-project coordinates still need an explicit final-to-common transform;
that limitation is tracked as FT-030.

### 2. Ideal historical screen

`screen::initialize()` builds the ideal periodic additive pattern from the
screen type and strip widths.  Strip-width variables are enabled only for
screen families where they are meaningful.

### 3. Optional historical emulsion model

The solver can blur the ideal screen in screen-period units.  In the more
experimental multi-tile mode it can also fit a screen-space emulsion offset and
per-tile primary intensities, merge the resulting neutral layer with the
screen, and then apply the capture transfer.

### 4. Capture transfer

Exactly one capture-blur family should be active:

- the legacy Gaussian periodic-screen blur, scalar or per channel; or
- the scanner/camera MTF path, using optional residual Gaussian sigma plus
  scalar or per-channel physical defocus/compact blur diameter.

Combining legacy blur variables with scanner-MTF variables is rejected because
`apply_blur()` can apply only one family and the ignored variables would make
the optimization underdetermined.

For the MTF path, `screen::initialize_with_sharpen_parameters()` filters the
periodic screen.  It uses signed physical transfer values where available and
also accounts for the selected sharpening model when the tile already contains
that sharpening.  A failure to construct the MTF/PSF is propagated; the caller
must discard the partially initialized destination screen.

`pixel_size` converts scan-pixel quantities to the periodic screen grid.  The
current solver uses one representative pixel scale for the whole fit.

### 5. Sampling the periodic simulation

The filtered periodic screen is sampled at every cached screen coordinate and
stored as a finite `simulated_screen` tile.  A per-tile phase change can reuse
the already filtered periodic screen and resample it; a blur, MTF, strip-width,
or emulsion change increments the screen revision and rebuilds the necessary
stage.

### 6. Linear colour/image-layer estimation

Several variables are linear once geometry and blur are fixed:

- RGB responses of the ideal red, green, and blue screen primaries;
- grayscale primary intensities;
- in some modes, fog.

The solver therefore eliminates these variables inside each nonlinear
objective evaluation by patch collection or GSL linear least squares.  This
reduces the simplex dimension substantially.  The outer residual is currently
an L1 mean absolute difference, whereas the inner elimination is L2 least
squares; that is not exact variable projection and is tracked as FT-022.

### 7. Residual and blur penalty

The objective averages absolute simulated-versus-measured differences over all
non-outlier samples.  In BW mode it normalizes once by the maximum measured
intensity.  A small multiplicative penalty discourages legacy blur from growing
past the point where very contrasty fitted primary intensities can hide a
poorly identified blur.

Colour-scan residuals currently use a hard-coded Bayer-parity weighting.  That
is appropriate only for the assumed mosaic phase and is tracked as FT-021.

## Nonlinear optimizer and fit-quality score

The nonlinear variables are fitted with the in-tree Nelder--Mead simplex.  A
parameter-specific `constrain()` method clips each candidate to its supported
range.  Non-finite candidates are forced to a boundary instead of being left
as NaNs in screen-cache keys.

The first fit may be followed by an outlier pass.  Pixels with the largest
residuals are marked, least-squares storage is rebuilt for the reduced sample
set, and the simplex is run again.  The public field historically named
`uncertainty` stores the best final objective (including the blur-growth
penalty) divided by positional colour contrast.  Lower values are better.  It
is a heuristic fit-quality score used to rank and reject local fits; it is
neither a simplex-spread estimate nor a calibrated statistical confidence
interval.

Adaptive focus uses this fit-quality score in two stages:

- reject the least reliable coarse fits before calculating the global starting
  point;
- reject the least reliable dense sub-samples before forming each robust local
  correction.

The same accepted set is used for histogram range construction and histogram
population.

## Starting values

Good starting values matter because one MTF change can rebuild and FFT-filter a
complete periodic screen.

- position and emulsion offsets start at zero, or from supplied previous
  results;
- strip widths use caller values when `finetune_use_strip_widths` is set;
- legacy scalar/per-channel blur normally starts at 0.3 scan pixels, but
  `finetune_use_screen_blur` starts it from `render_parameters`;
- scanner MTF sigma and active defocus/blur diameter start from the current
  `render_parameters::sharpen.scanner_mtf` values;
- the adaptive prepass writes its robust global focus and strip widths into the
  worker's private rendering parameters, so every dense fit starts near the
  known global solution rather than at zero.

This warm start is exact: it changes only the initial simplex, not the forward
model or objective.

## Existing caches and invalidation

The solver has two useful but limited exact caches.

1. A fixed-screen fast path calls `render_to_scr::get_screen()` when no screen,
   blur, MTF, strip, or emulsion variable changes.  Its tile revision is marked
   current after a cache hit.
2. A variable solver instance remembers the last strip widths, emulsion state,
   legacy blur, MTF sigma, and per-channel focus.  It rebuilds the periodic
   screen only when one of those values changes; a pure phase update only
   resamples.

These caches are local to one `finetune_solver`.  Adaptive focus constructs a
new solver for every scan location, so exact screens and source FFTs are not
shared between neighbouring cells.  This is the principal performance issue
behind FT-034.

The current review deliberately does not interpolate between focus values.
Physical defocus has signed OTF zero crossings, and the screen filter can switch
between direct and wrapped-PSF implementations as support grows.  The proposed
order is therefore: instrument, share immutable/exact state, add a bounded exact
focus-node cache, and only then evaluate error-controlled interpolation with an
exact final objective/refinement.  See FT-034 through FT-037.

## Result-field contract

For scalar MTF focus, the field belonging to the active model is fitted while
its inactive counterpart preserves the value supplied in `render_parameters`:

- physical diffraction fitting updates `scanner_mtf_defocus` and preserves
  the input `scanner_mtf_blur_diameter`;
- compact metadata-free fitting updates `scanner_mtf_blur_diameter` and
  preserves the input `scanner_mtf_defocus`;
- per-channel fitting stores the RGB values in
  `scanner_mtf_channel_defocus_or_blur`, whose interpretation follows the
  active MTF model;
- legacy fitting stores scalar and/or RGB screen-blur radii;
- the experimental scalar post-mixing dark term is converted to an equivalent
  neutral pre-mixing RGB dark value when the mixing-weight sum is nonzero;
  modes that do not fit a dark term preserve the input RGB dark value.

A successful final objective must be finite.  Failed MTF/PSF construction,
invalid dimensions, cancellation, allocation failure reported by tile setup,
and an invalid fit-quality score all leave `success == false`.  Callers must
discard all numerical result fields on failure: some may contain intermediate
values written before a later validation failed.

## Threading

Adaptive callers commonly run many independent `finetune()` calls in parallel.
The inner screen filtering may also use OpenMP unless the no-progress flag or
caller policy disables it.  Nested parallelism and progress reporting are
therefore performance-sensitive.  The shared failure flag used by broad
coordinate discovery is atomic, but progress implementations still need to be
safe for the scheduling policy chosen by the caller.

There is also an unresolved process-global dependency: each fit temporarily
disables the GSL error handler.  Concurrent fits can race while changing and
restoring that handler; see FT-043.  Likewise, the generic simplex still uses
unchecked raw allocations; see FT-044.

## Related documentation

- `doc/finetune-tracking.md`: issue register and focus-cache/LUT plan;
- `doc/screen-simulation-pipeline.md`: ownership of screen blur, capture MTF,
  sampling, and sharpening;
- `doc/mtf-physical-model.md`: physical and compact scanner-MTF models;
- `doc/mtf-fitting-workflow.md`: slanted-edge measurement and explicit MTF
  fitting.
