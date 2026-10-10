# Color-Screen parameter archive format

**Schema v1 compatibility specification.** This document describes the
existing ZIP-based `.cspar` alpha format and its historical structured
supplements. It is **not** the agreed final format. The pre-beta target is a
**single native JSON schema-v2 document**, with no ZIP wrapper or embedded
legacy CSP mirror; see [Plain JSON v2 migration](parameter-json-v2-plan.md).
Work on v2 must preserve these v1 readers but must not extend the legacy
mirror as the final authoritative representation.

## Status

This document defines the post-GUI alpha file-format migration planned before
Color-Screen enters beta. The current `.par` / CSP stream remains readable
indefinitely for backward compatibility. New user-visible parameter saves should
move to the archive format only after the reader, writer, migration tests, GUI,
CLI and recovery paths are all ready together.

The working extension is **`.cspar`** ("Color-Screen parameters"). Existing
`.par` remains a legacy text sidecar and is never reinterpreted by extension
alone.

## Why JSON rather than a general YAML parser

The manifest is UTF-8 JSON. JSON is also valid YAML 1.2 syntax, so the manifest
remains consumable by YAML tooling while Color-Screen avoids adding a second
cross-platform parser dependency solely for configuration. The core library and
command-line utility must not depend on Qt just to read a parameter file.

A general YAML reader would also expose features we do not need and would then
have to constrain for safe deterministic files: application tags, anchors,
aliases, merge keys and implicit scalar typing. The JSON subset gives the same
nested mappings/sequences needed by Color-Screen with one unambiguous type model.

This is a syntax decision, not the compatibility contract. The schema and its
migration rules below are authoritative.

## Physical container

A `.cspar` file is a ZIP archive written with libzip, already a project
dependency. Entry names use forward slashes and UTF-8. Writers emit:

```
manifest.json
state/legacy.par
payload/...
```

`manifest.json` is mandatory. `state/legacy.par` is mandatory in schema
version 1 and contains the complete **legacy-format** CSP plus Qt metadata
payload that the application would otherwise write to `.par`. It provides an
exact compatibility/migration floor and lets old core semantics be validated
byte-for-byte. Fields that legacy CSP cannot represent are authoritative only in
required structured manifest features; readers apply those after parsing the
legacy mirror.

Dense arrays may be moved to typed `payload/` entries as the structured state
grows. A writer must never put large meshes/correction grids into enormous JSON
numeric arrays merely to claim the format is text based.

The ZIP container's own CRC protects entry integrity. A future schema may add a
strong digest to payload descriptors without changing the physical container.

## Manifest root

Schema version 1 begins with:

```json
{
  "format": "org.colorscreen.parameters",
  "schema_version": 1,
  "required_features": ["render-overrides-v1", "geometry-final-frame-v1", "image-area-v1"],
  "generator": {
    "application": "Color-Screen",
    "version": "2.0alpha"
  },
  "state": {
    "legacy_csp": "state/legacy.par",
    "render_overrides": {
      "ignore_infrared": false,
      "demosaiced_scaling": "default",
      "observer_whitepoint": [0.3457, 0.3585],
      "output_profile": "sRGB",
      "output_gamma": -1,
      "gamut_warning": false
    },
    "geometry_final_frame": {
      "final_angle": 90,
      "final_ratio": 1
    },
    "image_area": {
      "enabled": false,
      "rect": [0, 0, 0, 0]
    }
  },
  "payloads": []
}
```

Required root keys:

- `format`: exactly `org.colorscreen.parameters`;
- `schema_version`: positive integer;
- `state`: object describing state entries required by that schema.

`generator` is informational and optional on read.

`payloads` is optional and defaults to an empty list.

## Versioning and feature negotiation

Readers distinguish three cases:

1. unknown `format` — reject the file;
2. known format with a newer `schema_version` — reject unless the reader
   explicitly declares support for that version;
3. known supported version — parse required members, ignore unknown optional
   keys, and reject malformed values.

Schema v1 currently defines three independently negotiated required features:

- `render-overrides-v1`: `state.render_overrides` is present. Its three
  authoritative document inputs are `ignore_infrared`,
  `demosaiced_scaling`, and `observer_whitepoint`. Historical
  `output_profile`, `output_gamma`, and `gamut_warning` keys remain
  required but are strictly validated compatibility placeholders, not
  document state.
- `geometry-final-frame-v1`: `state.geometry_final_frame` supplies the
  authoritative final-image frame angle and axis ratio absent from legacy CSP.
- `image-area-v1`: `state.image_area` is the separately selected bounding
  rectangle of photographic content, distinct from the physical-object crop
  and absent from legacy CSP.

The per-render migration moved output colourspace, encoding gamma, gamut
warnings, and stitched-tile visibility outside document calibration. New
archives write the required schema-v1 compatibility values
`"output_profile":"sRGB"`, `"output_gamma":-1`, and
`"gamut_warning":false`, irrespective of the current view or export.
Historical archives with nondefault typed values remain readable, but those
values cannot override a view or an explicit Render-to-File request.
The appearance tone curve, adapted observer whitepoint, and process-colour
calibration remain persistent processing controls.

