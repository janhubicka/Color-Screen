# Color-Screen 2.0 — complete .cspar schema-3 field reference

This field catalogue is derived directly from the checked-in [JSON Schema `doc/parameters-schema-v3.json`](https://github.com/janhubicka/Color-Screen/blob/main/doc/parameters-schema-v3.json) (JSON Schema draft 2020-12). It lists **all 249 schema field/variant occurrences**, including nested array records and alternative object shapes.

**Read this alongside the [parameter format overview](Color-Screen-2.0-Parameter-Format).** Schema constraints describe types, required members and structural restrictions. The `libcolorscreen` reader also checks numeric finiteness, relationships between fields, IDs, source provenance, screen geometry and resource budgets. Conversely, UI slider ranges are not necessarily reader-enforced limits. The column **Required** is relative to the immediate containing object and conditional variants, not necessarily mandatory in every top-level document.

- Root identity: `"format": "org.colorscreen.parameters"` and `"schema_version": 3`.
- New parameter files are plain UTF-8 JSON. A historical `.par` file is imported through the legacy reader; it is not JSON.
- `source_image` is optional (or null) and identifies an external file; image pixels are not embedded.
- Unless the schema explicitly allows additional members, the C++ schema-3 reader may reject unknown fields. Prefer saving with the application for canonical form.

## Document identity

| Field path | Type | Required | Variant | Structural constraints | Description |
|---|---|:---:|---|---|---|
| `format` | constant | Yes | — | exactly org.colorscreen.parameters | — |
| `schema_version` | constant | Yes | — | exactly 3 | — |

## `source_image`

| Field path | Type | Required | Variant | Structural constraints | Description |
|---|---|:---:|---|---|---|
| `source_image` | one of 2 variants | No | — | — | Optional external scan or stitched project linked to this document. Null means reusable parameters; the original scan is not embedded. |
| `source_image.path` | string | Yes | oneOf 2 | string length ≥ 1 | — |
| `source_image.base` | enumeration | Yes | oneOf 2 | one of absolute, parameter_file | — |
| `source_image.width_px` | integer | Yes | oneOf 2 | ≥ 1 | — |
| `source_image.height_px` | integer | Yes | oneOf 2 | ≥ 1 | — |
| `source_image.size_bytes` | integer | Yes | oneOf 2 | ≥ 0 | — |

## `capture`

| Field path | Type | Required | Variant | Structural constraints | Description |
|---|---|:---:|---|---|---|
| `capture` | object | Yes | — | no unknown members | — |
| `capture.type` | string | Yes | — | string length ≥ 1 | — |
| `capture.raw` | object | Yes | — | no unknown members | — |
| `capture.raw.demosaic` | string | Yes | — | string length ≥ 1 | — |
| `capture.linearization` | one of 2 variants | Yes | — | — | — |
| `capture.linearization.mode` | constant | Yes | oneOf 1 | exactly srgb | — |
| `capture.linearization.mode` | constant | Yes | oneOf 2 | exactly power | — |
| `capture.linearization.exponent` | number | Yes | oneOf 2 | — | — |
| `capture.orientation` | object | Yes | — | no unknown members | — |
| `capture.orientation.quarter_turns` | integer | Yes | — | — | — |
| `capture.orientation.mirror` | boolean | Yes | — | — | — |
| `capture.bounds` | object | Yes | — | no unknown members | — |
| `capture.bounds.object_crop` | any of 2 variants | Yes | — | — | — |
| `capture.bounds.object_crop.x_px` | integer | Yes | anyOf 2 | — | — |
| `capture.bounds.object_crop.y_px` | integer | Yes | anyOf 2 | — | — |
| `capture.bounds.object_crop.width_px` | integer | Yes | anyOf 2 | ≥ 1 | — |
| `capture.bounds.object_crop.height_px` | integer | Yes | anyOf 2 | ≥ 1 | — |
| `capture.bounds.photograph` | any of 2 variants | Yes | — | — | — |
| `capture.bounds.photograph.x_px` | integer | Yes | anyOf 2 | — | — |
| `capture.bounds.photograph.y_px` | integer | Yes | anyOf 2 | — | — |
| `capture.bounds.photograph.width_px` | integer | Yes | anyOf 2 | ≥ 1 | — |
| `capture.bounds.photograph.height_px` | integer | Yes | anyOf 2 | ≥ 1 | — |
| `capture.levels` | object | Yes | — | no unknown members | — |
| `capture.levels.gain` | number | Yes | — | — | — |
| `capture.levels.black_offset` | number | Yes | — | — | — |

## `process`

| Field path | Type | Required | Variant | Structural constraints | Description |
|---|---|:---:|---|---|---|
| `process` | object | Yes | — | no unknown members | — |
| `process.screen` | object | Yes | — | no unknown members | — |
| `process.screen.type` | string | Yes | — | string length ≥ 1 | — |
| `process.screen.strip_widths` | object | Yes | — | no unknown members | — |
| `process.screen.strip_widths.red` | one of 2 variants | Yes | — | — | — |
| `process.screen.strip_widths.red.mode` | constant | Yes | oneOf 1 | exactly automatic | — |
| `process.screen.strip_widths.red.mode` | constant | Yes | oneOf 2 | exactly fraction | — |
| `process.screen.strip_widths.red.value` | number | Yes | oneOf 2 | — | — |
| `process.screen.strip_widths.green` | one of 2 variants | Yes | — | — | — |
| `process.screen.strip_widths.green.mode` | constant | Yes | oneOf 1 | exactly automatic | — |
| `process.screen.strip_widths.green.mode` | constant | Yes | oneOf 2 | exactly fraction | — |
| `process.screen.strip_widths.green.value` | number | Yes | oneOf 2 | — | — |
| `process.color_model` | string | Yes | — | string length ≥ 1 | — |
| `process.dyes` | object | Yes | — | no unknown members | — |
| `process.dyes.aging_fraction` | array | Yes | — | array length ≥ 3; array length ≤ 3 | — |
| `process.dyes.density` | array | Yes | — | array length ≥ 3; array length ≤ 3 | — |
| `process.contact_copy` | object | Yes | — | no unknown members | — |
| `process.contact_copy.simulate` | boolean | Yes | — | — | — |
| `process.contact_copy.preflash` | number | Yes | — | — | — |
| `process.contact_copy.exposure` | number | Yes | — | — | — |
| `process.contact_copy.boost` | number | Yes | — | — | — |
| `process.contact_copy.emulsion_curve` | object | Yes | — | no unknown members | — |
| `process.contact_copy.emulsion_curve.min` | array | Yes | — | array length ≥ 2; array length ≤ 2 | — |
| `process.contact_copy.emulsion_curve.linear1` | array | Yes | — | array length ≥ 2; array length ≤ 2 | — |
| `process.contact_copy.emulsion_curve.linear2` | array | Yes | — | array length ≥ 2; array length ≤ 2 | — |
| `process.contact_copy.emulsion_curve.max` | array | Yes | — | array length ≥ 2; array length ≤ 2 | — |

## `geometry`

| Field path | Type | Required | Variant | Structural constraints | Description |
|---|---|:---:|---|---|---|
| `geometry` | object | Yes | — | no unknown members | — |
| `geometry.imaging_model` | string | Yes | — | string length ≥ 1 | — |
| `geometry.mapping` | object | Yes | — | no unknown members | — |
| `geometry.mapping.center` | array | Yes | — | array length ≥ 2; array length ≤ 2 | — |
| `geometry.mapping.axis_x` | array | Yes | — | array length ≥ 2; array length ≤ 2 | — |
| `geometry.mapping.axis_y` | array | Yes | — | array length ≥ 2; array length ≤ 2 | — |
| `geometry.mapping.projection_distance` | number | Yes | — | — | — |
| `geometry.mapping.tilt` | array | Yes | — | array length ≥ 2; array length ≤ 2 | — |
| `geometry.final_frame` | object | Yes | — | no unknown members | — |
| `geometry.final_frame.rotation` | number | Yes | — | — | — |
| `geometry.final_frame.mirror` | boolean | Yes | — | — | — |
| `geometry.final_frame.angle_deg` | number | Yes | — | — | — |
| `geometry.final_frame.axis_length_ratio` | number | Yes | — | > 0 | — |
| `geometry.lens` | object | Yes | — | no unknown members | — |
| `geometry.lens.radial` | array | Yes | — | array length ≥ 4; array length ≤ 4 | — |
| `geometry.lens.center` | array | Yes | — | array length ≥ 2; array length ≤ 2 | — |
| `geometry.mesh` | any of 2 variants | Yes | — | — | — |
| `geometry.mesh.direction` | enumeration | Yes | anyOf 2 | one of screen-to-image, image-to-screen | — |
| `geometry.mesh.shift` | array | Yes | anyOf 2 | array length ≥ 2; array length ≤ 2 | — |
| `geometry.mesh.step` | array | Yes | anyOf 2 | array length ≥ 2; array length ≤ 2 | — |
| `geometry.mesh.dimensions` | array | Yes | anyOf 2 | array length ≥ 2; array length ≤ 2 | — |
| `geometry.mesh.points` | array | Yes | anyOf 2 | — | — |
| `geometry.mesh.points[]` | array | Yes | anyOf 2 | array length ≥ 2; array length ≤ 2 | — |

## `registration`

| Field path | Type | Required | Variant | Structural constraints | Description |
|---|---|:---:|---|---|---|
| `registration` | object | Yes | — | no unknown members | — |
| `registration.detection` | object | Yes | — | no unknown members | — |
| `registration.detection.red` | array | Yes | — | array length ≥ 3; array length ≤ 3 | — |
| `registration.detection.green` | array | Yes | — | array length ≥ 3; array length ≤ 3 | — |
| `registration.detection.blue` | array | Yes | — | array length ≥ 3; array length ≤ 3 | — |
| `registration.detection.black` | array | Yes | — | array length ≥ 3; array length ≤ 3 | — |
| `registration.detection.min_luminosity` | number | Yes | — | — | — |
| `registration.detection.min_ratio` | number | Yes | — | — | — |
| `registration.fit` | object | Yes | — | no unknown members | — |
| `registration.fit.optimize_lens` | boolean | Yes | — | — | — |
| `registration.fit.lens_center_distance` | number | Yes | — | — | — |
| `registration.fit.optimize_tilt` | boolean | Yes | — | — | — |
| `registration.points` | array | Yes | — | — | — |
| `registration.points[]` | object | Yes | — | no unknown members | — |
| `registration.points[].image` | array | Yes | — | array length ≥ 2; array length ≤ 2 | — |
| `registration.points[].screen` | array | Yes | — | array length ≥ 2; array length ≤ 2 | — |
| `registration.points[].color` | string | Yes | — | string length ≥ 1 | — |

## `calibration`

| Field path | Type | Required | Variant | Structural constraints | Description |
|---|---|:---:|---|---|---|
| `calibration` | object | Yes | — | no unknown members | — |
| `calibration.capture` | object | Yes | — | no unknown members | — |
| `calibration.capture.mtf` | object | Yes | — | no unknown members | — |
| `calibration.capture.mtf.model` | enumeration | Yes | — | one of automatic, physical-diffraction, empirical-fallback | — |
| `calibration.capture.mtf.sigma_px` | number | Yes | — | — | — |
| `calibration.capture.mtf.halo_fraction` | number | Yes | — | — | — |
| `calibration.capture.mtf.halo_sigma_px` | number | Yes | — | — | — |
| `calibration.capture.mtf.blur_diameter_px` | number | Yes | — | — | — |
| `calibration.capture.mtf.defocus_mm` | number | Yes | — | — | — |
| `calibration.capture.mtf.f_stop` | number | Yes | — | — | — |
| `calibration.capture.mtf.pixel_pitch_um` | number | Yes | — | — | — |
| `calibration.capture.mtf.sensor_fill_factor` | number | Yes | — | — | — |
| `calibration.capture.mtf.scan_dpi` | number | Yes | — | — | — |
| `calibration.capture.mtf.wavelengths_nm` | array | Yes | — | array length ≥ 4; array length ≤ 4 | — |
| `calibration.capture.mtf.selected_measurement_id` | any of 2 variants | Yes | — | — | — |
| `calibration.capture.mtf.sample_columns` | constant | Yes | — | exactly frequency_cycles_per_pixel,contrast_percent,uncertainty_percentage_points | — |
| `calibration.capture.mtf.measurements` | array | Yes | — | array length ≤ 256 | — |
| `calibration.capture.mtf.measurements[]` | object | Yes | — | no unknown members | — |
| `calibration.capture.mtf.measurements[].id` | string | Yes | — | string length ≥ 1 | — |
| `calibration.capture.mtf.measurements[].capture_group_id` | string | Yes | — | string length ≥ 1 | — |
| `calibration.capture.mtf.measurements[].channel` | enumeration | Yes | — | one of unknown, red, green, blue, ir | — |
| `calibration.capture.mtf.measurements[].image_layer` | boolean | Yes | — | — | — |
| `calibration.capture.mtf.measurements[].wavelength_nm` | number | Yes | — | — | — |
| `calibration.capture.mtf.measurements[].label` | string | Yes | — | — | — |
| `calibration.capture.mtf.measurements[].source` | object | Yes | — | no unknown members | — |
| `calibration.capture.mtf.measurements[].source.path` | any of 2 variants | Yes | — | — | — |
| `calibration.capture.mtf.measurements[].source.path.base` | enumeration | Yes | anyOf 2 | one of absolute, parameter_file | — |
| `calibration.capture.mtf.measurements[].source.path.value` | string | Yes | anyOf 2 | string length ≥ 1 | — |
| `calibration.capture.mtf.measurements[].source.dimensions` | one of 2 variants | Yes | — | — | — |
| `calibration.capture.mtf.measurements[].source.dimensions.status` | constant | Yes | oneOf 1 | exactly available | — |
| `calibration.capture.mtf.measurements[].source.dimensions.width_px` | integer | Yes | oneOf 1 | ≥ 1 | — |
| `calibration.capture.mtf.measurements[].source.dimensions.height_px` | integer | Yes | oneOf 1 | ≥ 1 | — |
| `calibration.capture.mtf.measurements[].source.dimensions.status` | constant | Yes | oneOf 2 | exactly unavailable | — |
| `calibration.capture.mtf.measurements[].source.dimensions.legacy_values` | array | Yes | oneOf 2 | array length ≥ 2; array length ≤ 2 | — |
| `calibration.capture.mtf.measurements[].source.roi` | one of 2 variants | Yes | — | — | — |
| `calibration.capture.mtf.measurements[].source.roi.status` | constant | Yes | oneOf 1 | exactly available | — |
| `calibration.capture.mtf.measurements[].source.roi.x_px` | integer | Yes | oneOf 1 | — | — |
| `calibration.capture.mtf.measurements[].source.roi.y_px` | integer | Yes | oneOf 1 | — | — |
| `calibration.capture.mtf.measurements[].source.roi.width_px` | integer | Yes | oneOf 1 | ≥ 1 | — |
| `calibration.capture.mtf.measurements[].source.roi.height_px` | integer | Yes | oneOf 1 | ≥ 1 | — |
| `calibration.capture.mtf.measurements[].source.roi.status` | constant | Yes | oneOf 2 | exactly unavailable | — |
| `calibration.capture.mtf.measurements[].source.roi.legacy_values` | array | Yes | oneOf 2 | array length ≥ 4; array length ≤ 4 | — |
| `calibration.capture.mtf.measurements[].source.edge_points_px` | array | Yes | — | array length ≥ 2; array length ≤ 2 | — |
| `calibration.capture.mtf.measurements[].edge_quality` | object | Yes | — | no unknown members | — |
| `calibration.capture.mtf.measurements[].edge_quality.angle_deg` | number | Yes | — | — | — |
| `calibration.capture.mtf.measurements[].edge_quality.fit_rms_px` | number | Yes | — | — | — |
| `calibration.capture.mtf.measurements[].edge_quality.contrast_fraction` | number | Yes | — | — | — |
| `calibration.capture.mtf.measurements[].edge_quality.snr` | number | Yes | — | — | — |
| `calibration.capture.mtf.measurements[].edge_quality.phase_coverage_fraction` | number | Yes | — | — | — |
| `calibration.capture.mtf.measurements[].samples` | array | Yes | — | array length ≤ 1000000 | — |
| `calibration.capture.mtf.measurements[].samples[]` | array | Yes | — | array length ≥ 3; array length ≤ 3 | — |
| `calibration.capture.scanner_primaries_xyz` | object | Yes | — | no unknown members | — |
| `calibration.capture.scanner_primaries_xyz.red` | — | Yes | — | — | — |
| `calibration.capture.scanner_primaries_xyz.green` | — | Yes | — | — | — |
| `calibration.capture.scanner_primaries_xyz.blue` | — | Yes | — | — | — |
| `calibration.process_color` | object | Yes | — | no unknown members | — |
| `calibration.process_color.profile_rgb` | object | Yes | — | no unknown members | — |
| `calibration.process_color.profile_rgb.dark` | array | Yes | — | array length ≥ 3; array length ≤ 3 | — |
| `calibration.process_color.profile_rgb.red` | array | Yes | — | array length ≥ 3; array length ≤ 3 | — |
| `calibration.process_color.profile_rgb.green` | array | Yes | — | array length ≥ 3; array length ≤ 3 | — |
| `calibration.process_color.profile_rgb.blue` | array | Yes | — | array length ≥ 3; array length ≤ 3 | — |
| `calibration.process_color.spots` | array | Yes | — | — | — |
| `calibration.process_color.spots[]` | array | Yes | — | array length ≥ 2; array length ≤ 2 | — |
| `calibration.flat_field` | object | Yes | — | no unknown members | — |
| `calibration.flat_field.black_correction` | number | Yes | — | — | — |
| `calibration.flat_field.grid` | any of 2 variants | Yes | — | — | — |
| `calibration.flat_field.grid.dimensions` | array | Yes | anyOf 2 | array length ≥ 2; array length ≤ 2 | — |
| `calibration.flat_field.grid.channels` | array | Yes | anyOf 2 | array length ≥ 4; array length ≤ 4 | — |
| `calibration.flat_field.grid.black_correction` | boolean | Yes | anyOf 2 | — | — |
| `calibration.flat_field.grid.luminosities` | array | Yes | anyOf 2 | — | — |
| `calibration.flat_field.grid.luminosities[]` | array | Yes | anyOf 2 | array length ≥ 4; array length ≤ 4 | — |
| `calibration.flat_field.grid.subtractions` | array | Yes | anyOf 2 | — | — |
| `calibration.flat_field.grid.subtractions[]` | array | Yes | anyOf 2 | array length ≥ 4; array length ≤ 4 | — |
| `calibration.spatial_blur` | any of 2 variants | Yes | — | — | — |
| `calibration.spatial_blur.mode` | string | Yes | anyOf 2 | string length ≥ 1 | — |
| `calibration.spatial_blur.dimensions` | array | Yes | anyOf 2 | array length ≥ 2; array length ≤ 2 | — |
| `calibration.spatial_blur.values` | array | Yes | anyOf 2 | — | — |
| `calibration.spatial_blur.values[]` | number | Yes | anyOf 2 | — | — |
| `calibration.tiles` | object | Yes | — | no unknown members | — |
| `calibration.tiles.dimensions` | array | Yes | — | array length ≥ 2; array length ≤ 2 | — |
| `calibration.tiles.adjustments` | array | Yes | — | — | — |
| `calibration.tiles.adjustments[]` | object | Yes | — | no unknown members | — |
| `calibration.tiles.adjustments[].exposure` | number | Yes | — | — | — |
| `calibration.tiles.adjustments[].dark_point` | number | Yes | — | — | — |
| `calibration.tiles.adjustments[].scanner_blur` | any of 2 variants | Yes | — | — | — |
| `calibration.tiles.adjustments[].scanner_blur.mode` | string | Yes | anyOf 2 | string length ≥ 1 | — |
| `calibration.tiles.adjustments[].scanner_blur.dimensions` | array | Yes | anyOf 2 | array length ≥ 2; array length ≤ 2 | — |
| `calibration.tiles.adjustments[].scanner_blur.values` | array | Yes | anyOf 2 | — | — |
| `calibration.tiles.adjustments[].scanner_blur.values[]` | number | Yes | anyOf 2 | — | — |

## `reconstruction`

| Field path | Type | Required | Variant | Structural constraints | Description |
|---|---|:---:|---|---|---|
| `reconstruction` | object | Yes | — | no unknown members | — |
| `reconstruction.image_layer` | object | Yes | — | no unknown members | — |
| `reconstruction.image_layer.ignore_infrared` | boolean | Yes | — | — | — |
| `reconstruction.image_layer.rgb_mixer` | object | Yes | — | no unknown members | — |
| `reconstruction.image_layer.rgb_mixer.weights` | array | Yes | — | array length ≥ 3; array length ≤ 3 | — |
| `reconstruction.image_layer.rgb_mixer.black_offsets` | array | Yes | — | array length ≥ 3; array length ≤ 3 | — |
| `reconstruction.collection` | object | Yes | — | no unknown members | — |
| `reconstruction.collection.quality` | string | Yes | — | string length ≥ 1 | — |
| `reconstruction.collection.threshold` | number | Yes | — | — | — |
| `reconstruction.color_field` | object | Yes | — | no unknown members | — |
| `reconstruction.color_field.screen_demosaic` | string | Yes | — | string length ≥ 1 | — |
| `reconstruction.color_field.resampling` | string | Yes | — | string length ≥ 1 | — |
| `reconstruction.color_field.screen_blur_radius` | number | Yes | — | — | — |
| `reconstruction.denoise` | object | Yes | — | no unknown members | — |
| `reconstruction.denoise.before_reconstruction` | object | Yes | — | no unknown members | — |
| `reconstruction.denoise.before_reconstruction.mode` | string | Yes | — | string length ≥ 1 | — |
| `reconstruction.denoise.before_reconstruction.strength` | number | Yes | — | — | — |
| `reconstruction.denoise.before_reconstruction.noise_variance_floor` | number | Yes | — | — | — |
| `reconstruction.denoise.before_reconstruction.noise_variance_slope` | number | Yes | — | — | — |
| `reconstruction.denoise.before_reconstruction.patch_radius` | integer | Yes | — | — | — |
| `reconstruction.denoise.before_reconstruction.search_radius` | integer | Yes | — | — | — |
| `reconstruction.denoise.before_reconstruction.bilateral_sigma_s` | number | Yes | — | — | — |
| `reconstruction.denoise.before_reconstruction.bilateral_sigma_r` | number | Yes | — | — | — |
| `reconstruction.denoise.after_reconstruction` | object | Yes | — | no unknown members | — |
| `reconstruction.denoise.after_reconstruction.mode` | string | Yes | — | string length ≥ 1 | — |
| `reconstruction.denoise.after_reconstruction.strength` | number | Yes | — | — | — |
| `reconstruction.denoise.after_reconstruction.noise_variance_floor` | number | Yes | — | — | — |
| `reconstruction.denoise.after_reconstruction.noise_variance_slope` | number | Yes | — | — | — |
| `reconstruction.denoise.after_reconstruction.patch_radius` | integer | Yes | — | — | — |
| `reconstruction.denoise.after_reconstruction.search_radius` | integer | Yes | — | — | — |
| `reconstruction.denoise.after_reconstruction.bilateral_sigma_s` | number | Yes | — | — | — |
| `reconstruction.denoise.after_reconstruction.bilateral_sigma_r` | number | Yes | — | — | — |
| `reconstruction.sharpening` | object | Yes | — | no unknown members | — |
| `reconstruction.sharpening.mode` | string | Yes | — | string length ≥ 1 | — |
| `reconstruction.sharpening.unsharp` | object | Yes | — | no unknown members | — |
| `reconstruction.sharpening.unsharp.radius` | number | Yes | — | — | — |
| `reconstruction.sharpening.unsharp.amount` | number | Yes | — | — | — |
| `reconstruction.sharpening.scanner` | object | Yes | — | no unknown members | — |
| `reconstruction.sharpening.scanner.snr` | number | Yes | — | — | — |
| `reconstruction.sharpening.scanner.mtf_scale` | number | Yes | — | — | — |
| `reconstruction.sharpening.richardson_lucy` | object | Yes | — | no unknown members | — |
| `reconstruction.sharpening.richardson_lucy.iterations` | integer | Yes | — | — | — |
| `reconstruction.sharpening.richardson_lucy.sigma` | number | Yes | — | — | — |
| `reconstruction.sharpening.deconvolution_supersample` | integer | Yes | — | — | — |
| `reconstruction.sharpening.resampling_kernel` | string | Yes | — | string length ≥ 1 | — |

## `color`

| Field path | Type | Required | Variant | Structural constraints | Description |
|---|---|:---:|---|---|---|
| `color` | object | Yes | — | no unknown members | — |
| `color.illuminants` | object | Yes | — | no unknown members | — |
| `color.illuminants.photograph_temperature_k` | number | Yes | — | — | — |
| `color.illuminants.backlight_temperature_k` | number | Yes | — | — | — |
| `color.viewing` | object | Yes | — | no unknown members | — |
| `color.viewing.whitepoint_xy` | array | Yes | — | array length ≥ 2; array length ≤ 2 | — |
| `color.adjustments` | object | Yes | — | no unknown members | — |
| `color.adjustments.white_balance` | array | Yes | — | array length ≥ 3; array length ≤ 3 | — |
| `color.adjustments.presaturation` | number | Yes | — | — | — |
| `color.adjustments.dye_balance` | string | Yes | — | string length ≥ 1 | — |
| `color.adjustments.saturation` | number | Yes | — | — | — |
| `color.adjustments.brightness` | number | Yes | — | — | — |
| `color.adjustments.tone_curve` | object | Yes | — | no unknown members | — |
| `color.adjustments.tone_curve.type` | string | Yes | — | string length ≥ 1 | — |
| `color.adjustments.tone_curve.control_points` | array | Yes | — | — | — |
| `color.adjustments.tone_curve.control_points[]` | array | Yes | — | array length ≥ 2; array length ≤ 2 | — |

## Implemented validation versus recommended use

The machine-readable schema is authoritative for **structural requirements**, not a specification of all physical/semantic invariants. The typed reader additionally validates finite and consistent scalars, coordinate/mesh and image areas, MTF measurement/capture-group identities, exact required children, source-image paths and metadata, and aggregate resource limits. Do not treat a value accepted by a generic JSON Schema validator as proof that Color-Screen can load or use it.

Output ICC destination profile, export gamma/transfer, per-view gamut warning, window layout, cache budgets, scratch directory and transient measurements are not document-processing fields. They belong to the render request, view or application preferences.

For an example of real canonical documents, use the test generators and round-trip fixtures in [`src/libcolorscreen/unittests.C`](https://github.com/janhubicka/Color-Screen/blob/main/src/libcolorscreen/unittests.C); do not use abbreviated table fragments as valid input files.

