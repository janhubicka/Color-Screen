# Historical screen simulation: processing trace and issue register

## Purpose

Color-Screen uses a periodic RGB transmission pattern to model a historical
viewing filter placed in contact with, or close to, a panchromatic emulsion.
For colour-loss compensation, the program must predict how that filter appears
in the **digitally captured and sharpened scan**, not merely how the ideal
periodic pattern looks.

This document traces the path through `screen.C`, `render-to-scr.C`,
`simulate.C`, and the colour-loss code in `finetune.C`.  It assigns ownership to
every blur, sampling, and sharpening stage so that a transfer function cannot
be applied twice.  The issue identifiers are intended to remain stable across
subsequent patches and reviews.

## Representations and units

### Periodic screen

`colorscreen::screen` stores one `128 x 128` periodic unit cell:

- `screen::mult[y][x][channel]` is linear-light multiplicative transmission;
- `screen::add[y][x][channel]` is presentation-only preview data;
- screen coordinates are periodic and normally expressed in unit-cell units;
- `screen::interpolated_mult()` performs periodic bilinear interpolation.

Optical filtering changes `mult`.  It preserves `add` unless a caller
explicitly regenerates a preview pattern.

### Finite simulated capture

`colorscreen::simulated_screen` stores a finite image in capture-pixel order:

- integer pixel `(x, y)` is stored at `data[y * width + x]`;
- its image-space centre is `(x + 0.5, y + 0.5)`;
- fractional lookup is bicubic in the interior and clamped bilinear at a
  finite-image boundary;
- unlike `screen`, this image is not periodic.

### Transfer functions

Three conceptually different transfers are involved:

1. **Historical screen/emulsion spread:** physical diffusion or separation
   between filter and emulsion, when modelled.
2. **Capture transfer:** lens OTF, defocus, residual optical blur, flare/halo,
   sensor aperture, and any other pre-sampling response represented by the
   selected physical or measured MTF.
3. **Digital sharpening:** unsharp masking, Wiener inversion, or
   Richardson--Lucy processing after sampling.

The intended linear-light order is

```text
ideal periodic filter
    -> historical filter/emulsion spread
    -> capture lens/optical OTF
    -> sensor aperture integration
    -> discrete capture samples
    -> digital sharpening
    -> finite simulated screen used for colour collection
```

Each stage has exactly one owner.

## Sensor-aperture ownership contract

`screen_sampling` records how a periodic screen is converted to capture
samples:

```cpp
enum class screen_sampling
{
  point_sample,
  integrate_pixel
};
```

The decision is made when the forward periodic screen is constructed, not in
the per-pixel callback.  The rules are:

| Forward screen transfer | Aperture owner | Sampling operation |
|---|---|---|
| Measured slanted-edge MTF | the measurement | `point_sample` |
| Analytical MTF with `sensor_fill_factor > 0` | `mtf_parameters::sensor_mtf()` | `point_sample` |
| Analytical MTF with sensor term disabled | finite sampler | `integrate_pixel` |
| No capture MTF was applied | finite sampler | `integrate_pixel` |
| Legacy Gaussian screen blur | finite sampler | `integrate_pixel` |

A measured curve is assumed to have been obtained from sampled capture data;
it therefore includes the sensor aperture.  An imported theoretical curve
must not be marked as a measured capture curve unless it has the same
semantics.

The current analytical sensor term is the deliberately retained radial
first-cut model.  This patch fixes ownership of that response; it does not
replace it with a directional square-pixel transfer.

`mtf_parameters::includes_sensor_aperture_p()` describes whether the selected
capture transfer contains aperture attenuation.
`screen_sampling_for_capture_transfer()` combines that fact with the separate
fact that the transfer was actually applied to the periodic screen.  This
second condition is essential: metadata cannot own a stage which was skipped.

## End-to-end call trace

### 1. Request from the renderer

`render_to_scr::simulate_screen(progress)` in `render-to-scr.C`:

1. obtains the local conversion from image pixels to screen coordinates with
   `pixel_size()`;
2. copies the active `sharpen_parameters`;
3. converts screen-space blur and scanner-MTF scale to the periodic screen
   grid;
4. calls `render_to_scr::get_screen(..., anticipate_sharpening=false, ...)`;
5. receives both the filtered periodic screen and its `screen_sampling`
   policy;
6. passes them to `get_simulated_screen()` together with the **original**
   digital-sharpening parameters and finite image size.