Future manifests may add other independently negotiated features, for example a
typed mesh payload. An unknown required feature is a hard error even when the
integer schema version is otherwise recognized. Unknown optional keys and optional payload entries are
ignored. This lets independent optional provenance grow without making every
addition a format bump.

Writers never silently downgrade a file containing a required feature they
cannot preserve.

## Payload descriptors

When a schema stores a dense payload outside JSON, `payloads` records enough
information to interpret it without C++ layout assumptions. Example:

```json
{
  "name": "geometry.mesh",
  "path": "payload/geometry-mesh.f64le",
  "element_type": "float64-le",
  "shape": [48, 64, 2],
  "required": false
}
```

Rules:

- `name` is a stable schema identifier, not a translated UI label;
- `path` must be a relative archive path with no empty, `.`, or `..`
  component;
- `element_type` uses explicitly sized endian-qualified spellings;
- `shape` contains non-negative dimensions and readers must check overflow
  before allocation;
- the byte size must exactly equal the product implied by type and shape;
- duplicate payload names or paths are invalid;
- no payload may be interpreted by casting archive bytes to a C++ struct.

## Security and resource limits

The reader treats the archive as untrusted input.

It must reject:

- absolute paths, backslashes used as path separators, and traversal components;
- duplicate mandatory entries;
- encrypted entries;
- unsupported compression methods;
- a manifest above a conservative fixed limit;
- unreasonable entry counts;
- a payload whose declared dimensions overflow or exceed configured allocation
  limits;
- recursive/nested archives used as parameter payloads.

The reader never extracts a parameter archive to the current working directory.
Small entries are read directly; large typed entries are streamed or copied to a
private temporary file when an existing library API requires a `FILE *`.

## Atomic writes

All frontends retain the same failure contract:

1. serialize the complete parameter payload before replacement;
2. build/finalize the archive in a sibling staging file on the target
   filesystem;
3. atomically replace the target only after successful archive finalization;
4. on any serialization, archive, or replacement failure, leave an older target
   byte-for-byte intact.

The shared `write_parameter_payload_file()` implementation provides this
UTF-8-path contract for archive and core/CLI legacy writes, using POSIX
`rename()` or Windows `MoveFileExW(...REPLACE_EXISTING|WRITE_THROUGH)`.
Qt uses the same core primitive for archives; its existing `QSaveFile` bridge
remains appropriate for GUI-only FILE*/text recovery and legacy-save paths.

## Legacy compatibility

Readers inspect file content, not just suffix:

- plain CSP text is loaded through the existing `load_csp` path;
- ZIP parameter archives are recognized by ZIP signature and validated through
  this format;
- a file that is neither valid CSP nor a valid Color-Screen archive fails with a
  clear format diagnostic.

For schema version 1, archive loading reads `state/legacy.par` into a private
temporary stream and feeds the normal CSP loader. Qt then processes its existing
metadata postamble exactly as for legacy `.par`. The live document is published
only after the whole archive and parameter payload have parsed successfully.

The schema-v1 ZIP writer was the transitional default earlier in 2.0alpha.
New .cspar targets now use native JSON v2 by default (see the v2 plan), while
this ZIP-v1 writer remains an explicit compatibility export. Legacy `.par`
export remains available, and an established target always preserves its
loaded/chosen physical format on ordinary Save.

## Structured migration within schema version 1

Schema version 1 creates the durable container/versioning boundary. Independent
required features can then migrate state incrementally without bumping the whole
schema merely because one domain gains an authoritative structured
representation.

The first migration is `render-overrides-v1`. Its historical schema
requires six typed values, but only three are persistent processing inputs
missing from legacy CSP:

- `ignore_infrared`;
- `demosaiced_scaling`;
- `observer_whitepoint`.

The remaining three required keys, `output_profile`, `output_gamma`
and `gamut_warning`, previously controlled an output/rendering request.
Current readers strictly validate these historical keys and ignore them
rather than apply them to the document. Current writers emit neutral
compatibility values so older schema-v1 readers continue to accept the
archive.

A reader that understands the feature parses the legacy CSP mirror first
and applies the three authoritative processing values. A reader that does not
understand the feature must reject the archive. A manifest containing
`state.render_overrides` without declaring the feature is invalid, as is a
manifest declaring the feature without the complete structured object. This
prevents silent data loss in older alpha readers.

The second feature, `geometry-final-frame-v1`, records two persistent
`scr_to_img_parameters` values ignored by the legacy CSP serializer:
`final_angle` (the angle of the final screen-coordinate axes, in degrees) and
`final_ratio` (their positive axis ratio). Both must be finite numbers and the
ratio must be strictly positive. A feature without the complete section or a
section without the feature is invalid. An archive without this feature retains
the original CSP/default behavior; it is not silently assigned a new final
geometry frame. CLI `adjust-par`, Qt document saves, reproducibility reports
and crash-recovery snapshots must all preserve non-default values.

