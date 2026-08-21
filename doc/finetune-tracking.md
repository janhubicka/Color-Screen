# Finetune and adaptive-focus issue register

## Scope and status

This register covers `finetune.C`, `analyze-scanner-blur.C`, the adaptive
correction table, and the screen-generation work invoked by local focus fits.
Issue identifiers are intended to remain stable.

Status values are:

- **fixed**: corrected in the reviewed source;
- **partially fixed**: a safe subset is implemented, with explicit remaining
  work;
- **open**: a demonstrated defect or missing guard;
- **model limitation**: current behaviour is deliberate or historical, but its
  assumptions restrict accuracy;
- **performance design**: optimization work that needs measurement and an
  explicit error budget before implementation.

## Fixed correctness and robustness issues

### FT-001 — dense adaptive results used the coarse-grid stride

**Severity:** critical correctness

**Status:** fixed

The dense worker stored results with width `xsteps * xsubsteps`, but one access
path indexed them with `strip_xsteps`.  Depending on dimensions this selected a
different fit or crossed a row boundary.  Dense access now goes through
`mainpass_result()`, which owns and checks the correct stride.

### FT-002 — rejected and failed fits returned during histogram population

**Severity:** high correctness

**Status:** fixed

The first histogram pass filtered on success/fit score, while later passes
could account a larger set.  This made the robust range and robust average
refer to different populations.  Coarse and dense reductions now use one
`accepted_result_p()` predicate in every pass and also reject non-finite fitted
corrections.

### FT-003 — empty successful-fit sets reached histogram operations

**Severity:** high robustness

**Status:** fixed

Coarse and dense reductions now fail normally when no identifiable fits, or
no fits surviving fit-score and correction-value filtering, remain.  They no
longer finalize or query an empty histogram.

### FT-004 — fixed-screen cache hits were never marked current

**Severity:** medium performance

**Status:** fixed

The fixed-screen fast path copied a cached periodic screen but did not update
`last_screen_revision`, causing the same copy/cache lookup on every objective
evaluation.  The revision is now updated after a successful hit.

### FT-005 — adaptive dense focus ignored the prepass estimate

**Severity:** high performance and convergence

**Status:** fixed

The prepass correctly computed a robust global focus, but physical MTF solvers
initialized focus at zero and legacy channel-blur solvers initialized at 0.3.
Physical defocus and measured-MTF residual blur now start from the active render
MTF.  Dense legacy fits set `finetune_use_screen_blur`, and both scalar and
per-channel legacy blur honour the prepass value.

The metadata-free empirical fallback is an intentional exception.  Real-data
validation later showed that its blur/color objective is multimodal: a nonzero
warm start can select a several-pixel blur/color-compensation basin instead of
the historical low-blur basin.  Revision 0032 therefore restores zero-blur
initialization specifically for empirical fallback while retaining warm starts
for physical defocus.

### FT-006 — rectangular tiles used transposed centre indexing

**Severity:** high geometry correctness

**Status:** fixed

The centre of the row-major position table used width as both stride and row
coordinate.  Wild coordinate discovery also exchanged width and height.  Both
paths now use X=width, Y=height and the correct row-major centre.

### FT-007 — RGB contrast was read from the BW colour cache

**Severity:** medium result quality

**Status:** fixed

Final fit-score scaling could use `last_color` even after an RGB objective
had updated `last_red`, `last_green`, and `last_blue`.  A common
`compute_contrast()` helper now selects the cache matching the data mode.

### FT-008 — BW normalization was repeated after every tile

**Severity:** high multi-tile correctness

**Status:** fixed

The accumulated BW residual was divided by `maxgray` inside the tile loop,
repeatedly rescaling contributions from earlier tiles.  Normalization now
happens once after all tiles and rejects a non-positive/non-finite normalizer.

### FT-009 — nonlinear BW colour estimation fell through to least squares

**Severity:** high correctness

**Status:** fixed

`bw_get_color()` assigned the nonlinear colour and then entered a second
independent conditional whose `else` invoked least squares.  The second branch
is now `else if`, making the three colour-estimation modes exclusive.

### FT-010 — outlier refinement returned the pre-refinement fit score

**Severity:** high adaptive rejection

**Status:** fixed

The second simplex changed both the optimum and objective, but the old
contrast-scaled fit score was retained.  The refined objective is now
contrast-scaled and stored, so adaptive filtering ranks the solution actually
returned.

### FT-011 — three adaptive CLI options wrote the wrong variables

**Severity:** high configuration correctness

**Status:** fixed

`--skip-max` overwrote `skipmin`; `--strip-width` and `--strip-height` overwrote
the dense-grid dimensions.  They now update `skipmax`, `strip_xsteps`, and
`strip_ysteps` respectively.

### FT-012 — NaN simplex coordinates survived range constraints

**Severity:** high numerical robustness

**Status:** fixed

`std::clamp()` leaves NaNs unchanged.  Comparison-based clipping now sends NaN
and negative infinity to the lower bound and positive infinity to the upper
bound before they reach cache keys or the simulator.

### FT-013 — correction-table allocation/load destroyed valid state on failure

**Severity:** medium robustness

**Status:** fixed

Allocation validates dimensions and overflow, allocates before freeing the old
table, and updates the cache identity only after success.  Loading parses into
a temporary object and swaps only after the complete record and end marker
have been read.

### FT-014 — broad coordinate discovery raced on its failure flag

**Severity:** medium concurrency

**Status:** fixed

