# Color-Screen parameter archive format

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
version 1 and contains the complete CSP plus Qt metadata payload that the current
application would otherwise write to `.par`. It provides an exact migration
floor while the structured schema is introduced and lets old core semantics be
validated byte-for-byte against the archive reader.

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
  "generator": {
    "application": "Color-Screen",
    "version": "2.0alpha"
  },
  "state": {
    "legacy_csp": "state/legacy.par"
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

Future manifests may contain:

```json
"required_features": ["structured-render-v2", "mesh-f64le-v1"]
```

An unknown required feature is a hard error even when the integer schema version
is otherwise recognized. Unknown optional keys and optional payload entries are
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
UTF-8-path contract for fresh archive and core/CLI legacy writes, using POSIX
`rename()` or Windows `MoveFileExW(...REPLACE_EXISTING|WRITE_THROUGH)`.
`rewrite_parameter_archive_payload_file()` uses the same sibling-staging
replacement contract when editing an existing archive. Qt uses these core
primitives for archives; its existing `QSaveFile` bridge remains appropriate
for GUI-only FILE*/text recovery and legacy-save paths.

## Forward-preserving archive edits

A reader may ignore unknown optional manifest keys, but an editor must not turn
"ignored" into "deleted". When a frontend loads a supported archive and saves a
derived archive from that same source, it must use the archive-preserving rewrite
path:

- validate the source archive, schema version and required features first;
- preserve `manifest.json` bytes unchanged;
- preserve the uncompressed contents of every archive entry other than the
  manifest-selected legacy CSP entry;
- replace only that legacy CSP entry;
- reject unsupported required features before creating a replacement target.

The implementation copies non-legacy entries through libzip's compressed-source
path so dense future payloads do not need to be inflated into application
memory. ZIP timestamps, comments and compression choices are container metadata,
not schema state; future Color-Screen features must put required information in
the manifest or entry contents rather than depending on opaque ZIP metadata.

Fresh Save As to a new archive is intentionally different: it creates a fresh
archive from the current document and must not inherit unknown entries from an
unrelated file merely because the destination path already exists.

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

The writer should initially support an opt-in/new Save-As path and only become
the default after cross-platform round-trip fixtures are established.

## Structured migration after version 1

Version 1 intentionally creates the durable container/versioning boundary before
reimplementing every historical CSP keyword. Subsequent work can migrate
individual domains into manifest sections while retaining `state/legacy.par`
as a compatibility mirror until all readers use the structured representation.

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

## Archive regression gate

The default has moved to `.cspar`; the following remain permanent CI
requirements:

- legacy `.par` import;
- archive v1 write/read round trip for representative full ParameterState;
- Qt profile-spot metadata round trip;
- deterministic manifest output for equal logical state;
- unknown optional-key tolerance and byte-preserving rewrite of optional
  manifest data/opaque entries;
- unknown required-feature rejection before any archive rewrite;
- newer schema-version rejection;
- malformed/truncated ZIP and manifest failures;
- duplicate/path-traversal/oversized-entry rejection;
- dense-payload shape/byte-count validation;
- transactional GUI load failure with no state/dirty/target mutation;
- atomic save failure preserving an older archive;
- CLI and GUI reading the same fixtures;
- Windows, macOS and Linux filenames containing Unicode.

Recovery should switch formats only after the same tests cover unclean-shutdown
restore. Until then it may keep the internal legacy payload even after ordinary
user Save As supports `.cspar`.

## Rollout

Implementation status in the alpha tree:

- schema-v1 libzip/JSON infrastructure, hostile-input validation, UTF-8 host
  paths and cross-platform atomic replacement are merged;
- Qt and CLI both read/write `.cspar`, preserve physical format for established
  targets, and exercise Czech/CJK Unicode archive filenames in CI;
- archive-first image sidecars and the `.cspar` default for genuinely new
  parameter targets are merged; explicit legacy `.par` remains available;
- this step makes edits of an existing supported archive forward-preserving:
  optional manifest bytes and opaque entries survive while only
  `state/legacy.par` is replaced.

Remaining sequence:

1. Migrate high-value structured sections incrementally, using optional mirrors
   first and an explicit schema/required-feature transition before any section
   becomes authoritative over `state/legacy.par`.
2. Move dense geometry/correction data to typed payload entries where it brings
   a real size/precision benefit.
3. Keep crash recovery on its internal legacy payload until archive recovery has
   equivalent unclean-shutdown coverage.
4. Only after the structured migration and remaining alpha gate are green,
   advance the product version toward beta.
