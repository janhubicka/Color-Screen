# Explicit measured-MTF fitting workflow

This document describes how Color-Screen measures a slanted-edge spatial
frequency response (SFR), records the acquisition metadata associated with that
curve, and fits either the primary diffraction-based MTF model or the empirical
fallback model.

The central design rule is:

> **A numeric field stores a value. A separate Boolean says whether that value
> is fixed or optimized.**

Older Color-Screen code used zero for two different purposes: as a physical
value and as a request to optimize. That convention was ambiguous because zero
is a valid value for residual Gaussian blur, focus displacement, halo energy,
sensor-aperture correction, and fallback blur diameter. New GUI code and new
library callers use `mtf_estimation_options` instead. The historical overload is
retained only for source and command-line compatibility.

## 1. Data objects and responsibilities

Three objects deliberately have different responsibilities.

### 1.1 `mtf_parameters`

`mtf_parameters` stores model values and measured curves. Its fields never
encode GUI intent in the explicit workflow. For example:

- `sigma = 0` means exactly zero residual Gaussian blur;
- `defocus = 0` means the image plane is in focus;
- `halo_fraction = 0` means the broad halo is disabled;
- `f_stop = 8` means the marked aperture is fixed initially at f/8 unless the
  corresponding optimization option is enabled;
- each `mtf_measurement::wavelength` stores the wavelength associated with that
  particular edge measurement.

`mtf_parameters::model` selects one of:

- `physical_diffraction`: the normal calibrated-capture model;
- `empirical_fallback`: the metadata-free Gaussian/circular-blur backup;
- `automatic_legacy`: compatibility behavior for old project files and old
  callers.

A successful explicit fit selects either `physical_diffraction` or
`empirical_fallback` and clears `measured_mtf_idx`. This is intentional: the
newly fitted analytical model must become the active sharpening model rather
than being hidden by a previously selected measured curve.

### 1.2 `mtf_estimation_options`

`mtf_estimation_options` records fit intent independently of the model values.
It contains one Boolean for each scalar parameter that may be optimized, plus
vectors describing:

- which measured curves participate in the objective;
- which per-measurement wavelengths are optimized.

The vectors are indexed exactly like `mtf_parameters::measurements`. An empty
`include_measurements` vector means that every curve is included. Missing
entries in `optimize_measurement_wavelengths` mean false.

Keeping these options outside `mtf_parameters` has two advantages:

1. saving a project does not accidentally save a transient GUI operation as a
   physical property of the scanner;
2. several fitting strategies can be tried against the same model values
   without changing their meaning.

### 1.3 `slanted_edge_parameters`

`slanted_edge_parameters` controls creation of a new measured curve. It stores:

- ESF oversampling;
- retained LSF half-width;
- LSF window;
- wavelength;
- channel label;
- measurement name;
- whether the curve shares a capture with the preceding curve.

Oversampling, LSF support, and windowing belong to measurement, not fitting.
They change the measured curve itself and therefore cannot be adjusted later by
the model optimizer without remeasuring the edge.

## 2. Primary and fallback models

The physical diffraction model is the normal choice when scan resolution,
sensor pixel pitch, f-number, and measurement wavelength are available. For the
Hurley infrared example the known metadata is:

| field | value |
|---|---:|
| scan resolution | 1887 PPI |
| marked f-number | 8 |
| wavelength | 750 nm |
| Phase One pixel pitch | 3.760 µm |

Those values should normally remain fixed. The optimizer estimates quantities
that describe residual image formation: compact Gaussian sigma, image-plane
defocus, and the optional broad-halo fraction and radius. The GUI enables all
four residual terms by default. This does not force a halo into the result,
because zero halo fraction remains inside the fitted domain. A nonzero halo
should nevertheless be accepted as physical calibration only when the same
low-frequency shoulder appears in several independent measurements.

The empirical fallback is provided for scans whose calibrated physical metadata
is unavailable. Selecting it is explicit; it is not the normal route merely
because one metadata field has not yet been entered.

## 3. Fixed, optimizable, and required values

The library validates the request independently of the GUI. The dialog mirrors
that validation and forces optimization only where a missing value can actually
be estimated.

| value | zero valid as fixed value? | behavior when missing/zero |
|---|---:|---|
| residual sigma | yes | may remain fixed at zero or be optimized |
| image-plane defocus | yes | may remain fixed at zero or be optimized |
| sensor fill factor | yes | zero disables the first-cut sensor aperture term |
| halo fraction | yes | zero disables the broad component |
| halo sigma | no when halo is active | forced to optimize if an active/fitted halo has no positive radius |
| fallback blur diameter | yes | may remain fixed at zero or be optimized |
| marked f-number | no in physical model | Optimize is forced on and cannot be unchecked |
| measurement wavelength | no in physical model | Optimize is forced on and cannot be unchecked |
| scan PPI | no in physical model | user must enter it; it is not inferred from one radial MTF curve |
| sensor pixel pitch | no in physical model | user must enter it; it is not inferred from one radial MTF curve |

