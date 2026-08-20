# Lens correction review and solver tracker

Review date: 2026-08-17  
Code base: `7040f2f45b8fc64ba42d5155f50a4798376fb905`  
References: Adobe DNG Specification 1.7.1.0, `WarpRectilinear` (opcode 1),
and Adobe DNG SDK 1.7.1 Build 2652 (2026-07-14).

Implementation status: stage 1 contains the DNG/correctness fixes and
conformance regressions. Stage 2 adds conservative safeguards for automatic
lens fitting: robust spatial coverage and a bounded deformation envelope.

## Scope

Color-Screen implements the radial subset of DNG `WarpRectilinear`:

```
r_source = r_output * (kr0 + kr1*r^2 + kr2*r^4 + kr3*r^6)
```

There is one coefficient set and the tangential coefficients are fixed to
zero. Project files serialize the complete DNG-1-style record shape but the
loader requires `N=1` and `kt0=kt1=0`. Consequently the implementation is
compatible with that subset; it is not a complete implementation of every
`WarpRectilinear` feature.

The direction is important. DNG specifies inverse resampling: for a pixel in
the warped/output image, the opcode computes the source coordinate to sample.
`lens_warp_correction::corrected_to_scan()` has that direction.
`scan_to_corrected()` is Color-Screen's numerical inverse used by the
screen-to-image mapping.

There is an important specification/reference-implementation discrepancy in
the optical-center geometry. The DNG 1.7.1 prose describes normalized center
coordinates in terms of image pixels (and in particular says `(1,0)` denotes
the top-right pixel), which suggests `width-1`/`height-1` pixel-center
endpoints. Adobe DNG SDK 1.7.1 Build 2652 instead implements the operation
using `dng_rect` bounds: `dng_filter_warp` interpolates the center between
`bounds.l..bounds.r` and `bounds.t..bounds.b`, and computes the normalization
radius from the same rectangle. For `dng_rect(height,width)`, `r=width` and
`b=height` are exclusive image bounds. Color-Screen deliberately follows this
`width`/`height` SDK convention because interoperability with Adobe DNG and
future lens-profile imports is more important than the earlier literal reading
of the prose. The executed SDK dataset below makes this convention testable.

For the radial-only model, DNG invertibility reduces to

```
w'(r) = kr0 + 3*kr1*r^2 + 5*kr2*r^4 + 7*kr3*r^6 > 0
```

on `0 <= r <= 1`. `lens_warp_correction_parameters::is_monotone()` checks
the endpoints and all stationary points of this cubic in `r^2`, so the
analytic test is appropriate for the implemented subset.

`normalize()` is **not** a DNG requirement. The lens solver normalizes the
edge ratio to one because an overall radial scale is almost degenerate with
the simultaneously refitted homography. Imported DNG coefficients are used
without this normalization.

Moving-lens scanner modes are Color-Screen-specific extensions. DNG describes
a fixed 2-D image warp; the moving-lens modes intentionally remove the motor
axis before applying the remaining one-dimensional lens geometry.

## Review findings and implementation status

| ID | Status | Issue / action |
|---|---|---|
| LC-001 | Fixed after SDK cross-check | The initial spec-only review changed center/radius geometry to `width-1`/`height-1`. Executing Adobe DNG SDK 1.7.1 Build 2652 showed that the reference implementation actually uses the exclusive `dng_rect` bounds `width`/`height`; Color-Screen now follows the SDK. |
| LC-002 | Fixed | The direct inverse used by `lens_solver` searched only to `m`, while the lookup-table inverse extended beyond `m` when `f(1)<1`. Both paths now use the same search bound. |
| LC-003 | Fixed | `precompute()` accepted a non-monotone radial map although DNG requires an invertible warp. |
| LC-004 | Fixed | Moving-lens precomputation left the default normalized center on the axis that is explicitly removed before lens correction. |
| LS-001 | Fixed | A map-setup failure in `lens_solver::solve()` could return without initializing `chisq`; simplex could then consume an indeterminate objective value. |
| LS-002 | Fixed | Automatic lens fitting now requires the central 90% of registration points to cover at least half of every relevant scan axis, in addition to the point-count threshold. |
| LS-003 | Fixed | Normalized auto-fit candidates are restricted to a conservative center, displacement and radial-derivative envelope. This is solver safety policy, not a DNG-format restriction. |
| TEST-001 | Fixed | The lens test used a fixed `(500,500)` center for all nominal test cases and normalized a second warp from already-warped source corners. |
| TEST-002 | Fixed | A hand-calculated polynomial check was incorrectly described as an Adobe DNG worked example. It is retained as a synthetic formula check. |
| TEST-003 | Fixed | Twelve source coordinates emitted by the executed Adobe DNG SDK Build 2652 are frozen in `test_lens_warp()` and compared directly with Color-Screen output. |

