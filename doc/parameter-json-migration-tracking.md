# Parameter JSON redesign: decisions, implementation and validation

Status: **schema-3-first JSON format under active implementation**,
Color-Screen **2.0alpha**. Baseline: `31298ba4`.
Decision updated: **15 October 2026**. Only legacy `.par` parameter files exist
outside development. JSON-v1 and JSON-v2 are unreleased development artefacts,
not compatibility contracts. `parameter-json-v2-plan.md` is obsolete history.

## Scope and non-negotiable invariants

The unused ZIP-v1 parameter format is **entirely removed**: no reader, writer,
manifest, UI selection, recovery fallback, or compatibility fixture. The only
supported input formats are historical plain-text `.par` and native JSON.
Opening never rewrites a source file. Failed serialization, validation or
file replacement preserves the original bytes and document target/dirty state.
The ZIP-based `.csprj` stitching container is unrelated and remains supported.

Legacy `.par` export remains available with the existing loss-prevention
checks. Unknown vendor/postamble metadata must never disappear in conversion:
refuse conversion, retain the old file, and explain the unsupported metadata.
The retired `--zip-v1` command-line flag reports an error without writing;
there is no ZIP parameter-file encoding or decoding path.

**Compatibility boundary: .par only.** The shipping JSON contract will be
the semantically organized schema 3. We may replace/remove unreleased JSON-v2
encoders, readers, flags and migration adapters rather than preserve their
output or input. In the current alpha checkout, the GUI/CLI still temporarily
use JSON-v2 until the complete schema-3 reader, writer, Save As, and recovery
paths are wired and tested together. Do not expose partly written schema 3.
Tests must assert that legacy .par imports are lossless in their meaningful
stored inputs and that new schema-3 files can be reopened and rewritten
canonically; **no JSON-v2 or ZIP compatibility tests are required for release**.

Keep all codecs, validation and atomic file I/O in Qt-independent
`libcolorscreen`. Keep output/display profiles, output transfer gamma, gamut
warnings, view tile visibility, caches, preferences, worker flags, fit scratch
and derived plots out of parameter state. Keep genuine calibration, accepted
meshes, final-frame geometry, appearance tone curves and measurement provenance.

## Work and evidence ledger

`Planned` means no implementation claim. `Implemented` is not a test pass.
Every test report must identify its exact revision and distinguish local
validation from CI. No beta/final release is authorized by this work.

| ID | Requirement | Status | Implementation / acceptance evidence |
| --- | --- | --- | --- |
| JSON-001 | Remove ZIP-v1 completely from parameter persistence | Merged PR #549; platform CI passed | Delete archive reader, manifest structures, parser/writer, GUI format state, sidecar/recovery handling and ZIP test fixtures. Retired --zip-v1 errors without writing. `.csprj` stitching remains separate. |
| JSON-002 | Delete obsolete ZIP migration paths | Merged PR #549; platform CI passed | Only plain `.par` and JSON can be opened. Tests cover .par-to-JSON conversion, failed Save nonmutation and successful Save without ZIP format identity. |
| JSON-003 | Validate implicitly overwritten destinations, not just signatures | Implemented, locally tested | Shared full validator rejects `{}`, malformed/future/incomplete/unknown-field/type-invalid JSON and unsupported binary content. Core/CLI/Qt tests preserve bytes and target/Undo; explicit replacement still works. |
| JSON-004 | Writer/reader aggregate resource-budget agreement | Implemented, locally tested | Every component and full writer preflights the shared node/depth budget before publishing. Boundary/escaped-string/combined-collection tests plus an independent 10,000-tree value-count probe pass. Large real-project memory testing is still required. |
| JSON-005 | Versioned semantic organization and naming | Reversible reference prototype; C++ codec planned | Complete v2-to-v3 field map below, native codec and single-current-writer dispatch; no persistent field left unmapped. |
| JSON-006 | Explicit sentinels, units and coordinate conventions | Reference prototype tested; native codec planned | Tagged input transfer; null absent bounds; inactive settings retained; audit pixel/screen/physical coordinate domains. |
| JSON-007 | Stable MTF record identity and explicit capture groups | Reference prototype plus Qt-free in-memory identity/reorder foundations; persistence still planned | Core generates collision-free document-local IDs without replacing existing ones, maps selection to an ID, and refuses legacy reorder that splits a capture. Native schema-3 codec and GUI edit/Save/Save As preservation remain necessary. |
| JSON-008 | Named heterogeneous measurement metadata | Native isolated C++ MTF codec implemented, under review; full document integration planned | Codec directly writes/reads named edge quality, sample units, source/ROI status, path bases, selected record ID and group IDs without a v2 or Python intermediate. Unknown nested fields reject; full schema-3 document switch remains separate. |
| JSON-009 | Portable reference provenance | Reference audit and tests; native implementation planned | V3 prototype models absent sources with null, validates absolute versus parameter-relative path bases, rejects contradictory availability flags, and tests an in-memory POSIX/Windows Save As path rebase. Native C++ Save As and GUI lifecycle remain unimplemented. |
| JSON-010 | Shared field descriptions and clear subsystem boundaries | Planned | Split legacy import, JSON codecs and atomic I/O; avoid three independent field vocabularies. |
| JSON-011 | Authoritative schema and complete examples | Planned | Checked-in JSON Schema, valid default and rich fixtures; schema/core agreement and unknown-key tests. |
| JSON-012 | Nested member coverage and field-rich diagnostics | Planned | Nondefault and inactive settings; full object/array paths in failures; don't equate legacy corpus coverage with full C++ coverage. |
| JSON-013 | Historical corpus and typed-state equivalence | v2/legacy tested; v3 reference 128/128 tested; native v3 planned | All 128 `.par` fixtures round-trip byte-exactly with canonical JSON rewrites on the previous tested revision. Current no-ZIP change must pass the same corpus and typed-state checks; no schema-3 equivalence claim yet. |
| JSON-014 | Platform, sanitizer and real-project validation | No-ZIP PR #549 full CI passed; v3 reference CI pending | GCC O2 checking core/CLI/Qt build; focused core, CLI, full document-lifecycle and workspace-churn smoke pass. Request [ci:all] on the review PR. Field testing remains a beta gate. |

