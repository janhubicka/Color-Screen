#ifndef PARAMETER_ARCHIVE_H
#define PARAMETER_ARCHIVE_H
#include "include/dllpublic.h"
#include "include/render-parameters.h"
#include "include/render-type-parameters.h"
#include "include/scr-to-img-parameters.h"
#include "include/scr-detect-parameters.h"
#include "include/solver-parameters.h"

#include <cstdio>
#include <string>
#include <vector>

namespace colorscreen
{

/* Structured render fields that the legacy CSP mirror cannot represent.

   PRESENT is false for legacy files and schema-v1 archives without the
   render-overrides-v1 required feature. Four values remain authoritative
   reconstruction inputs applied after parsing state/legacy.par. Schema v1
   also requires historical OUTPUT_PROFILE and GAMUT_WARNING fields for
   interoperability: validate them on read, emit neutral values on write,
   but never apply them as saved document state. Views and export requests
   own those choices independently. */
struct parameter_archive_render_overrides
{
  bool present = false;
  bool ignore_infrared = false;
  render_parameters::demosaiced_scaling_t demosaiced_scaling
      = render_parameters::default_scaling;
  xy_t observer_whitepoint = d50_white;
  render_output_parameters::output_profile_t output_profile
      = render_output_parameters::output_profile_sRGB;
  luminosity_t output_gamma = -1;
  bool gamut_warning = false;
};

/* Return the structured archive supplement for RPARAM. */
DLL_PUBLIC parameter_archive_render_overrides
parameter_archive_render_overrides_from (const render_parameters &rparam);

/* Apply authoritative structured fields from OVERRIDES to RPARAM. */
DLL_PUBLIC void
apply_parameter_archive_render_overrides (
    const parameter_archive_render_overrides &overrides,
    render_parameters *rparam);

/* Authoritative final-frame geometry omitted by the legacy CSP serializer.

   PRESENT is false for plain CSP and earlier schema-v1 archives. An archive
   advertising geometry-final-frame-v1 must provide both values. */
struct parameter_archive_geometry_final_frame
{
  bool present = false;
  coord_t final_angle = 90;
  coord_t final_ratio = 1;
};

/* Extract the final geometry frame from PARAM for archive serialization. */
DLL_PUBLIC parameter_archive_geometry_final_frame
parameter_archive_geometry_final_frame_from (const scr_to_img_parameters &param);

/* Apply a validated final-frame supplement after loading legacy CSP. */
DLL_PUBLIC void
apply_parameter_archive_geometry_final_frame (
    const parameter_archive_geometry_final_frame &frame,
    scr_to_img_parameters *param);

/* Persist the inner photographic bounds separately from the physical-object
   scan crop stored by legacy CSP. A missing required image-area-v1 feature
   means that older files have no independent photographic image boundary. */
struct parameter_archive_image_area
{
  bool present = false;
  int_optional_image_area area;
};

/* Capture and apply the versioned photographic image-area supplement. */
DLL_PUBLIC parameter_archive_image_area
parameter_archive_image_area_from (const render_parameters &rparam);
DLL_PUBLIC void
apply_parameter_archive_image_area (const parameter_archive_image_area &bounds,
                                    render_parameters *rparam);

/* Parsed compatibility information from a Color-Screen parameter archive.  */
struct parameter_archive_manifest
{
  int schema_version = 0;
  std::string legacy_csp_path;
  parameter_archive_render_overrides render_overrides;
  parameter_archive_geometry_final_frame geometry_final_frame;
  parameter_archive_image_area image_area;
};

/* Return true if UTF-8 host path NAME starts with a ZIP signature and may
   therefore be a Color-Screen parameter archive. This is only format dispatch;
   callers must still validate the archive with read_parameter_archive. */
DLL_PUBLIC bool parameter_archive_signature_p (const char *name);

/* Open UTF-8 host path NAME as a parameter payload stream.

   Plain CSP text is opened directly. A ZIP signature is treated as a
   Color-Screen archive and its validated schema-v1 legacy payload is copied to
   a private temporary stream. The returned FILE* is positioned at byte zero and
   belongs to the caller. IS_ARCHIVE, when non-null, reports which path was
   taken. ERROR receives a diagnostic on failure. */
DLL_PUBLIC FILE *open_parameter_payload (
    const char *name, bool *is_archive, std::string *error,
    parameter_archive_manifest *manifest = nullptr);

/* Read and validate parameter archive at UTF-8 host path NAME.

   On success copy the schema-v1 legacy CSP payload to LEGACY_CSP and, when
   non-null, parsed manifest information to MANIFEST.  ERROR receives a
   user-facing diagnostic on failure.  No archive entry is extracted to the
   filesystem.  */
DLL_PUBLIC bool
read_parameter_archive (const char *name, std::string *legacy_csp,
                        parameter_archive_manifest *manifest,
                        std::string *error);

/* Write one schema-v1 archive to UTF-8 host path NAME.

   LEGACY_CSP is the complete CSP/Qt payload to place in state/legacy.par.
   GENERATOR_VERSION is informational and may be null.  NAME is a staging
   destination: this function may create/truncate it and deliberately does not
   implement the caller's final atomic replacement policy.  ERROR receives a
   diagnostic on failure.  */
DLL_PUBLIC bool
write_parameter_archive (
    const char *name, const std::string &legacy_csp,
    const char *generator_version, std::string *error,
    const parameter_archive_render_overrides *render_overrides = nullptr,
    const parameter_archive_geometry_final_frame *geometry_final_frame = nullptr,
    const parameter_archive_image_area *image_area = nullptr);

/* Atomically replace UTF-8 host path NAME with PAYLOAD.

   When ARCHIVE is true, PAYLOAD becomes state/legacy.par in a schema-v1
   archive; otherwise PAYLOAD is written as a legacy parameter file verbatim.
   GENERATOR_VERSION is used only for archive manifests and is ignored for a
   legacy payload. A complete sibling staging file is finalized before
   replacement, so a failed write never truncates an older usable target. */
DLL_PUBLIC bool
write_parameter_payload_file (
    const char *name, const std::string &payload, bool archive,
    const char *generator_version, std::string *error,
    const parameter_archive_render_overrides *render_overrides = nullptr,
    const parameter_archive_geometry_final_frame *geometry_final_frame = nullptr,
    const parameter_archive_image_area *image_area = nullptr);


/* Serialize one native JSON v2 registration component.

   This is a lossless building block for the eventual complete schema-v2
   document, NOT a standalone .cspar writer. It reads these structures
   directly, without constructing or parsing legacy CSP text. It includes
   accepted screen geometry (including mesh and final frame), detection dye
   controls, solver policy/points and profile spots. The full document writer
   must not be exposed until every persistent domain has native coverage.
   OUTPUT remains unchanged on failure. */
DLL_PUBLIC bool
encode_parameter_json_v2_registration (
    const scr_to_img_parameters &geometry,
    const scr_detect_parameters &detection,
    const solver_parameters &solver,
    const std::vector<point_t> &profile_spots,
    std::string *output, std::string *error);

/* Encode the scalar capture-input component as native JSON v2.
   This covers capture identification, RAW demosaicing choice, gamma, scan
   presentation, physical and photographic bounds, and exposure/dark-point
   controls. It does not serialize correction grids or scanner MTF, which have
   separate required native components still to implement. The returned
   object is not a complete standalone .cspar v2 document. */
DLL_PUBLIC bool
encode_parameter_json_v2_capture (
    const render_parameters &capture, std::string *output,
    std::string *error);

/* Decode capture-input values transactionally into CAPTURE, retaining all
   unrelated processing controls. On failure CAPTURE is unchanged. */
DLL_PUBLIC bool
decode_parameter_json_v2_capture (
    const std::string &input, render_parameters *capture,
    std::string *error);

/* Encode image-layer and colour-screen reconstruction controls as native
   JSON. Includes the infrared policy, RGB mixer, collection and demosaicing
   selections, screen blur/threshold, and both independent denoising stages.
   This remains a component: no full v2 .cspar writer exists yet. */
DLL_PUBLIC bool
encode_parameter_json_v2_reconstruction (
    const render_parameters &render, std::string *output,
    std::string *error);

/* Decode the reconstruction component transactionally into RENDER while
   leaving unrelated capture, sharpening and colour state untouched. */
DLL_PUBLIC bool
decode_parameter_json_v2_reconstruction (
    const std::string &input, render_parameters *render,
    std::string *error);

/* Encode historical process colour-model, screen strip widths, dye
   aging/density and complete contact-copy characteristic curve into native
   JSON. This is a component for a future complete v2 document. */
DLL_PUBLIC bool
encode_parameter_json_v2_process (
    const render_parameters &render, std::string *output,
    std::string *error);

/* Decode process parameters transactionally without changing other render
   controls, or the document, on failure. */
DLL_PUBLIC bool
decode_parameter_json_v2_process (
    const std::string &input, render_parameters *render,
    std::string *error);

/* Serialize persistent colour calibration and appearance, including scanner
   primaries, process-profile RGB matrices, balance/observer adjustments and
   editable output tone curve. Output monitor profile and transfer gamma are
   per-render resources and deliberately absent. */
DLL_PUBLIC bool
encode_parameter_json_v2_color (
    const render_parameters &render, std::string *output,
    std::string *error);

/* Decode one native colour/appearance component transactionally into RENDER
   while retaining all unrelated processing controls and caches. */
DLL_PUBLIC bool
decode_parameter_json_v2_color (
    const std::string &input, render_parameters *render,
    std::string *error);

/* Parse the native JSON v2 registration component into independent temporary
   state, committing only when all fields and bounds are valid. This prevents
   a malformed field from partially mutating a live document. ERROR gives a
   diagnostic; all outputs remain unchanged on failure. */
DLL_PUBLIC bool
decode_parameter_json_v2_registration (
    const std::string &input, scr_to_img_parameters *geometry,
    scr_detect_parameters *detection, solver_parameters *solver,
    std::vector<point_t> *profile_spots, std::string *error);

}

#endif
