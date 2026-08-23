# Regular-screen geometry detection issue register

## Scope and goals

This register covers the regular-screen geometry pipeline implemented primarily
in `src/libcolorscreen/scr-detect-geometry.C`, together with the color-class map,
initial linear solver, and `screen_map` operations it invokes.

The long-term goals are:

1. preserve reliable detection on sharp Paget/Finlay, Dufay, Dioptichrome, and
   Omnicolore scans;
2. detect screens whose element boundaries are moderately blurred or whose
   color classification is locally incomplete;
3. reject images without a usable regular screen substantially earlier; and
4. make every robustness or performance change measurable without increasing
   false-positive detections.

The first reviews deliberately introduce no blur-tolerance heuristic.  They fix
local defects whose intended behavior follows from the existing geometry, add
measurement hooks, and remove obsolete work that does not change detector
output.

Status values are:

- **fixed**: corrected by the initial conservative review;
- **open**: a demonstrated weakness or missing validation;
- **measurement prerequisite**: instrumentation or data needed before changing
  behavior;
- **performance design**: an optimization requiring profiles and an explicit
  correctness guard;
- **model limitation**: a deliberate current assumption that restricts the
  images that can be detected.

## Fixed correctness and diagnostic issues

### DG-001 — an empty Paget patch reached centroid division

**Severity:** high correctness

**Status:** fixed

The second predicted blue Paget patch logged a failed component lookup but then
called `patch_center()` with size zero.  `patch_center()` divided by the size.
The caller now rejects the candidate immediately, and `patch_center()` itself
also rejects empty input as a defensive invariant.

### DG-002 — slow confirmation initialized X and Y in reverse order

**Severity:** high correctness

**Status:** fixed

`confirm()` initialized `bestcy` from X and `bestcx` from Y.  Non-strip searches
usually replaced both values, masking the defect, while the strip path relied on
the initial prediction directly.  The initial center now preserves X and Y.

### DG-003 — Improved Dioptichrome checked the strip on the orthogonal vector

**Severity:** high screen-type correctness

**Status:** fixed

The next square row used the process-specific 107.77-degree lattice vector, but
the intervening strip was checked at a separately hard-coded 90-degree offset.
The strip is now checked halfway along the same second lattice vector used for
the following row.  Orthogonal Dufay-like screens are unchanged.

### DG-004 — Dufay-like flood fill ignored process color permutations

**Severity:** high screen-type correctness

**Status:** fixed

Flood fill always required a red intervening strip, even though Dioptichrome B
has a green strip and Improved Dioptichrome B/Omnicolore have a blue strip.  It
now uses the same permuted strip color as initial-grid detection.  Optional
solver-point output also derives square color from X parity, matching the screen
map and neighboring-patch traversal, rather than from the row number.

### DG-005 — Improved Dioptichrome attempts suppressed Omnicolore attempts

**Severity:** medium robustness

**Status:** fixed

The two screen types shared a visited-component bitmap.  A failed Improved
Dioptichrome candidate permanently marked its starting component, so the
immediately following Omnicolore test could not inspect the same patch.  Each
screen model now has independent candidate-visited state.

### DG-006 — known-range scanning stored an absolute coordinate as a distance

**Severity:** high coverage validation

**Status:** fixed

The unknown-area check tracks horizontal distance from a nearby known point.
After finding one in the two-dimensional neighborhood it stored `-xx`, making
subsequent behavior depend on absolute screen coordinates.  It now stores the
relative distance `x - xx`.  The same failure path no longer dereferences a null
progress reporter while printing its diagnostic.

### DG-007 — failed solvers and candidate attempts were accounted incorrectly

**Severity:** high failure handling

**Status:** fixed

The final geometry solver return value was ignored, allowing a failure sentinel
to flow into quality reporting and rendering setup.  Detection now returns an
unsuccessful result when the final fit fails.  An initial linear-solver failure
also no longer counts the same candidate twice against the flood-fill attempt
limit.

### DG-008 — diagnostics misstated the observed result

**Severity:** low diagnostics

**Status:** fixed

The review corrects reversed screen/color arguments, stale adjacent-patch size
and center output, the number of confirmed Dufay-row patches, several spelling
errors, and the quality percentage for distances at least one pixel.  Failed
classified-patch diagnostics now have defined predicted center values.

## Robustness work for moderately blurred scans

### DG-009 — seed discovery depends on exact connected color classes

**Severity:** high

**Status:** open

