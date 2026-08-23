# Slanted-edge robustness and external references

Color-Screen's slanted-edge measurement is intended for practical archival
camera and scanner captures, not only laboratory ISO charts.  A valid ROI may
range from essentially pixel-sharp to seriously out of focus, while texture,
dust, periodic screen structure, curved boundaries, multiple transitions, and
illumination gradients must still be rejected.  Returning no result is safer
than returning a plausible MTF curve for the wrong geometry.

## Localization and qualification

The implementation deliberately separates edge localization from acceptance.
The compact path uses the established 14-pixel, dominant-polarity gradient
centroid.  Opposite-polarity ringing remains visible to the coherence checks but
does not act as positive centroid mass.  A candidate must then pass dominant
transition, polarity, ROI coverage, robust straight-line fit, RMS/p95 residual,
angle, phase-motion, plateau SNR and contrast, ESF monotonicity, primary-LSF
energy, LSF/geometric alignment, and normalized-MTF checks.

A fixed centroid or LSF window nevertheless creates an accidental maximum blur.
The scale-adaptive path is therefore a fallback only: if the compact geometry
fails, Color-Screen estimates the median derivative FWHM after a short
1,4,6,4,1 low-pass and derives wider centroid and competing-edge supports from
that measured width.  Pathological widths occupying more than 35% of the ROI
normal dimension are rejected.  Edges accepted by the compact path keep their
historical geometry and output; the adaptive rules only remove fixed-scale
failure modes.

Synthetic Gaussian transitions currently exercise sigma = 0.35, 1, 4, 8, 12,
16, and 24 pixels.  A pair of broad sigma-16 transitions is still rejected, as
are periodic, textured, curved, gradient, boundary-clipped, and other invalid
ROIs.

## External implementations

MTF Mapper is the closest open-source complete comparator.  Its `--single-roi`
mode is well suited to cropped razor edges and its LOESS ESF model is useful for
independent numerical comparison.  Its single-ROI mode assumes that the caller
has actually supplied one edge, whereas Color-Screen deliberately keeps
stronger fail-closed ROI qualification.

Imatest's published comparison of centroid, low-pass, and matched-filter edge
localization is particularly relevant: filtering can improve noisy edge
location, but smoothing must not be allowed to hide a second transition.
SFRMAT-style implementations are useful references for ESF/LSF/SFR calculation
once edge geometry is known.  FidMTF is useful for its per-transect fitting and
outlier-removal ideas on low-quality imagery.  None of these removes the need
for Color-Screen's explicit arbitrary-ROI qualification.

A compact Canny/ordinary-least-squares implementation (`u-onder/mtf.py`) was
also tested against Color-Screen fixtures.  It handled the sharp Nikon channels
but missed most deliberately defocused channels and most real Phase One edges;
it also accepted several adversarial ROIs and could return plausible MTF50 even
with badly wrong edge geometry.  A successful FFT is therefore not itself a
quality certificate.

## Epson V850 real severe-defocus regression

`testsuite/epson-v850-3200dpi-defocused-edge.tif` is a 48x48 crop of the green
scanner channel from an Epson V850 scan at 3200 DPI.  The scanner was focused on
its film-holder plane while the razor blade was placed directly on the glass
bed, deliberately producing a visibly severe focus error.

The current estimator measures approximately:

- edge angle: 6.87 degrees;
- line-fit RMS: 0.087 pixel;
- phase coverage: 99%;
- MTF50: 0.059 cycles/pixel, about 7.4 lp/mm at 3200 DPI.

This real edge is already accepted by the compact locator.  It therefore guards
a different property from the synthetic sigma-16/24 cases: future adaptive
changes must not perturb a badly focused real scan that the historical compact
path could already measure.

## Measured-MTF Wiener ringing

The same Epson measurement exposes a separate sharpening problem.  Applying
Wiener sharpening directly with the measured curve produces a very visible
oscillatory wave on both sides of the razor edge.  That artifact should not be
"fixed" by changing edge localization merely to alter the measured curve.  A
strongly low-pass transfer, finite measurement/window support, and small
high-frequency structure can give an inverse filter a ringing spatial response
even when the forward MTF is measured correctly.

A follow-up sharpening regression should inspect the step/impulse response of a
measured-MTF Wiener filter and define an explicit ringing or transfer-shaping
policy.  Until that policy is specified, the Epson fixture validates
measurement and geometry only; it intentionally does not bless the appearance
of the sharpened razor edge.
