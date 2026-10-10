# Schema-v2 state-ownership audit

This document supplements [the JSON-v2 migration plan](parameter-json-v2-plan.md)
and the machine-checked [legacy keyword inventory](../testsuite/parameter-v2-key-inventory.tsv).
It tracks *live C++ parameters* as well as historical CSP keywords.

## Source audit, 10 October 2026

The baseline corpus consists of 4 `.par` fixtures from `testsuite/` and
124 from `examples/amcolony/`, found via a recursive repository-tree scan.
All 128 currently pass canonical `.par` → native JSON v2 → `.par`
byte-for-byte comparisons, plus canonical JSON resave, on Ubuntu
AMD64, ARM64 and in the source release `make distcheck`.

The following exact C++ comparison members were also cross-checked against
the native v2 component encoders/decoders. These counts are **members checked**,
not numbers of JSON keys:

| Stateful C++ type | Members inspected | Native JSON ownership |
| --- | ---: | --- |
| `render_parameters::operator==` | 51 | `capture`, `process`, `reconstruction`, `sharpness`, `color`, `correction_grids` |
| `scr_to_img_parameters::operator==` | 15 | `geometry` (including mesh, lens and final frame) |
| `solver_parameters::operator==` | 4 | `registration` |
| `mtf_parameters::equal_p` | 13 | `sharpness.mtf` |
| `sharpen_parameters::equal_p` | 10 | `sharpness` |
| `denoise_parameters::equal_p` | 8 | `reconstruction.screen_denoise` and `demosaiced_denoise` |

All members named by these comparisons are represented in native v2 code;
this was checked against the actual serializers, not solely the CSP output
keywords. **The full-document decoder also checks the complete schema-v2
object-key vocabulary**, including nested MTF records and per-tile corrections.
Unknown fields fail with an explicit error rather than disappearing on Save;
dedicated regression tests exercise those cases without mutating live state. The independent detection state (`black`, `red`, `green`,
`blue`, `min_luminosity`, `min_ratio`) is represented in `detection`.
All retained MTF curve sample data and metadata are encoded separately from
the MTF model, including frequency, contrast, uncertainty, native channel,
image-layer indicator, wavelength, source location/size, ROI, edge endpoints
and quality diagnostics.

### Bug found

`render_parameters::operator==` failed to compare `temperature`, the saved
photograph illumination temperature, even though it compared the separate
`backlight_temperature`. This could make a photograph-temperature change
invisible to Qt's modified/Undo checks. The missing comparison has been
added and a core unit regression verifies both temperatures independently.
The two values already had separate JSON v2 fields and round-trip coverage.

### Complex resources

- Optional `mesh_trans` is stored as dimensions, physical shift/step,
  direction and a row-major array of [x,y] vertices; no C++ object pointer or
  opaque binary is serialized.
- Optional backlight correction is stored as all four channel flags, every
  luminosity and subtraction sample and black-reference mode, including
  disabled channels. The separate `backlight_correction_black` scalar is
  saved under `capture`.
- Global and per-tile scanner blur corrections record physical mode, grid
  dimensions and all stored correction cells. Cache-derived diagnostics are
  intentionally excluded. Stitched-tile exposure, dark point and grid shape
  are persisted.
- User tone curves persist the selected type **and all control points**, even
  when the curve is currently inactive. Both denoising stages similarly save
  inactive editable settings.
- Qt `profileSpots` are saved directly in the native `profile` section;
  derived `color_match` results are not document state.

### Lossless compatibility export

Legacy `.par` predates five categories of authoritative structured-only fields:
final-frame angle/ratio, photographic image area, ignore-infrared choice,
reconstruction scaling and observer whitepoint. Exporting a nondefault setting
in any of these categories to `.par` is intrinsically lossy. Qt and CLI now
call the same `legacy_csp_can_represent_parameters()` preflight **before any
write**, rejecting the downgrade and directing the user to `.cspar` instead.
Core tests exercise each setting independently and the CLI negative test checks
that a requested `.par` target is not created. The ordinary historical `.par`
workflow remains valid when the structured-only values are at defaults.

### Runtime-only values (do not reintroduce)

Monitor/export output colourspace/ICC profile and output transfer gamma,
per-view gamut warning, transient solver state and caches, and per-view tile
visibility are supplied by the renderer/view rather than persisted into
document parameters.

## Remaining validation gates

The member audit is stronger than the old-keyword list but **not a formal proof
of all serialization invariants**. Review pointers whose value equality differs
from identity equality (mesh, backlight and scanner-blur resources); verify
schema-v2 handling of null/default combinations and imports with old optional
postambles. Add new fixtures when a genuinely persistent field is added, and
keep the full 128-file corpus test mandatory.

Before changing the default new-file Save format or merging the PR, run the
final complete Ubuntu checking, Windows GCC/Clang, macOS and dedicated sanitizer
matrix and review all platform/Qt smoke results. The product remains 2.0alpha.
