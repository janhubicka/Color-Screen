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
3. `step2()` first rejects numerically invalid or insufficient-contrast
   prepass fits, then rejects the least reliable remaining fits by fit score,
   computes robust global strip widths and focus, updates the worker's private
   rendering parameters, derives the useful scalar analytical blur/focus range when dense
   interpolation is enabled, and allocates the dense pass;
4. `analyze_blur()` fits each dense sub-sample and exposes it to the GUI only
   when its fitted screen modulation is identifiable;
5. `step3()` applies the same identifiability guard before fit-score filtering
   and robustly reduces every group of sub-samples to one correction-table
   entry.

The worker is single-use.  It stores one scalar correction model: legacy blur
radius, physical MTF defocus, or compact MTF blur diameter.  Scanner MTF sigma
may be fitted jointly with defocus, but sigma is not the spatial correction
stored in the table.

The Qt **Adaptive sharpening analysis** dialog exposes this worker configuration
as one-run operational settings rather than persistent rendering parameters.
The fitting tab selects the correction family and auxiliary local fit flags,
including measured monochrome/IR.  The sampling tab controls the coarse
prepass, final correction table, per-cell sub-sampling and robust reduction.
The focus-cache tab controls scalar analytical blur/focus interpolation and profiling.
Automatic dimensions are passed as zero and resolved by `step1()`, so the GUI
does not duplicate the worker's aspect-ratio rules.  The worker reports its
resolved coarse and dense grid sizes back to the chart before emitting samples.

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

The adaptive Qt dialog exposes `finetune_bw` explicitly as **Use monochrome /
IR channel**.  The `analyze-scanner-blur` CLI still lacks the corresponding
choice and neither front end prefers measured IR automatically; that remaining
work is tracked as FT-046.

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

### Identifiability and fitted contrast

A converged simplex is not sufficient evidence that local focus was measured.
A flat, saturated, damaged, or otherwise weakly modulated patch can have a
finite optimum even though the additive-screen phase and blur are effectively
unconstrained.  `finetune_result::contrast` records the positional colour
contrast derived from the fitted screen primaries and is used as the local
identifiability signal.

The adaptive worker accepts a fit only when:

- the solver completed successfully;
- fitted contrast is finite and nonnegative;
- fitted contrast is at least `min_contrast` (1/1024 by default);
- the historical contrast-scaled fit score is finite, nonnegative, and below
  its failure sentinel.

This classification happens before either coarse or dense fit-score histograms
are built.  Consequently, an area containing only weak fits fails explicitly
instead of promoting its least bad arbitrary focus value.  Invalid correction
values and invalid Dufay strip widths remain separate post-classification
failures.

The internal default threshold is 1/1024, or approximately 0.09765625% of
normalized image range.  The Qt dialog exposes it as **Minimum fitted
contrast** in percent.  The command-line equivalent is
`analyze-scanner-blur --min-contrast=PERCENT`; zero disables only the positive
contrast floor, while numerical validity checks remain active.  Profiling
reports usable, low-contrast, invalid-contrast, invalid-fit-score, and solver-
failure counts separately for the coarse and dense passes, together with the
finite fitted-contrast range and mean.  Individual `finetune_result` objects
retain their raw contrast.

### Per-cell repeatability diagnostics

A final correction-table value is reduced from the dense sub-samples belonging
to one cell.  The table now retains, for every cell:

- the robust retained correction range (`high - low`) after the configured
  correction-tail rejection, in the same units as the stored correction;
- the number of identifiable, fit-score-accepted samples with valid correction
  values, and the total number attempted for that cell;
- the mean fitted contrast of those accepted samples.

These are deliberately separate diagnostics rather than a single synthetic
confidence score.  A spread has physical units and is directly interpretable as
repeatability; sample support depends on the configured sub-sampling; fitted
contrast depends on local image content.  Combining them requires
mode- and data-specific calibration that is not yet available.

The adaptive Qt chart exposes correction, robust spread, accepted-sample
fraction, and mean contrast as selectable maps and reports the exact values in
a cell tooltip.  The command-line worker summarizes the ranges when profiling
and can write one CSV row per cell with
`--out-diagnostics=NAME.csv`; each row names the correction mode so its units
remain interpretable without the corresponding CSP file.  Diagnostics are intentionally not embedded in
the CSP correction section, so older Color-Screen versions can still read saved
correction tables.  Consequently they describe the current analysis result and
are not restored when a CSP file is reopened.

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

## Focus caches, discretization, and invalidation

The solver uses six exact reuse levels and one deliberately narrow
approximation for the dense scalar analytical blur/focus pass.

