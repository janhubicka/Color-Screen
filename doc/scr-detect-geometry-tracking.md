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

**Status:** partially fixed by DG-025 and DG-026; broader class-map continuity remains open

`find_patch()` uses eight-connected equality in the thresholded color-class
map.  Blur can join neighboring elements diagonally, split one element into
several islands, or replace the expected center pixel with `unknown`.  Initial
grid discovery then fails before the slower image-domain confirmation code can
help.

A replacement must preserve the current sharp-image path as the cheap first
choice.  Candidate extensions to evaluate include accepting a small unknown
boundary, component morphology bounded by predicted screen scale, and using a
confidence-valued class map rather than changing hard labels globally.

### DG-025 — fast flood fill rejects blur-shrunken patch interiors

**Severity:** high robustness/performance

**Status:** fixed first-stage continuity

After a valid initial grid and preliminary geometry fit, fast flood fill still
required every square-patch component to contain at least the historical
`min_patch_size` number of hard-classified pixels.  In the historical
unsharpened 0/0 detector runs, blur reduced the pure-color interior even when the
component centroid remained very close to the predicted lattice point.  On
NGS00428 Tile05 at 0/0,
diagnostics showed roughly 155,000 fast patch confirmations rejected as too
small, compared with about 23,000 under the historical 2/3 mask.

Keep the historical size and distance gates for normal components.  A component
between half the normal minimum size and the normal minimum is now accepted only
when its centroid is within half the normal geometric distance tolerance.  This
uses the already-fitted lattice as the extra evidence needed for a small
fragment; it does not relax initial-grid discovery, color classification,
maximum component size, strip confirmation, or slow image-domain confirmation.

Several broader alternatives were measured and rejected.  Halving the minimum
size globally improved fast-only coverage but made combined fast+slow flood fill
much slower by expanding the slow-confirmation frontier.  A one-pixel same-class
seed search and a conservative unknown-hole rule recovered little coverage and
added work.  The geometry-conditioned fragment rule is the first tested variant
that improves both coverage and normal fast+slow runtime.

### DG-026 — hard strip classes discard useful geometric color evidence

**Severity:** high robustness/performance

**Status:** fixed for unsharpened Dufay-like flood fill

The hard color map deliberately favors purity: after scanner colors are mapped
into the optimized three-dye coordinates, `classify_adjusted_color()` assigns a
class only when one coordinate exceeds the sum of the other two times
`min_ratio` (normally 1).  This gives clean connected components, but a blurred
narrow strip can become `unknown` even when its expected dye is still clearly
stronger at the strip position.

Do not globally loosen this purity gate.  On NGS00428 Tile05 at detector mask
0/0, reducing `min_ratio` to 0.9 improved fast-only screen coverage from 79.01%
to 81.81%, and 0.8 reached 84.03%; lower values expanded the search sharply.
The 0.8 result is essentially the same coverage reached by the local strip
fallback, but it changes every pixel in the component graph and therefore has a
substantially larger false-positive and performance surface.

Fast Dufay-like row growth now keeps the hard component test as its first path.
Only when that strip component is absent, it samples the continuous normalized
dye fraction at
three positions along the already predicted strip and at corresponding points
on both neighboring rows.  The expected dye must beat the stronger neighboring
row by `min_patch_contrast`.  Even then, the row is accepted through this soft
strip observation only if the destination square patch passes the existing fast
geometric component confirmation.  A failed soft observation falls through to
the historical slow image-domain confirmation when that path is enabled.

This keeps the color map itself unchanged and prevents soft strip evidence from
opening a slow-confirmation frontier by itself.  It also makes the compatibility
behavior explicit: any active legacy preclassification unsharp mask disables
the new fallback, so the normal 2/3 path follows the historical control flow.

On the same optimized local build, NGS00428 Tile05 at 0/0 with fast-only flood
fill improves from 79.01% screen coverage / 1,726,273 patches to 84.00% /
1,835,339 patches.  Tile01 moves only from 62.99% / 1,359,400 to 63.58% /
1,372,414, preserving the real raster-free corner.  With the historical 2/3
mask, Tile01 remains exactly 93.46% / 2,040,920 patches before and after the
change because the fallback is disabled.

### DG-027 — slow strip contrast tested an accumulator strip mode zeroed

**Severity:** high correctness/performance

**Status:** fixed

