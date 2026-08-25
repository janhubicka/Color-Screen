# MTF channel-domain tracking

Color-Screen handles several data domains whose component indexes can look
similar while representing physically different things.  Keeping those domains
explicit is important whenever wavelength-dependent scanner/camera MTF is
applied.

## Component domains

1. **Native capture channels** are scanner/camera red, green and blue samples.
   Their MTFs and wavelengths belong to the capture device.
2. **Process-screen primaries** are the red, green and blue transmission masks
   of the historical additive screen.  Their component index is a process
   colour index, not a scanner-channel index.
3. **The image layer** is the scalar photographic density/transmission signal
   used behind the additive screen reconstruction.  Its provenance determines
   which capture transfer is physically meaningful.

The image layer has four practically important provenance cases:

- **Native infrared plane.**  RGB+IR scanner files may supply the image layer
  directly as their fourth capture channel.  This is one scalar capture with
  the IR transfer and wavelength.
- **Native visible grayscale plane.**  A monochromatic negative or transparency
  may be scanned directly as grayscale.  This is one scalar visible-light
  capture and must not be treated as infrared merely because it occupies the
  scalar storage slot.
- **Synthetic mix from RGB capture of a monochrome original.**  A monochromatic
  negative/transparency scanned by an RGB scanner has one underlying scalar
  optical image observed through three scanner channels.  The channels may
  have different gains and MTFs, but there is no process-primary colour mixing.
  Native R/G/B transfer is applied per scanner channel and the resulting
  channels can then be mixed into the scalar image layer.
- **Synthetic mix from RGB capture of a transparency with the viewing filter
  present.**  Here the RGB scanner channels observe genuinely different
  mixtures of the historical process primaries.  Process-primary spectral
  mixing and scanner-channel transfer do not commute; this is the case that
  needs the full colour-aware periodic capture model described below.

The last two cases must not be conflated just because both ultimately produce a
scalar mix from RGB input.

## Invariants

- Native RGB sharpening happens before RGB mixing and uses three capture-channel
  transfer functions.  `render.C` is the reference implementation of this
  invariant.  This is correct both for ordinary RGB output and for an RGB scan
  of a monochrome original whose image layer is mixed afterwards.
- A real scalar capture has one transfer.  With RGB plus a fourth plane the
  fourth plane is interpreted as infrared and defaults to 750 nm.  Standalone
  grayscale defaults to visible light (550 nm).
- A measured image-layer MTF belongs to the scalar image layer.  It must not be
  used automatically as a native R/G/B MTF.  Conversely, a selected native
  red/green/blue curve must not silently become a scalar image-layer curve.
- Arrays passed to `screen::initialize_with_sharpen_parameters()` index the
  three **process-screen primary planes**.  They are not scanner-channel arrays.
- An RGB-derived image layer is not automatically a viewing-filter capture.
  The expensive process-primary/scanner-response model is required only when
  the captured RGB data actually contains the coloured viewing filter/screen.

## Audit and work items

### MTF-DOM-001 — Native RGB render sharpening

**Status:** correct on current main.

`render::precompute_all()` specializes scanner sharpening independently for
native R/G/B and mixes an RGB-derived image layer only after those channels have
been sharpened.  Keep this as the semantic reference for full-image capture
processing.  In particular, this is already the right order for RGB scanning
of a monochromatic negative/transparency.

### MTF-DOM-002 — Explicit image-layer measurement domain

**Status:** fixed by PR #159.

Slanted-edge measurements made from the image layer are marked explicitly.
Native-channel specialization must reject such a curve; scalar image-layer
specialization may select it.  Legacy unlabelled measurements remain distinct
from explicit image-layer measurements.

### MTF-DOM-003 — Grayscale slanted-edge setup

**Status:** fixed by PR #159.

When a scan has no native RGB there is no source choice, so the measurement
source selector is hidden.  The dialog starts from the effective scalar capture
wavelength (explicit slot-3 wavelength or the visible 550 nm fallback) instead
of displaying an unexplained unknown wavelength.

### MTF-DOM-004 — Scalar screen/render/finetune specialization

**Status:** fixed by PR #159.

Screen simulation and BW finetune used to copy the generic sharpening object and
change only `scanner_mtf.wavelength`.  This could retain a selected native
measured curve, and the dynamic BW focus path could even enter the physical
model without setting the scalar wavelength.  Scalar users now request one
complete image-layer sharpening specialization.

The specialization remains intentionally conservative for synthetic RGB image
layers until capture provenance is represented reliably: it does not steal one
native measured RGB curve and pretend that curve is the scalar transfer.

### MTF-DOM-005 — Monochrome original captured through RGB channels

**Status:** partially fixed; exact one-channel mixes are handled, while general
weighted multi-channel forward models remain open.

An exact image-layer mix with only one nonzero RGB weight now inherits that
native channel's measured/analytical MTF.  Full-image rendering sharpens only
that channel when only the scalar image layer is requested; if the identical
full RGB sharpening is already cached, a non-generating cache peek reuses it.
When RGB output is requested explicitly, the normal three-channel pass remains
the shared source for both RGB and the image layer.