The third feature, `image-area-v1`, separates the physical **object crop**
(`render_parameters::scan_crop`) from the inner **photographic image area**
(`render_parameters::image_area`). A museum scan may show mounting paper,
binding tape, plate borders or handwritten marks within the object crop. The
image area instead bounds the actual picture. It is stored as
`{ "enabled": true|false, "rect": [x,y,width,height] }`, using unrotated scan
pixel coordinates, a nonnegative integer origin, and positive dimensions when
enabled. The rectangle may not overflow signed 32-bit pixel coordinates.
Disabled means `[0,0,0,0]`. Readers must reject malformed values, missing
or undeclared required sections, and unknown required features. Old archives
without `image-area-v1` retain the legacy behavior without a separate inner
rectangle. The Qt editor draws the image area's boundary while continuing to
show the complete object crop; export size defaults to the inner image bounds
when selected, including through the final-screen transform. Saving an active
inner area to legacy `.par` is refused rather than silently discarding it.

Subsequent work can migrate additional domains into manifest sections while
retaining `state/legacy.par` as a compatibility mirror until all relevant
state has a structured representation.

Candidate sections are:

- `capture`;
- `process`;
- `registration`;
- `reconstruction`;
- `color`;
- `profile`;
- `provenance`.

A migrated section has one authoritative representation per schema version.
Readers must not merge two competing representations silently. During a
transition, the manifest states which representation is authoritative and the
legacy entry is verification/fallback data only.

## Stable values

All enums in structured state use stable ASCII identifiers already used by the
project where available, never raw integer ordinals or translated labels.

Numbers must be finite unless a field explicitly defines a sentinel. JSON
`NaN`, `Infinity`, and `-Infinity` are invalid.

Absent fields receive defaults defined by the schema version, not whatever
default happens to be compiled into a later application. A migration that
changes a historical default must materialize the old semantic value explicitly.

## Testing gate

The default-save gate below is now implemented and must remain green in CI:

- legacy `.par` import;
- archive v1 write/read round trip for representative full ParameterState;
- Qt profile-spot metadata round trip;
- deterministic manifest output for equal logical state;
- unknown optional-key tolerance;
- unknown required-feature rejection;
- newer schema-version rejection;
- malformed/truncated ZIP and manifest failures;
- duplicate/path-traversal/oversized-entry rejection;
- dense-payload shape/byte-count validation;
- transactional GUI load failure with no state/dirty/target mutation;
- atomic save failure preserving an older archive;
- CLI and GUI reading the same fixtures;
- structured render and final-frame geometry feature round trips, mismatch
  rejection, invalid numeric rejection, CLI rewrite and crash-recovery parity;
- Windows, macOS and Linux filenames containing Unicode.

Private crash recovery now uses `recovery_params.cspar` with the complete
native JSON v2 codec and the same atomic replacement primitive, while
retaining schema-v1 ZIP and legacy .par snapshot readers. The lifecycle
smoke covers unclean-shutdown restoration of structured-only values, profile
spots, original target/dirty metadata, corrupt/truncated archives, and old
`recovery_params.par` snapshots. An invalid newer archive never falls back to
an older legacy snapshot.

## Rollout

Implementation status in the alpha tree:

- the strict schema-v1 libzip/manifest core, hostile-input coverage, shared
  content-signature dispatch, UTF-8 host-path handling, and the cross-platform
  atomic replacement primitive are merged;
- Qt archive Open/Save As, format-preserving ordinary Save, recovery-format
  metadata, and transactional Unicode workspace smoke are merged;
- CLI read/write parity, Unicode Windows argv handling, format-preserving atomic
  rewrite, and the Czech/CJK archive fixture are merged;
- automatic image-sidecar discovery now prefers `.cspar` and falls back to
  legacy `.par`, never merging both;
- genuinely new Save As/no-sidecar targets now default to JSON-v2 `.cspar`;
  established ZIP-v1/JSON-v2/Legacy targets remain format-preserving;
- `render-overrides-v1` is the first authoritative structured-state feature,
  retaining three saved processing fields missing from legacy CSP and three
  neutral output-compatibility keys;
- `geometry-final-frame-v1` preserves two final-image geometry fields
  missing from the legacy mirror;
- `image-area-v1` preserves the independently selectable photographic area
  within the outer object crop.

Remaining pre-beta rollout sequence (updated decision):

1. Keep schema-v1 ZIP as a supported importer and explicit compatibility
   writer; do not extend its legacy mirror.
2. Native plain JSON v2 serializes persistent C++ and Qt state directly;
   see [parameter-json-v2-plan.md](parameter-json-v2-plan.md). Seven native
   codecs, the complete document/file writer, a 128-file historical .par
   corpus and a nested-state audit support the migration.
3. New Qt/adjust-par .cspar targets default to JSON v2; ordinary Save and
   in-place rewrites preserve existing ZIP-v1/JSON-v2/legacy encodings.
4. Complete the new-default CI matrix and real GUI operator field testing
   before beta. Color-Screen remains 2.0alpha.
