# Native JSON parameter format v2: pre-beta migration

## Decision

New `.cspar` version 2 will be a **single UTF-8 JSON file**, not a ZIP archive,
with no embedded legacy `.par` stream. JSON supplies all necessary mappings,
sequences, numeric and string types, and is YAML 1.2 compatible without an
additional parser dependency. The historical `.par` format and ZIP schema-v1
`.cspar` must remain supported **as import formats**.

**Status (2.0alpha):** new GUI parameter targets still default to compatible
schema-v1 ZIP, while explicit JSON-v2 Save As, automatic content-detected Open
and CLI conversion/read/rewrite are implemented. Private crash recovery now
writes complete native v2 JSON and accepts earlier ZIP-v1 or .par snapshots.
Do not switch the *default user save* to v2 until the full field/CI audit
and GUI workflows have been validated on all platforms. This
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
  "profile": {},
  "correction_grids": {}
}
~~~

Every section must have its own authoritative typed schema, precise defaults,
validation rules and a complete native reader/writer. A missing required
section must be rejected. Do not accept a partly filled prototype as a valid
full v2 parameter file.

### Implemented native JSON v2 components

Draft PR #538 has seven directly typed core components (registration,
capture, reconstruction, process, colour, complete sharpening/MTF and
calibration grids), plus a **complete v2 document and atomic file codec**. It composes a single
UTF-8 JSON root, decodes one syntax tree into private C++ state and supports
explicit native GUI/CLI I/O plus crash recovery. All old input formats remain
available; only the default new-file user Save-As format is still ZIP v1.
The old v1 ZIP continues to be the application's default until the full
platform/compatibility gates pass. The Qt editor now recognizes JSON v2 by
content, records its physical file identity, and offers an explicit **JSON
parameters v2** Save-As filter alongside the v1 ZIP and legacy text filters.
The CLI `adjust-par` can explicitly convert ZIP v1 or legacy .par to native
JSON using `--json-v2 --out path.cspar`, preserves the input format on
in-place rewrites, and can explicitly downgrade to v1 with `--zip-v1`.
Implicit `--out` to an existing `.cspar` additionally preserves the
**destination's** actual ZIP-v1/JSON-v2 encoding by content, regardless of
the source file's encoding, unless the caller selects an explicit conversion. These paths
retain imported Qt profile spots; they do not silently discard unknown
legacy trailing metadata. Private crash-recovery snapshots now contain complete native JSON v2,
while their separate metadata preserves the user's original JSON-v2, ZIP-v1
or legacy target identity. Recovery can still read previous ZIP-v1 and
legacy .par snapshots without a silent fallback if a newer .cspar is corrupt. They operate **directly
on C++ state** without using `save_csp` or `load_csp`, and emit component
objects for a future complete document. No component is a standalone v2
`.cspar` file.

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

- `capture` stores capture type, RAW demosaic method, gamma, scan
  quarter-turn rotation/mirror, independently enabled physical-object crop
  and photographic image area, scan exposure and global/backlight dark-point
  scalars. It does **not** yet include scanner blur grids, MTF spectral
  metadata or colour/process white balance.
- `reconstruction` stores IR handling, scalar/image-layer RGB mixer weights
  and offsets, collection quality, screen demosaic and scaling algorithms,
  blur radius and collection threshold, plus **all** persisted coefficients
  of pre- and post-screen denoise stages. Inactive denoise coefficients are
  preserved for Undo and future re-enabling, rather than omitted according
  to the currently effective algorithm.
- `process` stores the physical strip widths, stable historical colour-model
  identifier, dye aging and density triplets, and all editable contact-copy
  settings: simulate, preflash, exposure, boost and four paired H&D curve
  coordinates. The complete curve is retained even when simulation is off.
- `color` saves RGB process calibration, scanner XYZ primaries, white balance,
  observer and backlight temperatures, observer whitepoint, dye balance,
  saturation/brightness and user output tone-curve selection and all control
  points (including unused/custom control points). **It does not save the
  output/display ICC profile, transfer gamma, view gamut diagnostic or other
  per-render output settings.**