Slow image-domain confirmation samples an expected Dufay-like strip and the two
neighboring square rows.  In strip mode those neighboring-row samples are stored
in `bestouter_ud`; left/right samples are folded into `bestinner`, after which
`bestouter_lr` is deliberately reset to zero.  The final acceptance test still
compared `bestinner` with `bestouter_lr * min_patch_contrast`.  Consequently any
strip with a positive inner signal passed the slow contrast gate regardless of
the measured neighboring-row dye fraction.

Use the accumulator selected by the sampling geometry: `bestouter_ud` for a
strip and the historical `bestouter_lr` for non-strip patches.  Sampling
locations, `min_patch_contrast`, displacement search, priority calculation,
fast hard-strip confirmation, DG-026 soft-strip confirmation, and all non-strip
acceptance behavior are unchanged.

On the full-resolution NGS00428 controls in combined fast+slow mode, the stricter
slow strip test changes little useful coverage: Tile05 at detector mask 0/0
moves from 88.77% to 88.71% screen coverage and 3,398,865 to 3,393,568 patches;
Tile01 moves from 93.51% to 93.37% and keeps its real raster-free corner.  With
the historical 2/3 mask, Tile05 changes from 98.70% to 98.63% and only 371 of
about 3.78 million patches disappear; Tile01 scan coverage remains 56.02% versus
56.01% with fewer than one thousand of about 2.14 million patches removed.
Local wall and flood timings vary substantially between repeated full-resolution
runs, so they are not used as a compatibility criterion.

Slow-only Tile05 becomes deliberately much more selective: accepted patches fall
from 2,288,129 to 1,551,816 and scan coverage from 59.78% to 40.66%.  This shows
that the old path relied heavily on strips that did not meet its nominal 2x
contrast requirement; it is not presented as an improvement in slow-only scan
coverage.  The repository's real Nikon Dufay integration suite nevertheless
passes all six cases after the correction, including its dedicated slow-only
`min-screen-percentage=80` test, and the Capture One stitched Dufay suite also
passes all six cases.  The normal combined detector therefore retains its real
regression gates while the slow path now enforces the contrast it already
measures.

The next blur-robustness target remains geometry-gated square-patch/`unknown`
continuity rather than another relaxation of strip classification.

### DG-010 — predicted patch lookup samples only one rounded pixel

**Severity:** high

**Status:** open, but blocked on a demonstrated failure after DG-024

After DG-024, Dufay-like and Paget-like initial-grid code preserves fractional
predictions and rounds them to the nearest image pixel before calling
`find_patch()`.  A genuinely blurred or unknown center can still reject an
otherwise coherent grid, so a bounded local search remains a possible follow-up.

Do not add that heuristic preemptively.  On the real NGS00428 corner tile, a
one- and two-pixel neighborhood prototype reduced zero-mask seed scanning to
exactly the same 95 pixels as correct nearest-pixel rounding alone.  The wider
search therefore added no observed robustness on the case that first motivated
it.  Revisit local search only when DG-019 contains a scan that fails with
correct rounding; then rank candidates by center displacement, color confidence,
size consistency, and lattice agreement, with the radius kept below half the
nearest competing-element distance.

### DG-024 — predicted patch centers were truncated before lookup

**Severity:** high robustness/performance

**Status:** fixed

Initial-grid geometry is floating point, but many predicted Dufay-like and
Paget/Finlay patch positions were stored in integer temporaries or passed
implicitly to `find_patch()`.  C++ conversion truncated those coordinates rather
than selecting the nearest image pixel.  A subpixel prediction error could
therefore become an avoidable one-pixel classification miss and force the outer
seed scan to continue.

Predicted coordinates now remain `coord_t` until a shared helper rounds them to
the nearest image pixel.  Strip predictions likewise retain their fractional
coordinates before `confirm_strip()` performs its existing nearest-pixel
rounding.  Seed pixels supplied by the outer image scan remain integer pixels;
only geometry-derived predictions change.

On NGS00428 Tile01 with the legacy detector mask explicitly disabled, this
reduces `seed_pixels` from 23,702 to 95 while preserving 93.35% detected screen
coverage.  A one-pixel neighborhood search produced the same 95-pixel result,
which is why DG-010 remains deferred rather than being bundled with this fix.
Historical radius 2 / amount 3 detector-mask runs on Tile01 and Tile05 retained
the same coverage, patch counts, and seed counts as before this change.  Those
measurements remain useful history, but DG-020 later removed the detector mask.

### DG-011 — initial grids are accepted or rejected as all-or-nothing patterns

**Severity:** high

**Status:** partially fixed for Paget/Finlay by a guarded affine seed fallback

