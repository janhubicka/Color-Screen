# Physical MTF model, measured SFR, and deconvolution

This document describes the optical-transfer model implemented by
`src/libcolorscreen/mtf.C`, the parameter definitions in
`src/libcolorscreen/include/mtf-parameters.h`, the conversion of that model to
blur and sharpening filters in `deconvolve.C`, and the slanted-edge measurement
path in `slanted-edge.C`.

The intended reference case is a high-quality, linear, monochrome or
narrow-band camera capture of film made with a Phase One-class sensor and a
macro lens. The first worked example is the Hurley infrared scan:

| quantity | value |
|---|---:|
| object-space scan resolution | 1887 DPI |
| marked aperture | f/8 |
| wavelength | 750 nm |
| sensor pixel pitch | 3.760 µm |

The implementation deliberately has three distinct modes:

1. **Physical diffraction model.** This is the primary model whenever pixel
   pitch, scan DPI, marked f-number, and wavelength are known. It contains
   circular-pupil diffraction, exact wave-optical defocus, the sensor aperture,
   an optional residual core Gaussian, and an optional broad-scatter halo.
2. **Measured MTF/SFR.** A sampled curve can be used directly for sharpening.
   Since a measured slanted-edge curve contains magnitude but not phase, it is
   interpreted as a radial, real, nonnegative transfer function.
3. **Metadata-free fallback.** If the physical metadata is unavailable, a
   Gaussian multiplied by the transfer of a uniform circular blur disk is
   available as a compact empirical approximation. It is not used when the
   physical model is active.

Keeping these modes separate is important. A fitted blur-disk diameter must not
silently replace known diffraction physics, and diffraction must not be applied
a second time to a measured curve that already includes it.

## 1. Frequency, planes, and units

All public scalar frequencies are in **cycles per output image pixel**. In the
normal camera-scanning workflow one output pixel corresponds to one sensor
pixel; the TIFF DPI then describes the size of that pixel in the photographed
film plane.

The physical parameters use:

- sensor pixel pitch in micrometres;
- wavelength in nanometres;
- image-plane focus displacement in millimetres;
- scan resolution in dots per inch;
- dimensionless f-number.

The object-space pitch represented by one output pixel is

\[
p_o = \frac{25400}{D}\quad\text{micrometres},
\]

where \(D\) is the scan DPI. If one output pixel is one sensor pixel, the
reproduction ratio is

\[
m = \frac{p_s}{p_o},
\]

where \(p_s\) is the sensor pitch.

For the Hurley scan,

\[
p_o = \frac{25400}{1887} = 13.4605193429\ \mu\mathrm m,
\]

and

\[
m = \frac{3.760}{13.4605193429} = 0.2793354331.
\]

This convention is correct only if the capture was not resampled between the
sensor data and the image passed to Color-Screen. If the image was resized,
`scan_dpi` must describe the effective object-space resolution of the current
pixels and `pixel_pitch` alone is no longer sufficient to infer the original
camera magnification.

## 2. Working f-number at macro magnification

The model currently uses the thin, symmetric-lens approximation

\[
N_\mathrm{eff} = N(1+m),
\]

where \(N\) is the marked f-number. For the Hurley geometry this gives

\[
N_\mathrm{eff} = 8(1+0.2793354331)=10.2346834646.
\]

This is an image-side working f-number. It is the value used both for the
incoherent diffraction cutoff and for converting image-plane displacement to
defocus phase.

A real macro lens can have pupil magnification different from one. A more
general first-order relation is commonly written in the form

\[
N_\mathrm{eff}=N\left(1+\frac{m}{P}\right),
\]

where \(P\) is pupil magnification under the convention used by the lens data.
Color-Screen does not yet have a reliable Schneider-lens pupil-magnification
parameter, so the current formula must be understood as an explicit
approximation rather than an exact property of every macro lens. For demanding
calibration, measured working aperture or lens-specific pupil data is preferable
to fitting wavelength or nominal f-number away from known metadata.

## 3. Diffraction-limited circular pupil

For incoherent imaging through an unobstructed circular pupil, the normalized
optical transfer function is

\[
D(q)=\frac{2}{\pi}\left(\arccos q-q\sqrt{1-q^2}\right),
\qquad 0\le q<1,
\]

with \(D(0)=1\) and \(D(q)=0\) for \(q\ge1\).