The deliberate split is:

- the periodic screen receives the **forward capture transfer**;
- the finite sampled image receives the **digital inverse/filter**.

### 2. Construction of the periodic captured screen

`get_screen()` uses the `screen_cache` and `get_new_screen()`.

`get_new_screen()`:

1. initializes the ideal historical pattern with `screen::initialize()`;
2. when the existing screen-blur dispatch selects the scanner MTF, calls
   `screen::initialize_with_sharpen_parameters()`;
3. because `anticipate_sharpening` is false in this path, that function forces
   the digital mode to `none` and applies only the forward MTF/PSF;
4. otherwise, applies a legacy Gaussian screen blur with
   `screen::initialize_with_blur()` when requested.

`get_screen()` reports the matching sampling policy.  The helper which decides
whether the MTF was used mirrors the historical dispatch condition, so the
policy describes the screen which was actually returned rather than merely
the available metadata.

Spatially varying scanner-blur correction uses a `screen_table`.  Its entries
share one aperture-ownership policy because varying defocus or blur diameter
does not change whether the selected transfer includes the sensor aperture.

### 3. Mapping and finite-image sampling

`get_simulated_screen()` uses a one-entry LRU cache keyed by:

- periodic screen identity;
- mesh or analytical screen-to-image mapping;
- finite width and height;
- exact digital-sharpening parameters;
- explicit `screen_sampling` policy.

On a miss, `get_new_simulated_screen()` calls
`render_simulated_screen()` in `simulate.C`.

`render_simulated_screen()`:

1. validates and constructs the `scr_to_img` mapping;
2. selects either `get_point_pixel()` or `get_integrated_pixel()` once;
3. instantiates the sharpening/deconvolution source loop with that callback;
4. stores the filtered output in `simulated_screen`;
5. returns failure on invalid mapping, filter failure, or cancellation, so an
   incomplete image is not inserted into the cache.

The callback choice is outside the hot pixel loop.  The normal sensor-inclusive
MTF path therefore has one periodic-screen lookup per source pixel and no
per-pixel sampling-policy branch.

### 4. Fallback full-pixel integration

`antialias_screen()` is used only when the preceding transfer excludes the
sensor aperture.  It integrates the complete capture-pixel footprint with a
separable five-point Gauss--Legendre rule.

The one-dimensional nodes, already transformed to `[-0.5, +0.5]`, are

```text
-0.453089922969332
-0.269234655052842
 0
+0.269234655052842
+0.453089922969332
```

and the normalized weights are

```text
0.118463442528095
0.239314335249683
0.284444444444444
0.239314335249683
0.118463442528095
```

The two-dimensional weight is the product of the two one-dimensional weights.
The mapping is treated as locally affine across one capture pixel, as in the
historical implementation.  The centre and the two unit image directions
therefore require three `to_scr()` transformations, followed by 25 periodic
transmission lookups.

For a continuous sinusoid, the exact square-pixel transfer along one axis is

```text
sin(pi f) / (pi f).
```

Through Nyquist, the former equal-weight rule over `[-1/3,+1/3]` had a maximum
absolute error of about `0.10979`; the five-point Gauss--Legendre rule has a
maximum error of about `3.51e-8`.  At one cycle per pixel the former rule
incorrectly retained `0.2`, while the new rule is about `3.08e-5` from zero.
The practical unit test also integrates the actual piecewise-bilinear periodic
screen representation against a dense midpoint reference.

### 5. Use by colour-loss estimation

`determine_color_loss()` in `finetune.C` either:

- reads the already rendered `simulated_screen`; or
- renders and filters a local temporary image directly.

The explicit `screen_sampling` argument is used in both the unfiltered and
filtered local paths.  Consequently, colour-loss estimation and full-image
screen simulation use the same sensor-aperture ownership rule.

The function accumulates the average captured RGB values assigned to red,
green, and blue collection regions.  These values form the colour-loss
compensation matrix used by the renderer.

All finite-image coordinates in this stage are `(x, y)`.  Periodic
`screen::mult` storage remains `[y][x][channel]` internally.

## Stage-ownership table

