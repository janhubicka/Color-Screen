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
| `render_parameters::image_area` | Declared as a second optional crop in `render-parameters.h` and read by `get_image_area()`. No assignment or CSP serialization was found in the current tree; the getter falls back to the real saved `scan_crop` when unset. The getter affects focus, finetune and screen pixel-size calculations. | **High priority: verify external use, then consider removal.** Prefer one saved scan crop and explicit *per-analysis* ROIs. Do not simply drop the getter without checking those callers. |
| `render_parameters::tile_adjustment::{x,y}` | The tile array is indexed by row/column and has separately saved width/height. These per-element coordinate bytes are defaulted to zero, not initialized by `set_tile_adjustments_dimensions()`, not compared in `tile_adjustment::operator==`, and not written in CSP. | **Likely dead metadata.** Remove the per-element bytes after a final search of external callers. Keep the tile grid dimensions and real exposure/dark/blur/enabled adjustments. |
| `solver_parameters::copy_without_points()` and `solver_mesh(..., sparam2, smap, ...)` | The mesh routine copies lens/tilt optimisation policy to a scratch `solver_parameters`, but local mesh fitting retrieves nearby points from `screen_map` and performs its own weighted homography. Those copied policy flags are not consumed by the mesh point fitter. | **API/implementation simplification candidate.** Check all mesh callers and remove redundant parameter copying (potentially the unused `sparam2` argument) in a separate patch; preserve the point-neighbour selection and weighting. |
| `render_parameters::demosaic` | The member is documented as an image-loading choice, not a rendering computation. It is saved because reopening a camera RAW requires the selected demosaicer. | **Not obsolete.** Consider a future `capture_parameters` grouping; do not remove the value or stop persisting it. |
| `render_parameters::gamut_warning` | A display/render diagnostic toggle that deliberately highlights out-of-gamut pixels; it is currently document-owned and stored as a structured render override. | **Product decision only.** Could eventually be a per-view overlay preference, but it currently changes rendered pixels. Keep existing persistence until view-versus-document semantics are explicitly chosen. |

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