The normalized frequency is

\[
q = \frac{f}{f_c},
\qquad
f_c=\frac{1}{\lambda N_\mathrm{eff}},
\]

where all lengths use the same unit. In cycles per output pixel,

\[
f_{c,\mathrm{px}}
=\frac{p_s}{\lambda N_\mathrm{eff}}.
\]

For the Hurley values,

\[
f_{c,\mathrm{px}}=0.4898376536\ \text{cycles/pixel}.
\]

Thus the ideal optical cutoff lies just below the axial sampled Nyquist
frequency of 0.5 cycles/pixel. This makes the example particularly sensitive to
unit errors, working-aperture errors, and premature floating-point rounding.

The implementation evaluates the square root as
`sqrt((1-q)*(1+q))`, and uses a small-cutoff expansion when appropriate. This
avoids avoidable cancellation in `1-q*q` and in the subtraction of nearly equal
terms close to \(q=1\).

## 4. Exact wave-optical defocus

### 4.1 Why the old factor was replaced

The previous physical path multiplied the diffraction MTF by a
Stokseth/Bessel-style factor

\[
\frac{2J_1(z)}{z}.
\]

That approximation has the correct zero-defocus limit and is inexpensive, but
it replaces the actual lens-shaped overlap of two shifted circular pupils by a
simpler circular-blur transform. Its largest error occurs around defocus minima,
where inverse filtering is especially sensitive to small model errors.

The historical function remains available in diagnostic curves, but the
production physical model now evaluates the circular-pupil autocorrelation
directly.

### 4.2 Image-plane displacement and phase

Let `defocus` be the axial displacement of the sensor from the paraxial image
plane, in millimetres. In the small-angle approximation, the quadratic pupil
phase at normalized pupil radius \(\rho\) is

\[
\phi(\rho)=\frac{\pi\Delta z}{4\lambda N_\mathrm{eff}^2}\rho^2.
\]

For an incoherent OTF sample at normalized spatial frequency \(q\), the two
pupils are displaced by \(2q\). The phase difference across their overlap is
linear in the overlap coordinate. The phase reached at the edge of the overlap
is

\[
z = \frac{\pi\Delta z}{\lambda N_\mathrm{eff}^2}q(1-q).
\]

This is the `edge_phase` computed in `lens_defocus_mtf()`.

### 4.3 Pupil-autocorrelation integral

After symmetry reduction, the signed defocused OTF can be written

\[
H(q,z)=\frac{4}{\pi}
\int_0^{1-q}\sqrt{1-(x+q)^2}
\cos\left(\frac{zx}{1-q}\right)\,dx.
\]

At zero defocus this is exactly \(D(q)\). Color-Screen evaluates the defocus
**factor** \(H(q,z)/D(q)\), then multiplies by the diffraction term. The current
public physical model takes the magnitude because the rest of the program is
expressed as MTF rather than signed OTF. The internal integral remains signed,
which makes the implementation and validation valid across phase reversals.

The direct integral has a square-root endpoint. The substitution

\[
x=(1-q)(1-u^2)
\]

produces the smooth integral

\[
I(q,z)=\int_0^1 u^2\sqrt{2-(1-q)u^2}
       \cos\left(z(1-u^2)\right)\,du,
\]

and the defocus factor is simply \(I(q,z)/I(q,0)\). Common scale factors cancel.

`mtf.C` evaluates this expression with a composite 16-point Gauss--Legendre
rule. A panel spans at most approximately one phase cycle. Nodes, weights,
phase, and accumulation use `long double`; conversion to `double` occurs only
at the API boundary.

The independent `testsuite/validate-mtf-physical-model.py` script compares this
rule with an 80-decimal-digit `mpmath` integral. On its validation grid the
largest observed factor error is below \(10^{-18}\). On the same grid the old
Bessel approximation differs from the exact factor by as much as approximately
0.0534.

## 5. Residual core Gaussian

Even a high-quality macro lens is not an ideal defocused circular pupil. Small
residual aberrations, conversion filtering, camera shake, imperfect target
flatness, and weak resampling can often be represented by a compact Gaussian
PSF. Its MTF is

\[
G(f;\sigma)=\exp(-2\pi^2\sigma^2f^2),
\]

where \(\sigma\) is in output pixels.