## Target semantic organization (schema 3)

| Section | Ownership |
| --- | --- |
| `capture` | Interpretation, RAW method, input transfer, orientation, object crop, inner photographic bounds and input gain/black offset. |
| `process` | Physical screen type/strip fractions, historical colour model, dye aging/density and contact-copy response. |
| `geometry` | Accepted mapping, imaging geometry, lens polynomial, mesh and final coordinate frame. |
| `registration` | Detection colours/thresholds, fit policy and control points. |
| `calibration` | Capture characteristics and MTF evidence/model, scanner/process colour response and spots, flat-field/blur/tile correction grids. |
| `reconstruction` | Image-layer mixing, colour collection, screen demosaicing/resampling, pre/post denoise and sharpening choices. |
| `color` | Photographic/backlight illuminants, observer adaptation, balance and appearance adjustments. |

This is the file model, not a requirement to refactor all C++ structures at
once. A codec may map current structures into these domains without moving
renderer ownership or changing numerical algorithms.

## Field mapping and semantic hazards

| Old v2 location | Intended v3 location / representation | Preservation rule |
| --- | --- | --- |
| `capture.capture_type` | `capture.type` | Stable identifier, not UI text or ordinal. |
| `capture.demosaic` | `capture.raw.demosaic` | Capture RAW interpolation; distinct from screen reconstruction. |
| `capture.gamma` | `capture.linearization` mode + power exponent | sRGB and source-profile modes explicit; don't confuse with output gamma. |
| `capture.scan_rotation_quarter_turns`, `scan_mirror` | `capture.orientation` | Document exact order/direction; no change of pixel semantics. |
| `capture.scan_crop`, `image_area` | `capture.bounds.object_crop`, `photograph` | null means absent; named rectangle fields; photograph remains distinct from taped/mounted object. |
| `capture.scan_exposure`, `dark_point` | `capture.levels.gain`, `black_offset` | Operation is `(value - black_offset) * gain`; gain is NOT stops. |
| `capture.backlight_correction_black` + backlight grid | `calibration.flat_field` | Retain scalar, grid and disabled-channel data. |
| `geometry.screen_type`, `process.strip_widths` | `process.screen` | 0-width automatic defaults need explicit representation; audit crossed vs parallel strips. |
| `geometry.scanner_type` | `geometry.imaging_model` | Historical scanner model, not simply device make. |
| `geometry.center`, `axis_x`, `axis_y` | `geometry.mapping` | Specify source pixel and screen-coordinate units. |
| `geometry.final_*` | `geometry.final_frame` | Degrees/axis ratio with exact convention; these ARE persistent. |
| `geometry.lens`, `mesh` | `geometry.lens`, `geometry.mesh` | Explicit polynomial order/normalization, mesh direction and row-major order. |
| `detection` | `registration.detection` | Capture RGB colour references, not display RGB. |
| `registration` fit flags and points | `registration.fit`, `registration.points` | Accepted points and solver policy persistent, scratch not persisted. |
| `process.age`, `dye_density` | `process.dyes.aging_fraction`, `density` | Preserve per-dye values and inactive settings. |
| `process.color_model` | `process.color_model` (semantic audit) | Includes physical models AND pass-through/single-channel modes; don't falsely call all values dye spectra. |
| `process.contact_copy` | `process.contact_copy` | Preserve full inactive response curve and its coordinate meaning. |
| `reconstruction.mix_weights`, `mix_dark`, `ignore_infrared` | `reconstruction.image_layer` | Separate source policy and RGB mixer; native channel vs simulated scalar layer. |
| `reconstruction.demosaiced_scaling` | `reconstruction.color_field.resampling` | Stable algorithm names. |
| `reconstruction.screen_denoise`, `demosaiced_denoise` | named pre-/post-screen reconstruction stages | Keep all inactive method parameters; audit radius units. |
| `sharpness` algorithm controls | `reconstruction.sharpening` | Group Wiener/RL/unsharp options while retaining inactive values. |
| `sharpness.mtf` optical/native sensor values | `calibration.capture` | Pixel pitch in um, defocus in mm, wavelengths in nm; physical characterization is not an algorithm choice. |
| `sharpness.mtf.measurements` | `calibration.capture.mtf.measurements` | Stable IDs; channel and image-layer domain remain independent; capture group no longer inferred from adjacency. |
| `sharpness.mtf.selected_measurement` | ID reference | Never refer by editable label or array position. |
| MTF `edge_quality` tuple | named quality object | angle degrees, fit RMS pixels, contrast normalized, SNR, coverage fraction. |
| MTF `samples` | compact triples | frequency cycles/pixel, contrast percent, uncertainty percentage points. |
| MTF filename/dimensions/ROI/edge | measurement `source` object | Portable path with explicit base; preserve spatial provenance and unavailable metadata. |
| `color.scanner_primaries` | `calibration.capture.color_response` | Values are XYZ; zero-matrix source fallback becomes explicit. |
| `color.process_profile`, `profile.spots` | `calibration.process_color` | Calibration matrix is scanner RGB responses to screen filters; preserve spots, not transient matching results. |
| `color.temperature`, `backlight_temperature` | `color.illuminants.*_temperature_k` | First is photograph daylight; not observer temperature. |
| `color.observer_whitepoint` | `color.viewing.whitepoint_xy` | Observer adaptation distinct from illuminants. |
| `color` balance, saturation, brightness, curve | `color.adjustments` | Appearance settings are persistent, output monitor profile/transfer is not. |
| `correction_grids.scanner_blur`, `tiles` | `calibration.spatial_blur`, `tiles` | Retain dimensions/row order and physical blur units; no transient diagnostics or view visibility. |