| Physical operation | Owner | Status |
|---|---|---|
| Ideal periodic pattern | `screen::initialize()` | correct |
| Legacy Gaussian screen spread | `get_new_screen()`, when selected | correct |
| Lens/diffraction/defocus MTF | `screen::initialize_with_sharpen_parameters()` | correct |
| Measured capture MTF | `screen::initialize_with_sharpen_parameters()` | correct |
| Sensor aperture in physical MTF | `mtf_parameters::sensor_mtf()` when enabled | correct |
| Sensor aperture in measured MTF | measurement itself | correct by measurement contract |
| Explicit pixel-footprint integration | finite sampler, only when transfer excludes aperture | correct |
| Discrete finite sampling | selected `simulate.C` callback | correct |
| Digital sharpening | `render_simulated_screen()` | correct |
| Colour collection | `determine_color_loss()` | correct |

## Issue register

### SIM-001 — sensor aperture could be applied twice

**Severity:** high correctness and high performance impact

**Status:** fixed by the stage-ownership patch

The periodic screen can already contain sensor-aperture attenuation through a
measured MTF or the analytical model with `sensor_fill_factor > 0`.  The old
finite sampler nevertheless always integrated another pixel footprint.

The code now carries `screen_sampling` from periodic-screen construction to
finite rendering and colour-loss estimation.  Sensor-inclusive paths use one
point sample; aperture-exclusive paths use explicit integration.  The policy
is part of the finite-image cache key.

For a unit-fill sensor at axial Nyquist, the intended one-aperture response is
about `0.63662`.  The former additional partial-pixel rule multiplied it by
about `0.74641`, reducing the resulting transfer to about `0.47518`.  Thus the
old path discarded roughly another 25.4% of the surviving Nyquist contrast
before digital sharpening.  The exact effect depends on fill factor, mapping,
and frequency direction, but it was not a negligible numerical perturbation.

### SIM-002 — fallback integration covered only part of a pixel

**Severity:** medium correctness

**Status:** fixed by the stage-ownership patch

The old five equally weighted offsets covered approximately
`[-1/3,+1/3]` rather than the complete pixel.  The fallback now uses a
five-point Gauss--Legendre rule over `[-1/2,+1/2]` in both dimensions, retaining
the same 25 periodic-screen evaluations while greatly improving aperture MTF
accuracy.

### SIM-003 — signed physical OTF was clipped to an MTF magnitude

**Severity:** high for sufficiently defocused captures

**Status:** fixed by the signed-OTF patch

The exact circular-pupil calculation already produced a signed pupil
autocorrelation, but `lens_defocus_mtf()` took its absolute value before the
transfer reached the filtering code.  The small periodic-screen FFT, the
large-image deconvolver, and PSF reconstruction then also clamped transfer
coefficients to `[0,1]`.  A sufficiently defocused known pupil therefore lost
real phase reversals.

The public model now separates the two semantics explicitly:

- `lens_defocus_otf()`, `lens_otf()`, `sensor_otf()` and `system_otf()` return
  the signed, real zero-phase transfer of the analytical model;
- the corresponding `*_mtf()` accessors return magnitudes for fitting, charting
  and comparison with slanted-edge data.

`mtf::precompute()` stores the signed `system_otf()` only when evaluating the
known analytical model.  Measured slanted-edge tables still enter the cache as
nonnegative magnitudes and no phase is invented for them.  All FFT consumers
accept the physical range `[-1,+1]`.  The Wiener inverse therefore uses the
correct signed relation `H/(H^2+K)`, while forward blur naturally reverses the
contrast of a sinusoid in a negative OTF lobe.

A regression using the Hurley capture geometry with 0.5 mm image-plane
defocus verifies a negative lobe near 0.1875 cycles/pixel through both the
large-image deconvolver and the 128 x 128 periodic-screen FFT.  A matching
synthetic measured MTF remains nonnegative.  A 128 x 128 PSF reconstructed from
the same signed analytical OTF is finite and nonnegative in the tested case.

### SIM-004 — cache key omitted finite dimensions

**Severity:** high memory safety/correctness

**Status:** fixed in the first-pass patch

`simulated_screen_params::operator==()` compares `width` and `height`.  A
screen generated for one geometry can no longer be returned for another.

### SIM-005 — cache key used approximate sharpening equality

**Severity:** medium correctness

**Status:** fixed in the first-pass patch

A complete simulated image uses `sharpen_parameters::equal_p()` instead of the
interactive approximate comparison.  A small MTF-scale change therefore
cannot silently reuse stale finite pixels.

### SIM-006 — finite-image interpolation produced a black border

**Severity:** medium visible artifact

**Status:** fixed in the first-pass patch