In the physical model this is a **residual correction**, not a replacement for
diffraction. With no optional halo, the lens model is

\[
H_\mathrm{lens}(f)
=D(f)\,F_\mathrm{defocus}(f)\,G(f;\sigma).
\]

A fitted nonzero `sigma` should therefore be interpreted as unmodelled compact
blur after the known diffraction and focus terms have been accounted for. It
is not a direct estimate of a single physical lens aberration.

## 6. Optional broad-scatter halo

The Hurley curves have a low-frequency shoulder that cannot be reproduced well
by one compact diffraction/defocus/Gaussian core. Color-Screen now has an
optional, explicitly enabled broad component

\[
S(f)=(1-h)+h\exp(-2\pi^2\sigma_h^2f^2),
\]

where \(h\) is the scattered energy fraction and \(\sigma_h\) is a broad radius
in output pixels. The physical lens MTF becomes

\[
H_\mathrm{lens,total}(f)=H_\mathrm{lens,core}(f)S(f).
\]

In PSF terms, this means the core image is convolved with

\[
(1-h)\delta+hG_{\sigma_h}.
\]

This can represent weak flare, scattering, cover-glass diffusion, target or
illumination halo, or another broad shift-invariant component. Command-line
fitting remains opt-in through `--fit-halo`, or through explicit
`--halo-fraction` and `--halo-sigma` values. The graphical fitting dialog shows
the choices explicitly and enables halo fraction and radius optimization by
default. Because zero halo fraction is an allowed fitted result, the default
does not require a broad component.

When the halo width is estimated, the solver constrains it to remain broader
than the compact Gaussian. This prevents the two components from exchanging
labels. The halo is part of the physical branch only; setting halo parameters
does not modify the metadata-free fallback model.

The halo should not automatically be interpreted as a lens property. A finite
target edge, veiling flare, local illumination gradient, nonlinear tone curve,
or slanted-edge analysis convention can create a similar low-frequency shape.
It is best regarded as an optional residual component whose stability should be
checked across several edges and captures.

## 7. Sensor aperture

The first-cut sensor term is

\[
H_\mathrm{sensor}(f)
=\left|\operatorname{sinc}\left(f\sqrt{a}\right)\right|,
\]

where `sensor_fill_factor = a` is interpreted as active area relative to total
pixel area. For `a = 1`, the active width equals one pixel pitch and the MTF at
axial Nyquist is \(2/\pi\).

This is deliberately radial in the current implementation. A square pixel is
really separable,

\[
H(f_x,f_y)=\operatorname{sinc}(w_x f_x)
           \operatorname{sinc}(w_y f_y),
\]

not radial. Directional sensor modelling is deferred in this revision because
the intended 2000--5000 PPI captures are generally lens-limited. The scalar
model is exact along a sensor axis and is a reasonable first approximation at
lower frequencies.

## 8. Complete physical and fallback models

The primary physical system MTF is

\[
H_\mathrm{system}(f)=H_\mathrm{sensor}(f)
D(f)F_\mathrm{defocus}(f)G(f;\sigma)S(f).
\]

The fallback, used only when physical metadata is unavailable, is

\[
H_\mathrm{fallback}(f)=G(f;\sigma)
\left|\frac{2J_1(\pi d f)}{\pi d f}\right|,
\]

where \(d\) is a blur-disk diameter in pixels.

The optional physical halo is intentionally absent from the fallback. This
keeps the meaning of the two modes clear and prevents a saved physical
calibration from unexpectedly changing a metadata-free calculation.

## 9. Parameter fitting

### 9.1 Values and fit intent are separate

The explicit fitting API no longer assigns two meanings to numeric zero.
`mtf_parameters` contains physical values and starting estimates, while
`mtf_estimation_options` contains the independent Boolean choice of which
values are free. Consequently:

- zero residual sigma is a valid fixed value;
- zero image-plane defocus is a valid fixed in-focus value;
- zero sensor fill factor disables the first-cut sensor-aperture term;
- zero halo fraction disables the optional broad component;
- zero fallback blur diameter is a valid fixed value;
- a missing f-number or measurement wavelength is invalid in the physical
  model and therefore must be optimized or supplied;
- scan DPI and sensor pixel pitch are required metadata and are not inferred
  from one radial MTF curve.