## Canonical format and rollout sequence

1. The only historical parameter input is plain `.par`. Continue the
   existing checked loader, supported export path and unknown-metadata
   loss-prevention checks. Retired ZIP parameter files need no reader.
2. Finish the native schema-3 encoder/decoder for every persistent domain
   using semantic field names, explicit units, bounded arrays and
   transactional publication. Neither JSON-v1 nor JSON-v2 is a reader target.
3. Convert `.par` imports **once at load time** into complete native state:
   stable MTF record/capture IDs, normalized obsolete sentinels and
   document-relative provenance. Never regenerate existing IDs on Save.
4. Change GUI Open/Save/Save As/sidecars/recovery and the CLI together to
   use only canonical schema-3 `.cspar`, with atomic replacement and
   clear rejection of unsupported input. Delete the unused v2 runtime
   serializer, compatibility flags, older JSON-specific tests and adapter
   code after switch.
5. Check all 128 existing `.par` fixtures, synthetic full-state cases
   (including inactive fields, correction grids, MTF metadata and Unicode
   paths), schema-3 canonical rewrites, malformed-input nonmutation,
   failure-atomic Save As and real rendering parity.
6. Obtain full platform/sanitizer CI and operator testing on large real
   projects before beta.

## Validation log

- Baseline inspected: main `31298ba40bec0054809b2ecc7c11bc8c2c393bf8`.
- First-stage changes remain a separate review commit from the schema-3 design.
  The review commit contains only product changes, tests and documentation;
  dependency/source transport workflows are excluded.
- Debian 13 amd64, GCC, `-O2 -g -Wall`, OpenMP and checking enabled. Full static
  core, CLI and Qt build in `build-qt` passed with a private dependency prefix.
- Focused core: **11/11 groups pass** (`parameter_archive`,
  `parameter_json_budget`, eight `json_v2_*` component/full-document groups,
  and `legacy_csp_representability`).