The historical function assigned a result only when a complete `4 x 4`
bicubic neighbourhood existed.  It now clamps the coordinate to the finite
image, keeps bicubic interpolation in the interior, and uses clamped bilinear
interpolation at boundaries.

### SIM-007 — X/Y transpositions in finite-image access

**Severity:** high correctness; possible out-of-bounds access

**Status:** fixed in the first-pass patch

Both `render_to_scr::get_simulated_screen_pixel_fast()` and the
`simulated_screen` branch of `determine_color_loss()` pass `(x, y)` to
`get_pixel()`.

### SIM-008 — failed or cancelled render could enter the cache

**Severity:** high correctness

**Status:** fixed in the first-pass patch

`render_simulated_screen()` returns `bool`.  Invalid mapping, filter failure,
cancellation, and diagnostic-output failure propagate through
`get_new_simulated_screen()`.  The LRU implementation removes a cache entry
whose generator returns null.

### SCR-001 — normalized preview used the red divisor for all channels

**Severity:** medium visible colour error

**Status:** fixed in the first-pass patch

`screen::get_image()` normalizes red, green, and blue independently.
`get_image()` and `save_tiff()` share finite, zero-safe normalization logic.

### SCR-002 — empty patch proportions produced NaNs

**Severity:** medium numerical failure

**Status:** fixed in the first-pass patch

`screen::patch_proportions()` accumulates and normalizes in `double`, checks all
sums with the fast-math-safe `my_isfinite()`, and returns zero proportions when
total transmission is not positive and finite.

### SCR-003 — optical filtering could lose preview `add` data

**Severity:** low/medium preview inconsistency

**Status:** fixed in the first-pass patch

The MTF and point-spread initialization paths copy `screen::add` from their
source.  Only multiplicative transmission is filtered.

### SCR-004 — FIR allocation failure tested the wrong pointer

**Severity:** high on allocation failure

**Status:** fixed in the first-pass patch

`fir_blur::gen_convolve_matrix()` checks `*cmatrix` after allocation and rejects
a null output pointer.  Callers without a failure-return channel fall back to
the unblurred source or an identity transfer instead of dereferencing a null
kernel.

### SCR-005 — templated FFT helpers instantiated the wrong type

**Severity:** latent precision/type defect

**Status:** fixed in the first-pass patch

The generic two-dimensional and Richardson--Lucy paths call
`scale_by_weights<T>()`, not `scale_by_weights<screen_fft_t>()`.

### SCR-006 — Gaussian MTF helper contained a permanently true branch

**Severity:** low maintainability; potential future model divergence

**Status:** fixed in the first-pass patch

The `|| 1` branch and unreachable analytical implementation were removed.  The
helper explicitly transforms the same truncated FIR kernel as the direct blur
path, preserving the existing blur-regression contract.

### SCR-007 — failed PSF/MTF construction must invalidate the screen

**Severity:** medium/high on cancellation, allocation failure or malformed model

**Status:** fixed

PSF sums and FFT DC normalizers are checked before division, and
`screen::initialize_with_sharpen_parameters()` now returns `bool`.  A failure
may leave the destination only partly initialized; deliberately no second
`screen` is allocated or copied merely to make this operation transactional.
The API contract instead requires the caller to discard or ignore the
destination whenever the function returns false.

All current callers propagate that failure.  Cached periodic screens are not
inserted, spatially varying screen tables are marked invalid, and finetuning
paths stop using the affected result.  This keeps the normal performance- and
memory-critical path copy-free while making failure handling explicit.

### SIM-009 — one global pixel scale is used across a warped image

**Severity:** model limitation

**Status:** open, lower priority

The periodic screen is blurred once using a representative `pixel_size()`.
Strong perspective, mesh deformation, or local magnification would require a
spatially varying capture transfer, probably evaluated per image tile.

### SIM-010 — periodic-table interpolation has its own small MTF

**Severity:** precision limitation

**Status:** open, lower priority

Bilinear lookup of the `128 x 128` periodic table introduces a tent response.
For lens-limited 2000--5000 PPI captures this is usually secondary, but it
should be measured against the highest screen harmonics that survive the
capture MTF.

## Numerical and cache invariants

1. Optical and screen calculations are performed in linear light.
2. Every capture transfer is applied exactly once.
3. A measured MTF never supplies unknown phase or signed OTF lobes.
4. A known symmetric physical OTF may retain its sign.
5. Pixel-aperture ownership is explicit and is never inferred in a pixel loop.
6. Digital sharpening occurs after finite sampling.
7. `screen` coordinates are periodic; `simulated_screen` coordinates are
   finite and ordered `(x, y)`.
