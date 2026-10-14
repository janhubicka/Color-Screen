# Parameter JSON schema 3 — reference design and migration evidence

**Status: Python reference mapping tested; an isolated native C++ MTF
component codec is now under review.** Neither component constitutes a
complete schema-3 document. Nothing in this document is a promise that
Color-Screen already saves schema 3. The only supported `*.cspar`
writer continues to emit native JSON **schema 2**, and only `.par` and native
JSON files can be imported. The abandoned ZIP-v1 parameter format is removed;
ZIP-based `.csprj` stitching projects remain unrelated and supported.

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
  Import from v2 groups adjacent records when `same_capture` is true. The
  first-record legacy flag is retained only in the **reference** adapter's
  `legacy_first_same_capture`, an acknowledged compatibility wart that should
  not become a production-v3 default. Editor actions must preserve IDs when
  renaming, sorting, inserting or deleting records; generating IDs on each
  Save is not a valid implementation.
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
* Downgrading a v3 document whose capture group is noncontiguous in the
  measurement order fails rather than silently changing group membership.
  An unknown measurement ID, duplicate ID or unknown field also fails.
  Reverse conversion revalidates the strict nested v2 field vocabulary,
  preventing unknown settings in moved contact-copy/denoiser/calibration
  subtrees from being accepted accidentally.

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

To ship schema 3 we still need: (1) complete C++ typed codecs and detailed
value/coordinate units; (2) a real lifetime-stable MTF ID/group model in the
Qt-independent parameters and GUI edits; (3) portable path rebasing during
Save As and import; (4) authoritative JSON Schema and representative full
fixtures; (5) round-trip tests across every persistent nested C++ member;
(6) failure-atomic CLI/Qt/recovery upgrades, platform sanitizers, and operator
field testing. All schema-2 readers remain read-compatible after switching.


## Native component implementation checkpoint (15 October 2026)

The C++ implementation exposes two *nonshipping* library functions:
\`encode_parameter_json_v3_mtf\` and \`decode_parameter_json_v3_mtf\`.
They use the same strict bounded JSON parser and native numeric types as the
current file codec, and operate on exactly the
\`calibration.capture.mtf\` subtree. Unlike the reversible Python reference,
the native representation omits \`legacy_first_same_capture\`; a first-record
legacy flag of \`true\` is rejected rather than silently discarded, pending
an explicit migration policy. The production JSON-v2 save path is unchanged.

Until \`same_capture\` is retired from the numerical fitting path, native
schema-3 component decoding also rejects noncontiguous capture groups instead
of silently evaluating the wrong fitted focus coordinates. This restriction
is narrower than the final schema-3 design; group-aware fitter/editor changes
are required before the full document codec can ship.