`find_patch()` uses eight-connected equality in the thresholded color-class
map.  Blur can join neighboring elements diagonally, split one element into
several islands, or replace the expected center pixel with `unknown`.  Initial
grid discovery then fails before the slower image-domain confirmation code can
help.

A replacement must preserve the current sharp-image path as the cheap first
choice.  Candidate extensions to evaluate include accepting a small unknown
boundary, component morphology bounded by predicted screen scale, and using a
confidence-valued class map rather than changing hard labels globally.

### DG-010 — predicted patch lookup samples only one rounded pixel

**Severity:** high

**Status:** open

After the first two patches, both Dufay-like and Paget-like initial-grid code
round each predicted center to one image pixel and call `find_patch()` there.
Small accumulated vector error or a blurred/unknown center therefore rejects an
otherwise coherent grid.

Evaluate a bounded local search around each prediction.  Rank components by
center displacement, color confidence, size consistency, and agreement with the
current lattice estimate.  The search radius must be derived from screen scale
and kept below half the nearest competing-element distance.

### DG-011 — initial grids are accepted or rejected as all-or-nothing patterns

**Severity:** high

**Status:** open

The current seed requires a complete 5 by 10 Dufay-like pattern or the fixed
Paget/Finlay validation pattern.  One damaged, occluded, or weak element rejects
the candidate even when the remaining observations determine a convincing
lattice.

Introduce an explicit candidate score and robust lattice fit.  Missing elements
may be tolerated only when enough independent rows, columns, and colors remain
to distinguish the requested screen family.  The score must be compared with
negative/no-screen images before replacing the present exact-grid gate.

### DG-012 — slow confirmation has a fixed blur gap and sampling geometry

**Severity:** medium

**Status:** model limitation

`confirm()` leaves one fixed sampling gap between inner and outer samples.  This
helps mildly blurred elements but is not tied to estimated screen period or blur
width.  It may sample the transition on low-resolution scans or miss useful
contrast on large elements.

Evaluate scale-normalized inner/outer offsets and, only after measurement, a
small set of blur hypotheses.  Do not simply lower `min_patch_contrast`: that
would make weak image texture more likely to seed flood fill.

### DG-013 — no independent coarse periodicity estimate exists

**Severity:** medium

**Status:** open

All initial orientation and period estimates come from classified connected
components.  A coarse frequency/orientation estimate could guide local searches
when individual boundaries are soft.

Compare downsampled autocorrelation, channel-opponent gradients, and a restricted
Fourier peak search.  The coarse estimator should propose bounded period and
angle ranges, not declare success; color-order and flood-fill consistency remain
mandatory safeguards.

## Failure-time and performance work

### DG-014 — failure stages need structured counters and timings

**Severity:** high

**Status:** fixed baseline instrumentation

When a report file is supplied, regular-screen detection now emits two stable
summary records.  `detect_stats:` records the final result, detected type, search
regions and seed pixels examined, completed initial grids, initial-solver
failures, flood-fill attempts and failures, patch count, last flood rejection
reason, color-optimization/precompute failures, class-map builds, RGB
precomputations, and whether the legacy pre-classification unsharp mask was
active.  `detect_stats_ms:` records wall time spent in color optimization,
precomputation, class-map construction, initial solvers, flood fill, final
solver, mesh solver, and the complete detection.

Flood fill supplies stable rejection identifiers for invalid screen scale,
flipped Dufay-like geometry, mapping failures, too few patches, refined-solver
failure, unknown areas, insufficient coverage, border failures, and
cancellation.  Clock sampling is disabled when there is no report file.  CPU
time and finer per-screen-family seed counters remain optional follow-up if wall
time does not identify the bottleneck clearly.

### DG-015 — color optimization can rebuild image-wide state for every search cell

**Severity:** high

**Status:** performance design

With `optimize_colors` enabled, every one of the 6 by 6 candidate regions may
change detection colors, discard the renderer and class map, precompute the
image again, and rebuild a full Paget candidate map.  This dominates failures on
large scans.

Measure how much the optimized colors differ between regions.  Possible designs
are a small central/global calibration followed by local validation, caching
render/classification state by the resulting color parameters, or optimizing a
few ranked regions before scanning the remaining cells with the best valid
model.

### DG-016 — impossible candidates are rejected too late

**Severity:** high

**Status:** performance design

A completed initial grid invokes a solver and may launch a broad flood fill even
when early rows already show inconsistent period, angle, patch size, or color
order.  Add cheap invariant checks before the solver and a bounded flood-fill
probe before allocating/validating the full map.  Every early rejection must be
shown equivalent to a later existing rejection on the benchmark corpus.