The OpenMP workers now report allocation failure through `std::atomic<bool>`.

### FT-015 — invalid public inputs and non-finite final fits were accepted

**Severity:** medium robustness

**Status:** fixed

`finetune()` now checks the outlier fraction, range, previous-result count,
local pixel size, multitile search size, and sharpening-border size.  Non-finite
initial/refined fit scores and non-finite final objectives fail normally.

### FT-016 — simulated-IR dark result copied the unrelated fog value

**Severity:** high result correctness

**Status:** fixed

The solver fits a scalar dark value after RGB mixing, while rendering stores an
RGB dark subtracted before mixing.  Simulated-IR results now return a neutral
RGB value `scalar / sum(weights)`, which reproduces the fitted scalar whenever
the weight sum is nonzero.  Emulsion-intensity fitting can derive new mixing
weights but does not fit a dark term, so it now preserves the input RGB dark
instead of replacing it with fog or an inactive zero value.

### FT-017 — scalar focus populated both mutually exclusive result fields

**Severity:** high API correctness

**Status:** fixed

Physical MTF fitting updates `scanner_mtf_defocus`; compact metadata-free
fitting updates `scanner_mtf_blur_diameter`.  The inactive field preserves the
corresponding input render value.  This prevents unit confusion while allowing
existing callers that copy both fields back to retain the inactive model's
configuration.

### FT-018 — incompatible blur variables could be silently ignored

**Severity:** high identifiability

**Status:** fixed

Legacy screen-blur and scanner-MTF variables cannot be active together because
the forward path selects only one.  Scalar and per-channel versions of the same
focus coordinate are likewise alternatives.  `finetune()` rejects these
combinations, and the adaptive worker additionally requires exactly one stored
correction family.

### FT-019 — diagnostic dot-spread path used an uninitialized source screen

**Severity:** medium diagnostics

**Status:** fixed

An early `finetune_produce_images` block called `apply_blur()` with an
uninitialized source and assigned the result under an unrelated output
condition.  It has been removed.  The later dot-spread path starts from
`screen::initialize_dot()` and remains the single owner of that diagnostic.

### FT-020 — zero-dimensional simplex divided by zero

**Severity:** high robustness

**Status:** fixed

Some legal fixed-model configurations eliminate every nonlinear variable while
still requiring one objective evaluation to profile the linear colours and
produce results.  The generic simplex used to divide by the dimension while
constructing its initial vertices.  It now evaluates a zero-dimensional
objective exactly once and returns that value.  A regression covers this path.

### FT-050 — per-channel capture MTFs collapsed when sharpening was disabled

**Severity:** high focus/model correctness

**Status:** fixed

`screen::initialize_with_sharpen_parameters()` used
`sharpen_parameters::operator==` to decide whether adjacent channels could
reuse one transfer.  That comparison intentionally returns true for digital
sharpening mode `none`, but the capture MTF is still applied when sharpening is
not anticipated.  Distinct red, green, and blue capture MTFs could therefore
all be filtered with the first channel's transfer.

The periodic-screen filter now compares scanner-MTF scale and model explicitly,
independently of digital sharpening mode, before reusing a channel transfer.
The exact finetune cache uses the same rule in its key.  A regression constructs
three distinct empirical channel MTFs in mode `none` and checks both channel
separation and exact cached-versus-direct output.

### FT-051 — generic LRU eviction selected the most recently used entry

**Severity:** medium performance and cache behaviour

**Status:** fixed

The free-entry selection comparison in `lru-cache.h` was reversed: once a cache
was full it selected the entry with the greatest timestamp, effectively
implementing MRU eviction.  The comparison now selects the smallest timestamp.
A capacity-two regression touches the first entry before inserting a third and
verifies that the untouched second entry is evicted.

### FT-038 — simplex shrink retains stale objective values

**Severity:** high optimizer correctness and compatibility

**Status:** open; intentionally not changed in this review

After a failed contraction, Nelder--Mead moves every non-best vertex halfway
towards the best vertex but reevaluates only two named vertices.  In dimensions
above two this associates some moved coordinates with stale objective values
and can mis-rank later steps.

The obvious generic correction was tested during this review, but it also
changes every other caller of `nmsimplex.h`.  In particular, screen-colour
detection selected a different local solution and the DT/Capture One stitching
fixture moved from its established `-16,-10` overlap to `-15,-10`, with a
larger overlap residual and smaller accepted overlap.  Shipping that global
behavioural change as part of a finetune review would therefore be unsafe.
A dedicated optimizer change should add caller-specific regressions, compare
screen-colour and geometry quality, and either improve initialization or gate
the corrected shrink semantics explicitly.

### FT-039 — area fit quantiles included failed/non-finite results

**Severity:** high geometry robustness

**Status:** fixed

The regular-grid geometry helper sorted the complete result array, including
failed entries carrying default scores, before selecting its acceptance
quantile.  It also destroyed the spatial order of the array and accepted an
unchecked retention ratio.  Both area helpers now compute the cutoff from
successful finite scores only, preserve result order, validate their public
parameters, and document `uncertainty_ratio` as the fraction retained rather
than discarded.

### FT-040 — objective failure sentinel was converted to a valid score

**Severity:** high failure propagation

**Status:** fixed

Screen/MTF construction reports an objective failure with the largest finite
`coord_t`.  Contrast scaling previously capped that value to an ordinary large
fit score, allowing a failed candidate to participate in best-solver selection
and outlier refinement before the final objective check rejected it.  The
sentinel is now detected before reading colour caches and propagated unchanged
as an invalid fit score.