## Adobe DNG SDK reference dataset

The cross-implementation regression is based on data produced by **executing
Adobe's implementation**, not by reimplementing its equations in a helper
program. The reproducible probe lives on the experimental branch
`agent/dng-sdk-reference-run`; the source state used to generate the frozen
fixture is commit `cfb5f31fa852322eb6d3a6b8e566e1bfa2bdf3ce`.

The workflow performs the following steps:

1. Download Adobe's official
   `dng_sdk_1_7_1_2652_20260714.zip` from
   `https://download.adobe.com/pub/adobe/dng/dng_sdk_1_7_1_2652_20260714.zip`.
   The workflow refuses to continue unless its SHA-256 is
   `73499b47f4683e12120a234bd0946f02e52ab2ff9834bcbd0e9f8ab4f923360e`.
2. Unpack the archive and verify that the actual Adobe
   `dng_sdk/source/dng_lens_correction.cpp` has SHA-256
   `89112619dce4a205761dc9e3b0c641d6c1d99911829a18c181a7229af4e8521f`.
3. Clone `hfiguiere/dng_sdk` only to obtain its Meson build scaffolding, then
   overwrite its `dng_sdk/source` directory with the files from the verified
   Adobe archive. Thus the lens implementation being executed is the official
   Build 2652 source, not the mirror's copy.
4. Build with the SDK validation helpers enabled (`qDNGValidateTarget=1`).
   Build 2652 generic headers require the libjxl 0.11 API while the runner's
   packaged libjxl is older, so the workflow supplies the public libjxl 0.11.1
   headers, omits unrelated `dng_jxl.cpp`, and links throwing/no-op stubs for
   JPEG-XL entry points. None of these entry points is called by the lens-warp
   probe.
5. Append `.github/dng-reference/reference-wrapper.inc` to Adobe's
   `dng_lens_correction.cpp`. This is intentional: `dng_filter_warp` is an
   implementation-private class in that translation unit. The wrapper can
   therefore instantiate the **real** class rather than duplicate its math.
   It creates a `dng_negative`, `dng_simple_image` source and destination with
   bounds `dng_rect(height,width)`, loads the four radial coefficients through
   `dng_warp_params_radial::SetWarpRectilinear_1_3`, sets tangential terms to
   zero, constructs `dng_warp_params_rectilinear`, constructs
   `dng_filter_warp`, and calls its real `GetSrcPixelPosition()` method.
6. `.github/dng-reference/reference_probe.cpp` invokes that wrapper for twelve
   cases: a simple centered polynomial, seven points of one non-square
   off-center profile, three points using the Nikon Coolscan coefficients, and
   one constant-ratio case with `f(1)<1`.
7. The program prints all inputs and returned source coordinates at 17-digit
   precision. The successful run saved them as
   `.github/dng-reference/generated/adobe-dng-sdk-2652-warp-fixture.txt` with a
   companion `PROVENANCE.txt`. Those exact numeric outputs are copied into
   `test_lens_warp()`; the unit test never runs the Adobe SDK itself.

This probe exposed the `width-1` mistake immediately. For example, Build 2652
returns `(1011.2046333360396, 703.35264195316779)` for output pixel
`(1000,700)` of the `1001 x 701`, center `(0.23,0.67)` fixture with
`kr=(0.992,0.045,-0.028,0.009)`. That result is produced using center
`(0.23*1001,0.67*701)` and the `1001 x 701` rectangle bounds. The previous
pixel-center interpretation gives a measurably different result.

Color-Screen keeps an independent equation transcription as a secondary
arithmetic check, but the frozen Build 2652 coordinates are the authoritative
interoperability regression. A future DNG/lens-database importer should add
representative imported profiles to this table before extending the supported
opcode subset.

## Automatic solver safety envelope

The lens solver refits a homography for every nonlinear lens candidate. On a
small point cloud, low-order radial distortion is easily absorbed by that
homography and the high-order terms/optical center are poorly identified.
A very small local residual can therefore extrapolate to a very large global
deformation. Point count does not cure this conditioning problem.

The implemented conservative gate is:

* retain the existing minimum point count (100 for ordinary screens, 200 for
  vertical-strip screens);
* require the central 90% of point coordinates to span at least 50% of the
  scan in both axes for fixed-lens models;
* for a moving horizontal lens require the vertical span, and vice versa;
* constrain an automatically fitted normalized optical center to
  `[-0.5, 1.5]` on each fitted axis;
* after removing the solver's overall-scale gauge, require maximum radial
  displacement `|r * (f(r)-1)| <= 0.05` of DNG radius `m`;