The one-hot test is exact rather than epsilon-based: any second nonzero mix
weight means that two different capture transfers contribute and must use a
multi-channel model.  The one-channel simplification is valid regardless of
capture provenance, including a transparency scanned with its viewing filter:
for one selected scanner channel the same `H_c` filters the complete
process-primary mixture, so convolution distributes over that sum.

For a monochrome original the ideal image layer `L(f)` is scalar.  Scanner
channel `c` observes the same layer through a channel response/gain `a_c` and
capture transfer `H_c(f)`.  A synthetic scalar mix therefore has an effective
linear transfer of the form

```
L_mix(f) = sum_c w_c a_c H_c(f) L(f)
```

for scalar mix weights `w_c`.  No process-primary 3x3 colour matrix is needed.

Where periodic screen simulation or BW finetune currently uses one
representative analytical transfer for an RGB-derived monochrome layer, add an
exact weighted-channel-transfer reference and compare it with the shortcut.
This should be substantially cheaper than the viewing-filter model below and
may often reduce to one combined Fourier transfer before the inverse FFT.

### MTF-DOM-006 — Viewing-filter transparency captured in RGB

**Status:** open; this is the colour-aware part of SIM-011.

When an actual screen transparency is scanned with its viewing filter present,
each scanner channel sees a different mixture of process primaries.  For
scanner channel `c`, the physically correct periodic signal is

```
C_c(f) = M_cR S_R(f) + M_cG S_G(f) + M_cB S_B(f)
C'_c(f) = H_c(f) C_c(f)
```

where `S_R/S_G/S_B` are process-primary spectra, `M` is the scanner response to
those primaries and `H_c` is the capture transfer of scanner channel `c`.
Only after applying the three scanner transfers may the channels be mixed into
an image layer or compared as RGB.

Implement an exact Fourier-domain reference and benchmark it against the current
representative-transfer shortcut.  The prepared finetune source already stores
the three process-primary spectra, so the per-frequency 3x3 mixing is cheap and
requires only three inverse FFTs for RGB output.  For a final scalar RGB-derived
image layer, the scalar scanner-channel mix may be combined in Fourier space
before the inverse FFT.

### MTF-DOM-007 — Color finetune process-primary/scanner-channel crossing

**Status:** open, tied to the viewing-filter model in MTF-DOM-006.

`finetune_solver::capture_sharpen_parameters()` currently supplies three
scanner-channel wavelengths to an API whose three entries filter the three
process-primary planes.  The numeric indices happen to be red/green/blue in
both domains but the operation is not physically valid for a colour
viewing-filter capture.  Do not "fix" this by renaming the indices or by further
specializing those three process-primary planes.  Replace it with the
MTF-DOM-006 spectral-mixing model and benchmark the focus fit before changing
production defaults.

This issue does **not** apply in the same way to an RGB scan of a monochrome
negative/transparency: there the underlying image layer is scalar and
MTF-DOM-005 is the appropriate model.

### MTF-DOM-008 — Precise-RGB screen collection and superposition

**Status:** open where the input is a viewing-filter colour capture.

`render_interpolate` precise-RGB collection and color `render_superpose_img`
consume native scanner RGB while their periodic screen model is still scalar.
For captures where the viewing filter is present they must share the exact
scanner-channel-after-spectral-mixing model from MTF-DOM-006.  Do not impose
that cost on monochrome RGB captures, which only need MTF-DOM-005.

### MTF-DOM-009 — Reliable image-layer capture provenance

**Status:** open prerequisite for choosing between MTF-DOM-005 and MTF-DOM-006
automatically.

`render_parameters::capture_type` already has names distinguishing plain
negative/transparency captures from `*_with_screen` variants, but the current
`capture_properties` table is empty and code search does not show reliable
runtime plumbing that can be used as a physical discriminator.  Audit and
complete that metadata path, or add an explicit provenance field, before
selecting different MTF models automatically from these enum names.

Until then, production code should stay conservative rather than infer viewing
filter presence merely from `has_rgb()`.

## Regression policy

Tests should make domain mistakes obvious by using deliberately very different
wavelengths/curves for R, G, B and scalar image-layer data.  At minimum cover:

- an explicit image-layer measured curve is not selected for native R/G/B;
- a native R/G/B measured curve is not selected as the scalar image-layer
  transfer merely because the layer is synthesized from RGB;
- a native scalar/IR curve is usable for a real fourth channel;
- standalone grayscale receives the visible scalar fallback;
- BW finetune and scalar periodic-screen paths carry the same specialized MTF
  state as direct scalar rendering;
- exact one-channel RGB mixes inherit the selected native MTF and scalar-only
  rendering matches the full RGB sharpen-then-mix result, including cache reuse;
- general monochrome RGB forward modelling matches a slow weighted per-channel
  reference without introducing process-primary colour mixing;
- the eventual viewing-filter RGB model is compared against a slow reference
  with scanner response matrices that have strong off-diagonal terms.
