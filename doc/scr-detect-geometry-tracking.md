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

The first review deliberately changes no detection thresholds and introduces no
new heuristic.  It fixes only local defects whose intended behavior follows
from the existing geometry and records larger algorithmic work here.

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

### DG-014 — failure stages have no structured counters or timings

**Severity:** high

**Status:** measurement prerequisite

Reports currently contain prose but no stable failure reason or stage timing.
Add optional counters for:

- color optimization and render precomputation;
- full-image classification;
- starting components inspected per screen type;
- initial grids completed;
- linear-solver rejections;
- flood-fill patches visited and rejection reason;
- coverage, unknown-area, border, and final-solver rejection; and
- elapsed wall and CPU time for each stage.

Instrumentation must be inactive or negligible in normal builds and should use
stable identifiers suitable for tests and benchmark summaries.

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

## Validation corpus

### DG-019 — build a geometry-detection benchmark and regression corpus

**Severity:** high

**Status:** measurement prerequisite

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

1. Implement DG-014 and assemble DG-019 without changing detection behavior.
2. Add the bounded prediction-neighborhood search from DG-010 behind an
   experimental switch and calibrate it against false positives.
3. Introduce robust partial-grid scoring (DG-011), using the local search as its
   observation layer.
4. Compare a coarse periodicity proposal (DG-013) only for regions where the
   exact component seed fails.
5. Use the resulting failure counters to implement early rejection and state
   reuse (DG-015 through DG-017).
6. Revisit scratch storage and lower-level micro-optimization (DG-018) only
   after the algorithmic failure cost is under control.