1. A fixed-screen fast path calls `render_to_scr::get_screen()` when no screen,
   blur, MTF, strip, or emulsion variable changes.  The returned immutable
   periodic screen is shared directly, and its tile revision is marked current
   after both cache hits and misses.
2. Every `finetune_solver` remembers its last strip widths, emulsion state,
   legacy blur, MTF sigma, and per-channel focus.  It rebuilds the periodic
   screen only when one of those values changes; a pure phase update only
   resamples.
3. MTF sigma/focus fits with an emulsion-independent source use a dedicated,
   thread-safe finetune cache of exact final periodic screens.  Its nominal
   capacity is 64 entries, about 24 MiB with the current 128x128 RGB screen
   representation.  The key contains the screen family, relevant strip widths,
   whether sharpening is anticipated, and the complete per-channel capture and
   sharpening parameters.  Construction-only state such as the OpenMP choice
   is not part of the key.
4. Within one dense scalar-focus solver, already acquired grid nodes are kept
   as weak references indexed by node number.  Repeated simplex evaluations
   therefore avoid traversing and locking the global linked-list LRU.  Weak
   ownership preserves the global cache's eviction and memory bound.
5. Fixed-geometry scalar physical-defocus and empirical-fallback fits
   additionally use a small thread-safe cache of immutable source spectra.  Its
   key contains only the process-screen family and relevant strip widths.  The
   first exact scalar state constructs the ideal periodic screen and forward-
   transforms its three channels; subsequent exact nodes and exact-final
   evaluations reuse those spectra.
6. Physical defocus additionally uses a second small thread-safe cache for the
   analytical transfer with defocus removed from its key.  Fixed sensor,
   diffraction, residual-sigma, halo, pupil-overlap, and Fourier-grid terms are
   prepared once.  A new focus state evaluates only the signed
   defocus-dependent pupil integral.  The empirical fallback has no comparable
   expensive fixed state: it directly evaluates its compact analytical radial
   transfer table on each exact node.

The finetune cache is deliberately separate from the ordinary rendering cache,
so transient simplex vertices cannot evict display/render entries.  Entries
still referenced by active solvers cannot be evicted, so the nominal capacity
is a soft bound under heavy concurrent use.  A failed MTF/PSF construction is
never published.  Emulsion blur, offset, and intensity fits continue to use
private copy-on-write screens because their source screen varies by tile and
objective state.

The exact reuse levels are numerically exact.  The cache comparison explicitly
includes per-channel capture MTFs even when digital sharpening mode is `none`;
the capture transfer remains active in that mode.  This also prevents distinct
channel MTFs from being mistaken for one common filter.

Source-spectrum sharing is intentionally narrower than the final-screen
cache.  It is enabled when one scalar capture coordinate is being fitted,
strip widths and emulsion state are fixed, and Richardson--Lucy sharpening is
not active.  This now covers both analytical scalar models used by adaptive
blur detection: physical defocus and the metadata-free empirical fallback blur
diameter.  The experimental Dufay strip-width prepass stays on the ordinary
exact path because every width trial changes the ideal screen.

For the prepared analytical paths, radial coordinates of the fixed 128x128
Fourier grid are computed once rather than through repeated `hypot()` calls.
Physical defocus reuses a defocus-independent signed-OTF state.  The empirical
fallback builds the same 512-sample radial `system_otf()` table used by the
general MTF implementation, but applies it directly to the periodic Fourier
coefficients instead of estimating PSF support and reconstructing a spatial
PSF.  In both cases convolution of the periodic source is performed as direct
Fourier-series multiplication, followed only by the required inverse channel
FFTs.  The ordinary support-dependent direct/wrapped-PSF implementation remains
in use for variable strip widths, measured MTF curves, residual-sigma and
per-channel fits, emulsion-dependent screens, and the other unsupported cases.

### Dense scalar analytical focus/blur interpolation

An exact cache alone has limited value for adaptive fitting because a simplex
normally generates different floating-point parameter values in every local
fit.  For the second, dense stage of scanner-blur analysis, the worker can
therefore restrict either supported scalar analytical coordinate to a shared
nonlinear node table and interpolate between neighboring exact cached screens.
The coarse prepass remains exact.

The supported coordinates are:

- physical defocus, stored in millimetres; and
- metadata-free empirical fallback blur diameter, stored in pixels.

Measured MTF curves are deliberately excluded because their transfer and useful
range are supplied by external data rather than one analytical scalar family.
Per-channel focus, residual sigma, strip-width fitting and emulsion-dependent
sources are also excluded.