The GUI mirrors the library validator. It presents a value and an **Optimize**
checkbox separately; a missing f-number or wavelength forces the checkbox on
and prevents it from being disabled. Known f-number and wavelength values
remain fixed unless the user explicitly chooses otherwise. Residual sigma,
defocus, halo fraction, and halo radius are initially free in the physical
workflow; the fitted halo fraction may still converge to zero. The resulting
halo values are also displayed beside sigma and defocus in the Sharpness panel.

Each measured curve has an authoritative wavelength independent of its channel
label. For compatibility with old files, a missing measurement wavelength may
fall back to a per-channel value and then to the global wavelength. A successful
explicit physical fit writes the effective value into every included curve.
This fixes the former bug in which a supplied 750 nm global wavelength could be
treated as unknown and moved to the 1000 nm fitting boundary.

The old `estimate_parameters` overload retains its zero-sentinel convention for
source compatibility. New GUI and library code uses the overload taking
`mtf_estimation_options`. See `doc/mtf-fitting-workflow.md` for the complete GUI,
command-line, persistence, and validation workflow.

### 9.2 Solver order

Pure defocus is even around zero displacement, so its first derivative vanishes
at the in-focus starting point. Beginning with a derivative-based least-squares
solver can therefore leave the fit at a poor boundary solution. The current
sequence is:

1. derivative-free Nelder--Mead search to find a useful basin;
2. optional GSL nonlinear least-squares refinement;
3. retain the refined result only if the true residual norm improves.

All model parameters, residuals, objective accumulation, and finite-difference
work arrays use `double`.

### 9.3 Hurley fit results

Using the QuickMTF reference curve and fixing the supplied metadata at
1887 DPI, f/8, 750 nm, and 3.760 µm gives:

| model | residual core sigma | image-plane defocus | halo fraction | halo sigma | RMS percentage-point error |
|---|---:|---:|---:|---:|---:|
| physical core only | 0.65319 px | 0.17939 mm | 0 | 0 | 2.3245 |
| physical core + optional halo | 0.77464 px | 0 mm | 0.15297 | 5.16562 px | 0.8040 |

The exact parameter split is not unique: compact Gaussian blur and small
defocus are correlated, and a broad halo can change the optimum core width.
The robust conclusions are that the known diffraction cutoff is retained, the
core-only model leaves a systematic low-frequency residual, and an additional
roughly 5--7 pixel broad component containing about 15--18 percent of the
energy accounts for most of that shoulder in the tested Hurley curves.

## 10. Slanted-edge SFR and the QuickMTF difference

A slanted-edge computation measures the complete spatial-frequency response of
its target, capture, processing, and analysis aperture. It is not automatically
the MTF of the lens alone.

Color-Screen's historical default uses the available edge extent and a Hann
window. The bundled QuickMTF LSF covers approximately -20 to +20 pixels and is
closer to a rectangular finite-support analysis. Truncating or tapering an LSF
changes its Fourier transform, especially at low frequencies when the edge has
weak broad wings.

For the bundled Hurley example, comparing Color-Screen directly with the
QuickMTF curve gives approximately:

| Color-Screen analysis | RMS difference up to 0.1 cyc/px | RMS difference up to Nyquist |
|---|---:|---:|
| default full-support Hann | 2.857 pp | 1.785 pp |
| 20-pixel half-width, rectangular | 1.120 pp | 1.528 pp |

At 0.02 cycles/pixel, the default result is about 5.04 percentage points below
QuickMTF, while the finite rectangular result is about 1.36 points below.
This shows that analysis support/window explains a substantial part of the
reported low-frequency discrepancy. It does not prove that all remaining
broad response is an artefact: the default full-support curve may retain real
weak PSF wings that the truncated analysis suppresses.

The default remains unchanged because it has produced useful sharpening on real
data. Explicit compatibility controls are available:

```text
--oversampling=2..64
--edge-half-width=<pixels>
--edge-window=hann|hamming|rectangular
```

A QuickMTF-like diagnostic comparison can use approximately:

```text
--edge-half-width=20 --edge-window=rectangular
```

This is a measurement-aperture choice, not a universal accuracy switch. A
calibration intended for sharpening should use the convention whose inverse
best predicts independent captured detail, and it should record the convention
alongside the curve.

## 11. Measured MTF path

A loaded measured curve is validated before interpolation:

