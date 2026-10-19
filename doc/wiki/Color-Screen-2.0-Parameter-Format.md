# Color-Screen 2.0 — parameter file format

## Overview

The current native `.cspar` parameter file is **UTF-8 JSON (schema 3)**, not a ZIP archive. It stores processing settings and optionally a reference to the corresponding source image, not the scan's pixel data. Older `.par` files remain readable for migration.

## Compatibility policy

There are no distributed legacy ZIP-based `.cspar` files that need write compatibility. New saves use JSON schema 3. `.csprj` stitching projects are a separate format; the new schema-2 JSON project document contains schema-3 parameter objects for each tile. Its historical reader accepts version-1 keyword text.

## What the file describes

The persistent configuration covers screen identification/geometry and registration, image-area selection, demosaicing and reconstruction, color/tone and relevant processing adjustments. Source-image association allows a saved parameter file to reopen its scan; if that scan has moved, a replacement may be selected.

Display/output-specific configuration (such as output ICC profile, output gamma and per-view gamut warning) is not intrinsic to the saved processing parameters. Rendering targets may require different output configurations without changing the underlying reconstruction settings.

## Validation and units

**Important:** a JSON reader accepting a numeric value does not establish that it is physically meaningful. Keep three notions separate when extending this reference: (1) the type and constraints enforced by the reader; (2) the quantity's units and interpretation; (3) recommended operational ranges. Do not invent a strict limit merely because a UI slider has a narrower range.

## Editing and recovery

Use the GUI to save parameter files whenever possible. For hand editing, preserve valid JSON syntax and retain unknown settings only if the running reader supports them. Keep a copy before converting old `.par` files. A `.cspar` file alone is not a backup of the source scan.

**Further reference work:** enumerate every schema-3 key and exact parser validation rule directly from the current serialization code before calling this a complete normative schema reference.
