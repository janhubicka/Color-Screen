# Parameter JSON schema 3 — reference design and migration evidence

**Status: implementation/reference note for the first public JSON format.**
The only pre-existing user parameter files are legacy `.par`. Early ZIP/JSON
parameter formats were alpha experiments and **do not require compatibility**.
Until the complete schema-3 document codec and its Qt/CLI/recovery integration
are tested, the current development build still writes temporary JSON-v2;
those readers and writers will be replaced, not carried into the release.
ZIP-based `.csprj` stitching projects are unrelated and remain supported.

The design follows `doc/parameter-json-migration-tracking.md` (requirements
JSON-005 through JSON-013) and is prototyped by
`testsuite/parameter-json-v3-reference.py`. The CLI/GUI do **not** call this
Python tool. The final writer/reader must be implemented in **Qt-free
libcolorscreen** and validated separately before switching the version.

## Ownership

| Section | Owns | Existing v2 source |
| --- | --- | --- |
| `capture` | Captured medium, RAW decoding, input linearization, scan orientation, physical-object and photographic bounds, input levels | `capture` |
| `process` | Screen type/strip widths, historical dye model, aging, contact-copy emulsion | `geometry.screen_type`, `process` |
| `geometry` | Imaging model, coordinate axes/projection, accepted lens/mesh, final-frame transform | remaining `geometry` |
| `registration` | Detection RGB references, registration fitting policy and accepted points | `detection`, `registration` |
| `calibration.capture` | Sensor primaries in XYZ, MTF model, measured MTF evidence/provenance | `color.scanner_primaries`, `sharpness.mtf` |
| `calibration.process_color` | Colour-profile scanner RGB responses to dyes and calibration spots | `color.process_profile`, `profile.spots` |
| `calibration.flat_field`, `.spatial_blur`, `.tiles` | Black correction, measured grids and per-tile adjustments | `capture.backlight_correction_black`, `correction_grids` |
| `reconstruction` | Image-layer mixer, screen demosaicing, collection and denoisers, all sharpening controls | `reconstruction`, `sharpness` except MTF |
| `color` | Photograph/backlight illuminants, observer whitepoint, persistent appearance adjustments and tone curve | remaining `color` |

Display/output gamma, the monitor ICC profile, per-view gamut overlays, cache
policies, temporary-worker state and derived plots remain **excluded**.

## Deliberate semantics

* `capture.linearization`: `{"mode":"srgb"}` replaces legacy gamma `-1`;
  `{"mode":"power","exponent":2.2}` represents power transfer. This is
  **input** linearization, not output transfer gamma. No other negative
  special cases are inferred.
* `capture.bounds.object_crop` and `photograph`: `null` means absent. A
  selected rectangle uses named `x_px`, `y_px`, `width_px`, `height_px`;
  signed object-crop origins are retained. Image coordinates are those of the
  decoded input at the active RAW decoding resolution.
* `capture.levels`: `black_offset` and multiplicative `gain` implement
  `(value - black_offset) * gain`; gain is **not** logarithmic stops.
* `process.screen.strip_widths`: `{"mode":"automatic"}` replaces legacy
  zero, while `{"mode":"fraction","value":0.37}` is explicit.
  Additional process-specific meaning remains to be documented in C++.
* `geometry.final_frame`: retains angle in degrees and the dimensionless
  length ratio, including the values when GUI controls are inactive.
* `calibration.capture.mtf`: measured MTF records have independent document-
  local `id` and `capture_group_id`. Selection uses ID, never an array index.
  Import from **legacy .par** groups adjacent records when `same_capture` is
  true. The first record's flag has no semantic predecessor and is ignored.
  The Python v2↔v3 reference adapter's `legacy_first_same_capture` field is
  an obsolete alpha-test artifact, never part of canonical JSON. Editor
  actions preserve IDs through rename/sort/insert/delete and Save.
* Each measured edge's `edge_quality` has named `angle_deg`, `fit_rms_px`,
  `contrast_fraction`, `snr` and `phase_coverage_fraction`. Samples remain
  compact triples; `sample_columns` fixes their units to cycles/pixel,
  contrast percent and uncertainty percentage points.
* Source provenance distinguishes absolute from parameter-file-relative
  path spellings. A missing source filename becomes `null`, not a relative
  empty filename. The downgrade rejects contradictory path bases rather than
  silently discarding them. Drive-relative Windows paths and root-only Windows
  paths with no volume are rejected as ambiguous; qualified UNC paths remain
  supported. Source dimensions and ROI use explicit `status`;
  historical sentinel values are retained for lossless migration, but
  changing a status without changing the dimensions is rejected.
  The reference adapter includes an in-memory
  `rebase_v3_source_paths()` experiment for POSIX and Windows Save As, keeping
  absolute references and missing-source records unchanged and falling back
  to absolute paths for cross-volume Windows moves. It neither writes files
  nor implements the native C++/Qt Save As transaction; that integration and
  any real source-file path resolution remain prerequisites for shipping v3.
* Schema 3 allows noncontiguous capture-group records. Native MTF fitting
  and channel specialization use persistent capture IDs; legacy adjacency
  is interpreted only when importing `.par`. Duplicate/dangling IDs and
  unknown fields remain invalid. There is no v3→v2 downgrade obligation.

## Validation and outstanding risks

The test uses the **real C++ v2 writer** to create a JSON document from each
checked-in `.par` source, then checks Python v2→v3→v2 **exact parsed-tree
identity**. Independent synthetic nondefault tests cover crop, orientation,
gamma, widths, final-frame geometry, inactive sharpening controls, Unicode
MTF source labels, scanner channel vs image-layer provenance, unavailable
source/ROI metadata, capture-group reordering, ID validity, source path-base agreement,
missing-source semantics, available/unavailable status consistency,
POSIX/Windows Save As rebasing without touching the original document, and
nested unknown fields. It does not replace the existing v2 corpus test, which separately
checks canonical `.par` output byte-for-byte after a real C++ v2 decode.

Local Debian 13/GCC 14, checking-enabled, native core/CLI build: **128/128
fixtures plus nondefault/MTF rejection tests passed** (reference-only).
Do not mistake this for shipping-v3 or for a fully audited native codec.

To ship schema 3 we still need: (1) complete typed C++ document codecs
with explicit units; (2) stable MTF IDs/groups through GUI editing, Undo
and Save; (3) source-path rebasing on Save As; (4) authoritative JSON Schema
and full fixtures; (5) tests covering every persistent C++ member from
legacy `.par` import to canonical JSON; (6) failure-atomic GUI/CLI/recovery
integration, platform sanitizers and operator testing. **JSON-v2 read
compatibility is expressly out of scope.**


## Native component implementation checkpoint (15 October 2026)

The C++ implementation exposes two *nonshipping* library functions:
`encode_parameter_json_v3_mtf` and `decode_parameter_json_v3_mtf`.
They use the same strict bounded JSON parser and native numeric types as the
current file codec, and operate on exactly the
`calibration.capture.mtf` subtree. Unlike the reversible Python reference,
the native representation omits `legacy_first_same_capture`; a first-record
legacy flag of `true` is rejected rather than silently discarded, pending
an explicit migration policy. The production JSON-v2 save path is unchanged.

The native numerical fitter, capture-wide uncertainty normalization and
channel specialization now use explicit capture IDs, including noncontiguous
records. Schema-3 parsing accepts such groups and ignores the obsolete
`same_capture` field. Remaining work is editor/Undo identity lifecycle and
the complete document codec.