The historical Paget/Finlay seed requires a complete fixed connected-component
pattern.  It also reconstructs the second image-space lattice direction by an
exact 90-degree rotation of the first.  A well-resolved plate can therefore
fail before the solver when projective capture geometry makes the local lattice
a parallelogram or when the deliberately pure color classifier leaves a few
otherwise coherent elements `unknown`.

Keep that exact seed as the first and preferred path.  When it fails, Paget and
Finlay now have a bounded partial-affine fallback.  Starting from the same green
component and two neighboring blue components, it uses the observed blue
directions as a local affine basis and probes a 5 by 5 lattice without changing
the global color classifier.  A candidate reaches the existing initial solver
only when all of the following hold:

- at least 37 of the 50 expected green and primary-blue observations are unique
  connected components;
- at least four of five rows and four of five columns contain independent
  support;
- the mean component-center residual is at most one quarter of the local period,
  clamped to 3 through 5 image pixels;
- at least 20 of 25 independently predicted red checkerboard positions contain
  red components.

The independent red test is important.  On the motivating 150 MP Phase One
Paget capture, two plausible green/blue sublattices had only 14/25 and 22/25 red
support, while the useful seed had 37/50 solver observations and 25/25 red
support.  Without this gate those false candidates enter slow flood fill and
make normal detection substantially slower.  Alternate-blue support is recorded
for diagnostics but is not an acceptance criterion because it did not separate
the false and useful candidates on this scan.

The fallback only proposes initial solver points.  `simple_solver()` and the
existing flood-fill, coverage, border and final-solver checks are unchanged and
remain authoritative.  The normal exact Paget/Finlay path is unchanged and is
always attempted first.

On the full external 150 MP Paget capture which previously did not escape the
first failed search region within 45 seconds, the current-main O2 build now
succeeds in the first region:

| flood mode | solver seed | screen coverage | patches | detector time |
| --- | --- | ---: | ---: | ---: |
| fast only | 37/50, red 25/25 | 76.39% | 1,801,856 | 24.18 s |
| fast + slow | 37/50, red 25/25 | 96.38% | 2,294,578 | 51.64 s |

The combined run spends 29.70 seconds in slow flood fill, so its remaining
runtime is a separate flood-fill/performance issue rather than seed discovery.
A 2400 by 1800 crop from the same Color-Screen-decoded image reaches 98.79%
screen coverage in O2 and 98.82% with `-Ofast -march=native` on current main.

The fallback has also been checked against deterministic grayscale, low-chroma
and colorful no-screen controls: none is allowed to start a Paget flood fill.
Synthetic regular-screen discovery now includes Paget alongside Finlay and
Dufay.  The broader DG-011 problem remains open for other screen families and
for future evidence that the present Paget thresholds need generalization; do
not turn the fallback into a global partial-grid relaxation without new corpus
measurements.

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
reason, color-optimization/precompute failures, class-map builds, and adjusted-RGB
precomputations.  `detect_stats_ms:` records wall time spent in color optimization,
precomputation, class-map construction, initial solvers, flood fill, final
solver, mesh solver, and the complete detection.

Flood fill supplies stable rejection identifiers for invalid screen scale,
flipped Dufay-like geometry, mapping failures, too few patches, refined-solver
failure, unknown areas, insufficient coverage, border failures, and
cancellation.  Clock sampling is disabled when there is no report file.  CPU
time and finer per-screen-family seed counters remain optional follow-up if wall
time does not identify the bottleneck clearly.

**DG-014 telemetry follow-up (2026-08-29).**  Failed detections copied
`detected_screen::patches_found` into the final statistics record even when no
flood fill had run, but the result field itself was not initialized.  Initialize
it to zero with the other failure-safe result state so negative-corpus reports
no longer contain stack garbage in `patches=`.  Successful flood fills continue
to overwrite the field with their measured patch count.
### DG-028 — collapsed optimized primaries trigger impossible grid searches

**Severity:** high failure-time/performance

**Status:** fixed first-stage rejection

Per-region color optimization runs before the expensive image-wide adjusted-RGB,
color-class-map, and lattice-search work.  On images without an additive screen,
the fitted red, green, and blue process colors often collapse to nearly the same
scanner RGB direction.  Continuing with such a color basis cannot produce three
useful screen classes and wastes the rest of that search-region attempt.

