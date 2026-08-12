# Slanted-edge ROI qualification and failure reporting

## Purpose

A slanted-edge Fourier transform can always produce a smooth-looking curve,
even when the selected rectangle contains no usable edge.  Texture, dust,
several transitions, a nearly grid-aligned edge, or an edge clipped by the ROI
can therefore yield a numerically finite but unrelated MTF.  Such a curve is
more dangerous than an explicit failure because it can subsequently be fitted
and used for deconvolution.

Color-Screen consequently treats edge detection and MTF calculation as two
separate stages:

1. qualify that the ROI contains one measurable straight edge;
2. calculate and store an SFR/MTF only after every qualification succeeds.

A failed call never appends a measurement, and its `edge_histogram` is empty.

## Returned status

`slanted_edge_results::success` remains the compatibility success flag.
`slanted_edge_results::failure` classifies rejection and
`slanted_edge_results::error` contains a human-readable explanation with
measured values where useful.

The result also reports the accepted edge angle, line-fit RMS, robust edge
contrast, plateau SNR, and pre-interpolation ESF-bin coverage.  These quantities
are diagnostics; they do not change the stored MTF.

The GUI displays the detailed failure text.  The command-line utility exits
nonzero and prints:

```text
Slanted edge analysis failed: <reason>
```

## Qualification pipeline

### 1. Work on the unsharpened source

The private render used for measurement disables unsharp masking and
deconvolution.  Linearization and capture corrections remain active.  This
prevents a scanner MTF from being measured from an image already corrected by
that same MTF.

The selected ROI is copied to a double-precision working buffer.  Non-finite
pixels, integer overflow, allocation failure, and ROIs smaller than 24 by 24
pixels are rejected.

### 2. Look for both possible orientations

The detector independently tests:

- a mostly vertical edge represented by `x = A y + B`;
- a mostly horizontal edge represented by `y = A x + B`.

A valid single edge should pass only one orientation.  If both orientations
contain strong straight edges, the ROI is ambiguous and is rejected rather
than selecting one arbitrarily.

### 3. Require one dominant transition on each scan line

For each row or column, the detector forms a central-difference gradient.  A
line contributes an edge centroid only when:

- every competing gradient outside a 12-pixel guard is at most 75
  percent as strong as the dominant gradient;
- at least 75 percent of the absolute gradient energy in the 21-pixel centroid
  window has one polarity.

The first test rejects periodic stripes, multiple edges, and smooth gradients.
The second rejects noise and ringing whose positive and negative derivatives
cancel locally.

At least 65 percent of all scan lines must supply qualified centroids.  At
least 85 percent of those centroids must have the same contrast polarity.  The
accepted coordinates must span 80 percent of the ROI and may not contain a
long unsupported gap.

### 4. Fit a robust straight line

Ordinary least squares can be displaced by a small number of dust spots.
Color-Screen therefore initializes the line from median centroids in up to 16
coordinate bands and a deterministic Theil-Sen median slope.  Residuals are
then clipped using a MAD-derived tolerance, and the inliers are refitted with
centered long-double accumulation.

The final accepted fit requires:

- at least 65 percent of all scan lines and 80 percent of the qualified
  candidates to remain inliers;
- RMS residual no larger than 0.50 pixel;
- 95th-percentile residual no larger than 1.00 pixel.

These limits are deliberately in pixel units.  The former residual/total
variance ratio became ill-conditioned for an almost axis-aligned edge.

### 5. Check angle and ROI containment

The line must be no more than 15 degrees from the nearest image axis.  It must
also move at least 0.75 pixel across the ROI, so a nominally vertical or
horizontal edge cannot masquerade as a supersampled measurement.

The fitted line must leave at least four pixels of plateau on both sides.  A
finite requested LSF half-width increases this requirement to
`half_width + 1` pixels.  An edge entering through a side of the rectangle, or
one selected too close to a boundary, is rejected before ESF construction.

### 6. Verify real subpixel phase coverage

Before filling any empty ESF bins, the detector measures:

- how many of the requested subpixel phases occur;
- the fraction of ESF bins populated by actual image pixels;
- the longest run of empty bins.

Acceptance requires at least 70 percent of phase classes, at least 65 percent
populated bins, and no empty run longer than half of one input pixel.  Empty
bins are interpolated only after these checks.  Thus interpolation cannot
manufacture most of the apparent supersampling.

### 7. Measure robust plateaus and SNR

Pixels in the outer quarters of the projected ROI provide two plateau samples.
Their levels are medians and their noise estimates are Gaussian-equivalent
median absolute deviations.

The robust plateau difference must be at least `1e-4` in normalized intensity,
and

```text
contrast / hypot(left_sigma, right_sigma) >= 8
```

must hold.  This is intentionally a qualification SNR rather than a claim
about the camera's published SNR.

### 8. Verify that the ESF is one transition

A validation-only ESF copy is smoothed by two one-pixel box filters.  The
original ESF remains untouched for the MTF calculation.  The smoothed curve
must satisfy all of the following:

- plateau difference divided by total variation is at least 0.50;
- at least 45 percent of absolute LSF energy lies within six pixels of the
  dominant transition;
- the dominant LSF peak lies within 1.50 pixels of the fitted geometric edge.

This rejects broad illumination ramps, periodic patterns, double edges, and
cases where line centroids locked onto a feature unrelated to the projected
ESF.  The six-pixel aperture still admits the broad wings in the real Hurley
infrared sample and optical blurs substantially softer than the intended
Phase One/Schneider capture path.

The dominant peak from this smoothed validation curve is also used to center
the window applied to the original LSF.  This prevents one noisy raw ESF bin
from moving the Fourier window.

### 9. Apply a final physical sanity check

After bin and central-difference corrections, a normalized optical MTF should
not have extreme gain.  A curve exceeding 150 percent is rejected as dominated
by noise, texture, or multiple transitions.  The generous limit permits small
measurement overshoot without accepting the catastrophic curves formerly
produced by stripe-like ROIs.

## Failure categories

The public `slanted_edge_failure` enum distinguishes:

- invalid parameters or ROI;
- render precomputation failure;
- absence of one unique edge;
- a nonlinear edge;
- unsuitable angle;
- an edge too close to the ROI boundary;
- insufficient phase coverage;
- low contrast or SNR;
- an unstable ESF;
- invalid numerical values;
- a nonphysical MTF.

The detailed string should be shown to the user because it includes the actual
angle, fit residual, coverage, SNR, or ESF statistic responsible for rejection.

## Recommended ROI selection

For high-quality macro captures:

- select exactly one edge;
- use an edge approximately 2 to 10 degrees from vertical or horizontal;
- include long, flat plateaus on both sides;
- keep dust, scratches, lettering, perforations, and neighboring edges out of
  the rectangle;
- make the ROI long along the edge, rather than excessively wide across it;
- use the wavelength associated with that measurement.

For the Hurley infrared example, the existing full ROI remains valid.  Its
approximately 5.1-degree edge, 0.17-pixel line-fit RMS, high plateau SNR, and
broad but coherent LSF all pass with substantial margin.

## Regression coverage

The unit test retains the realistic diffraction/defocus sequence and adds
explicit rejection cases for:

- a grid-aligned edge;
- periodic stripes;
- a smooth illumination gradient;
- two parallel slanted transitions;
- a curved transition;
- deterministic random texture;
- a low-contrast edge buried in noise;
- a slanted edge clipped by an ROI boundary.

The command-line tests also require a checkerboard ROI to fail with a detailed
message and verify that no requested MTF output file is created.  These cases
specifically guard against returning a random MTF from a numerically finite but
invalid selection.
