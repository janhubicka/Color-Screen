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

Coarse and dense reductions now fail normally when no successful fits, or no
fits surviving fit-score filtering, remain.  They no longer finalize or
query an empty histogram.

### FT-004 — fixed-screen cache hits were never marked current

**Severity:** medium performance

**Status:** fixed

The fixed-screen fast path copied a cached periodic screen but did not update
`last_screen_revision`, causing the same copy/cache lookup on every objective
evaluation.  The revision is now updated after a successful hit.

### FT-005 — adaptive dense focus ignored the prepass estimate

**Severity:** high performance and convergence

**Status:** fixed

The prepass correctly computed a robust global focus, but new MTF solvers
initialized focus at zero and legacy channel-blur solvers initialized at 0.3.
MTF sigma and focus now start from the active render MTF.  Dense legacy fits set
`finetune_use_screen_blur`, and both scalar and per-channel legacy blur honour
the prepass value.  This reduces expensive screen rebuilds without changing the
objective.

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

### FT-046 — adaptive front ends do not expose the measured-IR path

**Severity:** high focus-model selection

**Status:** open

`finetune()` supports measured grayscale/IR through `finetune_bw`, and RGB+IR
is usually the best-constrained focus input.  However, both
`analyze-scanner-blur` and `AdaptiveSharpeningWorker` hard-code adaptive flags
without `finetune_bw`; the CLI has no equivalent of the finetune command's
`--use-monochrome-channel`.  Consequently an RGB+IR scan silently uses
normalized RGB.  Add an explicit CLI/GUI choice and evaluate whether measured
IR should become the default when available, with an override for weak or
misregistered IR channels.

### FT-047 — weak-contrast adaptive fits can remain formally valid

**Severity:** high focus identifiability

**Status:** open

When positional colour contrast is too small, fit-score scaling returns a very
large finite score.  It is ranked behind ordinary fits, but if every cell is
weak the adaptive histograms can still accept those fits and reduce arbitrary
focus values.  Add an explicit minimum-contrast/identifiability threshold to
the adaptive worker, and distinguish low contrast from numerical or allocation
failure in its diagnostics.

### FT-048 — misregistered-area line fitting uses points outside the area

**Severity:** medium geometry robustness

**Status:** open

The conservative flood fill promises to trust only existing solver points in
the requested area.  It now rejects a request with no local anchor and uses
overflow-safe local bounds, but its line-width heuristic still calls
`solver->fit_line()` on the complete global point set.  An unrelated region can
therefore change the local expansion step.  Add a filtered line-fit helper or
construct the heuristic from only the points inside the area.

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
keeps transient optimizer entries out of the normal renderer cache.  It does
not yet share the immutable ideal screen or its channel FFTs, and active
references can temporarily make the nominal capacity a soft bound.  See
FT-052.  Revision 0017 additionally maps the dense scalar physical-defocus pass
onto shared quadratic nodes and interpolates neighboring exact node screens;
see FT-053.  Other MTF fits still use arbitrary exact simplex coordinates.

### FT-035 — no instrumentation separates optimizer and screen-filter cost

**Severity:** high performance engineering

**Status:** fixed; broader benchmarking remains useful

Revision 0016 adds opt-in `finetune_parameters::collect_profile`, returns a
snapshot in `finetune_result::profile`, and exposes aggregate reporting through
`analyze-scanner-blur --profile`.  It records:

- simplex runs, iterations, evaluations, and total objective calls;
- `init_screen()` calls, local reuse, exact builds, and fixed/focus cache
  hits/misses;
- MTF and PSF preparation, direct/wrapped transfer construction, and
  forward/inverse FFT counts;
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

**Status:** performance design

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

### FT-052 — exact focus-cache misses rebuild invariant source state

**Severity:** high performance

**Status:** open

The exact final-screen cache avoids a complete rebuild only on a bit-identical
key hit.  On every miss, its generator still reconstructs the ideal periodic
screen and forward-transforms all source channels before multiplying them by
the new capture transfer.  For the common adaptive case, screen family and
strip widths are usually shared across many cells, so this work is invariant.

Split periodic filtering so an immutable source screen and its channel FFTs can
be cached separately from focus-dependent signed transfer coefficients and the
inverse transform.  The source key must include screen type, relevant strip
widths, and any emulsion state that has already been folded into the source.
Keep emulsion-dependent optimization on a private path until that ownership
contract is explicit.  This remains an exact complementary optimization: the
0017 node table reduces the number of misses, while source-state sharing would
make every remaining node and exact-final miss cheaper.

