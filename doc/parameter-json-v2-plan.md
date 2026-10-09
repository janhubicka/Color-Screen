# Native JSON parameter format v2: pre-beta migration

## Decision

New `.cspar` version 2 will be a **single UTF-8 JSON file**, not a ZIP archive,
with no embedded legacy `.par` stream. JSON supplies all necessary mappings,
sequences, numeric and string types, and is YAML 1.2 compatible without an
additional parser dependency. The historical `.par` format and ZIP schema-v1
`.cspar` must remain supported **as import formats**.

**Status (2.0alpha):** current Save, Save As, CLI and recovery still write/read
schema-v1 ZIP by default. Do not enable v2 writes until all persistent state,
including Qt profile metadata, is represented and round-trip verified. This
document supersedes the former plan to migrate individual fields indefinitely
within schema v1.

## Target document

Illustrative *shape*, not yet a valid complete v2 sample:

~~~json
{
  "format": "org.colorscreen.parameters",
  "schema_version": 2,
  "capture": {},
  "process": {},
  "geometry": {},
  "detection": {},
  "registration": {},
  "reconstruction": {},
  "sharpness": {},
  "color": {},
  "profile": {}
}
~~~

Every section must have its own authoritative typed schema, precise defaults,
validation rules and a complete native reader/writer. A missing required
section must be rejected. Do not accept a partly filled prototype as a valid
full v2 parameter file.

### First implemented building block

Draft PR #538 adds internal `encode_parameter_json_v2_registration` and
`decode_parameter_json_v2_registration`. They operate **directly on C++**
geometry, detection, solver and profile-spot data without using
`save_csp` or `load_csp`. Their JSON consists of these four objects (to be
composed into a future full v2 root; it is not a standalone user file):

- `geometry` has stable string `screen_type` and `scanner_type`,
  `center`, `axis_x`, `axis_y` as [x,y] pairs,
  `projection_distance`, `tilt` [x,y], `final_rotation`,
  `final_mirror`, `final_angle`, `final_ratio`, a `lens` object with
  `radial` [kr0,kr1,kr2,kr3] and `center` [x,y], plus `mesh`.
  The `mesh` is null or an object with `direction`
  ("screen-to-image"/"image-to-screen"), `shift` [x,y], `step` [x,y],
  `dimensions` [width,height], and a flat row-major `points` array of
  numeric [x,y] pairs. Shift and step follow the current mesh coordinate
  convention. No C++ memory images, untyped buffers or compressed blobs.
- `detection` saves `red`, `green`, `blue`, `black` as numeric RGB
  triplets, plus `min_luminosity` and `min_ratio`.
- `registration` saves `optimize_lens`, `lens_center_distance`,
  `optimize_tilt` and `points` (array of
  {"image":[x,y],"screen":[x,y],"color":"red"} with stable colour names).
- `profile` currently saves spot coordinates as the `spots` array of
  numeric [x,y] pairs. The final profile section must also cover any other
  genuinely persisted calibration state.

The component codec requires finite scalars, positive final ratio, stable
known enum identifiers, correct numeric tuple lengths, compatible mesh
dimensions/point count, and valid numerical conversions. It parses into
independent local state and only publishes outputs after complete validation.
C++ float/double values are written with enough decimal precision for exact
round trips. Separately bounded large-array parsing does **not** weaken the
small schema-v1 ZIP manifest limit.

The internal component has a 128 MiB text budget, max 1 million mesh vertices
and max 1 million elements per point collection. These are limits of the
prototype codec, not claims about general image dimensions or required
application scale. Profile real large projects before the final format ships.

## Field-coverage audit required for full v2

The complete migration must inspect each persistent field of
`render_parameters`, `scr_to_img_parameters`,
`scr_detect_parameters`, `solver_parameters`, MTF/curve structures,
scanner and backlight corrections, mesh, and Qt `ParameterState`.