### FT-041 — coordinate-discovery failure could continue with invalid state

**Severity:** medium robustness

**Status:** fixed

A failed tile copy in one broad coordinate-discovery worker set the shared
failure flag but then continued into solver initialization and optimization.
The worker now stops immediately.  Failure is reported only after the
process-global GSL error handler has been restored, so an allocation failure
cannot silently change error handling for later library calls.

### FT-042 — mixed measured-IR and RGB-derived BW tiles shared one mode bit

**Severity:** high multi-tile model correctness

**Status:** fixed

The solver has one BW colour/constraint model, but the multi-tile setup used a
single Boolean that became true when *any* tile lacked IR.  A fit could
therefore combine measured IR in one tile with RGB-derived grayscale in another
while applying simulated-IR bounds and result conversion to every tile.  Mixed
BW source modes are now rejected explicitly.  Homogeneous all-measured or
all-RGB-derived multi-tile fits remain supported.

### FT-049 — correction tables are implicitly shallow-copyable

**Severity:** high ownership robustness

**Status:** open

`scanner_blur_correction_parameters` owns a heap-allocated correction array but
inherits the compiler-generated copy constructor and assignment operator.  An
accidental value copy therefore aliases the allocation and can end in
use-after-free or double-free.  Delete value copying (or implement an explicit
deep copy/move contract) after checking external API compatibility.  Existing
in-tree users already pass the table through smart pointers, and transactional
loading can continue to transfer state through the private swap helper.

## Open correctness and numerical issues

### FT-021 — RGB residual weighting assumes one Bayer pattern and phase

**Severity:** high model correctness

**Status:** open

The objective doubles one channel according to hard-coded pixel parity.  That
is not valid for every raw mosaic phase and is inappropriate for demosaiced RGB
or non-Bayer sensors.  The sampling/mosaic descriptor must come from
`image_data` or rendering metadata; otherwise all three RGB residuals should be
weighted uniformly.

### FT-022 — inner L2 colour solve is not exact for the outer L1 objective

**Severity:** high numerical/model consistency

**Status:** open

Screen colours are often eliminated by linear least squares, but the outer
objective is mean absolute error and additionally clamps some fitted colours.
The resulting objective is useful but is not exact variable projection.  The
choices are to use an L2 outer residual, solve a robust/L1 inner problem, or
make the approximation explicit and validate its bias on geometry and focus.

### FT-023 — simulated-IR mixing can become singular

**Severity:** high numerical robustness, experimental mode

**Status:** open

The least-squares formulation divides by the blue mixing weight, while the
simplex currently has no positivity or conditioning constraint on the three
weights.  Matrix inversion used by derived weights can also be singular or
have zero sum.  A revised parameterization should guarantee positive normalized
weights (for example, logits/softmax), detect a singular colour matrix, and
set a physically justified range for the scalar dark term.

### FT-024 — GSL allocation and initializer failures are not propagated

**Severity:** high robustness

**Status:** open

The current review rejects non-positive/non-finite BW normalization, zero
surviving samples, and invalid final objectives.  However, GSL vector, matrix,
and workspace allocation returns are still used without a complete failure
path, and `finetune_solver::init()` remains `void`.  Convert initialization to a
status-returning operation, validate every allocation/initializer, and
propagate failure through all solver-construction paths.

### FT-025 — tile-bound calculations mix inclusive and exclusive maxima

**Severity:** medium robustness

**Status:** open

Tile extrema are clipped to `image.width`/`image.height`, then converted to
`+1` dimensions, while placement reserves an additional pixel and rounds to an
even origin.  Current tests exercise common cases, but the boundary convention
is not self-evident.  Convert the code to one explicit half-open convention and
add corner/one-pixel-margin tests before changing behaviour.

### FT-026 — coordinate offset ranges and transformation composition need review

**Severity:** medium geometry correctness

**Status:** open

The encoder and decoder for offsets use mode-dependent ranges that are not
obviously inverse in every coordinate-search mode.  Translation is composed
with scale/rotation in an order that can rotate the fitted offset.  Establish
the intended coordinate frame, add rectangular synthetic tests, then make
`get_offset()`/`set_offset()` exact inverses.

### FT-027 — solver construction relies on implicit initialization invariants

**Severity:** medium maintainability

**Status:** open

`finetune_solver` has an empty constructor and many primitive members that are
initialized only by a particular `init_tile()`/field-assignment/`init()` order.
The current review gives every optional parameter index an explicit `-1`
sentinel, but the wider construction invariant remains implicit.  Add in-class
defaults, remove dead fields such as `fixed_scanner_mtf`, and replace the
multi-step construction with a status-returning initializer or constructor.

### FT-028 — nested progress and parallelism contract is underspecified

**Severity:** medium concurrency

**Status:** open

CLI code, OpenMP, Qt thread pools, and inner screen filtering can all be active.
Cancellation is polled, but progress callbacks may be invoked from several
workers and nested parallelism can oversubscribe the machine.  Define whether
`progress_info` is thread-safe and centralize the inner/outer parallelism
policy.

### FT-029 — per-channel optical ordering and wavelength are incomplete

**Severity:** medium physical-model limitation

**Status:** open

Per-channel defocus currently applies the same wavelength unless disabled code
is revived, and the source comment notes that spectral scanner response should
be applied before channel-specific defocus.  Implement only after the spectral
ownership/order is specified in the screen-simulation model and validated
against RGB+IR data.