### FT-053 — arbitrary simplex focus nodes have a low exact-cache hit rate

**Severity:** high performance

**Status:** partially fixed for dense scalar physical displacement

The simplex generates unconstrained floating-point focus values.  Warm-started
neighbouring fits explore similar intervals, but usually not the same bit
patterns; initial 0016 profiles observed only a 7--8% exact-focus hit rate.
An exact final-screen LRU therefore removed some duplicate work but left most
filter builds intact.

Revision 0017 implements the first bounded one-dimensional table for the
second, dense adaptive pass when scalar physical defocus is the sole varying
screen-filter parameter.  The coarse prepass remains exact.  The useful range
ends at the first displacement where physical system MTF at the process-screen
frequency reaches 5% by default.  `N` exact nodes use
`d_i=d_max(i/(N-1))^2`, concentrating samples near best focus.  Intermediate
requests blend neighboring exact filtered screens from the existing linked-list
LRU; they never interpolate nonnegative MTF magnitude.  The selected optimum,
outlier handling, and result production are reevaluated with an exact screen,
and arbitrary final values are not inserted into the node cache.

The default 33-node fixture test reduced exact builds from 273 to 139 and wall
time from 5.14 s to 1.92 s without changing the saved correction table.  The
same test showed visible table changes with only 9 or 17 nodes, while 25 or
more matched the exact saved result on this fixture; 33 therefore leaves a
useful safety margin without filling all 64 cache entries.

Remaining work is error-controlled subdivision around poorly interpolated
intervals, wider real-data validation, and any future per-channel focus design.
Per-channel focus should use separate one-dimensional state rather than a dense
three-dimensional RGB table.

### Initial measurement and continuing validation

Revision 0017 supplies a reproducible first speed and equality check, but its
numbers are not a portable universal claim.  They depend on the screen family,
MTF model, metadata, FFT implementation, thread count, and where the useful
range crosses the direct/wrapped-PSF transition in FT-037.

The initial reproducible smoke command, run from `testsuite`, is:

```sh
OMP_NUM_THREADS=5 $BUILD/src/colorscreen/colorscreen analyze-scanner-blur \
  dufaycolor_nikon_coolsan9000ED_4000DPI_raw.tif \
  physical-focus-input.par \
  --strip-width=1 --strip-height=1 --width=1 --height=1 \
  --xsamples=2 --ysamples=1 --profile --interpolate-focus \
  --focus-min-mtf=5 --focus-cache-nodes=33 \
  --out=/tmp/focus-profile.par
```

`physical-focus-input.par` must contain a complete physical diffraction model;
the testsuite regression constructs one from the Dufay fixture.  The benchmark
is intentionally small and establishes accounting, output agreement, and
bottleneck location.  Parallel scheduling can change exact hit/miss counts
slightly between runs.  It is not a substitute for distributions from full
RGB+IR adaptive scans.

Representative datasets should record wall time, profile totals, fit counts,
cache hit rate, and median/upper-percentile work per local fit before
broadening the approximation or treating one node count as a global default.

## Proposed exact cache and focus table

The safe implementation order is deliberately conservative.

### Phase A — retain and measure warm starts

**Status:** complete in 0016

Global prepass warm starts are retained for MTF and legacy blur.  Opt-in
profiling now measures simplex work, exact screen construction, transfer/PSF
preparation, FFTs, cache behaviour, and major objective stages.  Broader
benchmark collection remains useful, but instrumentation is no longer a blocker
for exact optimization work.

### Phase B — share immutable source state

**Status:** open; complementary exact optimization

Split screen preparation into:

1. an immutable source periodic screen after strip-width and historical
   emulsion operations;
2. its channel FFTs;
3. focus-dependent signed transfer coefficients;
4. inverse-transformed filtered periodic channels.

A bounded shared cache can safely reuse stages 1 and 2 across local solvers.
Its key must include every state that changes the source or units:

- screen type and strip widths;
- emulsion blur, offset, and per-tile intensities when active;
- any emulsion-derived source state already folded into the periodic
  samples.

Capture scale, MTF metadata, wavelength, sigma, halo, sensor aperture, and
sharpening mode belong to the subsequent transfer-state key rather than the
source-FFT key.  Neither cache may be keyed only on scalar focus.

### Phase C — exact focus-node cache

**Status:** complete for final screens in 0016; discretized reuse added in 0017

Exact filtered periodic screens at focus values actually evaluated by the
simplex are now stored in a bounded, thread-safe cache.  Failed MTF/PSF
constructions are not published, and emulsion-dependent fits are excluded.
The cache currently stores complete RGB screens and therefore still duplicates
source FFT work on misses.  Revision 0017 makes dense scalar physical-focus
requests converge on shared quadratic node keys, while other exact fits retain
arbitrary simplex values.