| Domain | Essential persisted content |
| --- | --- |
| Capture | capture type, selected RAW demosaicing, input gamma, capture resolution, scan mirror/rotation, physical-object crop, inner photographic image area, white balance, capture MTF wavelengths and other actual capture calibration |
| Process | physical screen type, scanner interpretation, strips, dye model/age/density, contact-copy response and historical-process settings |
| Geometry/detection/registration | complete first component above, including final frame, mesh, lens polynomial, solver policy and all control points |
| Reconstruction | image-layer weights/dark offsets, screen demosaicing, collection/blur thresholds, pre/post demosaic noise models, sharpness used specifically for screen reconstruction |
| Sharpness/MTF | sharpening mode, resampling/supersampling, deconvolution controls, MTF physical model, scanner/sensor parameters and all measured curves with uncertainties, channel/wavelength, source/ROI/edge provenance and selection |
| Color/appearance | scanner/process profile values, backlight/viewing correction, colour and dye balance, input-to-output colour transforms, tone controls and user curve points |
| Tile/correction arrays | stitching correction dimensions and per-tile exposure/dark point/blur, backlight correction grids, scanner-blur arrays and other variable-sized calibration data |
| Profile | persistent spots and other user calibration data; preserve any Qt-specific metadata currently appended to CSP |

Classify every previous `save_csp` / `load_csp` keyword and every
schema-v1 structured-only value as **native v2**, **import-only historical**,
or **runtime/derived and intentionally excluded**. Require a tracked explicit
decision for each before releasing the full v2 writer. Import-only values must
not be lost on legacy conversion.

**Keep saved:** `demosaic`, `final_angle`, `final_ratio`,
`image_area`, `ignore_infrared`, `demosaiced_scaling`,
`observer_whitepoint`, registration points, MTF measurements and spatial
provenance, saved corrections and profile spots.

**Do not save:** display/output profile, output transfer gamma, per-view gamut
warning or tile visibility, renderer or RAW caches, worker flags/progress,
partial solver scratch state, obsolete mesh weighting centres and derived chart
values. Their ownership remains separate from the document.

## Compatibility, safety and transactions

Load files by **content**:

1. legacy `screen_alignment_version` stream -> legacy CSP importer;
2. ZIP signature -> validated schema-v1 archive, including its legacy CSP
   mirror and structured supplements;
3. JSON object with correct format marker and schema version 2 -> direct v2
   decoder into fresh typed state.

Do not guess formats using the suffix alone. Reject unknown schema versions,
duplicate keys, nonfinite numbers, unknown required features, wrong tuple sizes,
out-of-bounds data and unrecognized enum values. Never partially change an open
document on failed parse, mix entries from separate sidecars, or silently load
an older recovery file after a corrupt newer one.

A v2 writer must serialize all persistent inputs **directly** and atomically
replace the target using the established sibling-staging/UTF-8-path primitive.
No ZIP wrapper, private CSP conversion or duplicate authoritative copies.
Preserve ordinary Save's existing target identity: an opened v1 ZIP should not
be silently rewritten as plain JSON by pressing Save. Offer explicit conversion
or Save As selection; switch new projects to v2 only after the gates below.

## Execution checklist

- [x] Decide plain-JSON v2 representation, independent of ZIP.
- [x] Start a native, transaction-safe and unit-tested
      geometry/detection/registration/profile-spots component.
- [ ] Implement the rest of the native typed serializers/readers; complete and
      audit mapping of every persisted field. No partial default writer.
- [ ] Compose full v2 root and robust content-based dispatch.
- [ ] Integrate GUI Save/Save As, CLI read/rewrite and private recovery.
- [ ] Add explicit conversion from legacy and ZIP v1; preserve existing
      target formats and old-file loaders.
- [ ] Verify all field families and cross-format round trips, huge point sets,
      meshes, MTF and correction grids, deterministic numeric fidelity,
      malformed input, transaction/atomic-failure preservation, Unicode names,
      Qt document/workspace smoke and sanitizer runs.
- [ ] Switch default new Save to v2 only after a full Ubuntu/macOS/Windows,
      sanitizer and distcheck matrix is green; then field test before beta.

The product remains **2.0alpha**. This implementation must not produce a
file claiming schema version 2 before all content is represented.