The physical inverse problem cannot identify a completely free f-number and all
free wavelengths from the same radial curves reliably because both primarily
move the diffraction scale. The validator therefore rejects that unanchored
combination. In a calibrated workflow at least one of these quantities should
be known, and usually both are known.

## 4. Wavelength semantics

Every new GUI or command-line slanted-edge measurement should carry a positive
wavelength. The wavelength is metadata: it does not alter the numerical
slanted-edge transform, but it determines how the physical model interprets the
measured curve.

For compatibility with older project files, the library resolves a missing
per-measurement wavelength in this order:

1. positive finite `mtf_measurement::wavelength`;
2. positive per-channel value in `mtf_parameters::wavelengths`;
3. positive global `mtf_parameters::wavelength`;
4. unknown.

The first item is authoritative regardless of the channel label. A measurement
marked infrared may therefore carry 750 nm even when an older channel default
is 850 nm. Channel is descriptive metadata; it never disables the wavelength
editor.

After a successful explicit physical fit, every included measurement receives
its effective or fitted wavelength directly. This makes the fitted data
self-contained and avoids later changes caused by a global fallback value.

## 5. Slanted-edge GUI workflow

Choosing **Measure slanted-edge MTF** opens a setup dialog before area
selection. The dialog contains:

- measurement name;
- channel label;
- wavelength;
- same-capture grouping;
- ESF oversampling from 2 to 64;
- retained LSF half-width;
- Hann, Hamming, or rectangular LSF window.

A positive wavelength is required before area selection. The most recently used
settings are retained for the current application session. Wavelength and
channel are initialized from the most recent measurement when possible, then
from the global MTF wavelength.

The defaults preserve the historical Color-Screen measurement:

- 10 times ESF oversampling;
- full ROI support (`lsf_half_width = 0`);
- Hann window.

For a diagnostic comparison with the bundled QuickMTF Hurley curve, a roughly
compatible analysis is a 20-pixel LSF half-width with a rectangular window.
That setting is a comparison convention, not necessarily a more accurate
estimate of the complete capture response.

The image area is selected only after the dialog is accepted. The actual edge
analysis runs in the existing background area-computation workflow, and one
measurement with all metadata is appended to the scanner-MTF state.

## 6. MTF fitting dialog

**Fit measured MTF model** opens the explicit optimizer dialog. It provides:

- physical diffraction model or empirical fallback selection;
- known scan PPI and sensor pixel pitch;
- a value and an **Optimize** checkbox for each applicable model parameter;
- a table of measured curves with inclusion, wavelength, wavelength
  optimization, channel, and capture grouping;
- derivative-free simplex and least-squares-refinement controls.

An unchecked box fixes the displayed value exactly. A checked box uses the
value as its starting estimate. Residual sigma, defocus, halo fraction, and halo
radius are checked initially for the physical model. The optimizer may return a
zero halo fraction, so this default tests for broad wings without requiring
them. Invalid missing values force the appropriate box on and disable it so the
user cannot request an impossible fixed model. Errors from the library
validator are displayed before the Fit button is enabled.

The measurement table derives its row height from the active Qt font and the
embedded spin boxes. The dialog also chooses its initial size from font metrics
and remains resizable. This avoids clipped wavelength text and checkbox
indicators with accessibility fonts or high-DPI desktop scaling.

The fit runs outside the GUI thread, reports cancellable progress, and operates
on a snapshot. When it finishes, Color-Screen verifies that the MTF state has
not changed in the meantime. A valid result is committed as one undoable
parameter change; a stale result is reported but not applied.

The numerical solver normally runs derivative-free simplex first and then an
optional local least-squares refinement. This order is important because the
defocus objective is even around zero and has zero first derivative at exact
focus.

After fitting, the Sharpness panel displays broad-halo fraction and radius next
to residual sigma and defocus. These controls edit the same stored physical
parameters, so the fitted result is visible and can be tuned without reopening
the optimizer. A zero fraction disables the halo while preserving the stored
radius as a possible future starting value.

The MTF chart always displays the analytical model independently of the
`Use measured MTF` sharpening switch.  Measured slanted-edge curves are plotted
as positive magnitudes and the model's system MTF is `abs(system OTF)`.  A
diagnostic `Show signed physical OTF` checkbox overlays the signed analytical
system transfer and expands the vertical chart range to -100..100%.  Negative
lobes are predictions of the fitted physical model; phase is never inferred
from the slanted-edge samples themselves.

When a broad halo is enabled, the fitter first combines the signed compact-core
OTF and the positive broad Gaussian halo component as an energy-weighted PSF
mixture.  Only then does it take the magnitude for the residual.  This keeps
halo fitting consistent with phase reversals predicted by defocus.

### 6.1 Per-frequency uncertainty weighting