- frequencies and contrasts must be finite;
- frequencies must be strictly increasing;
- contrast must be nonnegative;
- DC is normalized explicitly;
- irregular spacing is preserved;
- values beyond reliable measured support are tapered rather than cut off with
  a discontinuity.

The measured sample structure stores both frequency and contrast in `double`.
An earlier `luminosity_t` argument to `add_value()` silently rounded newly
parsed double data to float even though the container field had been changed to
double; this is now corrected.

A measured MTF already includes diffraction, capture defocus, sensor aperture,
and any processing present in the edge image. The optional measured-data
correction therefore applies only the explicitly requested metadata-free
Gaussian/circular adjustment. It does not multiply diffraction into the curve
a second time.

## 12. From MTF to blur and sharpening

### 12.1 Radial table and PSF

`mtf::precompute()` samples the selected radial transfer into a reusable
interpolating table. `compute_2d_psf()` evaluates the radial transfer on a 2-D
FFT grid and transforms it to a centered PSF. The PSF support is increased until
its tail is below the configured relative threshold.

The production image transform remains single precision by default because a
4096 by 4096 or larger complex double transform approximately doubles the
largest memory and bandwidth costs. The physical model, interpolation,
regularization, normalization, and scalar reductions are prepared in double
and cast only when values are stored in the FFT workspace.

### 12.2 Wiener inverse

For the current real, zero-phase model, the Wiener-style inverse is

\[
W(f)=\frac{H(f)}{H(f)^2+1/\mathrm{SNR}}.
\]

`scanner_snr` is therefore a regularization control expressed as an effective
signal/noise power ratio. It should not necessarily be copied from a camera
manufacturer's headline SNR. A nonpositive or nonfinite value disables Wiener
sharpening and produces identity.

The transfer and denominator are computed in double before the normalized
kernel is cast to the image FFT type. This matters near low MTF values, where
rounding before the division changes inverse gain disproportionately.

### 12.3 Richardson--Lucy

The Richardson--Lucy path uses the forward PSF and its conjugate. It enforces
nonnegative estimates and guards divisions by a type-appropriate epsilon. Zero
iterations produce identity. Because the current physical model supplies MTF
magnitude rather than a complex measured OTF, this iterative inversion still
assumes a centered symmetric PSF.

### 12.4 Tiling and supersampling

Filtering is tiled with reflected borders and a cosine taper. Reflection is
periodic and remains valid even when the requested border is wider than a tiny
image.

Supersampling uses phase-specific Lanczos kernels. Important precision and
correctness details are:

- phase is derived from exact integer numerator/denominator arithmetic, so
  `-Ofast` cannot turn an exact zero into a tiny negative value before `floor`;
- Lanczos coefficients are stored and normalized in `double`;
- resampling sums accumulate in `double`;
- even-factor pixel centres use exact Catmull--Rom midpoint weights
  `(-1, 9, 9, -1)/16` in double;
- only the final sample is converted to the image storage type.

## 13. Floating-point audit

The revision removes several significant precision/correctness hazards:

1. **Known double samples rounded through float.** `mtf_measurement::add_value`
   accepted `luminosity_t`; it now accepts `double`.
2. **Objective and optimizer vectors.** Physical-model fitting now uses double
   parameters, residuals, and sums throughout.
3. **Defocus quadrature.** Nodes, phase, panel sums, and total use long double.
4. **Diffraction cutoff.** Stable factored and asymptotic expressions avoid
   cancellation near \(q=1\).
5. **Wiener preparation.** Frequency, transfer, denominator, normalization, and
   gain are double before storage in the FFT kernel.
6. **FFT DC validation.** The stored DC coefficient contains \(1/N^2\). At
   4096 squared this is below float epsilon; validity is checked after undoing
   FFT normalization rather than comparing the stored coefficient directly.
7. **Lanczos resampling.** Coefficients and accumulation are double.
8. **Cubic downsampling.** The generic helper returned `luminosity_t`, silently
   truncating a nominal `deconvolution<double>` path. Exact midpoint arithmetic
   now remains double.
9. **Fast-math finiteness.** `std::isfinite()` may be optimized incorrectly by
   `-ffinite-math-only`; critical checks use the project's bit-level
   `my_isfinite()` helper.
10. **Persistence.** Physical parameters and measured samples are written with
    enough significant digits to round-trip double values.

