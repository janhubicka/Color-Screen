# Color-Screen 2.0 — stitching project format and JSON migration

## Status: proposed change, not yet implemented

Stitching projects use the `.csprj` extension. The historical project representation is a ZIP archive. The planned 2.0 writer should instead emit **plain UTF-8 JSON** while preserving a reader for the original ZIP representation. No distributed legacy ZIP projects are known, so new writes need not retain the old container.

## Migration requirements

- Keep `.csprj` as the extension; detect legacy ZIP versus JSON by file signature/content, not filename alone.
- Write new projects only as JSON once the migration lands.
- Preserve project-wide settings, source-image references, per-image parameters, stitching geometry and relationships; do not embed large source images by default.
- Support relative paths resolved from the project location; when an image has moved, allow reassociation without discarding the project.
- Provide explicit schema/version identification and errors for unsupported versions or malformed data.
- Retain the legacy ZIP reader, but do not silently reintroduce a ZIP writer.

## Verification before documenting a normative schema

Test JSON save/load/save round trips, old ZIP load followed by JSON save, missing or moved images, relative and absolute paths, and representative multi-image stitching projects. Compare reconstructed project state, not merely text formatting. Only after implementation should this page list exact JSON keys and defaults as authoritative.