### DG-017 — negative images still scan most classified pixels and screen models

**Severity:** medium

**Status:** performance design

Visited-component maps avoid retrying one component for one model, but a
no-screen image can still traverse large portions of every region and test all
regular-screen families.  Use DG-014 data to evaluate component prefilters,
region ordering by periodic evidence, and stopping rules based on an upper bound
on the best remaining candidate score.

### DG-018 — component scratch storage uses variable-length stack arrays

**Severity:** low

**Status:** performance design

Several functions use compiler-extension variable-length arrays for component
coordinates.  Reusable scratch storage could improve portability and avoid
repeated large stack frames, but heap allocation per candidate would make the
failure path slower.  Change this only with profiles; prefer per-thread reusable
storage with an explicit capacity.

### DG-020 — pre-classification unsharp masking is a legacy detector aid

**Severity:** high compatibility/robustness

**Status:** compatibility default retained; removal blocked by DG-019

The `scr_detect_parameters` radius/amount unsharp mask predates the current MTF
model.  An attempt in PR #35 to change its historical radius 2 / amount 3
default to zero exposed a real compatibility dependency: both existing Dufay
CLI integration tests aborted during autodetection.  The Coolscan raw fixture
exhausted all 36 search regions, while the Capture One stitched fixture failed
on a later tile after earlier tiles had been detected.

Keep radius 2 / amount 3 as the default for now.  Explicit zero values remain a
supported experimental mode and are exercised by synthetic fast-only discovery.
This is not an endorsement of unsharp masking as the long-term detector design;
it is a regression guard until DG-009 through DG-013 make the classifier robust
to imperfect boundaries without artificially sharpening them.

Before retiring the compatibility mask again, compare the historical default
and explicit zero values on the current CLI fixtures and the Batch 08 corpus in
DG-019.  Any replacement must match or improve successful detection without
increasing false positives or failure time.

### DG-021 — color optimization copied a padded image through a zero-amount mask

**Severity:** medium performance

**Status:** fixed

The area-based `optimize_screen_colors()` path allocated a padded RGB buffer and
called the generic unsharp-mask helper with a hard-coded amount of zero.  The
helper therefore copied the linearized input exactly; no sharpening occurred.
Color ranking now uses the same linearized RGB values directly, eliminating the
second image buffer and copy without changing the samples presented to the
optimizer.

### DG-022 — screen-class cache equality omitted classification thresholds

**Severity:** high correctness

**Status:** fixed

`scr_detect_parameters::operator==()` participates in the color-class-map cache
key but did not compare `min_luminosity` or `min_ratio`, even though both values
change `classify_adjusted_color()`.  Changing either threshold could therefore
reuse a class map computed with stale classification rules.  Both values are now
part of the comparison; legacy sharpening parameters remain part of the key as
before.

### DG-023 — fast-only flood fill should not precompute the slow RGB image

**Severity:** medium performance

**Status:** fixed for the explicitly unsharpened path

Fast flood fill itself uses only the color-class map, but the historical
pre-classification unsharp mask needs adjusted RGB in order to build that map.
Consequently a default radius 2 / amount 3 run legitimately performs one RGB
precomputation even in fast-only mode.  The detector statistics now count this
implicit precomputation rather than reporting a misleading zero.

When the compatibility mask is explicitly disabled and slow image-domain
confirmation is also disabled, adjusted RGB is not materialized.  Synthetic
fast-only discovery exercises this zero-mask mode and requires
`rgb_precomputes=0`.  Thus the slow-path allocation is still avoidable, while
DG-020 accurately records the cost of the compatibility mask.

## Validation corpus

### DG-019 — build a geometry-detection benchmark and regression corpus

**Severity:** high

**Status:** in progress

The report-only DG-014 records now provide a stable baseline format, and the
existing synthetic discovery tests cover sharp Finlay and Dufay detection with
both slow+fast and fast-only flood fill.  `testsuite/benchmark-screen-detection.py`
provides the manual corpus driver.  It runs external scans through `autodetect`,
uses sparse parameter files to select detector sharpening without changing the
normal compatibility default, and writes one CSV row per scan/mode together with
full reports, logs, and successful output parameter files.  The CSV includes the
DG-014 counters and timings, scan/screen coverage, process wall time, and Linux
peak resident memory when `/proc` is available.