### FT-030 — stitched results are returned in tile coordinates

**Severity:** medium geometry correctness

**Status:** open

Input locations can be mapped into a stitched source tile, but result geometry
and `tile_pos` are not translated back to the common/final coordinate system.
Return-coordinate semantics must be documented and converted before multi-tile
stitched fitting is exposed as authoritative geometry.

### FT-031 — one representative pixel scale is used for a warped fit

**Severity:** model limitation

**Status:** open, lower priority

Strong perspective or mesh distortion changes local magnification.  The solver
currently applies one `pixel_size` to the entire periodic capture transfer.
This is the finetune counterpart of SIM-009.  A spatially varying transfer is
probably necessary only for larger tiles or strongly warped scans.

### FT-032 — RGB normalization can bias focus

**Severity:** model limitation

**Status:** open

Per-pixel RGB normalization assumes the neutral image layer can be divided out
after capture blur.  In reality, the image layer and screen are multiplied
before the capture transfer, so blur and division do not commute.  Quantify the
focus/phase bias on synthetic data and prefer measured IR when available.

### FT-033 — multi-tile result fields describe only the first tile

**Severity:** medium API limitation

**Status:** open

Per-tile offsets and emulsion states can participate in one fit, but
`finetune_result` exposes a single offset, tile centre, and solver point.  Either
return a vector of tile results or restrict/document the API as returning the
first tile plus shared parameters.

### FT-043 — per-call GSL error-handler changes race across concurrent fits

**Severity:** high concurrency and process-state correctness

**Status:** open

`gsl_set_error_handler_off()` changes process-global state.  Two concurrent
`finetune()` calls can observe and restore one another's handler: one fit may
unexpectedly run with the aborting handler, and the last restore can leave GSL
handling disabled after both calls finish.  A mutex around the complete fit
would be correct but would serialize adaptive cells.  Prefer one documented
application/library-wide policy, or isolate all fallible GSL operations behind
a serialized wrapper that reports status without changing the handler around
whole optimizations.

### FT-044 — the generic simplex does not handle allocation failure

**Severity:** high robustness

**Status:** open

`nmsimplex.h` allocates the vertex matrix and work vectors with unchecked
`malloc()` calls.  Low-memory failure therefore becomes a null dereference, and
the function cannot distinguish cancellation, invalid input, and allocation
failure in its return value.  Replace the raw arrays with RAII containers and
return a status plus objective.  At the same time validate negative dimensions,
non-positive/non-finite scale and epsilon, and iteration bounds.

### FT-045 — outside contraction can accept a point worse than reflection

**Severity:** medium optimizer correctness

**Status:** open

For an outside Nelder--Mead contraction, the implementation accepts the
contracted point whenever it improves on the old worst vertex.  Standard
Nelder--Mead accepts it only when it also improves on the reflected point;
otherwise the simplex is shrunk.  The current condition can discard a better
reflection and take a worse contraction.  Add a deterministic outside-
contraction regression before changing this historical optimizer behaviour.

### FT-046 — complete measured-IR selection in adaptive front ends

**Severity:** high focus-model selection

**Status:** partial

`finetune()` supports measured grayscale/IR through `finetune_bw`, and RGB+IR
is usually the best-constrained focus input.  The adaptive Qt dialog now
exposes this explicitly as **Use monochrome / IR channel**, so GUI runs no
longer have to use normalized RGB.  The setting is forwarded unchanged to
both the exact prepass and dense pass.

The `analyze-scanner-blur` CLI still lacks an equivalent of the finetune
command's `--use-monochrome-channel`, and neither front end automatically
chooses measured IR when it is available.  Add the CLI switch, then evaluate
whether measured IR should become the default with an override for weak or
misregistered IR channels.

### FT-047 — weak-contrast adaptive fits can remain formally valid

**Severity:** high focus identifiability

**Status:** fixed

When positional colour contrast was too small, fit-score scaling returned a
very large finite score.  It ranked behind ordinary fits, but an all-weak
prepass or dense cell could still accept its least bad finite results and
reduce essentially arbitrary focus values.

The adaptive worker now classifies every completed fit before constructing
fit-score histograms.  A usable result must have solver success, finite
nonnegative fitted contrast at or above a configurable threshold, and a finite
nonnegative historical fit score.  Low contrast, invalid contrast, invalid fit
score, and solver failure are counted separately.  If no identifiable fit
remains, the worker reports those categories and fails normally instead of
querying a histogram or returning a focus correction.

The internal default threshold is 1/1024, approximately 0.09765625% of
normalized image range, matching the established geometry-detection floor.
The Qt dialog and `--min-contrast=PERCENT` CLI option present the threshold in
percent; zero disables the positive floor but retains numerical validity
checks.  Profile output reports coarse
and dense category counts plus the finite contrast range and mean.  The raw
per-fit contrast remains in `finetune_result` for diagnostics and possible
confidence-map presentation.

### FT-048 — misregistered-area line fitting uses points outside the area

**Severity:** medium geometry robustness

**Status:** open

The conservative flood fill promises to trust only existing solver points in
the requested area.  It now rejects a request with no local anchor and uses
overflow-safe local bounds, but its line-width heuristic still calls
`solver->fit_line()` on the complete global point set.  An unrelated region can
therefore change the local expansion step.  Add a filtered line-fit helper or
construct the heuristic from only the points inside the area.

