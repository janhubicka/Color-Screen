# Color-Screen 2.0 — stitching project JSON format

A `.csprj` file is a **plain UTF-8 JSON document**, with no ZIP wrapper or embedded image pixels. The native writer uses the project identifier `org.colorscreen.stitch-project` and project `schema_version` **2**. Each tile stores only its screen type and a **schema-3 geometric mapping subtree**, using the same typed codec as [`.cspar`](Color-Screen-2.0-Parameter-Format.md). It does not include irrelevant per-tile color, capture or MTF defaults.

This is the source-controlled format reference for the 2.0 migration in [PR #561](https://github.com/janhubicka/Color-Screen/pull/561); the published rolling build may precede that change until it is merged and packaged.

## Project JSON structure

| Field | Type | Interpretation |
|---|---|---|
| `format` | string | Exactly `org.colorscreen.stitch-project` |
| `schema_version` | integer | Exactly `2` |
| `grid` | object | Required integer `columns` and `rows` (each 1–10) |
| `pixel_size` | finite number | Pixel scale used by stitching/output |
| `tiles` | array | Exactly `columns × rows` records, in row-major order |

Each entry of `tiles` contains precisely these fields:

| Field | Type | Interpretation |
|---|---|---|
| `filename` | UTF-8 string | Source-image filename or relative path |
| `angle`, `ratio` | finite numbers | Detected screen angle and ratio |
| `position` | array of 2 finite numbers | Tile's upper-left position in common screen coordinates |
| `image_size` | array of 2 integers | Source image width and height in pixels |
| `screen_size` | array of 2 integers | Tile width and height in screen coordinates |
| `screen_shift` | array of 2 integers | Tile offset in screen coordinates; may be negative |
| `screen_geometry` | JSON object | Exactly `screen_type` (string) and `geometry` (object); `geometry` uses the same keys and units as the `geometry` section of schema-3 `.cspar`, including any mesh |

The `.cspar` schema-3 codec validates the `screen_geometry` contents against its native typed geometric mapping. A file containing extra capture, color, calibration, MTF or reconstruction settings is **rejected**, rather than accepting values that the stitching project would silently discard. No legacy `.par` text or processing-document defaults are embedded.

File keys are validated strictly: unknown or missing fields, unsupported versions, incorrect grid dimensions and invalid processing parameters cause a load error. Parsing is bounded to prevent unexpectedly large projects. The file is serialized in a consistent order.

## Paths and portability

Source images remain external to the project. Relative filenames are resolved from the project file's directory, not the process's current working directory. Moving a project together with its relative-path images therefore preserves the references.

The project file does not contain the source scan itself. Keep your scans when archiving a project.

## Older projects and migration

The **actual historical stitching-project format in `libcolorscreen` is text**, starting with:

```text
color_screen_stitch_project_version: 1
num_images: 2 1
pixel_size: 1.0
```

It then stores tile geometry with `stitch_image_...` keywords and a legacy CSP parameter block per tile. The original text reader remains available; opening an old project and saving it with the new writer produces JSON. The reader chooses between JSON and legacy text by the first non-whitespace character, not by the `.csprj` extension. The former ZIP archives mentioned in some alpha-era parameter documentation were *parameter-file* experiments, not this core stitching project format.

New files are JSON only; there is no legacy project writer.

## Tests

The implementation includes native JSON encode/decode, deterministic save/load/save, malformed-field rejection, project `FILE*` saving/loading, version-1 text import and conversion, and bounds checks on legacy grid dimensions. Exact test results are tracked with PR #561.