8. Failed or cancelled calculations do not populate caches.
9. Cache equality includes every parameter capable of changing a stored
   pixel, including the sampling policy.
10. Sums, normalizers, and transfer preparation use `double`; compact image
    storage may remain `float` where measured error justifies it.
11. Runtime finiteness checks use `my_isfinite()`, because the project builds
    with `-Ofast -ffinite-math-only`.

## Performance constraints and measurements

The blurred-screen path is performance critical.  The implementation follows
these rules:

- no per-output-pixel allocation or virtual dispatch;
- an inlined source callback is selected once by a template instantiation;
- point sampling is used whenever aperture response is already in the capture
  transfer;
- fallback integration uses compile-time nodes and weights;
- the small `128 x 128` optical FFT and reductions remain in `double`;
- sampling is benchmarked separately from full-image FFT sharpening.

A single-thread benchmark of the actual periodic-screen lookup functions on a
`1024 x 768` Dufay screen gave:

| Source operation | Lookups/pixel | Throughput |
|---|---:|---:|
| Point sample | 1 | 70.29 MPix/s |
| Former partial-pixel 5 x 5 rule | 25 | 5.69 MPix/s |
| Full-pixel Gauss--Legendre 5 x 5 rule | 25 | 5.04 MPix/s |

Point sampling is about `12.34x` faster than the old unconditional integration
in this isolated callback benchmark.  The accurate fallback integration is
about `13%` slower than the old partial rule, but it is now used only for
aperture-exclusive paths.

A second single-thread benchmark exercised the complete
`get_new_simulated_screen()` path after warming FFTW plans and MTF caches:

| Complete finite-screen operation | Dimensions | Throughput | Point/integrated speedup |
|---|---:|---:|---:|
| No digital sharpening, point sample | 1024 x 768 | 31.89 MPix/s | 18.69x |
| No digital sharpening, integrated | 1024 x 768 | 1.71 MPix/s | -- |
| Hurley-like Wiener, 2x Lanczos-3, point sample | 384 x 256 | 0.816 MPix/s | 2.53x |
| Hurley-like Wiener, 2x Lanczos-3, integrated | 384 x 256 | 0.323 MPix/s | -- |

The Wiener case used the physical metadata `1887 PPI`, `f/8`, `750 nm`, and
`3.760 um` sensor pitch.  It demonstrates the expected smaller, but still
substantial, end-to-end improvement once supersampled FFT processing dominates
the runtime.  These figures are local comparative measurements, not portable
absolute throughput claims.

## Regression coverage

The `screen_simulation` unit group verifies:

- finite width and height participate in cache equality;
- exact MTF parameters participate in cache equality;
- sampling policy participates in cache equality;
- measured MTFs select point sampling after application;
- analytical sensor-inclusive MTFs select point sampling;
- aperture-exclusive or unapplied transfers select integration;
- the full-pixel quadrature agrees with a dense reference integral;
- the former partial-pixel rule is distinguishable by the same test;
- finite rendering obeys both explicit sampling policies;
- all four finite-image borders replicate valid image data instead of black;
- non-square images preserve `(x, y)` ordering;
- red, green, and blue normalize independently;
- an all-zero screen produces finite black preview data and zero patch
  proportions;
- known patch proportions are accumulated accurately;
- `screen::add` survives MTF and point-spread filtering.

The complete optimized unit suite and Autotools testsuite must pass before this
stage contract is changed further.

## Planned implementation sequence

1. **First pass, complete:** tracing, cache identity, coordinate ordering,
   finite borders, failure propagation, zero-safe display and proportions,
   preview-data preservation, allocation check, and template/dead-branch
   cleanup.
2. **Stage ownership, complete:** explicit point versus integrated sampling,
   cache-key update, full-footprint Gauss--Legendre integration, and shared
   ownership in colour-loss estimation.
3. **OTF semantics, complete:** separate known signed physical OTF from
   measured nonnegative MTF and preserve physical phase reversals in every FFT
   path.
4. **Filtering robustness:** make periodic-screen filtering transactional so a
   failed per-channel MTF/PSF calculation cannot leave a partially filtered
   screen.
5. **Precision validation:** quantify the periodic-table interpolation MTF,
   local mapping scale, and representative Phase One / Schneider screen
   harmonics.