The worker calculates the process-screen frequency using the same expression as
the GUI MTF widget:

```text
screen_frequency = scr_names[screen_type].frequency * representative_pixel_size
```

Starting from the sharpest end of the active scalar model, it finds the first
parameter value where the complete system MTF magnitude at that frequency
reaches a configurable threshold, 5% by default.  For physical defocus this
uses the first positive signed-OTF crossing and deliberately ignores later
phase-reversed lobes.  For the empirical fallback it uses the first crossing of
the analytical Gaussian/circular-blur transfer.  Analysis fails normally when
the zero-blur response is already below the threshold or when the exact coarse
estimate lies beyond the useful interval.

For useful upper bound `v_max` and `N` nodes, the table uses

```text
v_i = v_max * (i / (N - 1))^2,  i = 0,...,N-1.
```

Quadratic spacing is intentionally dense near the sharp end, where a small
change affects the screen most strongly, and progressively coarser towards the
low-MTF boundary.  The default is 49 nodes.  Physical-defocus fixtures had
already stabilized with fewer nodes, but the compact fallback model showed a
visible correction change at 33 nodes on the Dufay regression while 49 and 64
matched the exact saved values; 49 therefore becomes the common conservative
default while remaining below the 64-entry exact-screen LRU capacity.

Each supported fixed-source node is built by its exact direct periodic-transfer
path and stored in the existing LRU.  A requested intermediate scalar value
linearly blends the `mult` samples of the two neighboring exact periodic
screens; presentation-only `add` data is unchanged by optical filtering and is
copied exactly.  The 128x128x3 blend is performed as one flat contiguous SIMD
loop.  Solver-local weak references avoid repeated global LRU traversal while
leaving eviction ownership with the bounded shared cache.

The interpolation object is the already filtered periodic screen, not an MTF
magnitude.  Physical nodes therefore retain signed-OTF phase reversals before
the blend is performed.  The empirical fallback is nonnegative by model
construction but uses the same screen-space interpolation machinery.

Only the exploratory simplex objective is approximated.  At the selected
optimum the solver constructs the periodic screen again with the exact active
analytical transfer, recomputes the objective and fitted colours, and keeps
that exact state for outlier selection and result production.  Arbitrary final
optima are not inserted into the node LRU, because they would evict the shared
grid one value at a time.

The Qt adaptive worker enables interpolation automatically for supported scalar
analytical models and disables it for measured MTF data or incompatible fit
flags.  The CLI keeps it opt-in through `--interpolate-focus`;
`--focus-min-mtf` and `--focus-cache-nodes` select the range threshold and node
count.

For both scalar models, an exact node miss now reuses immutable source spectra.
Physical defocus also reuses its defocus-independent physical-transfer state;
the fallback instead evaluates its inexpensive analytical 512-sample transfer
table directly and never enters the generic MTF/PSF-support path.  Variable
strip or emulsion-dependent fits still rebuild their source state.  Future
error-controlled subdivision should validate interpolation error in output
units rather than simply lowering the common node count.  See FT-034, FT-037,
FT-052, FT-053, FT-054, and FT-057.

## Profiling

Profiling is opt-in so normal geometry and focus fitting does not pay clock or
atomic-counter overhead.  Set `finetune_parameters::collect_profile` and read
`finetune_result::profile` to obtain:

- simplex runs, iterations, evaluations, and total objective calls;
- screen initialization, local-state reuse, final-screen and source-spectrum
  cache hits/misses, solver-local node-reference hits/misses, and exact builds;
- interpolated screen constructions, exact node uses, and exact final builds;
- general MTF/PSF preparation, physical-transfer-state hits/misses, empirical
  fallback transfer-table builds, direct-transfer and wrapped-PSF construction,
  and FFT counts;
- accumulated time in the objective, screen cache/filter and interpolation
  paths, periodic-screen sampling, colour estimation, and residual calculation.

`analyze-scanner-blur --profile` enables profiling for all coarse and dense
fits and prints their aggregate.  It also prints the adaptive minimum contrast
and separate coarse/dense identifiability counts and fitted-contrast ranges.
Times are steady-clock nanoseconds summed
across worker threads.  They are intentionally overlapping diagnostic totals,
not components that add to wall time: in particular a cache miss includes the
exact filter construction, and the objective includes all of its sub-stages.
Cache hits include a caller that waited for another thread to publish the same
exact entry.  Low-level MTF/FFT counters cover builds owned by finetune's
variable-screen paths; a miss in the ordinary renderer cache contributes cache
time and an exact-build count but does not currently expose its internal FFT
breakdown.

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