- `sharpness` now stores capture deconvolution and unsharp settings,
  MTF model and optical coefficients, four native channel wavelengths,
  the selected measured-curve index, and every measured curve with its
  frequency/contrast/uncertainty samples, channel, source filename/dimensions,
  ROI, accepted edge, edge-quality metrics and same-capture metadata.
  Fit-request flags and computed chart curves remain runtime-only.
- `correction_grids` stores complete backlight channel flags, luminosity
  and subtraction tables (including disabled channels), black-reference
  state, global scanner-blur cells with physical mode and per-tile
  exposure/dark point and nested scanner-blur grids. Per-cell scanner-blur
  reduction diagnostics are exported separately, not persistent CSP inputs.

All component codecs require finite scalars, stable known enum identifiers,
valid array shapes and representable numeric conversions. Geometry additionally
requires positive final ratio and compatible mesh dimensions/point count;
capture independently checks outer crop and inner photographic bounds.
Every decoder works in local state and only publishes after complete validation.
C++ float/double values are written with enough decimal precision for exact
round trips. Separately bounded large-array parsing does **not** weaken the
small schema-v1 ZIP manifest limit.

The internal component has a 128 MiB text budget, max 1 million mesh vertices
and max 1 million elements per point collection. These are limits of the
prototype codec, not claims about general image dimensions or required
application scale. Profile real large projects before the final format ships.

## Checked-in legacy parameter corpus (validated)

A baseline corpus test now discovers **all 128 .par fixtures** recursively under
`testsuite/` (4 files) and `examples/` (124 files). For each it invokes
the actual CLI, without editing any original:

```text
original .par -> normalized current-writer .par -> JSON v2 -> reloaded .par
```

The normalized legacy files are compared **byte-for-byte**. The JSON document
is also loaded and saved again and required to be byte-for-byte canonical.
A failure reports the individual fixture and a unified diff; the runner is
part of both `make check` and packaged `make distcheck`.

**Result on commit ac0ebaaf (10 October 2026): 128/128** legacy comparisons
passed on both Ubuntu amd64 and arm64, and the packaged `make distcheck`
test passed. This detected and fixed an old Amcolony file using the historical
model spelling `Miethe_Goerz_mesured_by_Wagner`. The parser accepts that
legacy alias and outputs the canonical `...measured...` spelling; the source
fixture remains unchanged to guard backwards compatibility.

Passing this corpus verifies the legacy-persisted fields in these fixtures.
It is **not** equivalent to full coverage of recently added C++/Qt fields
or arbitrary malformed/incomplete input; maintain separate typed-state tests
and the nested-field ownership audit.

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
duplicate keys, **unrecognized fields at any nesting level**, nonfinite numbers,
unknown required features, wrong tuple sizes, out-of-bounds data and
unrecognized enum values. The v2 writer cannot preserve unknown keys, so
accepting them would silently discard them on ordinary Save. Future extensions
must negotiate a new schema/required feature instead. Never partially change an open
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
- [x] Implement all seven core/Qt-spot native components, including
      complete MTF/provenance and spatial correction tables.
- [x] Compose a complete schema-v2 JSON root in memory with one-pass,
      transactionally decoded typed C++ state.
- [ ] Audit native field coverage against every persistent C++ member and
      nested legacy/Qt serializer, not only literal CSP keyword names.
- [ ] Add content-based on-disk dispatch with preserved v1/legacy imports.
- [x] Add explicit GUI native JSON v2 Save As/Open and format-preserving
      ordinary Save; opt-in CLI adjust-par conversion, native replay and
      legacy ZIP interoperability, with a dedicated GUI/CLI smoke test.
- [x] Save complete native JSON v2 private recovery snapshots, preserving
      independent user-target metadata and reading old ZIP-v1/.par snapshots.
      Crash-recovery smoke verifies state restoration and no-stale-fallback.
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