The large production FFT and image buffers remain float when instantiated as
`deconvolution<float>`. This is an intentional memory/performance choice, not
an assertion that float and double FFTs are mathematically identical. Unit tests
exercise both instantiations and compare their outputs on representative
signals and supersampling factors.

## 14. Validation and regression tests

The `mtf_model` unit-test group checks:

- the Hurley magnification, working f-number, and cutoff;
- circular-pupil diffraction values and cutoff behavior;
- exact defocus values at selected normalized frequencies;
- DC normalization and high-frequency limit of the optional halo;
- strict separation of the physical and fallback models;
- preservation of authoritative 750 nm per-measurement wavelength metadata;
- independent fixed/optimized handling of valid zero-valued parameters;
- rejection of missing fixed f-number/wavelength, malformed option vectors,
  invalid model selectors, and unanchored f-number/all-wavelength fits;
- activation and project-file round-trip of the explicitly selected model;
- recovery of a synthetic broad halo;
- retention of measured contrast samples in double precision.

The `mtf_deconvolution` group checks:

- irregular measured-table interpolation and DC normalization;
- Wiener and Richardson--Lucy forward/inverse behavior;
- diagonal frequencies under the radial model;
- supersampling factors 1, 2, 3, 4, 5, 8, and 16;
- float-versus-double filtering;
- tiny images and repeated reflection;
- a 4096-squared Phase One-like single-precision kernel path;
- invalid SNR and zero-iteration behavior.

The `slanted_edge` group checks synthetic blurred edges generated from the same
physical model. The command-line tests retain the four analytical Gaussian
cases and the real Hurley image.

The independent validation script can be run as:

```sh
python3 testsuite/validate-mtf-physical-model.py \
  --csv physical-mtf-validation.csv
```

## 15. Recommended use for Phase One / Schneider captures

1. Work from linear raw or linear 16-bit data with sharpening, denoising,
   contrast enhancement, and resampling disabled.
2. Enter the actual output scan DPI, marked f-number, narrow-band wavelength,
   and 3.760 µm sensor pitch.
3. Keep those known quantities fixed during fitting.
4. Fit compact residual sigma, image-plane defocus, halo fraction, and halo
   radius; zero halo fraction remains a valid compact-model result.
5. Retain a nonzero broad halo only if its low-frequency shoulder and approximate
   radius are stable across multiple edges and captures.
6. Compare the historical full-support Hann SFR with a recorded finite-support
   curve. Do not mix curves measured with different support conventions in one
   calibration without labelling them.
7. Validate sharpening on independent fine detail, not only by how closely the
   fitted curve follows the edge from which it was estimated.
8. Use conservative Wiener regularization near the optical cutoff. No increase
   in model precision makes inversion across a genuine OTF zero stable.

## 16. Remaining limitations and next physical improvements

The current model is a substantial improvement over the old product of
ideal diffraction and a Bessel defocus approximation, but it remains a radial,
shift-invariant first-order model. Important remaining limitations are:

- pupil magnification and true working aperture of the specific Schneider macro
  lens;
- finite spectral bandwidth and chromatic focus even in an infrared band;
- aperture shape when the iris is visibly polygonal;
- spherical aberration, coma, astigmatism, field curvature, sensor tilt, and
  cover-glass aberration;
- target-edge MTF and illumination gradients;
- phase information absent from a measured SFR magnitude;
- anisotropic square-pixel aperture, intentionally deferred for this first cut;
- uncertainty propagation from repeated edge measurements into Wiener
  regularization.

The best next physical improvement is not another empirical replacement for the
diffraction core. It is to add a calibrated working-aperture/pupil-magnification
parameter and, when spectral data is available, integrate signed monochromatic
OTFs across the actual infrared band before taking the final magnitude.

## References

- H. H. Hopkins, “The frequency response of a defocused optical system,”
  *Proceedings of the Royal Society A* 231 (1955), 91--103,
  DOI: 10.1098/rspa.1955.0158.
- P. A. Stokseth, “Properties of a defocused optical system,” *Journal of the
  Optical Society of America* 59 (1969), 1314--1321,
  DOI: 10.1364/JOSA.59.001314.
- P. D. Burns, “Slanted-edge MTF for digital camera and scanner analysis,”
  PICS 2000, for the practical edge/SFR framework and finite-analysis effects.