- First-stage CLI integration: **17/17 TAP cases passed** before deleting ZIP import,
  including rendering parity, retired-output refusal, malformed/future/unknown-key
  destinations, explicit replacement, Unicode paths and unknown postamble guards.
- Corpus: **128/128** normalized `.par` -> JSON -> `.par` byte comparisons and
  canonical JSON rewrites pass. No original fixture is rewritten.
- First-stage Qt offscreen **document-lifecycle** and **workspace-churn** probes
  passed; the new no-ZIP edition replaces the ZIP migration probes with
  legacy `.par` and JSON lifecycle, failed Save nonmutation and recovery checks.
- The standalone resource-count probe also agrees with an independent Python
  JSON-tree counter for **10,000** deterministically generated documents,
  accepting their exact count and rejecting a budget one below it.
- `make dist` succeeds. Packaging is checked for this tracker, the internal
  budget header. ZIP parameter fixtures are removed rather than regenerated.
- These are local correctness checks, not a claim of a completed sanitizer,
  macOS, Windows or full `make distcheck` run on the review commit.
- No claim that the semantic schema-3 redesign is complete merely because
  version-2 corpus tests pass.

## 13 October 2026: no-ZIP continuation

- User confirmed ZIP-v1 was never used: **no compatibility reader is needed**.
  Previous decisions and first-stage tests involving ZIP are historical, not
  part of the supported input contract.
- Library removes all ZIP parameter manifest structures, archive dispatch and
  archive parsing. GUI saves/loads track only legacy `.par` versus JSON-v2;
  sidecars and recovery obey the same rule. Existing stitch-project ZIP is
  unrelated and must remain functional.
- Replace fixed ZIP-reader tests with independent `.par` import, JSON format,
  corrupt/unsupported binary, metadata preservation and atomic-write tests.
- This follow-up is separate from the upcoming schema-3 renaming and the full
  nested persistent-member audit. Release remains 2.0alpha.
- No-ZIP removal was published in commit `c8a83afaa0feaf6016f7ed50c859c0af316550f1`
  on draft PR #549. Its initial workflow runs reported `action_required`
  (not test passes); refresh CI with an ordinary contributor-authored push.
  Track outcomes against the new exact head revision.
- The previous PR-head platform/sanitizer results do not validate the no-ZIP
  commit. Do not label schema 3 complete or promote to beta until its typed
  codecs, field audit and CI are ready.

## 13 October 2026: schema-3 reference validation (PR #550)

- The no-ZIP cleanup was merged to main in PR #549 (merge commit
  `9cadfbc9757f30c527f8743007919d18251570d0`). Ubuntu, macOS,
  Windows GCC/Clang and the sanitizer matrix passed on the exact PR head;
  the Windows AddressSanitizer multi-window smoke required a successful rerun.
- A separate **nonshipping** Python reference translator and design note are
  introduced in PR #550. They map all existing schema-2 sections to a
  semantically grouped schema-3 proposal and back; persistent legacy state is
  never interpreted as display/runtime configuration.
- Local Debian 13, GCC 14, checking-enabled C++ CLI: all **128/128** checked-in
  legacy .par fixtures were serialized with the actual native-v2 writer, then
  round-tripped v2 -> prototype v3 -> v2 with exact parsed-JSON equality.
  The new TAP wrapper passed (1/1), and `make dist` packaged the reference
  mapping, tests and documentation.
- Rich regression checks cover optional bounds, explicit input gamma/strip
  sentinels, final-frame geometry, inactive sharpen settings, Unicode MTF
  provenance, selected measurement IDs, capture-group reordering and
  deterministic rejection of unknown keys, duplicate/dangling IDs or
  nonrepresentable discontiguous groups.
- Prototype-generated MTF record IDs must **not** be confused with lifetime-
  stable native identities. The final C++/Qt model, Save As path rebasing,
  schemas/examples and native-v3 writer/readers are still pending. All
  production .cspar output remains native JSON schema version **2**.

## 14 October 2026: schema-v3 reference integrity follow-up

- Draft PR #550 provides the nonshipping reversible v2/v3 reference translator and
  128-fixture parity tests; active `.cspar` production saving remains v2.
- Stacked draft PR #551 adds absent-source semantics, cross-platform path-base
  validation and availability/crop/migration-flag integrity tests. Local
  checking-enabled core/CLI runs pass 128/128 legacy fixture mappings and the
  new malformed-data cases, including POSIX/Windows Save As rebasing in memory.
  This is reference-level evidence, not a native C++ schema-3 codec or a
  Windows/macOS sanitizer result.