* require radial derivative `0.5 <= w'(r) <= 1.5` throughout the image.

These values are intentionally generous and remain provisional. They are not
claimed physical limits and are not applied when merely loading or retaining a
valid DNG/manual profile. They only decide whether an **automatically inferred**
lens solution is safe enough to install. If spatial coverage is insufficient,
lens optimization is skipped and any existing lens profile remains active.

For scale, the existing Nikon Coolscan 9000ED fixture

```
kr = 0.99508 0.0245411 -0.0521967 0.0325757
```

has about 0.185% maximum normalized radial displacement; evaluating the
polynomial gives a radial-derivative range of about 0.9945..1.0357.  The
normalized synthetic lens-solver regression is also comfortably inside the
envelope (about 2.46% displacement and 0.952..1.190 derivative).  Thus the
gate is aimed at underconstrained extrapolation, not at rejecting the
distortions already represented by the test corpus.

## Stage 2 regression coverage

The unit suite now checks that exactly the ordinary 100-point threshold is
accepted when points are broadly distributed; a dense local cloud remains
rejected even with remote outliers; and moving-lens scanners require coverage
only perpendicular to the movement axis. It also verifies that the existing
synthetic and Nikon Coolscan profiles pass the automatic-fit envelope, an
extreme but still monotone search-box profile is rejected, and insufficient
coverage does not replace an existing manual lens profile.

## Deferred improvements

The following are deliberately *not* part of this conservative patch.

### DNG model coverage

* Implement nonzero tangential `kt0`/`kt1` terms if real input profiles require
  them.
* Support multiple coefficient sets (`N == plane count`) for lateral chromatic
  aberration.
* Evaluate `WarpRectilinear2` support separately; do not silently reinterpret
  it as opcode 1.
* If DNG opcodes are imported directly, audit active-area/crop coordinate
  semantics rather than assuming the complete scan rectangle.

### Solver conditioning and statistics

* Compute a conditioning/identifiability diagnostic from the nonlinear
  Jacobian (singular values or covariance). Spatial coverage is only a cheap
  proxy.
* Report uncertainty for center and radial terms and suppress parameters that
  are not individually identifiable.
* Add model-order selection: fit `k1`, then `k1+k2`, and only enable `k3` when
  supported by a significant residual improvement.
* Consider weak regularization/prior information for the optical center and
  high-order terms instead of only hard bounds.
* Compare the lens model with a no-lens baseline on held-out registration
  points so an extra global model is installed only when it predicts better.

### Robust fitting

The inner homography fit intentionally avoids RANSAC because a discontinuous
RANSAC objective is hostile to the outer nonlinear optimizer. A future
replacement should use a smooth robust loss (for example Huber or Tukey with a
fixed/iteratively estimated scale) rather than reintroducing RANSAC inside the
lens objective.

### Calibration and UI

* Build a corpus of calibrated macro lenses, high-resolution phones, camera
  scanning rigs and film scanners; measure actual displacement and derivative
  envelopes before tightening or relaxing the provisional 5% / 0.5..1.5
  limits.
* Distinguish a trusted/calibrated lens profile from a lens model inferred from
  the current registration points. The former should not be subject to the
  automatic-solver safety envelope.
* The geometry panel now warns when point count is sufficient but coverage is
  too local. A future UI can also show the numeric point coverage and solver
  conditioning rather than only the pass/fail warning.
* Preserve rejected candidate diagnostics (objective, coverage, center,
  displacement, derivative range) for troubleshooting.

## Validation of stage 1

The stage-1 patch was built locally from the exact `7040f2f` source tarball
with a private Debian 13 dependency prefix. The complete 40-group unit binary
passes with both `-O2 -Wall -Wextra --enable-checking` and
`-Ofast -march=native -Wall --enable-checking`. The focused `warp`,
`lens_correction` and `1d_homography` groups also pass independently in both
builds. Render-original, slanted-edge, raw/autodetect and Capture One/stitch
integration scripts pass in the O2 build. The long finetune integration script
is left to repository CI because it exceeds the local command execution
window; its first six cases completed successfully locally.

## Testing targets

Keep the following regressions as the lens code evolves:

1. Frozen source coordinates emitted by Adobe DNG SDK 1.7.1 Build 2652.
2. Independent radial-formula evaluation using Adobe SDK `dng_rect`
   `width`/`height` bound semantics.
3. Forward/inverse agreement for in-domain source pixels.
4. Equality of lookup-table and direct inverse search domains.
5. Known Coolscan coefficients (also represented in the Adobe SDK fixture).
6. Full-frame synthetic solver recovery.
7. Rejection of a dense registration cloud confined to a small scan area.
8. Acceptance of a broad registration cloud with the same point count.
