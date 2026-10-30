# Parameter JSON redesign: decisions, implementation and validation

Current status (30 October 2026): **schema-3 JSON `.cspar` is implemented
on main**, first integrated through merged PR #556. The separate stitching
project format uses native **schema-2 JSON `.csprj`** after merged PR #561,
with the historical version-1 keyword-text reader retained. The version-2
GUI manual and file-format wiki pages were published through PR #562
and GitHub Actions. Color-Screen is still **2.0alpha**, not beta/final:
operator acceptance, real large-scan/stitching tests and final installer
packaging remain. Historical entries and their pending CI notes below
record the *then-current* validation state; they must not be interpreted as
the latest repository/CI status.

Original design baseline: `31298ba4`. Decision updated
15 October 2026: only legacy `.par` parameter files existed outside
development. JSON-v1 and JSON-v2 were unreleased development artefacts,
not compatibility contracts. `parameter-json-v2-plan.md` is obsolete history.

## 16 October 2026: linked-source regression and operator Open integration

- CI on `8d31e0c` compiled the source-linked document implementation on
  Ubuntu/macOS and passed Ubuntu `distcheck`, but the macOS ASan+UBSan
  DocumentLifecycleSmoke reported "Linked-source size mismatch did not warn".
  The test wrote a valid linked .cspar into another folder without rebasing
  its parameter-relative scan path; the correct production behavior was
  therefore to present Locate image instead of the mismatch warning.
  Rebase the test fixture with the Qt-independent helper, and assert that
  its absolute image identity is unchanged before testing stale prompts.
- Qt lifecycle smoke now additionally exercises *successful* Locate image
  from a missing source reference, user selection of the existing scan,
  modified/dirty state, explicit Save, and recovery of the rewritten
  portable source link. It also checks that ordinary Ctrl+O exposes
  `*.cspar` in the image/document chooser, not only through desktop
  file associations and All Files.
- Normal File → Open was previously labeled "Open Image(s)" and filtered
  out `.cspar` despite desktop/positional support. It now advertises
  images and documents, with a separate `.cspar` filter and equivalent
  fallback routing when no application-level manager is available.
- **Current branch requires new exact-head CI** after these fixes.
  Installer PR #554 still has an independent Windows NSIS runtime stall:
  the native package builds, but unattended smoke consumed one core and
  extracted no files within 150 seconds. Keep that separate from core
  schema-3 validation; do not declare either release-qualified yet.

## Scope and non-negotiable invariants

The unused ZIP-v1 parameter format is **entirely removed**: no reader, writer,
manifest, UI selection, recovery fallback, or compatibility fixture. The only
supported input formats are historical plain-text `.par` and native JSON.
Opening never rewrites a source file. Failed serialization, validation or
file replacement preserves the original bytes and document target/dirty state.
Stitching `.csprj` files are a separate format: the historical reader handles
version-1 keyword text, and the new writer emits project JSON schema 2.

Legacy `.par` export remains available with the existing loss-prevention
checks. Unknown vendor/postamble metadata must never disappear in conversion:
refuse conversion, retain the old file, and explain the unsupported metadata.
The retired `--zip-v1` command-line flag reports an error without writing;
there is no ZIP parameter-file encoding or decoding path.