### FT-056 — adaptive correction cells lacked reproducibility diagnostics

**Severity:** high focus-map reliability

**Status:** fixed

A successful local simplex and an absolute contrast cutoff do not show how
well repeated samples agree inside one final correction cell.  The reduction
already computed robust low/high bounds but discarded them after applying the
tolerance check.

Each returned correction cell now retains its robust correction spread,
accepted/total sample support, and mean accepted fitted contrast.  Profile
output summarizes their ranges across the completed table.  The Qt chart can
display correction, robust spread, accepted sample fraction, or mean contrast
and reports exact cell values in its tooltip.  The CLI can export the same raw
diagnostics through `--out-diagnostics=NAME.csv`.

The implementation intentionally does not collapse these quantities into one
confidence number: spread has physical units, accepted support depends on the
configured sub-sampling, and contrast depends on source content.  Future hole
filling or confidence-based rejection should first establish mode-specific
thresholds from real RGB+IR maps.

Diagnostics are not serialized inside the CSP correction section, preserving
backward compatibility with older readers.

## Adaptive-focus performance work

### FT-034 — exact blurred screens are not shared between local fits

**Severity:** high performance

**Status:** partially fixed

Changing sigma or focus calls
`screen::initialize_with_sharpen_parameters()`, which prepares the capture
transfer and filters all periodic channels.  One solver still caches its last
exact state, and revision 0016 additionally introduces a dedicated thread-safe
LRU of exact final periodic screens for MTF sigma/focus fits whose source is not
modified by emulsion parameters.  It has a nominal 64-entry capacity and uses
the complete per-channel capture/sharpening state as its key.

This safely shares bit-identical simplex nodes across neighbouring fits and
keeps transient optimizer entries out of the normal renderer cache.  Active
references can temporarily make the nominal capacity a soft bound.  Revision
0017 additionally maps the dense scalar physical-defocus pass onto shared
quadratic nodes and interpolates neighboring exact node screens; see FT-053.
Revision 0022 splits the immutable source spectra from the focus-dependent
transfer for fixed-geometry scalar physical defocus, so remaining node misses
and exact-final evaluations no longer repeat the three source forward FFTs;
see FT-052.  Revision 0023 also separates the defocus-independent physical
transfer state and applies the signed OTF directly at periodic harmonics, so
the supported physical path no longer reconstructs a spatial PSF or performs a
kernel FFT for every exact node; see FT-054.  Revision 0031 extends the source-
spectrum and nonlinear-node path to the metadata-free empirical fallback blur
diameter and applies its analytical radial transfer directly; see FT-057.
Measured MTFs and other multi-parameter fits still use arbitrary exact simplex
coordinates.

### FT-035 — no instrumentation separates optimizer and screen-filter cost

**Severity:** high performance engineering

**Status:** fixed; broader benchmarking remains useful

Revision 0016 adds opt-in `finetune_parameters::collect_profile`, returns a
snapshot in `finetune_result::profile`, and exposes aggregate reporting through
`analyze-scanner-blur --profile`.  It records:

- simplex runs, iterations, evaluations, and total objective calls;
- `init_screen()` calls, local reuse, exact builds, global fixed/focus cache
  hits/misses, and solver-local focus-node hits/misses;
- general MTF and PSF preparation, physical-transfer cache/table work,
  empirical fallback transfer-table work, direct/wrapped transfer construction,
  and forward/inverse FFT counts;
- steady-clock time in the objective, filtering/cache path, sampling, colour
  estimation, and residual evaluation.

Profiling is disabled by default, so ordinary fits do not perform clock reads or
atomic increments.  Reported times are thread-summed and intentionally overlap:
the objective contains every sub-stage, and a cache miss includes its exact
filter construction.

Revision 0016's original five-thread smoke runs of the Dufay Coolscan fixture
used 605 objective calls and produced 515--520 exact builds, 41--46 exact-focus
hits, and a 7.3--8.2% hit rate.  The filter path dominated accumulated
objective time, confirming both the bottleneck and the limited value of exact
floating-point reuse alone.

With a physical 4000-ppi capture model, revision 0017 compared the exact and
discretized dense paths on the same fixture.  The exact run took 5.14 s and
constructed 273 exact screens.  The default 33-node quadratic table took
1.92 s, constructed 139 exact screens, performed 209 interpolations, and
produced a byte-identical saved correction table.  Thread-summed interpolation
time was 10.1 ms; filtering remained the dominant cost.  Counts can vary
slightly with parallel scheduling.  Larger RGB+IR datasets should still be
profiled and reported with medians and upper percentiles before treating this
small fixture as a universal speedup estimate.

Revision 0022 additionally reports source-spectrum cache hits and misses.  On
the same one-thread Dufay fixture with experimental coarse strip-width fitting
enabled, the exact run reused one source spectrum for 80 of 81 eligible focus
builds.  Total screen forward transforms fell from the 555 that would have
matched all inverse transforms to 315; the remaining 312 transforms belong to
104 variable-strip prepass builds plus three transforms for the one fixed
source.  The interpolated run reused 23 of 24 eligible source lookups and used
315 forward versus 384 inverse transforms.

