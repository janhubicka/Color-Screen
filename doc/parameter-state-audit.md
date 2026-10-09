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
| `solver_parameters::copy_without_points()` and `solver_mesh(..., sparam2, smap, ...)` | Mesh code copies lens/tilt policy to a scratch `solver_parameters`, but the local fit uses nearby points and its own weighted homography. | **Deferred at user's request.** No saving impact; leave the API alone for now. |
| `render_parameters::demosaic` | An image-loading choice, rather than a rendering step. Correctly persisted so RAW capture can be reopened with the same demosaicer. | **Keep unchanged** in this alpha. |
| `render_parameters::output_profile` | View/display/output colourspace selection, not a property of the scanned object. Older schema-v1 `render-overrides-v1` manifests nonetheless require a typed field. | **Per-view/operation** in PR #533; PR #534 writes `sRGB` as the neutral schema-v1 compatibility value. Do not conflate this with the persistent output tone curve, gamma, or adapted whitepoint without a separate semantic decision. |
| `render_parameters::gamut_warning` | Diagnostic intent for an individual preview. The core renderer still consumes the flag on its temporary render-request copy. | **View-owned** in PR #533; PR #534 accepts historical schema-v1 values strictly but emits a neutral compatibility key and does not restore it into document state. Neither PR is part of this cleanup branch. |

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

## Confirmed user decisions for the next GUI work

- `scan_crop` frames the entire **physical object**, including binding tape
  and borders. `image_area` is a separately selected rectangle identifying
  the **photographic image** and its reconstruction/analysis bounds. It must be
  exposed in the Capture GUI and kept in the archive.
- `tile_adjustment::{x,y}` have been removed; tile index and grid dimensions
  already determine position.
- Defer `copy_without_points()`, which has no impact on saving.
- Keep `demosaic` in its current structure.
- `gamut_warning` and the preview `output_profile` are **per-view**, without
  modifying the shared document or Undo, in PR #533. PR #534 makes their
  historical archive keys neutral compatibility placeholders, while retaining
  the four genuine structured inputs including `output_gamma`. Explicit
  Render-to-File output choice remains separate from view display choices.

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
