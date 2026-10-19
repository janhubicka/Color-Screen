# Color-Screen 2.0 — stitching project format and JSON migration

## Status: proposed change, not yet implemented

Stitching projects use the `.csprj` extension. **The current core implementation saves and reads a version-1 keyword-based text format**, beginning with `color_screen_stitch_project_version: 1` (see `src/libcolorscreen/stitch-project.C`). Do not confuse this with the separate ZIP-based parameter archive history. The planned 2.0 writer should emit **plain UTF-8 JSON** while retaining the version-1 text reader. Any ZIP-based project variants discovered in the frontend must be inventoried and handled separately before removing their readers.

## Migration requirements

- Keep `.csprj` as the extension; dispatch by actual content/header rather than filename alone.
- Write new projects only as JSON once the migration lands.
- Preserve project-wide settings, source-image references, per-image parameters, stitching geometry and relationships; do not embed large source images by default.
- Resolve relative paths against the project location; allow reassociation of moved images.
- Include an explicit schema/version marker and clear errors for unsupported versions or malformed data.
- Retain the legacy version-1 text reader; do not silently reintroduce a legacy writer.

## Verification before documenting a normative schema

Test JSON save/load/save round trips, old version-1 text load followed by JSON save, missing or moved images, relative and absolute paths, and representative multi-image stitching projects. Compare reconstructed project state, not merely text formatting. Only after implementation should this page list exact JSON keys and defaults as authoritative.
