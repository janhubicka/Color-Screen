#!/usr/bin/env python3
"""Reference-only, lossless **semantic** v2 <-> v3 parameter translation.

This is NOT a supported file writer or loader and is not called by the GUI,
CLI or recovery.  It exists to audit the proposed field hierarchy with real
v2 documents before replacing the authoritative Qt-independent C++ codec.
Do not generate schema-v3 files in production using this experiment.
"""

from __future__ import annotations

from copy import deepcopy
import argparse
import json
import os
from pathlib import Path
import ntpath
import posixpath
from typing import Any

FORMAT = "org.colorscreen.parameters"


def _obj(value: Any, path: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValueError(f"{path}: expected object")
    return value


def _keys(value: dict[str, Any], required: set[str], path: str) -> None:
    missing, extra = required - value.keys(), value.keys() - required
    if missing or extra:
        raise ValueError(f"{path}: missing {sorted(missing)}, unknown {sorted(extra)}")


def _take(value: dict[str, Any], fields: str, path: str) -> dict[str, Any]:
    names = fields.split()
    _keys(_obj(value, path), set(names), path)
    return {n: deepcopy(value[n]) for n in names}


def _rect_from_old(value: Any, path: str) -> Any:
    area = _take(value, "enabled rect", path)
    dims = area["rect"]
    if not isinstance(area["enabled"], bool) or not isinstance(dims, list) or len(dims) != 4 or any(type(v) is not int for v in dims):
        raise ValueError(f"{path}: invalid enabled/rect representation")
    if not area["enabled"]:
        if dims != [0, 0, 0, 0]:
            raise ValueError(f"{path}: disabled area retains noncanonical bounds")
        return None
    return dict(zip(("x_px", "y_px", "width_px", "height_px"), dims))


def _rect_to_old(value: Any, path: str) -> dict[str, Any]:
    if value is None:
        return {"enabled": False, "rect": [0, 0, 0, 0]}
    val = _take(value, "x_px y_px width_px height_px", path)
    if any(type(x) is not int for x in val.values()) or val["width_px"] <= 0 or val["height_px"] <= 0:
        raise ValueError(f"{path}: expected integer bounds with positive dimensions")
    return {"enabled": True, "rect": [val[k] for k in ("x_px", "y_px", "width_px", "height_px")]}


def _optional_rect_with_sentinel(value: Any, names: tuple[str, ...], path: str) -> Any:
    if not isinstance(value, list) or len(value) != len(names) or any(type(x) is not int for x in value):
        raise ValueError(f"{path}: invalid integer tuple")
    unavailable = any(x <= 0 for x in value[-2:])
    if unavailable:
        # Historical MTF metadata may contain noncanonical sentinels (even
        # a nonzero origin). We cannot discard that information on import.
        return {"status": "unavailable", "legacy_values": deepcopy(value)}
    return {"status": "available", **dict(zip(names, value))}


def _optional_rect_to_old(value: Any, names: tuple[str, ...], path: str) -> list[int]:
    val = _obj(value, path)
    if val.get("status") == "unavailable":
        _keys(val, {"status", "legacy_values"}, path)
        output = val["legacy_values"]
    elif val.get("status") == "available":
        _keys(val, {"status", *names}, path)
        output = [val[k] for k in names]
    else:
        raise ValueError(f"{path}: unsupported metadata availability")
    if not isinstance(output, list) or len(output) != len(names) or any(type(x) is not int for x in output):
        raise ValueError(f"{path}: invalid coordinates")
    # Reject contradictory availability instead of silently changing its
    # interpretation if the record is imported again after a Save As.
    available = all(x > 0 for x in output[-2:])
    if available != (val["status"] == "available"):
        raise ValueError(f"{path}: declared availability contradicts dimensions")
    return deepcopy(output)


def _source_path(value: str) -> dict[str, str] | None:
    if not isinstance(value, str):
        raise ValueError("measurement.source_filename: expected text")
    if not value:
        return None  # Missing source, not a relative empty filename.
    # A drive-relative Windows path (C:folder\\image.tif) depends on the
    # process's current directory *on that drive*. Likewise \\folder\\image.tif
    # is volume-rooted but lacks a drive. Neither is an absolute or reliably
    # parameter-file-relative reference; reject instead of guessing on Save As.
    drive, _ = ntpath.splitdrive(value)
    if (drive and not ntpath.isabs(value)) or (value.startswith("\\") and not value.startswith("\\\\")):
        raise ValueError("measurement.source_filename: ambiguous Windows-rooted source path")
    # ntpath handles Windows drive and UNC paths even on Linux. Do not assume
    # the host platform is the one on which the calibration was created.
    absolute = os.path.isabs(value) or ntpath.isabs(value)
    return {"value": value, "base": "absolute" if absolute else "parameter_file"}


def _source_to_old(value: Any) -> str:
    if value is None:
        return ""
    path = _take(value, "value base", "measurement.source.path")
    if not isinstance(path["value"], str) or not path["value"]:
        raise ValueError("measurement.source.path: use null for absent filenames")
    # Reject an incompatible path base rather than discarding it.  A Windows
    # absolute filename must never silently become parameter-relative.
    if _source_path(path["value"])["base"] != path["base"]:
        raise ValueError("measurement.source.path: base contradicts filename")
    return path["value"]


def rebase_v3_source_paths(v3: dict[str, Any], old_document: str,
                           new_document: str) -> dict[str, Any]:
    """Reference-only Save As path rebasing without filesystem I/O.

    A relative measurement source should continue referring to the same
    absolute image after the parameter document moves. The original v3 object
    remains unchanged if any path cannot be interpreted unambiguously.
    This adapter does not implement the Qt/C++ document transaction.
    """
    if not isinstance(old_document, str) or not isinstance(new_document, str):
        raise ValueError("source and destination document paths must be text")

    def windows_path(value: str) -> bool:
        return bool(ntpath.splitdrive(value)[0]) or "\\" in value

    old_windows = windows_path(old_document)
    if old_windows != windows_path(new_document):
        raise ValueError("cannot rebase between Windows and POSIX document paths")
    lib = ntpath if old_windows else posixpath
    if not lib.isabs(old_document) or not lib.isabs(new_document):
        raise ValueError("Save As requires absolute old and new document paths")

    changed = deepcopy(v3)
    try:
        measurements = changed["calibration"]["capture"]["mtf"]["measurements"]
    except (TypeError, KeyError) as exc:
        raise ValueError("calibration.capture.mtf.measurements: missing") from exc
    if not isinstance(measurements, list):
        raise ValueError("calibration.capture.mtf.measurements: expected array")
    original_dir = lib.dirname(lib.normpath(old_document))
    destination_dir = lib.dirname(lib.normpath(new_document))
    for i, measurement in enumerate(measurements):
        location = f"calibration.capture.mtf.measurements[{i}].source.path"
        try:
            path = measurement["source"]["path"]
        except (TypeError, KeyError) as exc:
            raise ValueError(f"{location}: missing") from exc
        _source_to_old(path)  # Validate missing/absolute/relative spelling.
        if path is None or path["base"] == "absolute":
            continue
        spelling = path["value"]
        if (old_windows and not lib.isabs(spelling)
                and ntpath.splitdrive(spelling)[0]):
            raise ValueError(f"{location}: drive-relative Windows path is ambiguous")
        if not old_windows and "\\" in spelling:
            raise ValueError(f"{location}: Windows separators in a POSIX-relative path")
        resolved = lib.normpath(lib.join(original_dir, spelling))
        if (old_windows and ntpath.normcase(ntpath.splitdrive(resolved)[0])
                != ntpath.normcase(ntpath.splitdrive(destination_dir)[0])):
            # Do not rely on ntpath.relpath raising for different drives.
            # Some Windows/Python environments return a syntactically relative
            # spelling that cannot designate the original image on the new
            # volume. The target must be a real absolute path instead.
            path.update({"value": resolved, "base": "absolute"})
            continue
        try:
            relative = lib.relpath(resolved, destination_dir)
        except ValueError:
            # Defensive fallback (e.g. inconsistent UNC/mount spelling).
            path.update({"value": resolved, "base": "absolute"})
        else:
            path["value"] = relative
    return changed


def _mtf_measurement_upgrade(m: dict[str, Any], index: int, group_id: str) -> dict[str, Any]:
    path = f"sharpness.mtf.measurements[{index}]"
    _keys(m, set("channel image_layer wavelength_nm same_capture name source_filename source_dimensions roi edge edge_quality samples".split()), path)
    if type(m["same_capture"]) is not bool:
        raise ValueError(f"{path}.same_capture: expected boolean")
    quality = m["edge_quality"]
    if not isinstance(quality, list) or len(quality) != 5:
        raise ValueError(f"{path}.edge_quality: expected five fields")
    return {
        "id": f"measurement-{index + 1:06}",
        "capture_group_id": group_id,
        "channel": deepcopy(m["channel"]),
        "image_layer": deepcopy(m["image_layer"]),
        "wavelength_nm": deepcopy(m["wavelength_nm"]),
        "label": deepcopy(m["name"]),
        "source": {
            "path": _source_path(m["source_filename"]),
            "dimensions": _optional_rect_with_sentinel(m["source_dimensions"], ("width_px", "height_px"), f"{path}.source_dimensions"),
            "roi": _optional_rect_with_sentinel(m["roi"], ("x_px", "y_px", "width_px", "height_px"), f"{path}.roi"),
            "edge_points_px": deepcopy(m["edge"]),
        },
        "edge_quality": dict(zip(("angle_deg", "fit_rms_px", "contrast_fraction", "snr", "phase_coverage_fraction"), quality)),
        "samples": deepcopy(m["samples"]),
    }


def _mtf_measurement_downgrade(m: dict[str, Any], same_capture: bool) -> dict[str, Any]:
    _keys(m, set("id capture_group_id channel image_layer wavelength_nm label source edge_quality samples".split()), "calibration.capture.mtf.measurements[]")
    src = _take(m["source"], "path dimensions roi edge_points_px", "measurement.source")
    q = _take(m["edge_quality"], "angle_deg fit_rms_px contrast_fraction snr phase_coverage_fraction", "measurement.edge_quality")
    return {
        "channel": deepcopy(m["channel"]),
        "image_layer": deepcopy(m["image_layer"]),
        "wavelength_nm": deepcopy(m["wavelength_nm"]),
        "same_capture": same_capture,
        "name": deepcopy(m["label"]),
        "source_filename": _source_to_old(src["path"]),
        "source_dimensions": _optional_rect_to_old(src["dimensions"], ("width_px", "height_px"), "measurement.source.dimensions"),
        "roi": _optional_rect_to_old(src["roi"], ("x_px", "y_px", "width_px", "height_px"), "measurement.source.roi"),
        "edge": deepcopy(src["edge_points_px"]),
        "edge_quality": [q[k] for k in ("angle_deg", "fit_rms_px", "contrast_fraction", "snr", "phase_coverage_fraction")],
        "samples": deepcopy(m["samples"]),
    }



def _validate_v2_nested_vocabulary(v2: dict[str, Any]) -> None:
    """Reject unknown nested v2 values before moving opaque grids/curves.

    Keep this list matched to v2_check_document_vocabulary in the C++ reader.
    A new C++ field must cause an explicit reference-audit update rather than
    disappear invisibly through an opaque field moved to another section.
    """
    nested = {
        "capture.scan_crop": "enabled rect",
        "capture.image_area": "enabled rect",
        "process.strip_widths": "red green",
        "process.contact_copy": "simulate preflash exposure boost emulsion_curve",
        "process.contact_copy.emulsion_curve": "min linear1 linear2 max",
        "geometry.lens": "radial center",
        "reconstruction.screen_denoise": "mode strength noise_variance_floor noise_variance_slope patch_radius search_radius bilateral_sigma_s bilateral_sigma_r",
        "reconstruction.demosaiced_denoise": "mode strength noise_variance_floor noise_variance_slope patch_radius search_radius bilateral_sigma_s bilateral_sigma_r",
        "color.scanner_primaries": "red green blue",
        "color.process_profile": "dark red green blue",
        "color.tone_curve": "type control_points",
        "correction_grids.tiles": "dimensions adjustments",
    }
    for path, fields in nested.items():
        obj = v2
        for part in path.split("."):
            obj = _obj(obj, path)[part]
        _keys(_obj(obj, path), set(fields.split()), path)
    optional = {
        "geometry.mesh": "direction shift step dimensions points",
        "correction_grids.backlight": "dimensions channels black_correction luminosities subtractions",
        "correction_grids.scanner_blur": "mode dimensions values",
    }
    for path, fields in optional.items():
        obj = v2
        for part in path.split("."):
            obj = _obj(obj, path)[part]
        if obj is not None:
            _keys(_obj(obj, path), set(fields.split()), path)
    for index, point in enumerate(v2["registration"]["points"]):
        _keys(_obj(point, f"registration.points[{index}]"), {"image", "screen", "color"}, "registration.points[]")
    tiles = v2["correction_grids"]["tiles"]["adjustments"]
    for index, tile in enumerate(tiles):
        path = f"correction_grids.tiles.adjustments[{index}]"
        _keys(_obj(tile, path), {"exposure", "dark_point", "scanner_blur"}, path)
        if tile["scanner_blur"] is not None:
            _keys(_obj(tile["scanner_blur"], path + ".scanner_blur"), {"mode", "dimensions", "values"}, path + ".scanner_blur")

def upgrade_v2(v2: dict[str, Any]) -> dict[str, Any]:
    """Make a schema-3 *reference* document without mutating v2.

    Unknown fields fail closed, so a future v2 parameter never disappears just
    because this experiment is behind the C++ implementation.
    """
    _keys(v2, set("format schema_version capture process geometry detection registration profile reconstruction sharpness color correction_grids".split()) | ({"required_features"} if "required_features" in v2 else set()), "document")
    if v2["format"] != FORMAT or v2["schema_version"] != 2 or v2.get("required_features", []) != []:
        raise ValueError("unsupported v2 document identity/features")
    _validate_v2_nested_vocabulary(v2)
    cap = _take(v2["capture"], "capture_type demosaic gamma scan_rotation_quarter_turns scan_mirror scan_crop image_area scan_exposure dark_point backlight_correction_black", "capture")
    proc = _take(v2["process"], "color_model age dye_density strip_widths contact_copy", "process")
    g = _take(v2["geometry"], "screen_type scanner_type center axis_x axis_y projection_distance tilt final_rotation final_mirror final_angle final_ratio lens mesh", "geometry")
    reg = _take(v2["registration"], "optimize_lens lens_center_distance optimize_tilt points", "registration")
    profile = _take(v2["profile"], "spots", "profile")
    rec = _take(v2["reconstruction"], "ignore_infrared mix_weights mix_dark collection_quality screen_demosaic demosaiced_scaling screen_blur_radius collection_threshold screen_denoise demosaiced_denoise", "reconstruction")
    sharp = _take(v2["sharpness"], "mode unsharp_radius unsharp_amount scanner_snr scanner_mtf_scale richardson_lucy_iterations richardson_lucy_sigma deconvolution_supersample resampling_kernel mtf", "sharpness")
    mtf = _take(sharp["mtf"], "model sigma_px halo_fraction halo_sigma_px blur_diameter_px defocus_mm f_stop pixel_pitch_um sensor_fill_factor scan_dpi wavelengths_nm selected_measurement measurements", "sharpness.mtf")
    color = _take(v2["color"], "scanner_primaries process_profile white_balance presaturation temperature backlight_temperature observer_whitepoint dye_balance saturation brightness tone_curve", "color")
    grids = _take(v2["correction_grids"], "backlight scanner_blur tiles", "correction_grids")
    _keys(_obj(proc["strip_widths"], "process.strip_widths"), {"red", "green"}, "process.strip_widths")
    def width(x: Any) -> dict[str, Any]:
        return {"mode": "automatic"} if x == 0 else {"mode": "fraction", "value": x}
    group_index = -1
    upgraded = []
    for i, m in enumerate(mtf["measurements"]):
        if i == 0 or not m["same_capture"]:
            group_index += 1
        upgraded.append(_mtf_measurement_upgrade(m, i, f"capture-{group_index + 1:06}"))
    selected_index = mtf["selected_measurement"]
    if type(selected_index) is not int or selected_index < -1 or selected_index >= len(upgraded):
        raise ValueError("invalid v2 MTF measurement selection")
    v3_mtf = {k: deepcopy(mtf[k]) for k in "model sigma_px halo_fraction halo_sigma_px blur_diameter_px defocus_mm f_stop pixel_pitch_um sensor_fill_factor scan_dpi wavelengths_nm".split()}
    v3_mtf.update({
        "selected_measurement_id": None if selected_index == -1 else upgraded[selected_index]["id"],
        "measurements": upgraded,
        "sample_columns": ["frequency_cycles_per_pixel", "contrast_percent", "uncertainty_percentage_points"],
        "legacy_first_same_capture": bool(mtf["measurements"] and mtf["measurements"][0]["same_capture"]),
    })
    v3 = {
        "format": FORMAT, "schema_version": 3,
        "capture": {
            "type": cap["capture_type"], "raw": {"demosaic": cap["demosaic"]},
            "linearization": {"mode": "srgb"} if cap["gamma"] == -1 else {"mode": "power", "exponent": cap["gamma"]},
            "orientation": {"quarter_turns": cap["scan_rotation_quarter_turns"], "mirror": cap["scan_mirror"]},
            "bounds": {"object_crop": _rect_from_old(cap["scan_crop"], "capture.scan_crop"), "photograph": _rect_from_old(cap["image_area"], "capture.image_area")},
            "levels": {"gain": cap["scan_exposure"], "black_offset": cap["dark_point"]},
        },
        "process": {
            "screen": {"type": g["screen_type"], "strip_widths": {k: width(proc["strip_widths"][k]) for k in ("red", "green")}},
            "color_model": proc["color_model"], "dyes": {"aging_fraction": proc["age"], "density": proc["dye_density"]},
            "contact_copy": proc["contact_copy"],
        },
        "geometry": {
            "imaging_model": g["scanner_type"],
            "mapping": {k: g[k] for k in ("center", "axis_x", "axis_y", "projection_distance", "tilt")},
            "final_frame": {"rotation": g["final_rotation"], "mirror": g["final_mirror"], "angle_deg": g["final_angle"], "axis_length_ratio": g["final_ratio"]},
            "lens": g["lens"], "mesh": g["mesh"],
        },
        "registration": {"detection": deepcopy(v2["detection"]), "fit": {k: reg[k] for k in ("optimize_lens", "lens_center_distance", "optimize_tilt")}, "points": reg["points"]},
        "calibration": {
            "capture": {"mtf": v3_mtf, "scanner_primaries_xyz": color["scanner_primaries"]},
            "process_color": {"profile_rgb": color["process_profile"], "spots": profile["spots"]},
            "flat_field": {"black_correction": cap["backlight_correction_black"], "grid": grids["backlight"]},
            "spatial_blur": grids["scanner_blur"], "tiles": grids["tiles"],
        },
        "reconstruction": {
            "image_layer": {"ignore_infrared": rec["ignore_infrared"], "rgb_mixer": {"weights": rec["mix_weights"], "black_offsets": rec["mix_dark"]}},
            "collection": {"quality": rec["collection_quality"], "threshold": rec["collection_threshold"]},
            "color_field": {"screen_demosaic": rec["screen_demosaic"], "resampling": rec["demosaiced_scaling"], "screen_blur_radius": rec["screen_blur_radius"]},
            "denoise": {"before_reconstruction": rec["screen_denoise"], "after_reconstruction": rec["demosaiced_denoise"]},
            "sharpening": {"mode": sharp["mode"], "unsharp": {"radius": sharp["unsharp_radius"], "amount": sharp["unsharp_amount"]},
                           "scanner": {"snr": sharp["scanner_snr"], "mtf_scale": sharp["scanner_mtf_scale"]},
                           "richardson_lucy": {"iterations": sharp["richardson_lucy_iterations"], "sigma": sharp["richardson_lucy_sigma"]},
                           "deconvolution_supersample": sharp["deconvolution_supersample"], "resampling_kernel": sharp["resampling_kernel"]},
        },
        "color": {
            "illuminants": {"photograph_temperature_k": color["temperature"], "backlight_temperature_k": color["backlight_temperature"]},
            "viewing": {"whitepoint_xy": color["observer_whitepoint"]},
            "adjustments": {k: color[k] for k in ("white_balance", "presaturation", "dye_balance", "saturation", "brightness", "tone_curve")},
        },
    }
    if "required_features" in v2:
        v3["required_features"] = deepcopy(v2["required_features"])
    return v3


def downgrade_v3(v3: dict[str, Any]) -> dict[str, Any]:
    """Downgrade only a fully representable v3 reference document.

    Group IDs can be in any order. Each group's measurements must form one
    contiguous run for old same_capture adjacency flags to express them.
    Selection is recovered by immutable measurement ID, not array index.
    """
    _keys(v3, set("format schema_version capture process geometry registration calibration reconstruction color".split()) | ({"required_features"} if "required_features" in v3 else set()), "document")
    if v3["format"] != FORMAT or v3["schema_version"] != 3 or v3.get("required_features", []) != []:
        raise ValueError("unsupported v3 document identity/features")
    cap = _take(v3["capture"], "type raw linearization orientation bounds levels", "capture")
    proc = _take(v3["process"], "screen color_model dyes contact_copy", "process")
    g = _take(v3["geometry"], "imaging_model mapping final_frame lens mesh", "geometry")
    reg = _take(v3["registration"], "detection fit points", "registration")
    cal = _take(v3["calibration"], "capture process_color flat_field spatial_blur tiles", "calibration")
    rec = _take(v3["reconstruction"], "image_layer collection color_field denoise sharpening", "reconstruction")
    col = _take(v3["color"], "illuminants viewing adjustments", "color")
    tf = _obj(cap["linearization"], "capture.linearization")
    if tf.get("mode") == "srgb":
        _keys(tf, {"mode"}, "capture.linearization")
        gamma = -1
    elif tf.get("mode") == "power":
        _keys(tf, {"mode", "exponent"}, "capture.linearization")
        gamma = tf["exponent"]
        if gamma == -1:
            raise ValueError("capture.linearization: -1 reserved for sRGB")
    else:
        raise ValueError("capture.linearization: unknown transfer mode")
    orient = _take(cap["orientation"], "quarter_turns mirror", "capture.orientation")
    bounds = _take(cap["bounds"], "object_crop photograph", "capture.bounds")
    levels = _take(cap["levels"], "gain black_offset", "capture.levels")
    raw = _take(cap["raw"], "demosaic", "capture.raw")
    screen = _take(proc["screen"], "type strip_widths", "process.screen")
    _keys(screen["strip_widths"], {"red", "green"}, "process.screen.strip_widths")
    def strip(w: Any, field: str) -> float:
        obj = _obj(w, field)
        if obj.get("mode") == "automatic":
            _keys(obj, {"mode"}, field)
            return 0
        if obj.get("mode") == "fraction":
            _keys(obj, {"mode", "value"}, field)
            if obj["value"] == 0:
                raise ValueError(f"{field}: zero must be automatic")
            return obj["value"]
        raise ValueError(f"{field}: invalid width mode")
    strips = {c: strip(screen["strip_widths"][c], f"process.screen.strip_widths.{c}") for c in ("red", "green")}
    dyes = _take(proc["dyes"], "aging_fraction density", "process.dyes")
    mapping = _take(g["mapping"], "center axis_x axis_y projection_distance tilt", "geometry.mapping")
    frame = _take(g["final_frame"], "rotation mirror angle_deg axis_length_ratio", "geometry.final_frame")
    fit = _take(reg["fit"], "optimize_lens lens_center_distance optimize_tilt", "registration.fit")
    ccap = _take(cal["capture"], "mtf scanner_primaries_xyz", "calibration.capture")
    pcolor = _take(cal["process_color"], "profile_rgb spots", "calibration.process_color")
    flat = _take(cal["flat_field"], "black_correction grid", "calibration.flat_field")
    mtf = _take(ccap["mtf"], "model sigma_px halo_fraction halo_sigma_px blur_diameter_px defocus_mm f_stop pixel_pitch_um sensor_fill_factor scan_dpi wavelengths_nm selected_measurement_id measurements sample_columns legacy_first_same_capture", "calibration.capture.mtf")
    if mtf["sample_columns"] != ["frequency_cycles_per_pixel", "contrast_percent", "uncertainty_percentage_points"]:
        raise ValueError("calibration.capture.mtf.sample_columns: unsupported sample layout")
    measurements = mtf["measurements"]
    if not isinstance(measurements, list):
        raise ValueError("calibration.capture.mtf.measurements: expected array")
    if type(mtf["legacy_first_same_capture"]) is not bool or (not measurements and mtf["legacy_first_same_capture"]):
        raise ValueError("calibration.capture.mtf.legacy_first_same_capture: invalid migration-only flag")
    ids = set()
    seen_groups = set()
    last_group = None
    selected_index = -1
    old_measurements = []
    for i, m in enumerate(measurements):
        ident, group = m["id"], m["capture_group_id"]
        if not isinstance(ident, str) or not ident or ident in ids:
            raise ValueError(f"calibration.capture.mtf.measurements[{i}].id: duplicate or empty")
        ids.add(ident)
        if not isinstance(group, str) or not group:
            raise ValueError(f"calibration.capture.mtf.measurements[{i}].capture_group_id: missing")
        if group != last_group:
            if group in seen_groups:
                raise ValueError("cannot downgrade discontiguous capture groups to v2")
            seen_groups.add(group)
        same_capture = mtf["legacy_first_same_capture"] if i == 0 else group == last_group
        old_measurements.append(_mtf_measurement_downgrade(m, same_capture))
        if ident == mtf["selected_measurement_id"]:
            selected_index = i
        last_group = group
    if mtf["selected_measurement_id"] is not None and selected_index < 0:
        raise ValueError("calibration.capture.mtf.selected_measurement_id: dangling ID")
    original_mtf = {k: mtf[k] for k in "model sigma_px halo_fraction halo_sigma_px blur_diameter_px defocus_mm f_stop pixel_pitch_um sensor_fill_factor scan_dpi wavelengths_nm".split()}
    original_mtf.update({"selected_measurement": selected_index, "measurements": old_measurements})
    image = _take(rec["image_layer"], "ignore_infrared rgb_mixer", "reconstruction.image_layer")
    mixer = _take(image["rgb_mixer"], "weights black_offsets", "reconstruction.image_layer.rgb_mixer")
    collect = _take(rec["collection"], "quality threshold", "reconstruction.collection")
    field = _take(rec["color_field"], "screen_demosaic resampling screen_blur_radius", "reconstruction.color_field")
    denoise = _take(rec["denoise"], "before_reconstruction after_reconstruction", "reconstruction.denoise")
    sharpen = _take(rec["sharpening"], "mode unsharp scanner richardson_lucy deconvolution_supersample resampling_kernel", "reconstruction.sharpening")
    usm = _take(sharpen["unsharp"], "radius amount", "reconstruction.sharpening.unsharp")
    scanner = _take(sharpen["scanner"], "snr mtf_scale", "reconstruction.sharpening.scanner")
    rl = _take(sharpen["richardson_lucy"], "iterations sigma", "reconstruction.sharpening.richardson_lucy")
    illuminants = _take(col["illuminants"], "photograph_temperature_k backlight_temperature_k", "color.illuminants")
    viewing = _take(col["viewing"], "whitepoint_xy", "color.viewing")
    appearance = _take(col["adjustments"], "white_balance presaturation dye_balance saturation brightness tone_curve", "color.adjustments")
    old = {
        "format": FORMAT, "schema_version": 2,
        "capture": {
            "capture_type": cap["type"], "demosaic": raw["demosaic"], "gamma": gamma,
            "scan_rotation_quarter_turns": orient["quarter_turns"], "scan_mirror": orient["mirror"],
            "scan_crop": _rect_to_old(bounds["object_crop"], "capture.bounds.object_crop"),
            "image_area": _rect_to_old(bounds["photograph"], "capture.bounds.photograph"),
            "scan_exposure": levels["gain"], "dark_point": levels["black_offset"], "backlight_correction_black": flat["black_correction"],
        },
        "process": {"color_model": proc["color_model"], "age": dyes["aging_fraction"], "dye_density": dyes["density"], "strip_widths": strips, "contact_copy": proc["contact_copy"]},
        "geometry": {"screen_type": screen["type"], "scanner_type": g["imaging_model"], **mapping,
                     "final_rotation": frame["rotation"], "final_mirror": frame["mirror"], "final_angle": frame["angle_deg"], "final_ratio": frame["axis_length_ratio"],
                     "lens": g["lens"], "mesh": g["mesh"]},
        "detection": reg["detection"], "registration": {**fit, "points": reg["points"]}, "profile": {"spots": pcolor["spots"]},
        "reconstruction": {"ignore_infrared": image["ignore_infrared"], "mix_weights": mixer["weights"], "mix_dark": mixer["black_offsets"], "collection_quality": collect["quality"],
                           "screen_demosaic": field["screen_demosaic"], "demosaiced_scaling": field["resampling"], "screen_blur_radius": field["screen_blur_radius"], "collection_threshold": collect["threshold"],
                           "screen_denoise": denoise["before_reconstruction"], "demosaiced_denoise": denoise["after_reconstruction"]},
        "sharpness": {"mode": sharpen["mode"], "unsharp_radius": usm["radius"], "unsharp_amount": usm["amount"], "scanner_snr": scanner["snr"], "scanner_mtf_scale": scanner["mtf_scale"],
                      "richardson_lucy_iterations": rl["iterations"], "richardson_lucy_sigma": rl["sigma"],
                      "deconvolution_supersample": sharpen["deconvolution_supersample"], "resampling_kernel": sharpen["resampling_kernel"], "mtf": original_mtf},
        "color": {"scanner_primaries": ccap["scanner_primaries_xyz"], "process_profile": pcolor["profile_rgb"],
                  "white_balance": appearance["white_balance"], "presaturation": appearance["presaturation"], "temperature": illuminants["photograph_temperature_k"],
                  "backlight_temperature": illuminants["backlight_temperature_k"], "observer_whitepoint": viewing["whitepoint_xy"],
                  "dye_balance": appearance["dye_balance"], "saturation": appearance["saturation"], "brightness": appearance["brightness"], "tone_curve": appearance["tone_curve"]},
        "correction_grids": {"backlight": flat["grid"], "scanner_blur": cal["spatial_blur"], "tiles": cal["tiles"]},
    }
    if "required_features" in v3:
        old["required_features"] = deepcopy(v3["required_features"])
    # Moving an opaque calibration/denoiser subtree must not circumvent the
    # strict vocabulary check that was already required on v2 import. In
    # particular, a future nested v3 field must not be silently carried into
    # an old version or ignored by callers before the native C++ decoder runs.
    _validate_v2_nested_vocabulary(old)
    return deepcopy(old)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--direction", choices=("upgrade", "downgrade"), required=True)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    with args.input.open(encoding="utf-8") as source:
        state = json.load(source)
    output = upgrade_v2(state) if args.direction == "upgrade" else downgrade_v3(state)
    args.output.write_text(json.dumps(output, indent=2, ensure_ascii=False, allow_nan=False) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
