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

/* Return false with a diagnostic if document parameters include saved values
   that the historical .par serializer cannot represent at all. Reject such
   output before truncating/replacing any destination rather than silently
   losing JSON/v1-only inputs when exporting a legacy file. Either pointer
   may be null for a partial CLI workflow; this is not a CSP syntax check. */
DLL_PUBLIC bool
legacy_csp_can_represent_parameters (
    const scr_to_img_parameters *geometry,
    const render_parameters *render, std::string *error);

/* Open a historical .par file at UTF-8 host path NAME for reading.
   The returned FILE* belongs to the caller; ERROR receives a useful failure
   diagnostic. ZIP parameter archives are no longer recognized or supported. */
DLL_PUBLIC FILE *open_parameter_legacy_file (const char *name,
                                              std::string *error);

/* Parse the complete legacy profile-spots postamble TRAILING into SPOTS.
   Unknown metadata is rejected rather than lost when converting to JSON.
   ERROR receives the diagnostic; SPOTS remains unchanged on failure. */
DLL_PUBLIC bool
parse_parameter_profile_spots (const std::string &trailing,
                               std::vector<point_t> *spots, std::string *error);

/* Validate the full native JSON schema or legacy CSP payload before an
   implicit overwrite. Neither malformed JSON nor unsupported formats are
   treated as safe destinations. ERROR receives a diagnostic. */
DLL_PUBLIC bool
validate_parameter_document_file (const char *name, std::string *error);

/* Atomically replace UTF-8 path NAME with complete text PAYLOAD. A sibling
   staging file is finished before replacement. ERROR receives failures;
   an older usable destination is left unchanged. No ZIP writer exists. */
DLL_PUBLIC bool
write_parameter_text_file (const char *name, const std::string &payload,
                           std::string *error);


/* Construct the first public native .cspar JSON document directly from
   typed processing state, using the canonical semantic schema-3 layout.
   Legacy .par records are upgraded to stable record/capture IDs on a private
   copy; original caller values are never modified. No JSON-v1/v2 input is
   accepted by the new reader. This format is not yet selected by GUI/CLI
   until the document/recovery integration and corpus gates pass. */
DLL_PUBLIC bool
encode_parameter_json_v3_document (
    const scr_to_img_parameters &geometry,
    const scr_detect_parameters &detection,
    const render_parameters &render,
    const solver_parameters &solver,
    const std::vector<point_t> &profile_spots,
    std::string *output, std::string *error);

/* Read a strict complete schema-3 document transactionally into native
   state. Unknown fields, invalid capture IDs/metadata and incomplete
   calibration leave every output argument unchanged. */
DLL_PUBLIC bool
decode_parameter_json_v3_document (
    const std::string &input,
    scr_to_img_parameters *geometry,
    scr_detect_parameters *detection,
    render_parameters *render,
    solver_parameters *solver,
    std::vector<point_t> *profile_spots,
    std::string *error);

/* Read or write canonical schema-3 .cspar at UTF-8 filesystem path NAME.
   Readers stage all persistent state until validation succeeds; writers
   complete the document before atomic same-directory replacement. No
   unreleased JSON-v1/v2 is recognized by these APIs. */
DLL_PUBLIC bool
read_parameter_json_v3_file (
    const char *name, scr_to_img_parameters *geometry,
    scr_detect_parameters *detection, render_parameters *render,
    solver_parameters *solver, std::vector<point_t> *profile_spots,
    std::string *error);

DLL_PUBLIC bool
write_parameter_json_v3_file (
    const char *name, const scr_to_img_parameters &geometry,
    const scr_detect_parameters &detection,
    const render_parameters &render, const solver_parameters &solver,
    const std::vector<point_t> &profile_spots, std::string *error);

/* Rebase relative measured-MTF source paths when a canonical parameter
   document is saved under a different absolute pathname. Source references
   must continue designating the same image. Handles POSIX, Windows drives
   and UNC paths independent of the host OS; cross-volume Windows moves use
   absolute paths. Fails without modifying MTF on invalid/ambiguous paths. */
DLL_PUBLIC bool
rebase_parameter_mtf_source_paths (
    mtf_parameters *mtf, const std::string &old_parameter_file,
    const std::string &new_parameter_file, std::string *error);

/* Compose one complete native JSON schema-v2 document from the core and
   Qt profile-spot processing/calibration inputs. Every component is encoded
   directly from typed C++ state, without a legacy CSP mirror or ZIP wrapper.
   OUTPUT is not modified if a component or the aggregate resource budget
   is invalid. This in-memory API performs no file I/O. */