New slanted-edge measurements also store an optional one-standard-deviation
uncertainty for each MTF sample, in percentage points.  The reported MTF curve
itself is unchanged.  Color-Screen estimates uncertainty by splitting the ROI
into four interleaved groups of scan lines, recomputing the MTF on each group
with the already-qualified edge geometry, and converting the between-group
scatter to the standard error of the full ROI.  The uncertainty curve is
smoothed over a narrow frequency interval because adjacent zero-padded FFT bins
are strongly correlated.

The physical-model residual remains expressed in percentage points.  When a
curve contains usable uncertainty estimates, the fitter multiplies each
residual by an inverse-uncertainty weight.  A 0.25 percentage-point floor, or
one quarter of the curve's median uncertainty when that is larger, prevents an
accidentally tiny variance estimate near DC from dominating the optimization.
The weights are RMS-normalized within each measurement so uncertainty changes
only the relative importance of frequencies inside that curve; it does not
silently increase or decrease the total influence of the curve compared with
the historical objective.

Project files may therefore contain either

```text
scanner_mtf_point: frequency contrast
```

or

```text
scanner_mtf_point: frequency contrast uncertainty
```

Old two-value records load with zero uncertainty and use the original uniformly
weighted fit.  Imported QuickMTF data likewise remains uniformly weighted unless
an uncertainty estimate is explicitly available.

## 7. Capture grouping and current storage limitation

`same_capture` groups adjacent measurements. Curves in one group share a fitted
defocus coordinate, while a new group receives an independent defocus
coordinate.

The current persistent analytical model still stores one scalar active
`defocus` and one scalar active `wavelength`. After a multi-capture explicit fit,
the scalar values correspond to the first included curve; all included curves
retain their own wavelengths, and diagnostic output evaluates each curve with
its own fitted defocus during fitting. This is sufficient for fitting and for a
single calibrated sharpening model, but it does not yet persist a separate
analytical PSF for every capture group. Separate project calibrations should be
used when different captures require materially different defocus kernels.

## 8. Command-line slanted-edge measurement

The command-line command accepts the same numerical analysis settings and a
wavelength label:

```sh
colorscreen slanted-edge input.tif \
  --gamma=1 \
  --wavelength=750 \
  --oversampling=10 \
  --edge-half-width=0 \
  --edge-window=hann \
  --save-mtf=curve.txt \
  --verbose
```

Accepted windows are `hann`, `hamming`, and `rectangular`. The wavelength range
is 1--2000 nm and oversampling range is 2--64.

With `--verbose`, the command prints the stored wavelength. The slanted-edge
CLI regression suite passes `--wavelength=750` for every synthetic and real
case and verifies that line. This guards the acquisition-metadata path as well
as the numerical SFR comparison.

The QuickMTF-compatible text file written by `--save-mtf` contains only the
frequency-domain columns; that file format has no standard field for wavelength
or analysis settings. Metadata remains attached when the curve is measured
inside a Color-Screen project. When exporting a standalone file, include the
wavelength and support/window convention in its filename or accompanying notes.

## 9. Backward compatibility

Old project files load with `model = automatic_legacy` when they do not contain
an explicit model field. The fitting dialog presents the physical model first
and exposes missing metadata rather than silently switching to the fallback.

The historical overload

```cpp
estimate_parameters(parameters, table, progress, error, flags)
```

still interprets zero as a request to optimize. It is retained for existing
source callers and the legacy MTF command-line fitting interface. New code
should call

```cpp
estimate_parameters(parameters, options, table, progress, error, flags)
```

and should never encode fit intent in a numeric value.

## 10. Recommended Hurley procedure

1. Measure the infrared edge with wavelength 750 nm and record the chosen
   oversampling/window/support convention.
2. Open the fit dialog and select **Physical diffraction model**.
3. enter 1887 PPI, 3.760 µm, and f/8;
4. leave f-number and wavelength unchecked so they remain fixed;
5. optimize residual sigma, defocus, halo fraction, and halo radius;
6. keep sensor fill factor fixed at the intended first-cut value;
7. regard a fitted zero halo fraction as the preferred compact-model result;
8. retain a nonzero halo only when its low-frequency shoulder and approximate
   radius repeat across independent edges;
9. validate the resulting sharpening on independent fine detail rather than
   only on the edge used for fitting.

## 11. Regression coverage

The library unit tests verify:

- fixed zero sigma and defocus remain exactly zero;
- explicitly free sigma and defocus improve a synthetic objective;
- physical and fallback model selections remain distinct;
- a per-measurement 750 nm value overrides conflicting global/channel values;
- missing fixed f-number or wavelength is rejected, while optimizing it is
  accepted;
- excluded incomplete measurements do not invalidate a fit;
- option-vector size errors and invalid model values are rejected;
- an unanchored simultaneous f-number/all-wavelength fit is rejected;
- successful explicit fitting activates the analytical model;
- model and per-measurement wavelength round-trip through project files.

The slanted-edge unit and command-line tests verify that wavelength, name,
and channel metadata are copied to the generated curve, and that the
oversampling, support, and window controls are honored while preserving the
numerical MTF regressions. Capture grouping is exercised by the explicit
multi-measurement fitting tests.