With strip widths held fixed to isolate physical defocus, the exact run reused
the source on 112 of 113 builds and performed only three source forward FFTs
versus 339 inverse FFTs.  The 33-node interpolated run reused it on 55 of 56
builds and performed three source forward versus 168 inverse FFTs.  The two
saved correction parameter files were byte-identical to revision 0021 and to
each other.  Five alternating warm runs gave median wall times of 2.15 s in
0021 and 2.14 s in 0022 for exact focus, and 1.11 s versus 1.08 s for the
interpolated path.  Median accumulated filtering time changed from 2103.9 to
2084.8 ms and from 1053.8 to 1023.9 ms respectively.  Thus source reuse is a
small exact improvement on this fixture, not another interpolation-sized
speedup: PSF construction and inverse FFTs now dominate.  These times are
diagnostic rather than a claim about full-scan scaling.

Revision 0023 removes that repeated PSF construction from the supported
fixed-source scalar physical-defocus path.  A one-thread fixed-strip run used
the same analytical transfer table as the general MTF model but sampled it
directly at the periodic Fourier harmonics.  Three alternating measurements
reduced the exact-path median from 2.36 s in 0022 to 0.10 s and the 33-node
interpolated median from 1.14 s to 0.09 s.  The exact run reported 112 direct
periodic transfers, one miss plus 111 hits in the defocus-independent physical
state cache, zero general MTF/PSF preparations, zero wrapped PSFs, and zero
kernel FFTs.  The fixed-strip saved correction files remained byte-identical
to 0022 for both exact and interpolated runs.

With the experimental Dufay strip-width prepass enabled, revision 0023 keeps
the 104 variable-source coarse builds on the ordinary wrapped-PSF path and
uses the direct physical path only after the robust widths become fixed.  The
same local benchmark changed from approximately 4.94 s to 2.52 s.  This mixed
case is intentionally not evidence for strip-width approximation: the dense
periodic convolution now evaluates the analytical signed OTF directly rather
than reproducing the older sampled-and-wrapped spatial-PSF approximation, so
full-scan validation remains appropriate even though the fixed-strip
displacement regression is unchanged.

### FT-036 — adaptive output is scalar but per-channel fits are accepted

**Severity:** medium API/model correctness

**Status:** open

`scanner_blur_correction_parameters` stores one scalar correction per spatial
cell.  The worker nevertheless accepts per-channel legacy blur or per-channel
MTF focus flags.  `finetune_result` then supplies a scalar mean as well as the
three channel values, and the adaptive table keeps only the mean.  This is easy
to mistake for a per-channel adaptive correction.  Either reject per-channel
flags in this worker, document an intentional mean-focus mode explicitly, or
extend the correction-table and renderer formats to preserve three channels.

### FT-037 — exact filtering changes numerical regime inside the focus range

**Severity:** high approximation correctness

**Status:** fixed for prepared scalar physical defocus and analytical fallback;
open for the remaining ordinary filtering paths

`screen::initialize_with_sharpen_parameters()` does not use one numerically
uniform implementation over the whole focus range.  Depending on the estimated
PSF support it uses either a direct sampled transfer grid or a wrapped spatial
PSF followed by FFT filtering.  A uniformly spaced table can therefore straddle
an implementation transition even when the underlying optical model varies
smoothly.  Interpolation must either use a common frequency-domain
representation, split intervals at this transition, or detect the local error
and fall back to exact filtering.

Revision 0017's first-stage implementation interpolates the final exact
periodic-screen samples on a quadratic grid and always rebuilds the selected
optimum exactly.  This avoids interpolation of MTF magnitude and passed the
current midpoint and end-to-end regressions, but it does not yet adaptively
subdivide or detect every direct/wrapped transition.  Keep this issue open for
broader models, screen frequencies, and error-controlled refinement.

Revision 0023 removes the implementation transition from the supported
fixed-source scalar physical-defocus path.  A periodic source is convolved
exactly by multiplying its Fourier-series coefficients with the signed OTF at
the corresponding harmonics, so this path no longer estimates PSF support or
switches to a sampled-and-wrapped spatial kernel.  Revision 0031 applies the same periodic-source observation to the metadata-free
empirical fallback: it constructs the established 512-sample analytical
`system_otf()` table and multiplies it directly with the cached source spectra,
so blur-diameter nodes no longer enter the direct/wrapped-PSF decision either.
Variable-strip sources, measured MTFs, residual-sigma/per-channel fits, and the
other ordinary filtering paths retain the direct/wrapped choice and therefore
keep this issue relevant.

### FT-052 — exact focus-cache misses rebuild invariant source state

**Severity:** high performance

**Status:** fixed for fixed-geometry scalar physical defocus and empirical
fallback blur; broader variable-source sharing remains open

The exact final-screen cache avoids a complete rebuild only on a bit-identical
key hit.  On every miss, its generator still reconstructs the ideal periodic
screen and forward-transforms all source channels before multiplying them by
the new capture transfer.  For the common adaptive case, screen family and
strip widths are usually shared across many cells, so this work is invariant.

Revision 0022 splits periodic filtering into immutable source spectra and a
focus-dependent signed transfer/inverse-transform stage.  A separate bounded,
thread-safe LRU is keyed by screen type and the relevant fixed strip widths.
The first supported focus state constructs the ideal screen and forward-
transforms its three channels; later exact nodes and exact-final evaluations
reuse that state.  Regression coverage requires the second exact focus state
to perform no source forward FFTs.  Revision 0023 validates the prepared path
against the analytical signed transfer itself rather than requiring equality
with the ordinary sampled-PSF numerical implementation.

