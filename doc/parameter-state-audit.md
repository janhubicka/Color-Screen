# Parameter-state ownership audit (2.0alpha)

This is an audit of the **meaning** of fields in the processing parameter
structures, not merely a list of fields missing from legacy CSP. A field that
isn't serialized may be an obsolete temporary, an algorithm-local working value,
or a real processing/calibration input that needs a structured archive feature.
Do not automatically migrate every member into `.cspar`.

## Confirmed obsolete members: removed

- `solver_parameters::weighted` and `solver_parameters::center` were not
  exposed by the current Qt GUI, were initialized to `false` and `{0,0}`,
  and were not serialized by the existing CSP format. The active nonlinear
  mesh fit instead calls `homography::get_matrix` with
  `solve_image_weights`/`solve_screen_weights` and the **current mesh
  point** as its local weighting centre. Ordinary global solver and lens
  fitting use the unweighted path. These two members have been removed from
  the solver parameter structure, its equality/copy logic, and global solver
  call sites; local mesh weighting is unchanged.
- The draft `solver-options-v1` proposal (PR #529) was incorrect and was
  closed rather than merged. No `solver_options` feature is part of the
  intended parameter-archive contract.

## Further candidates to review

| Field or API | Evidence in the current tree | Suggested action |
| --- | --- | --- |
| `render_parameters::image_area` | Independent photographic-image bounding area, distinct from the physical-object crop. Used by focus/finetune and pixel-size estimation but not represented in legacy CSP. | **Keep.** GUI selection and structured archive persistence are implemented and full-CI tested separately in PR #531 (stacked on #528); `scan_crop` may include bindings and tapes. |
| `render_parameters::tile_adjustment::{x,y}` | The adjustment array is indexed by row/column and stores dimensions; per-element bytes were not initialized, compared or saved. | **Removed in this branch**, retaining real tile adjustment values. |
| `render_parameters::tile_adjustment::enabled` | Former Tiles checkbox and asynchronous readiness flag, never in legacy CSP. | **Removed in PR #535.** User visibility is a view-local render-request mask; physical readiness comes from loaded tile data and triggers no Undo/dirty change. Saved per-tile exposure, dark-point and blur remain untouched. |
| `solver_parameters::copy_without_points()` and `solver_mesh(..., sparam2, smap, ...)` | Mesh code copies lens/tilt policy to a scratch `solver_parameters`, but the local fit uses nearby points and its own weighted homography. | **Deferred at user's request.** No saving impact; leave the API alone for now. |
| `render_parameters::demosaic` | An image-loading choice, rather than a rendering step. Correctly persisted so RAW capture can be reopened with the same demosaicer. | **Keep unchanged** in this alpha. |
| `render_parameters::output_profile` | Output colourspace selection, not a property of the scanned object. | **Moved to `render_output_parameters` in PR #535.** The View menu sets each display; Render to File chooses an export target. Historical v1 archives require a neutral typed key. |
| `render_parameters::gamut_warning` | View/render-only gamut diagnostic. | **Moved to `render_output_parameters` in PR #535.** Independent of saved calibration and Undo. Historical schema-v1 keys still validate. |

## Output-request ownership decision (2.0alpha)

**Implementation in PR #535:** `render_output_parameters` is an immutable
per-render request value carried alongside `render_type_parameters`, not a
member of `render_parameters` or saved `ParameterState`. It contains:

- output profile/colourspace selection (default sRGB);
- output gamma/transfer (-1 for the sRGB curve, 1 for linear), without a
  processing-panel control;
- gamut warning as a diagnostic for that render;
- an optional grid-shaped enabled/disabled stitch-tile mask, defaulting to
  all tiles visible.

The GUI View menu still selects the *display* profile and warning of the
current canvas, and Render to File still exposes an explicit export profile.
These are presentation/export choices, not reconstruction settings. A future
ICC-/wide-gamut/HDR-aware output pipeline should resolve the actual target
display profile and transfer for each view (including 10/16-bit HDR surfaces),
rather than store a single document gamma. This request structure is the
starting boundary, not an implementation of monitor ICC/HDR handling.

**Compatibility:** existing `.cspar` schema-v1 render overrides continue to
require typed `output_profile`, `output_gamma`, and `gamut_warning` keys
because old alpha readers insist on them. New archives write
sRGB/-1/false; all three historical keys are parsed strictly but ignored
when reconstructing document state. The persistent render overrides remain
`ignore_infrared`, `demosaiced_scaling` and `observer_whitepoint`.
The outer object crop, photographic bounds, final axis geometry, scan settings,
output appearance tone curve, and capture calibration remain persistent.

**Stitched images:** `tile_adjustment::enabled` is not saved and no longer
exists in the calibration grid. Per-view tile visibility is a request-local
mask. Physical readiness is represented by loaded tile image data, and worker
completion only triggers canvas updates; it cannot change the document,
Undo or a deliberate view mask. Per-tile exposure, dark-point and blur
corrections remain saved.

**Demosaic follow-up (separate project):** retain the saved `demosaic`
choice. Consider an immutable, on-demand image-data cache keyed by decoder
and demosaic parameters, preserving the original mosaic/non-demosaiced input
for fast reprocessing. Bound memory with LRU eviction and lifetime-safe shared
ownership, and ensure that cancellation and scan replacement cannot publish
results from an obsolete request. Do not integrate that larger storage/decoder
refactor into a renderer-output migration.

## MTF persistence audit: separate real values from bookkeeping

The inspection compares `src/libcolorscreen/include/mtf-parameters.h` with
the actual `save_csp()` and `load_csp()` in
`src/libcolorscreen/loadsave.C`. Absence of a legacy `.par` keyword is a
reason to investigate ownership, **not** automatic grounds for removal.

| Field(s) | Legacy `.par` and active code | Decision |
| --- | --- | --- |
| `mtf_parameters::wavelength` (scalar) | **Not saved** as an independent global wavelength. Runtime channel specialization, finetune and CLI one-off model evaluation assign it; persistent native channel wavelengths and measurement wavelengths are separate. | **Removed by separate PR #532**, replaced by operation-local wavelength arguments with full cross-platform and sanitizer validation. Native channel and measurement metadata remain persistent. |
| `mtf_parameters::wavelengths[4]` | Saved via `scanner_mtf_channel_wavelengths_nm`; used by the GUI and physical model. | **Keep persistent.** |
| `model`, `sigma`, `halo_fraction`, `halo_sigma`, `blur_diameter`, `defocus`, `f_stop`, `pixel_pitch`, `sensor_fill_factor`, `scan_dpi` | Explicit `scanner_mtf_*` and `scan_dpi` keys persist them; they affect analytical/modelled sharpening. | **Keep persistent.** |
| `measured_mtf_idx`, curve list and its frequency/contrast/uncertainty samples | Serialized; selected curves and uncertainties affect deconvolution and numerical fitting. | **Keep persistent.** |
| `mtf_measurement::{channel,image_layer,wavelength,same_capture,name}` | Stored measurement identity, channel-domain, spectral and capture-group metadata. | **Keep measurement metadata.** |
| `mtf_measurement::{source_filename,source_width,source_height,roi,edge_p1,edge_p2}` | Source path/dimensions, image ROI and edge coordinates are **already saved** when available. Permit revisiting the measured location and checking provenance. | **Keep provenance**; consider splitting its storage type from model coefficients in future, but not deleting it. |
| `mtf_measurement::{edge_angle,edge_fit_rms,edge_contrast,edge_snr,phase_coverage}` | Saved as `scanner_mtf_measurement_edge_quality` when spatial metadata exists. Measurements of reliability rather than user tuning controls. | **Keep as measured evidence.** |
| `mtf_estimation_options` | Separate per-fit request structure supplied by the GUI, **not** embedded in `mtf_parameters`. | **Already appropriately separated.** |
| `mtf_parameters::computed_mtf` | Nested return-value containing derived chart/CSV curves, **not** an embedded parameter member. | **Already appropriately separated.** |

**Follow-up resolved in PR #532:** the effective optical wavelength is passed
to MTF/PSF/fitting and render operations explicitly, including color-loss
simulation, with wavelength-aware cache identities and preserved CLI override
semantics. That PR has a green full test matrix. The fact that measurement
provenance was originally considered bookkeeping is insufficient to remove it:
most of it is explicitly saved and used by the GUI.

## New stitched-tile lifecycle finding

The Qt stitched-image open path called
`set_tile_adjustments_dimensions(w,h)` even after sidecar parsing had
populated a same-sized grid. That helper unconditionally cleared all tile
exposures, dark points and per-tile scanner-blur corrections. The current
branch now makes same-dimension calls idempotent, with a
`stitch_tile_grid` regression group; genuine dimension changes still reset
the grid.

**Resolved in PR #535:** removing `tile_adjustment::enabled` avoids both a
false promise of persistence and Undo/dirty changes during tile loading.
The Tiles checkbox controls the current view's render mask, and successful
background loads notify canvases without modifying `ParameterState`.
`tile_for_scr(..., only_loaded)` continues checking physical readiness.
Per-tile calibration is unaffected.

## Confirmed user decisions for the next GUI work

- `scan_crop` frames the entire **physical object**, including binding tape
  and borders. `image_area` is a separately selected rectangle identifying
  the **photographic image** and its reconstruction/analysis bounds. It must be
  exposed in the Capture GUI and kept in the archive.
- `tile_adjustment::{x,y}` have been removed; tile index and grid dimensions
  already determine position.
- Defer `copy_without_points()`, which has no impact on saving.
- Keep `demosaic` in its current structure.
- `output_profile`, `output_gamma`, `gamut_warning` and tile visibility
  are **render-request-only** in PR #535, on top of the per-view GUI in
  merged PRs #533/#534. All three historical output keys are neutral schema-v1
  compatibility placeholders; genuine structured saved fields are
  `ignore_infrared`, `demosaiced_scaling`, and `observer_whitepoint`.
  Explicit Render-to-File encoding remains separate from display choice.

## Fields checked and intentionally retained

- `solver_parameters::points`, `optimize_lens`, `optimize_tilt` and
  `lens_center_distance`: real registration evidence and geometry-fitting
  policies, used in normal solving and retained by CSP.
- `scr_to_img_parameters::final_angle` and `final_ratio`: not transient
  per-mesh weighting. They define the final screen-coordinate frame,
  including stitched images, and therefore need persistent archive support
  (the separate PR #528).
- `scr_to_img_parameters::mesh_trans`: accepted nonlinear mapping/calibration
  result, not the temporary weighting centre of each mesh point.
- `render_parameters::scan_crop` and the tile adjustment grid
  dimensions/contents: saved document inputs; removing these would change
  reconstruction or export.
- `profileSpots` in the Qt `ParameterState`: saved user calibration points.
  The derived per-spot matching results remain transient analysis evidence.

## Follow-up discipline

Before removing more fields, check all consumers, explicit assignments,
existing file-format keywords, GUI controls, and numerical/mesh tests.
Distinguish persistent user/calibration decisions from local algorithm
arguments, working buffers and derived diagnostics. In particular, a member
used somewhere in the library is **not** automatically a user-facing parameter.
A legacy-CSP omission is not by itself a reason to introduce a required
`.cspar` feature.
