# Canonical parameter JSON schema 3 — implementation contract

**Status: draft implementation under CI, not yet merged or released.**
See the [implementation tracker](parameter-json-migration-tracking.md) and
the [parameter-file specification](parameter-archive-format.md).

Only old plain-text `.par` files exist outside development. There is no
compatibility requirement for the discarded ZIP archive or unreleased JSON-v1
or JSON-v2 experiments. The first supported `.cspar` is a single UTF-8
JSON document. Stitching `.csprj` projects are a separate format: the new project JSON schema embeds only the schema-3 geometric mapping and screen type per tile, while retaining the older version-1 keyword-text project reader.

## Structural ownership

| Root section | Authoritative persistent data |
| --- | --- |
| `capture` | Capture type and RAW method; sRGB/power input linearization; input orientation, physical-object crop and inner photograph area; gain and black level |
| `process` | Historical screen/process type, red/green strip fractions, dye colour model, density, age, contact-copy response and emulsion curve |
| `geometry` | Scanner imaging model, mapping centre/axes/tilt/projection, lens model, accepted mesh and final rotation/angle/ratio |
| `registration` | Screen-detection reference colours, optimization policy and accepted control points |
| `calibration` | MTF model and measured curves, scanner primaries, process colour profile and calibration spots, backlight/flat-field grids, spatial scanner blur and tile adjustments |
| `reconstruction` | Image-layer mixer, collection and resampling policy, pre/post denoising and persistent sharpening settings |
| `color` | Photograph and backlight illuminants, observer whitepoint, white/dye balance, brightness, saturation and output tone curve |

Monitor/output ICC profile, output transfer gamma, per-view gamut warning,
view tile visibility, cached pixels, global cache budgets, scratch preferences,
worker controls and fit scratch/plots are not document state.
`libcolorscreen` remains Qt-independent.

## Explicit field semantics

### Optional source-image document link

Top-level `source_image` is `null`, absent for earlier development-generated
reusable documents, or an object with `path`, `base`, `width_px`,
`height_px`, and `size_bytes`. The path is relative to the containing
`.cspar` or an unambiguous absolute path. Decoded dimensions must agree
before applying crop, registration, or calibration. A changed on-disk byte
size requires confirmation or a different scan; matching byte size alone
does not establish image identity. Zero means unknown source byte size.

Direct `.cspar` Open (Ctrl+O, command line, Explorer/Finder or Linux desktop)
loads the linked image before its processing parameters. Missing sources
first try a unique nearby candidate, then offer a nonblocking Locate image
chooser without overwriting the .cspar. A manual relink is dirty until the
operator saves the updated reference. Source-free parameter sets ask for an
image. Legacy `.par` remains a separate parameter-only import.

### Capture and process

`capture.linearization` distinguishes `{"mode":"srgb"}` from
`{"mode":"power","exponent":2.2}`; this is **input** decoding rather
than display/output gamma. The legacy `.par` value `gamma=-1` imports
as sRGB. The special exponent `-1` is not a power-law encoding.

The selected outer physical-object crop and inner photographic area use
named pixel fields `x_px`, `y_px`, `width_px`, `height_px`; a missing
area is `null`, not a four-integer zero array. These bounds refer to the
decoded input at its chosen RAW resolution, not final stitched/screen
coordinates. Signed crop origins are preserved.

`capture.levels.black_offset` and `gain` apply as
`(input - black_offset) * gain`; gain is not measured in stops.

`process.screen.strip_widths` uses explicit `mode=automatic`
or `mode=fraction` plus a nonzero numeric `value`. MTF frequency is
cycles per input pixel, while scanner capture resolution is `scan_dpi`,
pixel pitch is micrometres, defocus is millimetres and wavelength is
nanometres. More detailed coordinate/units tables remain a beta audit item.

### Capture MTF model

`calibration.capture.mtf.measurements` is an array of named records with
stable document-local `id` and `capture_group_id`. Curve selection is
an ID or `null`, **not** an array index. A capture group may have
nonadjacent records after user sorting: fitting, shared defocus/fallback
blur, relative uncertainty weighting and per-channel sharpening all use
the explicit group IDs.

