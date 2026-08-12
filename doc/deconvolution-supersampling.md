# Deconvolution supersampling and reconstruction kernels

This document describes the supersampling stage used by Color-Screen's Wiener,
Richardson--Lucy, and forward-blur deconvolution paths.  It explains the
geometry, the selectable reconstruction kernels, the performance trade-off,
the numerical-precision policy, and the project-file representation.

## 1. Position in the deconvolution pipeline

For a supersampling factor `S > 1`, each input tile is processed as follows:

1. reconstruct a fine grid with `S` samples per original-pixel interval in each
   direction;
2. taper the tile boundary;
3. apply the optical transfer or inverse transfer with a two-dimensional FFT;
4. sample the processed fine grid at the centers of the original pixels.

The optical model and Wiener regularizer are independent of the reconstruction
kernel.  The kernel controls only the first reconstruction step.  The final
sampling step uses an exact fine-grid center for odd factors and the established
Catmull--Rom midpoint reconstruction for even factors.

Input pixel centers are located at `j + 1/2`.  Fine-grid sample `i` is located,
in input coordinates, at

```
(i + 1/2) / S.
```

The implementation derives every phase from an exact integer numerator and
denominator before converting to floating point.  This is important with the
project's `-Ofast` configuration: algebraic reassociation previously turned an
exact zero phase into a tiny negative value, after which `floor()` selected the
wrong source sample.

## 2. Available kernels

The Sharpness panel exposes **Supersampling kernel** whenever supersampling is
greater than one.

### Lanczos 3 (faster)

Lanczos 3 has radius three and six nonzero taps per one-dimensional pass.  It is
the default.  This is normally the best choice for Color-Screen's real capture
workload:

- scans are commonly 2000--5000 PPI;
- the Schneider macro lens and diffraction substantially attenuate the spectrum
  near sampled Nyquist;
- preserving a mathematically valid but optically negligible FFT-corner band is
  less useful than reducing interactive and export time.

The six nonzero coefficients are stored in an eight-slot phase record.  The
last two entries are zero.  This deliberate padding lets the compiler map the
inner product to common SIMD vector widths; a literal six-iteration double loop
was markedly slower on the benchmark host.

### Lanczos 8 (maximum precision)

Lanczos 8 has radius eight and sixteen taps per pass.  It better preserves
signals very close to two-dimensional Nyquist, especially diagonal signals.  It
is useful for synthetic validation, unusually sharp critically sampled data,
or comparisons where the spectrum close to the FFT corners matters.

For lens-limited scans it normally changes little in the useful passband, while
performing more coefficient multiplications and memory reads.

## 3. Kernel definition and normalization

For radius `a`, the one-dimensional kernel is

```
L_a(x) = sinc(x) sinc(x/a),  |x| < a,
       = 0,                  otherwise,
```

where

```
sinc(x) = sin(pi x) / (pi x)
```

and `sinc(0) = 1` by continuity.

A separate kernel is precomputed for every supersampling phase.  Each phase is
normalized in double precision so that its coefficients sum to one.  This
preserves a constant image exactly up to the final conversion to the FFT data
type.

The previous generic `lanczos_kernel()` helper used `luminosity_t`, which can be
single precision.  The deconvolution path now evaluates `sinc` and all phase
coefficients directly in `double`; this avoids silently rounding a nominally
double resampling path to float.

## 4. Cache-friendly separable reconstruction

Reconstruction is separable.  The horizontal pass creates an intermediate
image with dimensions

```
original tile height x enlarged tile width.
```

The vertical pass then processes complete output rows.  For each output row it
combines the required intermediate rows while traversing all columns
contiguously.

The previous implementation processed one column at a time:

1. gather a strided column into a temporary vector;
2. resample the vector;
3. scatter the result back into a strided output column.

That design repeatedly crossed cache lines and allocated a temporary vector for
every tile.  The row-wise vertical pass removes the per-tile allocation and
turns the dominant second pass into contiguous reads and writes.  It accelerates
both selectable kernels; the shorter kernel provides an additional saving.

The cost is one per-thread intermediate buffer.  Its size is

```
original tile height x enlarged tile width x sizeof(pixel type),
```

which is smaller than the square enlarged FFT image for every supersampling
factor greater than one.

## 5. Tile border and FFT size

The unsharpened tile border is the maximum of

- the optical PSF support, including the taper policy; and
- the selected reconstruction-kernel radius.

The enlarged FFT dimension is the next power of two satisfying the complete
border requirement.

