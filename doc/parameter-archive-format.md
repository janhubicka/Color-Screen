# Color-Screen parameter file format

**Status: canonical pre-2.0 design, not yet the active writer.** This document
replaces the historical ZIP-based alpha proposal, which was never used outside
development. See [the current implementation tracker](parameter-json-migration-tracking.md)
and the [schema-3 reference design](parameter-json-v3-reference.md).

## Compatibility boundary

The only real historical parameter files are plain-text `.par` files.
The existing `.par` reader and supported (loss-checked) legacy export remain
available. Early ZIP-v1 and JSON-v2 `.cspar` implementations were unreleased
experiments. There are **no JSON parameter files requiring a compatibility
reader, format-preserving rewrite, or downgrade converter**.

ZIP-based `.csprj` stitching projects are separate and remain supported.

## Physical representation

A `.cspar` file is one UTF-8 JSON document; no ZIP wrapper, manifest,
embedded CSP text, or binary side payload is required. JSON is also valid
YAML 1.2 syntax, without importing YAML aliases/tags into our input language.

The current semantic design uses the `org.colorscreen.parameters` format
identifier and an integer `schema_version` (internally version 3 while
development revisions are being consolidated). The exact first-shipping
version marker should be finalized before beta; no alpha JSON files need
migrating.

An illustrative *structure*, not a complete valid fixture:

```json
{
  "format": "org.colorscreen.parameters",
  "schema_version": 3,
  "capture": {},
  "process": {},
  "geometry": {},
  "registration": {},
  "calibration": {},
  "reconstruction": {},
  "color": {}
}
```

Every required object is populated according to the authoritative schema;
the empty objects above are merely an overview, **not** valid input.

## Persistent ownership

| Section | Responsibility |
| --- | --- |
| `capture` | Input/camera/RAW interpretation, linearization, orientation, physical-object crop and inner photographic area, gain/black offset |
| `process` | Historical screen type and strip geometry, dye measurements and aging, color model and contact-copy response |
| `geometry` | Screen-to-image mapping, scanner imaging model, lens distortion, accepted meshes and final output coordinate frame |
| `registration` | Accepted control points, detection/reference colors and fitting policy |
| `calibration` | Capture MTF curves and metadata, device/process color references and profile spots, flat field, scanner blur and tile grids |
| `reconstruction` | Image-layer mixing, collection, screen demosaicing, resampling, denoising and sharpening |
| `color` | Photograph/backlight illuminants, observer adaptation, balance, saturation and tone curve |

Do not save monitor/output ICC profiles, display gamma, per-view gamut
warnings or tile visibility, cache and scratch budgets, GUI section
expansion, temporary workers, derived plots, preview buffers or fitting
scratch. Those are rendering or application preferences, not calibrated
document inputs. Keep `libcolorscreen` completely independent of Qt.

## Explicit semantics

Input linearization distinguishes sRGB from a numeric gamma exponent.
Cropping distinguishes the complete mounted physical object (including
binding tape) from the photograph's own inner rectangle, with named
pixel coordinates. `null` means a genuinely absent optional region.
Preserve accepted final-frame angle and axis ratio even when the
corresponding GUI controls are not currently active.

Store the unit or coordinate convention explicitly: pixel pitch in
micrometres, wavelength in nanometres, defocus in millimetres, screen
frequency in cycles per input pixel and known percentage/fraction fields
in their defined domains. Avoid reusing a special numeric value to mean
"automatic" when a named mode is clearer. All inactive but meaningful
calibration inputs remain serializable.

### Capture MTF measurements

The `calibration.capture.mtf.measurements` array contains stable
document-local `id` values and independent `capture_group_id` values.
Selection refers to an ID, never the position of a row or a translated
label. Records of the same capture may be nonadjacent; the numerical
MTF fitter and per-channel sharpening must group by capture ID.

The legacy `.par` reader maps its adjacent `same_capture` flags into
capture IDs **once when importing**. A first-record same-capture flag
has no predecessor and is semantically irrelevant. The canonical JSON
does not persist it or any `legacy_first_same_capture` helper.

Source provenance represents a missing filename as `null`, otherwise
records the path spelling and whether it is absolute or relative to the
parameter document. Dimension/ROI availability is explicit, while
unavailable legacy values are retained when they carry information.
Source-path rebasing on Save As must leave the referenced absolute image
unchanged, falling back to an absolute path for a Windows cross-volume
move. Ambiguous drive-relative paths, malformed UNC/root-only paths and
embedded NULs are invalid.

### Validation and atomicity

Readers verify the exact format/version, strict nested member vocabulary,
enum spellings, finite numbers, integer ranges, independent ID uniqueness,
selected-ID referential integrity, measurement sample ordering and bounds
on arrays, nesting, total nodes and serialized size. Unknown authoritative
fields and future versions are rejected, not silently lost.

A full document is parsed into temporary typed C++ values before any
live document state changes. Saving validates all domains before writing a
same-directory staging file, then atomically replaces the target. Failed
Open, Save, Save As or crash recovery must preserve the original document,
Undo state, dirty state, filename and previously saved bytes.

## Release validation

- Parse every checked-in historical `.par` fixture (currently 128) and
  write the canonical JSON form, then reopen and compare all persistent
  typed values and rendering results.
- Exercise rich nondefault and inactive parameters, meshes/grids, profile
  spots, Unicode source paths, nonadjacent MTF groups and missing metadata.
- Check deterministic JSON rewrite, malformed/future/unknown input
  rejection, resource budgets, symlink/overwrite safety and transaction
  nonmutation.
- Verify GUI Open/Save/sidecars/recovery and CLI conversion use the same
  library API, with Qt offscreen smoke and cross-platform sanitizer builds.
- Test real 150+ MP images and stitched projects under memory pressure before
  beta.

The schema-3 GUI/CLI/recovery switch is implemented on draft PR #556 but
**not yet merged or release-qualified**. Full platform CI, canonical .par
corpus, failure-atomic Save As/Undo and large real-image operator tests remain
beta gates. The implementation deliberately rejects unreleased JSON-v2 files.
