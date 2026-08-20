# Lens correction review and solver tracker

Review date: 2026-08-17  
Code base: `7040f2f45b8fc64ba42d5155f50a4798376fb905`  
Reference: Adobe DNG Specification 1.7.1.0, `WarpRectilinear` (opcode 1).

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

The DNG normalized optical center uses the top-left and bottom-right **pixel
centers** as 0 and 1. Therefore a `width x height` image uses coordinate
endpoints `width-1` and `height-1`. The farthest output pixel from the optical
center defines the normalization radius `m` and therefore `r=1`.

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
| LC-001 | Fixed | Production DNG center and corner normalization used `width`/`height` instead of the bottom-right pixel centers `width-1`/`height-1`. |
| LC-002 | Fixed | The direct inverse used by `lens_solver` searched only to `m`, while the lookup-table inverse extended beyond `m` when `f(1)<1`. Both paths now use the same search bound. |
| LC-003 | Fixed | `precompute()` accepted a non-monotone radial map although DNG requires an invertible warp. |
| LC-004 | Fixed | Moving-lens precomputation left the default normalized center on the axis that is explicitly removed before lens correction. |
| LS-001 | Fixed | A map-setup failure in `lens_solver::solve()` could return without initializing `chisq`; simplex could then consume an indeterminate objective value. |
| LS-002 | Fixed | Automatic lens fitting now requires the central 90% of registration points to cover at least half of every relevant scan axis, in addition to the point-count threshold. |
| LS-003 | Fixed | Normalized auto-fit candidates are restricted to a conservative center, displacement and radial-derivative envelope. This is solver safety policy, not a DNG-format restriction. |
| TEST-001 | Fixed | The lens test used a fixed `(500,500)` center for all nominal test cases and normalized a second warp from already-warped source corners. |
| TEST-002 | Fixed | A hand-calculated polynomial check was incorrectly described as an Adobe DNG worked example. It is retained as a synthetic formula check. |

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

1. DNG direct radial formula evaluation.
2. DNG optical-center conversion using `width-1`/`height-1`.
3. Forward/inverse agreement for in-domain source pixels.
4. Equality of lookup-table and direct inverse search domains.
5. Known Coolscan coefficients.
6. Full-frame synthetic solver recovery.
7. Rejection of a dense registration cloud confined to a small scan area.
8. Acceptance of a broad registration cloud with the same point count.