Consequently, selecting a shorter kernel can produce a much smaller FFT only
when the optical PSF is extremely compact.  For the representative Hurley
physical model (1887 PPI, f/8, 750 nm, 3.760 micrometre sensor pitch), the
optical/taper border is 48 original pixels.  Both radius 3 and radius 8 are
smaller than that border, so they use the same FFT tile.  In that case the speed
difference comes from reconstruction work, not a smaller Fourier transform.

## 6. Measured performance

The following single-thread measurements used the optimized GCC build on the
review host and the compact Hurley-like physical model.  Each reported value is
the median of three benchmark runs, each of which internally used five timed
batches.  They are intended as relative measurements; absolute throughput
depends on CPU, FFTW planning, compiler, and memory system.

| Supersampling | Former Lanczos 8 | New Lanczos 8 | New Lanczos 3 |
|---:|---:|---:|---:|
| 2x | 7.37 MPix/s | 10.75 MPix/s | 12.56 MPix/s |
| 3x | 3.57 MPix/s | 5.51 MPix/s | 6.24 MPix/s |
| 4x | 1.50 MPix/s | 2.38 MPix/s | 2.73 MPix/s |

At 2x, the row-wise vertical pass makes Lanczos 8 approximately 46% faster
than the preceding implementation.  Selecting Lanczos 3 provides another 17%
over the new Lanczos-8 path and about 71% over the former implementation.

A controlled build of the former column-gather implementation with radius
three reached only 4.32 MPix/s at 2x.  The six-tap loop vectorized poorly, so
the old radius-eight loop was paradoxically about 70% faster despite doing more
arithmetic.  The new Lanczos-3 phase table therefore pads six nonzero taps to
eight slots.  This comparison is important: the observed performance change
cannot be attributed to kernel radius alone; memory access and generated SIMD
code were at least as important.

Supersampling remains intrinsically expensive.  A factor `S` increases the
fine-grid area approximately by `S^2`, and FFT cost grows slightly faster than
area.  Kernel selection cannot remove that scaling.

## 7. Frequency-response trade-off

At low and moderate frequencies, both kernels are close.  A synthetic diagonal
signal at `(0.4, 0.4)` cycles/pixel is deliberately outside the regime expected
from the Hurley lens-limited scan but exposes the distinction:

- Lanczos 3 attenuates this near-corner signal appreciably;
- Lanczos 8 preserves it much more accurately.

The unit test therefore uses Lanczos 8 for its explicit near-Nyquist diagonal
regression and verifies both kernels for low-frequency identity reconstruction.
This makes the quality trade-off intentional rather than accidental.

## 8. Precision policy

The following quantities remain in `double`, even when the large image FFT uses
single precision:

- phase location;
- Lanczos coefficients and phase normalization;
- each one-dimensional resampling sum;
- physical MTF and Wiener denominator calculations.

The reconstructed pixel is converted to the selected FFT data type only after
the double inner product is complete.  Keeping the large FFT image and FFTW
workspace in float avoids roughly doubling their memory footprint and traffic.

Lanczos 3's padded slots are exactly zero and do not change the mathematical
kernel.  They are solely an execution-layout optimization.

## 9. Project files and cache keys

Color-Screen writes

```
deconvolution_supersample: 2
deconvolution_resampling_kernel: lanczos3
```

or `lanczos8` as appropriate.  Older projects contain neither field and load the
historical Lanczos-8 selection so their rendered output does not change.
Newly created parameter sets default to 2x supersampling and Lanczos 3.

The resampling kernel participates in image-cache equality when supersampling is
active.  It is ignored by output-cache equality at 1x because no reconstruction
kernel is evaluated, although exact parameter equality still retains the saved
selection.

## 10. Recommended settings

For normal Phase One or similar lens-limited captures:

- use 2x supersampling as the initial quality/performance point;
- use Lanczos 3;
- increase supersampling only after checking a representative crop;
- use Lanczos 8 only when a visible or measured benefit exists close to
  two-dimensional Nyquist.

When diagnosing speed, inspect the supersampling factor first.  Kernel choice is
important, but the approximately quadratic increase in fine-grid pixels is
usually the dominant scaling term.

## 11. Regression coverage

The unit tests verify:

- project-file round-trip of supersampling factor and kernel;
- image-cache distinction when the kernel is active;
- no cache distinction at 1x;
- constant/low-frequency identity behavior for both kernels at factors 1, 2, 3,
  4, 5, 8, and 16;
- the intentional Lanczos-8 advantage on a near-Nyquist diagonal signal;
- the existing Wiener, Richardson--Lucy, measured-MTF, large-FFT, and noise
  regularization paths.