DLL_PUBLIC bool
encode_parameter_json_v2_document (
    const scr_to_img_parameters &geometry,
    const scr_detect_parameters &detection,
    const render_parameters &render,
    const solver_parameters &solver,
    const std::vector<point_t> &profile_spots,
    std::string *output, std::string *error);

/* Parse a complete schema-v2 JSON document with one syntax tree and decode
   all persistent parameter domains into private state. An invalid header,
   missing/unknown required feature, malformed nested array, or failed
   decoder leaves *all* caller outputs unchanged. */
DLL_PUBLIC bool
decode_parameter_json_v2_document (
    const std::string &input,
    scr_to_img_parameters *geometry,
    scr_detect_parameters *detection,
    render_parameters *render,
    solver_parameters *solver,
    std::vector<point_t> *profile_spots,
    std::string *error);

/* True if the first non-whitespace byte of UTF-8 path NAME begins a
   native JSON document. This is signature-based dispatch only; the typed
   schema-v2 reader must still validate the format marker and version. */
DLL_PUBLIC bool parameter_json_signature_p (const char *name);

/* Read a complete plain JSON v2 parameter file from a UTF-8 host pathname.
   Every persistent core/Qt-spot input is parsed as typed JSON; no CSP
   stream or ZIP is involved. All destinations remain unchanged on error. */
DLL_PUBLIC bool
read_parameter_json_v2_file (
    const char *name, scr_to_img_parameters *geometry,
    scr_detect_parameters *detection, render_parameters *render,
    solver_parameters *solver, std::vector<point_t> *profile_spots,
    std::string *error);

/* Atomically save one complete JSON v2 parameter file to UTF-8 path NAME.
   The existing sibling-staging and native replacement primitive preserves
   an older destination if any serialization, write or rename fails.
   All current .cspar save paths use this writer; historical .par files remain readable. */
DLL_PUBLIC bool
write_parameter_json_v2_file (
    const char *name, const scr_to_img_parameters &geometry,
    const scr_detect_parameters &detection,
    const render_parameters &render, const solver_parameters &solver,
    const std::vector<point_t> &profile_spots, std::string *error);

/* Serialize one native JSON v2 registration component.

   This is a lossless building block for the eventual complete schema-v2
   document, NOT a standalone .cspar writer. It reads these structures
   directly, without constructing or parsing legacy CSP text. It includes
   accepted screen geometry (including mesh and final frame), detection dye
   controls, solver policy/points and profile spots. The complete document
   writer composes this with the remaining typed domains. OUTPUT remains
   unchanged on failure. */
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
   separate required native components. The returned
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
   This component is used by the complete native document writer. */
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
   JSON as one component of the complete native document. */
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

/* Serialize all persistent capture sharpening and MTF settings directly
   into JSON, including each measured transfer curve, uncertainty and spatial
   provenance. Do not include derived fit tables or transient fit options.
   This is a component, not a complete schema-v2 .cspar writer. */
DLL_PUBLIC bool
encode_parameter_json_v2_sharpness (
    const render_parameters &render, std::string *output,
    std::string *error);

/* Parse native sharpness and MTF input transactionally, without modifying
   unrelated processing/calibration state if an input is malformed. */
DLL_PUBLIC bool
decode_parameter_json_v2_sharpness (
    const std::string &input, render_parameters *render,
    std::string *error);

/* Experimental typed schema-3 capture-MTF component codec. The JSON root
   contains calibration.capture.mtf, including stable measurement/capture IDs,
   selected record ID, measured quality, sample units and source provenance.
   It is not a complete .cspar document and is NOT used by production
   JSON-v2 saving. Until the fitter supports arbitrary capture grouping,
   discontiguous group IDs are rejected rather than misinterpreted. */
DLL_PUBLIC bool
encode_parameter_json_v3_mtf (const mtf_parameters &mtf,
                              std::string *output, std::string *error);

/* Parse a native schema-3 MTF component into privately validated C++ values.
   On invalid metadata, inconsistent source paths, unknown fields, resource
   limits or broken record IDs, MTF remains unchanged. */
DLL_PUBLIC bool
decode_parameter_json_v3_mtf (const std::string &input,
                              mtf_parameters *mtf, std::string *error);

/* Encode all persistent spatial calibration grids directly into JSON:
   backlight luminosity/subtraction samples, scanner blur correction cells,
   and the full stitched-tile exposure/dark-point/blur grid. Reduction
   diagnostics are derived and excluded. No legacy text or ZIP intermediary. */
DLL_PUBLIC bool
encode_parameter_json_v2_correction_grids (
    const render_parameters &render, std::string *output,
    std::string *error);

/* Parse the complete correction-grid component into newly allocated
   resources, then commit to RENDER only on total success. */
DLL_PUBLIC bool
decode_parameter_json_v2_correction_grids (
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