**Compatibility boundary: .par only.** The shipping JSON contract will be
the semantically organized schema 3. We may replace/remove unreleased JSON-v2
encoders, readers, flags and migration adapters rather than preserve their
output or input. The standalone codec in PR #555 and the application
cutover in draft PR #556 now implement direct schema-3 read/write, Save As
provenance handling and recovery. These changes are **on review branches,
not yet merged or release-qualified**. The previous `main` build still uses
temporary v2. Do not release until full tests establish that schema 3 is
complete and failure-atomic.
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
| JSON-005 | Versioned semantic organization and naming | Full native codec and Qt/CLI runtime integration under CI (draft #555/#556) | Seven named semantic domains, shared bounded parser/typed-state checks; no external JSON-v2 compatibility accepted. Audit nested members before beta. |
| JSON-006 | Explicit sentinels, units and coordinate conventions | Native semantic codec implemented on review branch, not fully qualified | Input sRGB/power transfer, nullable named crop bounds, automatic/fraction strip widths, measured-MTF source status and sample units. Full field/units review remains. |
| JSON-007 | Stable MTF record identity and explicit capture groups | Core/GUI/runtime implemented in draft #553/#556; final CI pending | Imported `.par` records receive document-local identities once, selection and edits track IDs, numerical fitting and native-channel choice use explicit groups (including nonadjacent records), and schema-3 persistence retains them. |
| JSON-008 | Named heterogeneous measurement metadata | Integrated native schema-3 subtree on draft #555/#556; final CI pending | Full document codec embeds the Qt-independent measured-MTF writer/reader, naming quality/sample units, ROI availability and source path bases. Unknown/dangling IDs and nested fields reject without publishing document state. |
| JSON-009 | Portable reference provenance | Core rebase + GUI/CLI Save As and recovery snapshot implementation in draft #555/#556; CI pending | Lexical POSIX/Windows/UNC paths, cross-volume absolute fallback, validation before mutation, document-relative overlay lookup and absolute references in private recovery. |
| JSON-010 | Shared field descriptions and clear subsystem boundaries | Planned | Split legacy import, JSON codecs and atomic I/O; avoid three independent field vocabularies. |
| JSON-011 | Authoritative schema and complete examples | Draft-2020-12 schema and independent corpus conformance checker implemented in #556; current-head CI pending | `doc/parameters-schema-v3.json` defines the complete seven-domain vocabulary, MTF evidence and optional/sentinel forms. `parameter_schema_validate.py` checks real CLI output for required/unknown fields, types and tuples without optional Python packages. Static canonical rich fixtures and full schema/codec agreement remain beta gates. |
| JSON-012 | Nested member coverage and field-rich diagnostics | Rich native v3 typed-state regression extended, under CI | Native `json_v3_document` now exercises nondefault meshes, final frame, capture/process controls, inactive algorithms, MTF provenance, backlight/scanner-blur grids and tile calibration with complete render-state equality. Review remaining fields and diagnostic paths before beta. |
| JSON-013 | Historical corpus and typed-state equivalence | Full #556 pre-schema CI passed; expanded schema/corpus tests pending | Native JSON-v3 CLI/Qt and the .par corpus passed on #556 `7f637381`. New independent schema validation for every corpus fixture and richer structural comparison need exact current-head CI; no JSON-v2 compatibility is required. |
| JSON-014 | Platform, sanitizer and real-project validation | Complete #556 six-workflow run passed on 7f637381; current revision pending | Ubuntu amd64/arm64 and distcheck, macOS, Windows GCC/Clang and sanitizers passed on `7f637381`. The additional schema contract, retired v2 file API and field-rich tests are under revalidation. Real 150+ MP memory-pressure/operator qualification is still outstanding; beta is not authorized. |

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

## 15 October 2026: canonical document cutover under review

- Draft PR #555 adds complete in-memory/file schema-3 encoder and strict
  failure-atomic decoder. The implementation temporarily reuses the stable
  internal C++ component validators for non-MTF fields; this is **not** a
  JSON-v2 file reader. A later cleanup can remove those private helpers.
- Draft PR #556 routes GUI/CLI/sidecars/recovery to the schema-3 APIs,
  updates document-format identities and explicit `--json-v3` conversion,
  and rejects unreleased old JSON on load. Historical `.par` import assigns
  MTF IDs once and supported legacy export checks physical grouping.
- Native MTF source-path relocation is implemented in the core and used
  on GUI Save As, CLI `adjust-par` and private recovery (absolute snapshot
  references). Relative source display comparisons resolve against the
  accepted parameter path.
- The old Python JSON-v2↔v3 translation test has been removed from the
  release test matrix. New .par→v3→.par corpus, canonical v3 rewrite, hostile
  input and Qt lifecycle tests are registered. This is **not yet a pass**.
- First CI failures caught: #553 macOS unused-group variable (fixed),
  #555 macOS namespace mismatch (fixed), #556 Ubuntu core test referring
  to the retired signature function (fixed). The new .test wrapper needed
  executable mode 100755 (fixed). Platform/sanitizer and corpus results on
  the final head remain pending.
- Remaining before beta: pass all platform/distcheck/Qt tests, validate
  128 legacy fixtures and field-rich real projects, audit full nested state
  and units, publish authoritative JSON Schema/fixtures, then remove unused
  private v2 codecs once their typed implementations are replaced.

## 15 October 2026: schema-3 runtime integration CI and Undo audit

- Draft PR #556 now uses strict schema-3 JSON on GUI/CLI/sidecars/recovery
  rather than preserving unreleased JSON-v2. The former Python v2↔v3 reference
  migration harness is removed from the release suite; only historical `.par`
  corpus conversion and direct canonical JSON tests are relevant.
- The first complete CI run compiled the full code across its tested platforms
  and passed **all 73 core unit-test groups** on normal Ubuntu and macOS.
  The test stage and distcheck failed because the checked-in generated
  `testsuite/Makefile.in` still referenced deleted
  `parameter-v2-corpus.test`; both `.am` and `.in` now reference the
  schema-3 corpus and no obsolete reference test. This is an infrastructure
  repair, not a claim that the new corpus passed.
- Windows installer logs show `makensis` uses the **amd64-unicode**
  NSIS target. An earlier patch registered `nsDialogs` for `x86-unicode`,
  which sporadically failed despite the DLL being installed. PR #554 and
  the integration branch now register the plugin for `amd64-unicode`;
  final target-architecture CI is still required.
- Save As was also audited beyond the file writer: source filenames in
  `ChangeParametersCommand` snapshots are staged for rebasing, and both
  Undo and Redo are relocated after the atomic save succeeds. On any failed
  Save, live parameters and Undo commands remain untouched; if an obsolete
  Undo command cannot be relocated, successful Save As discards the unsafe
  history with a visible explanation instead of allowing stale relative
  source paths to be replayed. Qt document-lifecycle smoke now exercises
  Save As across sibling directories and subsequent Undo/Redo.
- The complete nested grammar (including an object-valued
  `capture.linearization`) is checked before dereferencing fields.
  Malformed scalars and missing transfer modes now receive an error without
  partially changing a live document.
- **Still unresolved:** final CI on the updated PR #556 head, full .par
  fixture conversion and rich real-image testing, authoritative complete
  JSON Schema and examples, structured prettier output, and removal of
  obsolete private v2 component codecs when their native implementations are
  no longer shared by the v3 encoder.

## 15 October 2026: final beta-preparation audit (review branch)

- On PR #556 revision `edcc67f`, **Ubuntu amd64/arm64 builds and checking,
  `distcheck`, macOS build, and all three dedicated sanitizer jobs passed**.
  Windows GCC/Clang built the application but failed in `makensis` at
  `installer.nsi:71`: `Plugin not found, cannot call System::Call`. Thus
  that revision was *not* an exact-head full-platform pass. The same issue
  affected dedicated installer PR #554. The optional Shell32 notification
  dependency was removed from **both** review branches; the new installer
  heads require their own Windows CI.
- An additional Qt source-path audit found a first-save gap. A newly opened
  image with only a *suggested* parameter path resolves relative measured-MTF
  sources from the image directory. First Save As to another directory must
  rebase them, just like later Save As, including both Undo snapshots. The
  GUI implementation now uses the same initial base as the source resolver
  and private recovery. Offscreen lifecycle smoke checks the absolute source
  identity, Undo/Redo and a complete reload of that first saved document.
- Do **not** call this beta yet: run the complete matrix at the new exact head
  (especially native installer generation and GUI smoke); qualify a real
  150+ megapixel document under constrained and normal cache budgets,
  including demosaic changes and scratch/mmap use; and exercise the manual
  end-to-end regular-screen, line-screen, monochrome/IR, MTF reference,
  cancellation, recovery, and installed/portable launch scenarios from
  `.github/RELEASE-2.0.md`. The integrated PRs #552/#553/#555/#556 are
  stacked; use the full #556 head for release qualification, and retire
  predecessor reviews only once that final integration is accepted.
- Schema-3 is the first supported `.cspar` format. Keep the sole `.par`
  compatibility boundary; do not add v1/v2 JSON or unused ZIP migration code
  back to unblock beta. Keep the final-release-only PR #510 draft.

## 16 October 2026: standalone source-linked .cspar documents (draft #556)

- The optional top-level `source_image` root member records a source scan or
  stitched project path, a lexical base, decoded dimensions and original file
  byte count. `null`/omission means reusable settings. No binary scan is
  embedded and `render_parameters` remains image-source-free.
- Native C++ schema-3 reader/writer and the JSON Schema validate this
  metadata transactionally. Qt saves a relative link on first Save/Save As,
  and CLI `adjust-par` retains/rebases links using the established
  host-independent path logic.
- Opening a .cspar from Explorer/Finder/positional arguments now routes
  through a prevalidated linked-document open. An original or unique
  nearby scan is loaded asynchronously, otherwise a Locate image file chooser
  is shown. File-size changes require acknowledgement and decoded dimension
  mismatches do not publish calibration. Moved-file linkage is marked dirty.
- Windows per-user .cspar association, macOS exported UTI and Linux
  MIME/desktop declarations are staged. Added native core canonical/invalid
  source tests and Qt lifecycle smoke for Save As and direct .cspar open.
- **Validation pending:** exact-head Ubuntu/macOS/Windows GCC+Clang,
  sanitizer, distcheck and manual GUI smoke on the installers. This does
  not authorize beta release or merge; do not treat earlier CI passes
  from before the source-link additions as evidence for these revisions.