Per-channel focus should ultimately use separable one-dimensional transfer
state rather than a dense three-dimensional RGB focus table.

### Phase D — error-controlled interpolation

**Status:** fixed-grid scalar implementation complete; adaptive error control open

Revision 0017 implements the deliberately narrow first option requested for
the displacement-only GUI pass: linearly blend already filtered exact periodic
screens at quadratic scalar-defocus nodes.  The exact coarse pass determines
the useful range from the first 5% process-screen-frequency MTF crossing, and
the final selected point is rebuilt exactly.

For broader or more aggressive approximation, two plausible linear objects are:

- signed frequency-domain transfer coefficients multiplied by the cached
  source FFT; or
- already filtered periodic channel samples.

Do not interpolate only nonnegative MTF magnitude: physical defocus has signed
OTF phase reversals and zero crossings.  Interpolation must preserve the DC
coefficient exactly and use adaptive subdivision where focus response is not
sufficiently linear.

The current fixed-grid implementation performs the interpolation directly.
An error-controlled extension should, for selected intervals:

1. form the interpolated candidate;
2. occasionally compute the exact node according to a validation schedule;
3. compare periodic samples and the local objective;
4. subdivide or fall back to exact filtering when the error budget is exceeded.

The implemented table range is driven by the physical screen-frequency MTF,
not the full legal 0--20 mm interval, and its spacing is quadratic rather than
uniform.  An adaptive extension should still detect intervals crossing the
direct/wrapped PSF transition from FT-037; the safest long-term design is to
interpolate one common signed frequency-domain representation instead of two
different final-screen paths.

### Acceptance criteria

An approximate focus table should not be merged merely because images look
similar.  On synthetic and real RGB+IR scans, validate:

- maximum and RMS periodic-screen sample error;
- objective error at identical geometry/colour variables;
- fitted scalar/per-channel focus displacement;
- fitted screen-phase and geometry displacement;
- final adaptive-table displacement and rejected-cell count;
- behaviour around signed-OTF zero crossings;
- deterministic results under parallel scheduling;
- bounded memory and predictable eviction.

Set thresholds in physical output units.  A reasonable initial goal is that
approximation changes fitted screen phase by far less than the geometry
acceptance threshold and changes focus by far less than the robust spread of
repeat measurements; the exact numerical limits need to be established from
current real-data reproducibility tests.

## Regression coverage added in this review

Revision 0015 added:

- incompatible finetune flag families and the zero-dimensional simplex path;
- scalar dark conversion for simulated IR, including singular/non-finite
  mixing weights;
- robust area-fit score retention, excluding failed and non-finite results;
- correction-table allocation failure preserving existing state, malformed
  transactional load, save/load round-trip, cache-id refresh, and zero-filled
  reallocation.

Revision 0016 adds:

- exact cached-versus-direct periodic-screen equality;
- distinct per-channel capture MTFs while digital sharpening mode is `none`;
- exact focus-cache miss/hit accounting and reuse across construction-only
  parallel settings;
- true LRU rather than MRU eviction at a capacity boundary.

Revision 0017 adds:

- quadratic focus-grid interval construction, endpoint stability, clamping,
  and invalid-input rejection;
- first-crossing useful-range detection at a configurable physical MTF
  threshold, including the unusable in-focus case;
- midpoint exact-versus-interpolated periodic-screen error checks;
- an end-to-end displacement regression which verifies that interpolation is
  exercised, reduces exact screen builds, and preserves the exact correction
  table within 0.00001 mm.

## Test work still required

1. Add synthetic rectangular-tile geometry tests that recover known phase,
   scale, and rotation.
2. Add BW tests for measured IR, RGB-derived grayscale, flat black input, and
   zero surviving samples.
3. Extend the current flag-validation regression to exercise every legal blur
   family and the adaptive worker's scalar-output restrictions.
4. Add adaptive-worker tests whose coarse and dense widths intentionally
   differ, so the FT-001 stride regression cannot return.
5. Add synthetic spatial-focus fields and compare recovered correction tables
   with known truth.
6. Collect full-scan RGB+IR profile distributions, including screen and
   objective error around direct/wrapped-PSF transitions, before broadening the
   approximation beyond the physical displacement-only GUI path.
7. Run the existing Dufay RGB finetune tests, all `libcolorscreen` unit tests,
   and the real MTF/edge reproducibility tests on Linux, macOS, and Windows.