Revision 0031 extends the same immutable source-spectrum cache to the
metadata-free scalar fallback blur diameter.  Both scalar models change only
the capture transfer while strip widths and emulsion state remain fixed.  Dufay
strip-width fitting continues on the ordinary exact path because its simplex
changes the source boundaries, and emulsion blur/intensity/offset fitting
remains private until source ownership is explicit.  Per-channel focus,
residual sigma, measured MTF and legacy blur remain outside this prepared scalar
path.

### FT-054 — physical focus nodes rebuild invariant transfer and spatial PSF

**Severity:** high performance

**Status:** fixed for fixed-source scalar physical defocus

After revision 0022, every exact focus-node miss still constructed a complete
`mtf` table, estimated its PSF support, reconstructed a sampled spatial PSF,
wrapped that kernel into the 128x128 period, and forward-transformed it.  Most
of the analytical system transfer is independent of defocus, and the spatial
PSF round trip is unnecessary for a periodic source.

Revision 0023 introduces a bounded thread-safe cache whose key contains the
physical capture model with defocus normalized to zero.  It prepares the fixed
sensor aperture, diffraction, residual Gaussian, halo, pupil-overlap
denominators, defocus phase scales, and periodic Fourier-bin radii once.  A new
focus value evaluates only the signed defocus-dependent pupil numerators and
constructs the same 512-sample radial transfer table used by
`mtf::precompute()`.

The prepared-source screen path samples that table directly at the periodic
harmonics, multiplies it by the cached source spectra, and performs the three
inverse FFTs.  This is the exact Fourier-series convolution for the analytical
periodic model and preserves negative OTF lobes.  It avoids PSF support
estimation, PSF reconstruction and wrapping, and the kernel forward FFT.

The defocus-independent fixed-state cache remains specific to physical
displacement.  The Dufay strip-width prepass changes the source screen and
continues through the ordinary filter, as do per-channel defocus, residual
sigma, measured MTFs, legacy blur, emulsion variables, and Richardson--Lucy
sharpening.  Revision 0031 handles empirical compact blur separately: it has no
expensive fixed pupil state to cache, but it now reuses the source spectra and
applies its analytical radial transfer directly, avoiding the same PSF round
trip; see FT-057.

### FT-053 — arbitrary simplex focus nodes have a low exact-cache hit rate

**Severity:** high performance

**Status:** fixed for dense scalar physical displacement; empirical fallback
kept exact after real-data validation

The simplex generates unconstrained floating-point focus values.  Warm-started
neighboring physical fits explore similar intervals but usually not the same bit
patterns; initial 0016 profiles observed only a 7--8% exact-focus hit rate.  An
exact final-screen LRU therefore removed some duplicate work but left most
physical filter builds intact.

Revision 0017 implements a bounded one-dimensional table for the second, dense
adaptive pass when scalar physical defocus is the sole varying screen-filter
parameter.  The useful range ends at the first displacement where physical
system MTF at the process-screen frequency reaches 5% by default.  Exact nodes
use `d_i=d_max(i/(N-1))^2`, concentrating samples near best focus.  Intermediate
requests blend neighboring exact filtered screens from the existing linked-list
LRU; the selected optimum, outlier handling, and result production are
reevaluated exactly.

Revision 0031 temporarily extended the same approximation to metadata-free
empirical fallback blur.  A large corner-scan stress fixture showed that this is
not robust: the fallback objective has competing blur/color basins, and small
screen-interpolation changes can decide which basin Nelder--Mead reaches.  On a
25x17 correction grid with 5x5 samples, exact versus 49-node interpolation
moved 23 cells by more than 0.2 pixel and the largest change exceeded one pixel.
Changing the node count was also non-monotonic on the smaller regression
fixture.  Revision 0032 therefore restricts interpolation to physical defocus.

The empirical fallback retains the source-spectrum and direct analytical
transfer acceleration from revision 0031, so exact evaluation is practical:
the same 25x17, 5x5 external stress run completed in about 19.6 seconds on the
validation host with no generic MTF/PSF preparations.  See FT-057 and FT-058.

Remaining physical-interpolation work is error-controlled subdivision around
poorly interpolated intervals and any future per-channel focus design.

### FT-055 — focus-node lookup and screen interpolation dominate dense fits

**Severity:** high performance

**Status:** fixed in revision 0027 for the scalar physical-focus table

After the direct physical-transfer work removed PSF construction, a full
fixed-strip profile exposed two new costs.  The linked-list LRU was searched
for both endpoints of nearly every interpolated simplex state, and each state
materialized a 128x128x3 screen through a three-level loop whose innermost
extent was only three.  On the profiled scan this accounted for 53.4 seconds
of cache lookup/wait and 151.3 seconds of interpolation in thread-summed time.

Revision 0027 keeps weak, solver-local references to exact grid nodes indexed
by their integer node number.  The global LRU remains authoritative and
bounded, but repeated evaluations in one simplex no longer traverse or lock it
when the node is still alive.  The materialized interpolation remains
mathematically unchanged, but its multiplicative array is treated as one
contiguous vector and blended with an OpenMP SIMD loop.  This is preferable to
lazy per-pixel blending: a prototype removed the full-screen write but doubled
the endpoint sampling work and was slower on small and single-threaded fits.

A five-thread fixed-strip regression was also run with the same grid shape as
the supplied full profile: 425 coarse and 10,625 dense fits, 11,050 in total.
Two alternating measurements produced byte-identical correction tables.  The
median wall time changed from 20.02 to 16.28 seconds; median thread-summed
objective time from 71.13 to 53.34 seconds; interpolation from 17.26 to 6.27
seconds; and global cache lookup/wait from 13.59 to 6.58 seconds.  The optimized
runs served 502,163 repeated node requests from solver-local references and
reduced global focus-cache accesses from roughly 672,000 to 170,000.  These
numbers are diagnostic single-host measurements, not portable universal
ratios; the full RGB+IR scan remains the relevant field benchmark.