- Before promoting schema 3, implement persistent record/group IDs in the
  Qt-free core and preserve them through edits and Save/Save As, including
  source path rebasing and migration-failure atomicity. Never infer stable
  identity from each record's current array index during a save.

- PR #550 merged to `main` at `f75a906e70c4a46fddc7153819e474a5f94a4f66` after Ubuntu, macOS, Windows GCC/Clang and sanitizer CI passed. The reference-only follow-up PR #551 was retargeted to `main` and requests full CI. Schema v2 remains the sole native JSON reader and shipping writer.

## 15 October 2026: native MTF identity foundation

- Qt-free `mtf_measurement` now has optional document-local `id` and
  `capture_group_id`. Both are empty in existing .par/JSON-v2 records, and
  the production readers/writers remain schema 2. Explicit migration
  `assign_missing_measurement_ids()` assigns collision-free
  `measurement-000001` / `capture-000001` tokens matching the reference
  import order, retaining any already-assigned IDs.
- Selection by ID is an independent library operation. A core reorder keeps
  selected-curve identity and refuses moves that would split a capture into
  nonadjacent pieces not representable by legacy `same_capture` flags.
  Sharpness's drag control now uses this safe operation and explains rejected
  moves instead of silently changing fitted focus-group membership.
- `mtf_record_identity` tests legacy grouping, idempotent migration, insertion,
  collision avoidance, selection, reordering, invalid IDs, and rejection without
  mutation. These are core API tests, not claims of complete v3 persistence.
- The MTF fitter and production GUI editors still use legacy
  `same_capture`/vector-index semantics until the format switch. The JSON-v2
  encoder and legacy .par stream writer explicitly refuse any record with
  upgraded IDs; neither can silently discard them. **Do not call the explicit
  migration in the live JSON-v2 save path**: schema 2 cannot persist these
  identities. Next: native v3 codec plus transaction-safe
  loading/saving, selection/GUI editing by IDs, and source path rebasing.

## 15 October 2026: native schema-3 MTF codec (stacked review)

- Implement explicit \`encode_parameter_json_v3_mtf()\` and
  \`decode_parameter_json_v3_mtf()\` in Qt-independent C++, operating directly
  on typed \`mtf_parameters\` with no JSON-v2 mirror and no Python runtime.
  The component-shaped root is \`calibration.capture.mtf\`. This is
  **not** a complete .cspar schema-3 writer, reader, or GUI migration.
- The codec saves named optical/model values, measured samples and declared
  units, stable record/capture IDs, selection by ID, channel/image-layer
  provenance, source paths (absolute versus parameter-relative, or null),
  available rectangle/dimension values and exact unavailable sentinels.
- Reader rejects duplicate/dangling/empty IDs, mismatched base/filename,
  malformed source availability, unknown nested fields, missing scalars,
  unexpected compatibility-only first-capture flags, invalid sample units
  and excessive resource use before mutating state.
- Native MTF fitting, per-channel specialization and capture-wide uncertainty
  weighting now use stable group identity, including nonadjacent records.
  The canonical schema-3 MTF codec does not persist or enforce legacy
  adjacency flags. GUI editor identity lifecycle and full document integration
  remain unfinished.
- New focused \`unittests json_v3_mtf\` verifies typed-state and canonical
  roundtrips, empty measured curves, path/sentinel cases and nonmutation
  on malicious or inconsistent metadata. The full-platform CI for this
  stacked review is tracked separately from PR #552 and prior merged work.

## 15 October 2026: explicit no-JSON-compatibility decision

- The operator confirmed there are **no real JSON parameter files** and no
  deployed ZIP-parameter files; the only existing parameter files are `.par`.
  Unreleased JSON versions are not migration input formats.
- Do not carry the Python reference translator's inverse v3→v2 adapter,
  `legacy_first_same_capture`, ZIP-v1 compatibility branches or JSON-v2
  reader/write-format preservation into 2.0. Retire their tests once native
  schema-3 corpus and field-rich tests replace them.
- Keep `.par` reading and supported legacy export, preserving meaningful
  historical fields and refusing lossy export when new values lack a `.par`
  representation. This is a different compatibility obligation from JSON.
- The current internal schema-3 proposal uses `schema_version: 3`. The
  final marker can be fixed before beta because no public JSON files exist.
- Native schema-3 MTF now supports nonadjacent capture-group IDs in the
  Qt-free numerical solver and per-channel sharpen selection. Remaining
  work is GUI/editor identity lifecycle, complete parameter codec,
  source-path rebasing, and replacement of temporary v2 GUI/CLI/recovery paths.