Subtract the optimized black point from each fitted primary, normalize the three
scanner-RGB vectors to unit length, and measure their minimum pairwise Euclidean
distance.  Reject only the current search region when that separation is below
0.10.  Degenerate or non-finite primary vectors count as zero separation.  The
detector still advances to later regions, so a raster-free border or a locally
low-chroma part of a real screen scan cannot reject the whole image.  Existing
`color_opt_failures` telemetry counts these early exits.

The threshold is intentionally conservative.  The difficult NGS00428 Tile01 and
Tile05 scans measure approximately 0.321 and 0.410 respectively, and the real
Nikon Dufay integration fixture measures approximately 1.328.  Their generated
parameter files are unchanged with the guard enabled, and synthetic Finlay and
Dufay discovery tests continue to pass.

On deterministic 1200 by 1200 no-screen controls in the optimized local build,
the region guard reduces failure wall time as follows:

| input | baseline | guarded | regions rejected early |
| --- | ---: | ---: | ---: |
| grayscale | 0.79 s | 0.44 s | 36 / 36 |
| low-chroma color | 1.70 s | 0.95 s | 36 / 36 |
| colorful no-screen texture | 1.59 s | 1.02 s | 29 / 36 |

The colorful control deliberately continues through the seven regions whose
scene colors are sufficiently separated; this is a cheap impossibility test,
not a replacement for lattice validation.  DG-015 remains open for broader
reuse of image-wide detector state across regions that survive this gate.

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

**Status:** fixed; detector-only sharpening removed

The historical `scr_detect_parameters` radius/amount unsharp mask predated the
current capture-sharpening pipeline and duplicated image processing inside the
detector.  Earlier attempts to set its radius 2 / amount 3 default to zero
failed on the real Nikon Coolscan Dufay integration fixture, so simply changing
the default was not sufficient.

The detector-specific mask is now removed completely.  `scr_detect_parameters`
contains only color-classification state, `render-scr-detect` no longer calls
the generic unsharp-mask helper, and new parameter files no longer write
`scr_detect_sharpen_radius` or `scr_detect_sharpen_amount`.  The loader still
parses and ignores those two obsolete keys so existing `.par` files remain
readable.

Capture sharpening, when genuinely needed, is supplied through the normal
`render_parameters::sharpen` path.  `detect_regular_screen()` accepts an optional
capture render-parameter snapshot; the CLI passes the parameters loaded by
`autodetect --par=...`, and the Qt worker passes the current document render
parameters.  Only the capture sharpening configuration is copied into the
detection renderer; detector color/geometry controls remain independent.
Native RGB capture sharpening is therefore performed by the same cache and
implementation used by normal rendering before screen-color classification.

The current Nikon Coolscan Dufay fixture still needs sharpening: fully
unsharpened autodetection exhausts its search and fails.  Its integration test
therefore uses a small capture parameter file selecting ordinary unsharp mask
radius 2 / amount 3.  With that explicit capture setting all six real cases pass,
including fixed/moving-lens geometry, mesh/no-mesh, fast-only, and slow-only
flood fill.  This preserves the compatibility requirement without retaining a
second hidden sharpening system in the detector.

The 150 MP Phase One Paget capture used for DG-011 also succeeds in fast-only
mode with neither detector nor capture sharpening: 1,692,520 patches and 72.54%
screen coverage.  It needs six search regions and about 107 s on the local O2
checking build, compared with the earlier sharpened first-region result.  Thus
sharpening is no longer a correctness requirement for this difficult Paget
case, but normal capture sharpening may still materially improve component
separation and search time when touching or blurred screen elements warrant it.

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
part of the comparison.  Detector-only sharpening parameters were subsequently
removed by DG-020 and no longer participate in the key.

### DG-023 — fast-only flood fill should not precompute the slow RGB image

**Severity:** medium performance

**Status:** fixed

Fast flood fill itself uses only the color-class map.  With detector-only
sharpening removed, an unsharpened fast-only detection no longer materializes
the adjusted-RGB image; synthetic discovery requires `rgb_precomputes=0`.
When slow image-domain confirmation is enabled, one adjusted-RGB precompute is
performed and the same synthetic test requires `rgb_precomputes=1`.

Explicit capture sharpening is the one intentional exception: classification
must see the sharpened native RGB capture, so that capture image and a region's
color-adjusted RGB representation are prepared before the class map even for
fast-only flood fill.  The native sharpened capture cache is reusable across
search regions.  Each region can still need a new adjusted-RGB representation
when optimized screen primaries change, but slow confirmation reuses that same
regional representation rather than computing it twice.

## Validation corpus