The two exact-final builds normally reported for each dense cell are not
duplicates in the current algorithm: the first exact screen defines outlier
selection, and a second is required after the refined optimum changes.  They
should be combined only with a deliberate redesign of outlier refinement, not
removed as a cache optimization.

### FT-057 — empirical fallback blur bypassed scalar-focus acceleration

**Severity:** high performance

**Status:** fixed in revision 0031 for exact evaluation; interpolation withdrawn
in revision 0032

The metadata-free fallback model uses one scalar compact blur diameter, but it
originally missed the scalar-focus acceleration available to physical defocus.
Arbitrary fallback simplex values therefore had poor exact-screen-cache reuse,
repeated source forward FFTs, and entered the generic MTF/PSF-support machinery
on each miss.

Revision 0031 reuses immutable source spectra and applies the fallback's
analytical 512-sample Gaussian/circular `system_otf()` table directly at the
periodic harmonics.  Exact fallback states no longer estimate PSF support,
reconstruct a spatial PSF, or perform a kernel FFT.  This exact direct path is
retained.

Revision 0031 also enabled nonlinear-node interpolation for fallback blur, but
revision 0032 removes that approximation after the corner-scan stress fixture
showed basin-sensitive results.  The exact analytical path is fast enough
without it: a 25x17 grid with 5x5 dense samples and fixed strip widths completed
10,735 local fits in about 19.6 seconds on the validation host, with three
source forward FFTs total and zero generic MTF/PSF preparations.

Variable Dufay strip widths remain on the ordinary exact path by design.

### FT-058 — empirical fallback blur/color objective is multimodal

**Severity:** high fallback-model correctness

**Status:** compatibility regression fixed in revision 0032; intrinsic
ambiguity remains open

The empirical fallback simultaneously fits a compact blur diameter and local
screen-primary colours.  Excessive blur can be partly compensated by making the
fitted primaries more saturated.  The source already contained a warning about
this failure mode, but its small blur regularizer applies to legacy screen blur,
not to scanner-MTF fallback blur diameter.

Before revision 0015, fallback MTF blur happened to start at the zero-blur
boundary.  Revision 0015 generalized the adaptive coarse-focus warm start to
scanner MTF fits.  This is beneficial for physical defocus, but on fallback it
moves the initial simplex away from the boundary and makes the several-pixel
color-compensation basin much easier to reach.  On the supplied corner crop,
the same local sample could converge near 0.19 pixel from zero or near 4.7--5
pixels from a nonzero start; the high-blur solution can even have a lower raw
residual because its fitted screen colours become unrealistically contrasty.

Revision 0032 restores the historical zero-blur initialization only for the
metadata-free empirical fallback.  Physical defocus and measured-MTF residual
blur keep their warm starts.  On one 5x5 stress cell, 21 of 25 individual
fallback fits then reproduce the pre-0015 values exactly; the robust retained
low-blur result remains close despite a few historical high-blur outliers.
The regression test also verifies that changing only the stored fallback
starting diameter from 1 to 4.75 pixels no longer changes the adaptive result.

This restores historical basin selection but does not make the fallback model
mathematically identifiable.  Some stress cells still show a several-pixel
within-cell robust spread because individual fits can reach both basins.
Future work should address that explicitly, for example by constraining or
regularizing fitted screen chromaticities from reliable sharp regions, or by a
mode-aware/spatially coherent fallback reducer.  It should not rely on a lucky
simplex starting point as the final solution.


### FT-059 — uniform-colour multi-tile focus model was tied to emulsion blur

**Severity:** high focus identifiability

**Status:** partially fixed; automatic area selection remains open

The original multi-tile experiment used the physically useful factorization
needed for robust focus analysis: one set of historical screen-primary scanner
responses is shared across the image, while every locally uniform scene tile
has three scalar image-layer intensities that dim those primaries before the
common capture transfer.  Several differently coloured tiles can therefore
constrain one blur/focus value without allowing each tile to invent its own
screen chromaticities.

That machinery had become reachable only indirectly when emulsion blur was
optimized together with capture blur, which also enabled per-tile emulsion
offsets and unnecessarily enlarged the nonlinear problem.  A dedicated
`finetune_uniform_image_layer` mode now reuses the per-tile intensity model
without enabling emulsion blur or offsets.  RGB normalization and patch-colour
data collection are disabled because the uniform image layer is explicitly
modelled.  The unblurred process screen supplies the primary-membership weight
when emulsion blur is not requested; the historical emulsion-blur experiment
continues to use its blurred weight screen.

`finetune_result::tile_primary_intensities` returns the fitted per-tile
transmissions in input-location order.  The existing shared `screen_red`,
`screen_green`, and `screen_blue` fields remain the one global primary response
set.  Regression coverage checks the shared-primary scaling operation itself
and runs the production `finetune()` path on synthetic differently coloured
uniform tiles with a known common blur.

Remaining work is to discover suitable solid areas automatically, reject weak
individual fits, choose a well-conditioned colour-diverse subset, and add
leave-one-area-out/held-out validation before the Qt workflow uses the joint
fit as an authoritative focus measurement.