The historical National Geographic failure corpus is available in Dropbox at
`/Batch 08 error samples`.  It contains 25 problematic NGS scans, generally as
nine original Capture One EIP tiles plus flattened outputs.  Treat the EIP tiles
as the authoritative regression inputs.  Start manual smoke testing with
`NGS00428/Tiles - EIP/dpa_ecp_NGS00428_Tile01.eip` and
`NGS00899/Tiles - EIP/dpa_ecp_NGS00899_Tile01.eip`, then expand to all tiles and
all scans.  These files are too large for routine CI, so keep the existing small
real Dufay fixtures as CI gates and use Batch 08 for manual robustness and
failure-time measurements.

For each Batch 08 input, compare the historical 2 / 3 mask, explicit 0 / 0, and
future blur-tolerant alternatives.  Record success/type, search regions, seed
pixels, initial grids, final failure stage, patch count, coverage, and stage
wall times from DG-014.

The first full-resolution baseline uses two 14204 by 10652 NGS00428 EIPs.  Tile
05 is an interior tile and should contain screen across essentially the whole
image.  Tile 01 is a corner tile; its top and right margins deliberately contain
no additive raster.  Both modes use Dufay, fixed-lens geometry, gamma 1, color
optimization, fast+slow flood fill, and no mesh.

| scan/mode | scan area | screen area | seed pixels | patches | flood ms | detector ms | wall s | peak RSS KiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Tile05, legacy 2/3 | 98.66% | 98.66% | 2 | 3,777,541 | 20,693.2 | 30,990.1 | 51.34 | 2,883,832 |
| Tile05, 0/0 | 86.66% | 86.67% | 1 | 3,317,587 | 34,835.0 | 42,676.8 | 62.33 | 2,867,576 |
| Tile01, legacy 2/3 | 56.01% | 97.27% | 1 | 2,140,419 | 10,888.9 | 21,359.9 | 40.90 | 2,842,300 |
| Tile01, 0/0 | 53.40% | 93.33% | 23,702 | 2,040,103 | 32,208.2 | 41,112.7 | 60.70 | 2,947,032 |

The corner tile shows a seed-discovery weakness very clearly: removing the mask
changes a first-pixel initial grid into a 23,702-pixel search.  This motivates
DG-009/DG-010 work, but does not isolate the single-pixel prediction rule from
connected-component quality by itself.  The interior tile is the complementary
warning: seed discovery remains immediate without sharpening, yet detected
patches drop by 12.2% and flood fill becomes 1.68 times slower.  On the corner
tile flood fill becomes 2.96 times slower.  Therefore a DG-010 seed fix alone is
not sufficient evidence that the compatibility mask can be removed; subsequent
work must also improve the class map/flood-fill completeness measured here.

The corpus should contain, for every available screen family:

- sharp scans that currently succeed, including rotations and perspective;
- the same crops with controlled optical blur and downsampling;
- low-contrast, noisy, faded, and locally damaged screen regions;
- partial screens and binding-tape/border occlusions;
- wrong declared screen type and color-permutation cases;
- ordinary photographs, smooth fields, repeated architectural textures, and
  color charts with no screen; and
- small images and edge-start candidates that exercise rejection paths.

Record success, detected type, period/vector error, solver residual, coverage,
patch count, failure reason, wall time, and peak memory.  Synthetic blur is useful
for controlled thresholds, but acceptance also requires real soft scans because
classification errors are not equivalent to Gaussian blur alone.

## Acceptance criteria for algorithmic follow-up

A robustness or speed patch should satisfy all of the following before its
heuristic becomes the default:

1. all current sharp-screen fixtures retain their detected type and geometry
   within the existing numerical tolerance;
2. no-screen and wrong-type fixtures acquire no new successful detections;
3. moderately blurred real and synthetic fixtures show a documented increase in
   success, not merely more candidate grids;
4. successful results still pass flood-fill coverage, consistency, and final
   solver checks;
5. median and worst-case rejection time are reported separately from successful
   detection time;
6. memory growth is reported for representative 150-megapixel scans; and
7. the complete unit and CLI tests pass in checking builds at both `-O2` and
   `-Ofast`.

## Recommended implementation order

1. Use DG-014 statistics to record baselines for the existing sharp fixtures,
   then extend DG-019 with real soft scans and representative negative images.
2. Add the bounded prediction-neighborhood search from DG-010 behind an
   experimental switch and calibrate it against false positives.
3. Introduce robust partial-grid scoring (DG-011), using the local search as its
   observation layer.
4. Compare a coarse periodicity proposal (DG-013) only for regions where the
   exact component seed fails.
5. Use the measured failure costs to implement early rejection and state reuse
   (DG-015 through DG-017).
6. Revisit scratch storage and lower-level micro-optimization (DG-018) only
   after the algorithmic failure cost is under control.