### DG-019 — build a geometry-detection benchmark and regression corpus

**Severity:** high

**Status:** in progress

The report-only DG-014 records now provide a stable baseline format, and the
existing synthetic discovery tests cover sharp Finlay and Dufay detection with
both slow+fast and fast-only flood fill.  `testsuite/benchmark-screen-detection.py`
provides the manual corpus driver.  It runs external scans through `autodetect`
and writes one CSV row per flood-fill mode together with full reports, logs, and
successful output parameter files.  Detector-sharpening modes were removed with
DG-020; scans that require capture sharpening should provide it through the
normal input parameter file used by the workflow being tested.  The CSV includes
the DG-014 counters and timings, scan/screen coverage, process wall time, and
Linux peak resident memory when `/proc` is available.

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
changes a first-pixel initial grid into a 23,702-pixel search.  DG-024 shows that
this particular failure was caused by truncating fractional lattice predictions:
nearest-pixel rounding alone reduces the zero-mask seed search to 95 pixels while
retaining 93.35% screen coverage.  Radius-one and radius-two neighborhood
prototypes produced the same result, so this scan does not justify DG-010.

The interior tile is the complementary warning: seed discovery remains immediate
without sharpening, yet detected patches drop by 12.2% and flood fill becomes
1.68 times slower.  On the corner tile flood fill becomes 2.96 times slower.
At the time, DG-024 alone was not sufficient evidence to remove the compatibility
mask; subsequent work therefore focused on DG-009 class-map/component continuity
and flood-fill completeness.  DG-020 later removed the duplicate detector mask
after those robustness fixes and moved the remaining real Dufay requirement to
normal capture sharpening.

DG-025 addresses one measured part of that gap.  The table below is an
apples-to-apples before/after run on the same machine, using the accepted DG-024
rounding baseline, Dufay, fixed-lens geometry, gamma 1, color optimization,
fast+slow flood fill, no mesh, five threads, and explicit detector mask 0/0.

| input | rule | screen area | patches | flood ms | detector ms | wall ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Tile05, interior | DG-024 baseline | 86.67% | 3,317,587 | 20,404.5 | 24,433.5 | 31,952.6 |
| Tile05, interior | DG-025 fragments | 88.76% | 3,398,805 | 18,006.8 | 21,654.9 | 28,874.2 |
| Tile01, corner | DG-024 baseline | 93.36% | 2,040,129 | 18,468.2 | 22,074.3 | 31,376.3 |
| Tile01, corner | DG-025 fragments | 93.52% | 2,043,553 | 16,208.7 | 19,524.2 | 27,099.5 |

Thus Tile05 gains 2.09 percentage points of screen coverage while flood time
falls 11.7%; Tile01 preserves the real raster-free border while flood time falls
12.2%.  Historical 2/3 runs remain effectively unchanged: Tile05 measures
98.70% screen coverage and 3,778,087 patches, while Tile01 measures 97.28% and
2,140,569 patches.  At this stage the compatibility mask was still retained
because DG-025 did not recover the remaining Tile05 coverage gap or the
strip/unknown-pixel part of DG-009; DG-020 records its later removal.

DG-026 then isolates the strip part without changing the global hard class map.
Fast-only 0/0 detection on Tile05 rises from 79.01% to 84.00% screen coverage;
Tile01 changes only from 62.99% to 63.58%.  A global `min_ratio` experiment
reached similar Tile05 coverage at 0.8, but because it changes the entire
component graph it is not adopted.  The remaining gap is therefore primarily
about square-patch/unknown continuity and the quality/cost of the slow fallback,
not a reason to make all hard pixel classes less selective.

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

1. Expand DG-019 across Batch 08 with `testsuite/benchmark-screen-detection.py`,
   including representative negative/no-screen images and repeated timing runs.
2. Continue DG-009 on remaining square-patch/unknown continuity.  DG-025 handles
   geometry-consistent small square fragments and DG-026 handles soft Dufay-like
   strip evidence without globally relaxing the hard color map.
3. Revisit the bounded neighborhood search in DG-010 only when a real scan
   still fails at a correctly rounded predicted center.
4. Introduce robust partial-grid scoring (DG-011) if isolated missing elements
   remain a significant seed failure after DG-009/DG-010 are understood.
5. Compare a coarse periodicity proposal (DG-013) only for regions where the
   component-based seed path still fails.
6. Use measured failure costs to implement early rejection and state reuse
   (DG-015 through DG-017), then revisit scratch storage (DG-018) only if it is
   visible in profiles.