Historical `.par` imports convert adjacent `same_capture` flags into
groups **once**, at load time. The first record's `same_capture` flag has
no predecessor and is not part of the JSON model. No
`legacy_first_same_capture` field or reverse JSON-v2 adapter is retained.

Each measurement stores channel, image-layer flag, wavelength, label, sample
triples, explicit `sample_columns` unit labels, quality diagnostics
(`angle_deg`, `fit_rms_px`, `contrast_fraction`, `snr`,
`phase_coverage_fraction`), and source provenance.
Unavailable source/ROI/dimensions are explicitly tagged; historical
sentinel values are carried when necessary to preserve meaningful input.

Source paths use `null` for missing, otherwise `value` plus
`base=absolute` or `base=parameter_file`. Drive-relative Windows
paths, ambiguous rooted spellings, malformed UNC paths and embedded NULs
are rejected rather than treated as working-directory-relative paths.

On GUI Save As, CLI conversion or recovery, references must still designate
the same source image. POSIX/Windows/UNC path rebasing is implemented in
Qt-independent C++; Windows cross-volume Save As falls back to an absolute
reference. Successful Save As rebases both the live document and its
Undo/Redo source snapshots. The entire operation is staged and failure-atomic.

Legacy `.par` export remains supported only when all its persistent values
and capture grouping can be expressed without changing their meaning.
Nonadjacent capture groups cannot be written back to its adjacency-only
representation and must reject before truncating an output file.

## Format implementation

The native C++ core exposes a complete
`encode_parameter_json_v3_document` /
`decode_parameter_json_v3_document` pair and atomic UTF-8 path APIs.
Qt document Open/Save/Save As/recovery and the CLI `adjust-par` now use
these APIs on **draft PR #556**. A valid `.par` load supplies defaults
and imported calibration fields; a v3 input file must pass the exact
format/version and strict field/ID checks before any live state is changed.

Some non-MTF sections still share **private typed C++ v2 component
encoders/validators** with the old development code. Their temporary
syntax-tree bridge is implementation detail, not a JSON-v2 reader,
on-disk compatibility guarantee or Python translation. Refactoring it
away without changing the first-public schema remains desirable.

The only supported shipped root identity will be
`format = "org.colorscreen.parameters"` and
`schema_version = 3`. Unknown nested authoritative members,
unsupported future versions, dangling/duplicate IDs, malformed scalars,
bad resource budgets, inconsistent availability flags and ambiguous
source provenance are rejected transactionally.

## Validation evidence and outstanding gates

The first full PR #556 CI compiled the native code on normal Ubuntu and
macOS and passed **73/73 core unit-test groups**. The same run's
`make check` and `distcheck` failed because the generated
`testsuite/Makefile.in` still listed a deleted v2 test, not because the
73 core tests failed. That generated manifest has since been corrected;
a separate NSIS `amd64-unicode` plugin-discovery issue was also patched.
Final exact-head CI results remain pending.

The release checks are:

- All historical `.par` fixtures (128 in the checked-in corpus) converted
  to v3, reopened, compared by typed state and rendering, and rewritten
  canonically, with no undocumented data loss.
- Rich nondefault and inactive settings: geometry/meshes, flat-field/tile
  correction grids, profile spots, MTF evidence, Unicode metadata and all
  seven persistent subsystems.
- Hostile or unsupported JSON, missing/incorrect fields and resource-bound
  inputs rejected without modifying any document, target file, Undo state or
  recovery snapshot.
- Qt offscreen document/workspace smoke including relative MTF source
  Save As, preserved Undo/Redo references and failure-atomic recovery.
- Complete Ubuntu, macOS, Windows GCC/Clang, sanitizers and
  `make distcheck` on the final integration head.
- Manual 150-megapixel-plus operator testing, stitching/large-memory use,
  and an authoritative JSON Schema with representative complete examples.

**Do not declare schema 3 shipping or 2.0 beta until these pass.**
