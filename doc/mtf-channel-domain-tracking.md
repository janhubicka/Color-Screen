# MTF channel-domain tracking

Color-Screen currently handles three different kinds of three-component data
which must not be confused merely because all of them are indexed 0, 1 and 2:

1. **Native capture channels** are scanner/camera red, green and blue samples.
   Their MTFs and wavelengths belong to the capture device.
2. **Process-screen primaries** are the red, green and blue transmission masks
   of the historical additive screen.  Their component index is a process
   colour index, not a scanner-channel index.
3. **The image layer** is one scalar capture signal.  It can be a real visible
   monochrome scan, a real infrared companion plane, or a scalar signal mixed
   from native RGB.

This distinction is especially important for the physical MTF model.  A
scanner-channel wavelength may select the transfer of a native R/G/B capture
channel, but it must never be selected merely because the same numeric index is
being used for a red/green/blue process-screen primary.

## Invariants

- Native RGB sharpening happens before RGB mixing and uses three capture-channel
  transfer functions.  `render.C` is the reference implementation of this
  invariant.
- A real scalar capture has one transfer.  With RGB plus a fourth plane the
  fourth plane is interpreted as infrared and defaults to 750 nm.  Standalone
  grayscale defaults to visible light (550 nm).
- An RGB-derived image layer is not one of the native scanner channels.  Until
  the exact spectral/spatial model below is implemented, its analytical screen
  simulation uses a documented representative wavelength rather than stealing
  a native measured R/G/B curve.
- A measured image-layer MTF belongs to the scalar image layer.  It must not be
  used automatically as a native R/G/B MTF.  Conversely, a selected native
  red/green/blue curve must not silently become the scalar image-layer curve.
- Arrays passed to `screen::initialize_with_sharpen_parameters()` index the
  three **process-screen primary planes**.  They are not scanner-channel arrays.

## Audit and work items

### MTF-DOM-001 — Native RGB render sharpening

**Status:** correct on current main.

`render::precompute_all()` specializes scanner sharpening independently for
native R/G/B and mixes an RGB-derived image layer only after those channels have
been sharpened.  Keep this as the semantic reference for full-image capture
processing.

### MTF-DOM-002 — Explicit image-layer measurement domain

**Status:** fixed by the first domain-correctness patch.

Slanted-edge measurements made from the image layer are marked explicitly.
Native-channel specialization must reject such a curve; scalar image-layer
specialization may select it.  Legacy unlabelled measurements remain distinct
from explicit image-layer measurements.

### MTF-DOM-003 — Grayscale slanted-edge setup

**Status:** fixed by the first GUI patch.

When a scan has no native RGB there is no source choice, so the measurement
source selector is hidden.  The dialog starts from the effective scalar capture
wavelength (explicit slot-3 wavelength or the visible 550 nm fallback) instead
of displaying an unexplained unknown wavelength.

### MTF-DOM-004 — Scalar screen/render/finetune specialization

**Status:** fixed by the first domain-correctness patch.

Screen simulation and BW finetune used to copy the generic sharpening object and
change only `scanner_mtf.wavelength`.  This could retain a selected native
measured curve, and the dynamic BW focus path could even enter the physical
model without setting the scalar wavelength.  Scalar users now request one
complete image-layer sharpening specialization.

### MTF-DOM-005 — RGB-derived scalar image-layer capture model

**Status:** open; corresponds to SIM-011.

For scanner channel `c`, the physically correct periodic signal is

```
C_c(f) = M_cR S_R(f) + M_cG S_G(f) + M_cB S_B(f)
C'_c(f) = H_c(f) C_c(f)
```

where `S_R/S_G/S_B` are process-primary spectra, `M` is the scanner response to
those primaries and `H_c` is the capture transfer of scanner channel `c`.
Only after applying the three transfers may the channels be mixed into a scalar
image layer.

Implement an exact Fourier-domain reference and benchmark it against the current
representative-transfer shortcut.  The prepared finetune source already stores
the three process-primary spectra, so the per-frequency 3x3 mixing is cheap and
requires only three inverse FFTs for RGB output.  For a final scalar RGB-derived
image layer, the scalar mix may be combined in Fourier space before the inverse
FFT and may need only one inverse transform.

### MTF-DOM-006 — Color finetune process-primary/scanner-channel crossing

**Status:** open, high priority after MTF-DOM-004.

`finetune_solver::capture_sharpen_parameters()` currently supplies three
scanner-channel wavelengths to an API whose three entries filter the three
process-primary planes.  The numeric indices happen to be red/green/blue in
both domains but the operation is not physically valid.  Do not "fix" this by
renaming the indices or by further specializing those three process-primary
planes.  Replace it with the MTF-DOM-005 spectral-mixing model and benchmark the
focus fit before changing production defaults.

### MTF-DOM-007 — Precise-RGB screen collection and superposition

**Status:** open.

`render_interpolate` precise-RGB collection and color `render_superpose_img`
consume native scanner RGB while their periodic screen model is still scalar.
They must share the same exact scanner-channel-after-spectral-mixing model as
MTF-DOM-005 rather than assigning capture wavelengths to process-primary
indices.

## Regression policy

Tests should make domain mistakes obvious by using deliberately very different
wavelengths/curves for R, G, B and scalar image-layer data.  At minimum cover:

- an explicit image-layer measured curve is not selected for native R/G/B;
- a native R/G/B measured curve is not selected for an RGB-derived scalar layer;
- a native scalar/IR curve is usable for a real fourth channel;
- standalone grayscale receives the visible scalar fallback;
- BW finetune and scalar periodic-screen paths carry the same specialized MTF
  state as direct scalar rendering;
- the eventual exact RGB screen model is compared against a slow reference with
  scanner response matrices that have strong off-diagonal terms.
