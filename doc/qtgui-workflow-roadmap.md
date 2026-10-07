# Qt GUI workflow and RAW-editor convergence roadmap

## Audience and design target

The primary user is not a casual photo consumer.  A likely operator is a museum,
archive or digitization-lab employee who understands scanners, camera capture,
colour management and image restoration, but may not know the implementation or
the historical quirks of every additive-colour process supported by
Color-Screen.

The useful comparison is therefore Lightroom, darktable, Capture One or another
non-destructive RAW editor **plus a calibration laboratory**.  Color-Screen has
legitimate controls those applications do not need: physical screen type,
registration geometry, dye models, contact-copy response, image-layer/infrared
construction and process-specific demosaicing.  The goal is not to hide those
controls.  It is to make their order, scope and state behave in familiar ways.

## The key mental model

A conventional RAW editor mostly answers:

> How should this digital capture be rendered?

Color-Screen must answer three questions in sequence:

1. **What did the digitizer do to the physical object?**
2. **What historical colour process is present, and how is its screen aligned?**
3. **How should the reconstructed colours be rendered and exported?**

The GUI should make those boundaries visible.  Many confusing choices come from
mixing parameters belonging to different questions in one undifferentiated set
of tabs.

A useful vocabulary for the UI is:

- **Capture** — properties/errors introduced by scanner or camera.
- **Process** — properties of the historical photographic material.
- **Registration/Reconstruction** — how the historical screen is located and
  turned into image data.
- **Appearance** — colour/tone decisions after reconstruction.
- **Output** — final size/profile/file rendering.

The same vocabulary should be used in tooltips, documentation and future preset
names.

## Recommended processing order

The existing panels are already surprisingly close to a useful pipeline.  A
remaining alpha work does not need a wholesale reorder.  The first goal should be to make the
stages explicit and remove the few semantic surprises.

### Stage 1 — Digital capture

Current panels/features:

- **Digital capture**
- **Tiles** for stitched captures
- capture part of **Sharpness**

Typical operations:

1. choose/reload demosaicing for a camera RAW where applicable;
2. establish resolution/pixel pitch/f-stop/wavelength metadata;
3. correct crop/orientation and flat-field/capture nonuniformity;
4. normalize stitched tiles;
5. characterize scanner/camera MTF and restore capture sharpness.

This is analogous to the input/lens/detail part of a RAW editor.  It should be
possible to explain these controls without mentioning Autochrome, Dufaycolor or
Joly: they correct the *digitization*.

**Recommendation:** visually label this group **Capture**.  Keep capture MTF and
deconvolution here conceptually even if the implementation remains the existing
Sharpness panel.  Avoid calling capture deconvolution "final sharpening"; that
would be misleading to photographers.

### Stage 2 — Source/image layer

Current **Image Layer** selects or synthesizes the scalar layer used by several
analysis/reconstruction paths, including RGB mixtures and IR/grayscale data.
This is not the same thing as a normal RAW-editor luminance mixer.

**Recommendation:** rename the user-facing concept eventually to something like
**Analysis image / image layer**, with a one-line status summary:

- `Native IR channel (750 nm)`
- `Native monochrome capture`
- `Simulated from RGB: 0.00 R + 1.00 G + 0.00 B`

The current detailed mix controls can stay in an Advanced section.  The common
case should present the chosen source and the automatic actions first.

### Stage 3 — Historical material/process

Current panels/features:

- **Contact copy** when applicable;
- screen type selector at the top of **Screen**;
- strip-width/process-specific screen settings.

These describe the object, not a subjective colour grade.

**Recommendation:** make **Process** an early, persistent summary even if its
detailed controls remain later in the inspector.  An operator should always be
able to see `Dufaycolor`, `Autochrome`, `Finlay`, `Paget`, `Joly`, etc. without
opening several groups.  Process selection determines which later tools are
applicable and is therefore closer to choosing a camera/lens profile than to a
creative adjustment.

Monochrome transparency/negative captures made through a separate additive
colour screen remain historical-screen workflows even though the screen colours
are absent from the captured data.  This includes an infrared capture that
effectively suppresses the screen colours of an otherwise screened
transparency.  In these cases the operator must choose the original **regular**
screen type and fit its geometry so the screen can be re-attached during
reconstruction.  Stochastic screens (Random, Autochrome, Agfa Farbenplatte)
cannot be reconstructed from monochrome data because the colour identity of
their individual screen elements has been lost.

The initial **Suggested image setup** guide now makes that distinction
actionable. Selecting one of these monochrome screened capture types exposes a
regular **Original color screen** selector and an **Automatically detect the
screen** checkbox which remains disabled until the screen type is chosen. It
reuses the Screen panel's thumbnail/pattern selector rather than maintaining a
second text-only list. When the selected process has a preferred dye model, the
guide offers that model as a checked suggestion. Conversely, when the historical
screen itself is visible in RGB, common screen types can be autodetected from
their colour pattern without forcing a prior screen-type selection or showing a
redundant screen chooser.

After successful regular-screen geometry discovery, the same guided path now
offers **Resolution from screen** as an independent checked PPI suggestion,
computed by the same physical-screen calibration used by Digital Capture, and
combines it with any still-needed preferred dye-model suggestion. RGB screen
identification and monochrome/known-screen geometry discovery converge on this
same final recommendation step. The recommendation is snapshot-bound and optional:
closing it never discards mandatory detected geometry, while an intervening
document/image change invalidates the optional suggestions.

### Stage 4 — Screen detection and geometry

Current panels/features:

- **Screen** autodetection and screen demosaicing;
- **Geometry** registration points, automatic registration, optimization,
  final orientation and geometry visualization.

This stage is unique to Color-Screen and deserves strong workflow guidance.
The operator should see a compact state summary such as:

`Screen: Dufaycolor | detected | 12,430 points | geometry fitted | residual …`

rather than needing to infer readiness from individual fields.

**Recommendation:** in the medium term, combine the *workflow* of Screen and
Geometry while keeping their implementation panels separate.  A small ordered
action strip is enough:

`Detect screen -> inspect points -> fit geometry -> validate overlay`

Each step should show prerequisites and outcome.  Automatic commands are verbs;
parameters controlling them are nouns/settings.  Do not mix the two visually.

While the combined **Detect screen** operation is still running, Workflow must
not reinterpret every incremental point/geometry publication as a completed
stage. Keep one operation-owned next-step hint: wait/Cancel during coordinate
autodetection and wait/Stop during incremental full-image point discovery.
Normal point-count, inspect, fit, and reconstruction recommendations resume only
after the active detection request finishes or is cancelled/stopped.

For precision tools that need two spatial anchors, prefer a hybrid interaction:
click the first anchor, allow navigation/zoom while a live preview follows the
pointer, and click the second anchor; retain press-drag-release as a quick shortcut
when both endpoints are already visible. Show the pending instruction in-canvas and
let Escape cancel it. This convention is used by screen-coordinate bootstrap,
measurement, Crop and temporary area selection; it should not replace single-click
semantics in Add Point or registration Select.

The primary interaction reference is a professional photo editor rather than CAD.
Follow Capture One's temporary Hand convention: while another canvas tool is active,
holding Space and left-dragging pans the zoomed image, and releasing Space restores
the original tool with its pending anchor intact. Wheel zoom remains available.
Right-click may additionally cancel an unfinished anchor where it has no competing
tool meaning; Escape remains the universal cancellation key.

The expected registration path is **autodetect first**. Successful **Detect
screen** already selects an appropriate image-layer + screen-filter reconstruction
view, so the Workflow summary must advance to inspection instead of repeating
"choose Mode". Automatic detection also leaves the current registration-overlay
visibility and canvas tool alone: it must not switch to Add Point simply because
it found points. Workflow guidance should teach the next useful controls instead:
**Registration -> Show Registration Points** (also available in Geometry) to
show/hide the green overlay, **Select (S)** to inspect or move points, and **Add
Point (A)** for missing points. **Swap screen colors** is a Screen-stage correction
and belongs beside Detect screen; the older duplicate "try luck" detector in
Digital Capture should not return.

If coordinate detection fails on an otherwise regular screen, `Screen coordinates`
is the explicit manual fallback: click the green origin dot, click its neighboring
green dot in the +X direction (the pair is committed as one undoable setup step),
optionally fine-tune/optimize that linear basis, then begin adding registration
points. Screen coordinates are the reference frame of every stored control point,
so automatic **Detect screen coordinates** is disabled once any point exists.
Re-running **Detect screen** with existing points reuses the current coordinates
and goes directly to finding/refining points; it must never replace the center/axes
underneath those points. If points somehow exist without valid geometry, restore
compatible coordinates or delete the points before redetection.

There is one internal recovery exception for difficult lens-distorted scans. If
normal automatic point growth stalls before the cloud spans enough of the scan to
fit a global lens model, the worker may construct a temporary nonlinear mesh from
the trusted cloud and use it for one extra discovery pass. This mesh is only a
search aid: it is discarded before publication, the enlarged cloud must first meet
the lens-fit coverage threshold, and the accepted result is re-solved with ordinary
global geometry plus lens correction. Points contributed by the temporary pass
are then pruned against that final map using the normal screen-space discovery
tolerance and the global model is solved once more. If coverage or either solve
fails, the speculative points are dropped. An explicit user request for nonlinear
geometry remains a separate persistent mode.

Once an accepted geometry fit (or user-requested nonlinear mesh) is based on those
control points, the base-coordinate tool disappears rather than offering two
competing ways to edit the same geometry. Its axis-lock and coordinate-finetune
controls are contextual and appear only while that tool is active. Stochastic/no-
screen processes never expose the tool.

### Stage 5 — Reconstruction/detail

Screen demosaicing, pre/post-screen denoising and any reconstruction-specific
sharpness belong here conceptually.  Some of these controls currently live in
Screen and Sharpness for sound implementation reasons.

**Recommendation:** do not move code merely to match the conceptual diagram.
Instead give each section a small stage badge/heading and later decide whether
users actually benefit from physical reordering.

### Stage 6 — Colour and tone

Current **Color** already has a useful internal progression:

- process-colour adjustments;
- backlight;
- screen dyes;
- viewing-condition correction;
- final adjustments.

This should feel most like the Develop/Color portion of a RAW editor.  The main
change needed is hierarchy: common corrections first, physical/diagnostic
parameters in collapsible Advanced groups, and a clear distinction between a
measured calibration and a subjective final adjustment.

### Stage 7 — Profile/calibration

Current **Profile** manages profile spots and optimisation.  It is best treated
as calibration of the appearance stage, not as an ordinary last-minute colour
slider.

**Recommendation:** retain it near Color but label it **Color profile** or
**Profile calibration**. Auto optimize should mean "rerun when calibration
spots change", not "rerun after every unrelated parameter refresh". The current alpha
hardening patch corrects that behaviour. Profile **Add spot** and Sharpness
**Analyze area** also share one temporary point-click owner: switching between
them preserves the operator's prior canvas tool, and choosing another canvas
tool cancels the temporary action rather than leaving an invisible pending mode.

### Stage 8 — Output

Rendering/export is currently a command/dialog rather than a parameter-panel
stage.  That is reasonable and conventional.

The current alpha render flow makes these items explicit without creating
another layer of saved export state:

- the native save dialog chooses destination/file format and retains native
  overwrite confirmation;
- Render to File repeats that exact destination and TIFF/DNG format before
  rendering;
- **Render mode** and **Output plane** identify the output coordinate/image
  plane;
- **Color and encoding** groups output profile, HDR choice and bit depth for
  TIFF output;
- **Size and resampling** groups antialiasing, scale/screen scale, explicit
  dimensions and the exact computed output size;
- a document-processing row states that the current crop and Sharpness settings
  are used and that there is no additional export-only sharpening;
- the primary action is **Render**, not a generic OK button.

Do not put export-only state into the document unless users need reproducible
saved export recipes.  If recipes are later added, model them explicitly rather
than silently turning the last export dialog state into image-processing state.

## Suggested inspector organization

### Current low-risk alpha presentation

Keep the existing tabs and order, but add stage vocabulary to documentation and
possibly short subtitles/tooltips.  The current order can be interpreted as:

1. Digital capture
2. Tiles
3. Sharpness (capture restoration)
4. Image Layer
5. Contact copy
6. Screen
7. Geometry
8. Color
9. Profile

This is defensible and avoids destabilizing a GUI that has just gained robust
multi-document/view handling.

### Preferred later-alpha presentation

After observing real operators, consider replacing the flat nine-tab row with
five workflow categories whose detailed panels remain reusable:

1. **Capture** — Digital capture, Tiles, capture Sharpness
2. **Process** — Image Layer, Contact copy, Screen/process identity
3. **Register** — detection, Geometry, overlays/quality diagnostics
4. **Reconstruct** — screen demosaic, denoise, restoration/detail
5. **Color** — Color, Profile, final adjustments

A sixth **Output** entry may simply open the existing render dialog.

This reduces navigation without hiding specialist functionality.  It also scales
better if more early-colour processes add their own controls.

## Familiar behaviours from non-destructive photo editors

### Persistent stage, local scroll position

Switching documents should restore the active panel/stage if practical, while
scroll position can be document- or view-local.  Avoid surprising jumps caused
only by an asynchronous refresh.

### Bypass and reset must be predictable

For a processing module with an enable state, users expect:

- one clear enable/bypass control;
- Reset for that module;
- a visible indication when values differ from defaults;
- reset/bypass to be undoable document edits;
- bypass not to destroy carefully entered values unless explicitly documented.

Not every physical calibration should have a bypass.  A screen type or fitted
geometry may instead have **Clear calibration** / **Refit**.  Use language that
matches semantics rather than forcing every panel into the Lightroom module
metaphor.

### Double-click/default gestures

Photographers often expect double-click on a slider label/value to reset it.
`ParameterPanel` now provides this centrally for numeric rows that explicitly
opt into default/modified/Reset metadata. A left-button double click on the
numeric value or its form label invokes the same standard Reset action; default
or disabled rows retain their ordinary double-click behavior. Do not implement
ad-hoc reset gestures panel by panel.

### Numeric entry remains first-class

Museum operators often know a measured DPI, wavelength, f-stop or calibration
value.  Sliders should never be the only way to enter a precise number.  The
existing slider + spin-box pattern is appropriate; preserve it.

### Keyboard navigation

A specialist tool benefits disproportionately from keyboard consistency:

- standard `Ctrl/Cmd+O`, Save, Undo/Redo and close shortcuts;
- arrows/tab traversal inside numeric controls;
- stable canvas focus after starting/stopping tasks;
- discoverable shortcuts for pan/zoom/fit and registration tools;
- no shortcut that changes document parameters while focus is in a text field.

The current alpha implementation scopes canvas tools, saved scan rotation and
registration editing shortcuts to ordinary `ImageWidget` descendants instead
of the whole top-level window. Render-mode digits and bare +/=/- zoom are
canvas-only too; standard modified application/view shortcuts remain
window-wide. Ordinary New Views reuse document-owned shared tool/navigation
actions, while each view keeps its own canvas-scoped 1–0 render-mode actions.

The multi-document work already treats focus as an invariant; preserve that.

## Standard grammar for every panel/module

Where applicable, use this order:

1. **Summary/state** — what is currently selected/fitted?
2. **Primary action** — Detect, Measure, Fit, Optimize, etc.
3. **Common parameters** — the values an expert routinely adjusts.
4. **Diagnostics/quality** — residuals, plots, overlays.
5. **Advanced** — model internals and rarely changed thresholds.

This is more important than making every panel visually identical.

For automatic operations, show four states consistently:

- Ready / prerequisites missing
- Running (progress + Cancel/Stop)
- Completed (quality/result summary)
- Stale (inputs changed since result)

"Stale" is especially valuable in Color-Screen because a geometry or MTF fit may
remain numerically present after a prerequisite changed. Geometry tracks an
accepted fit baseline for the current session: edits keep the numerical result
visible but label it stale, and a fit whose inputs change while it is running is
prevented from publishing. The Geometry fit section now mirrors this provenance
locally as current, running, failed, stale, or present-but-unverified, instead of
requiring the operator to infer freshness from the global Workflow card.
Measured MTF model fitting now follows the same
document-level presentation model across the main and reference inspectors; its
measurement ROI/edge metadata is persistent provenance but deliberately does
not stale the numerical model. Adaptive sharpening now retains the same exact
scan/document snapshot after a correction is accepted: the saved correction
stays active after later edits but is labelled stale, while a correction loaded
from `.par` remains usable with unverified session freshness. Profile
calibration follows the same rule: the exact screen/render/spot snapshot gates
publication, accepted fits retain their average DeltaE as quality provenance,
and later edits make the profile stale without discarding its coefficients.
Profiles loaded from `.par` remain available but are labelled as having
unverified session freshness. Automatic focus-area candidates/diagnostics are
transient rather than saved calibration: they retain the source scan plus the
render/screen-mapping snapshot used by discovery. Changes to those actual worker
inputs clear the rectangles/results and ask the operator to find areas again;
solver bookkeeping, detection settings and profile-spot edits do not
unnecessarily discard them.

## Control semantics: five categories

Every GUI control should be classified before more UI is added.

### 1. Processing parameter

Changes exported pixels and is saved with the document.  It participates in
Undo/Redo and dirty state.

Examples: reconstruction options, mix weights, final colour adjustments.

### 2. Calibration result

Also saved and affects output, but normally produced by an operation rather than
hand-tuned continuously.

Examples: fitted geometry, measured MTF, optimized profile values.

Present quality and provenance next to the value: measured vs. default, source
image/reference, wavelength, residual/error where available. Flat-field and
adaptive-sharpening corrections now also expose a concise saved-result status
beside their Set/Analyze and Clear actions, so an accepted calibration is not
visible only indirectly through a chart or button state. Flat-field freshness
is intentionally narrower than whole-document freshness: its reference analysis
depends on capture gamma and demosaic mode, so geometry/color/appearance edits
leave it current while changing either capture input marks it stale. A correction
loaded from parameters remains active with unverified session freshness.

### 3. Operation

A verb that computes or measures something.  The operation itself is not a
persistent parameter; the accepted result may be.

Examples: Autodetect screen, Analyze focus areas, Optimize color, Set by neutral
area.

### 4. View option

Changes only how the current presentation looks and must not dirty the document.

Examples: zoom, pan, overlays, render preview mode, coordinate display choice.

### 5. Application preference

Persists across documents but does not belong in `.par`/project processing
state.  Examples include workspace geometry and eventually UI density/theme
choices.

Confusion happens when these categories look and behave the same.  For example,
a view overlay should not be presented as a processing checkbox next to a saved
colour correction without some visual distinction.

## Contextual visibility vs. disabled controls

Use **disabled but visible** when the control teaches the next prerequisite:

- "Fit geometry" disabled with "needs at least N registration points";
- IR action disabled with "capture has no IR channel".

Use **hidden** when the concept genuinely does not exist for the selected
process/capture and showing it would be noise.

Do not base logical availability on effective QWidget visibility.  An inspector
may be temporarily hidden while it moves between a workspace and detached view.
The current alpha implementation fixes this exact issue in `MultiLineTabWidget`.

## Advanced mode without a second application

A global Beginner/Expert switch is not attractive here: even normal users are
advanced, and hiding process controls can make archival work less reproducible.
Prefer progressive disclosure inside each stage:

- common controls visible;
- diagnostics and model internals collapsed;
- automatic actions near the values they determine;
- explicit **Advanced** sections that stay discoverable.

Remember expanded/collapsed state as an application preference, not document
processing state.

## Presets and reproducibility

Presets are useful for repeated digitization campaigns, but they need scope.
Useful future preset types include:

- capture-device preset (scanner/camera metadata and capture MTF defaults);
- historical-process preset (known screen/dye/contact-copy assumptions);
- reconstruction preset;
- output recipe.

Avoid one giant opaque "preset" that overwrites unrelated geometry, profile
spots and output choices.  Before applying a preset, the UI should be able to
show which domains it changes.

For museum work, provenance matters.  Eventually record whether important values
were defaulted, read from metadata, manually entered, measured from a reference,
or fitted automatically.  This can initially be diagnostic metadata without
changing the rendering model.

## Notifications, errors and long tasks

Avoid modal dialogs for routine success.  Use the shared status/task UI for
progress and concise completion/failure messages.  A modal dialog is justified
when the operation cannot proceed without a decision, data would be lost, or the
result requires explicit acceptance.

Error messages should answer:

1. what failed;
2. what input/stage was affected;
3. whether existing document state is still valid;
4. what the operator can do next.

For example, "Screen detection failed" is less useful than "No regular Dufay
lattice was found in the selected scan; existing screen geometry was left
unchanged. Try a central raster area or verify the process type." The Screen /
Geometry registration workflow now follows this rule: unsupported/failed regular
screen discovery and coordinate detection state what prior state remains intact
and suggest a clearer raster or corrected Screen/capture setup, while progressive
point discovery explicitly says that already accepted point/geometry batches
remain in the document and tells the operator how to retry locally. These
background-analysis failures use parent-owned asynchronous dialogs, so reporting
a numerical failure does not block unrelated document/workspace interaction. Geometry-fit
failure follows the same rule: current geometry and registration points are left
unchanged, the operator is pointed to coverage/outlier guidance and fit settings,
and the warning is parent-owned/asynchronous rather than a nested static modal
call. Profile optimization now follows the same four-part rule without adding a
routine modal: the persistent Profile/Workflow status states that existing
profile correction values remain unchanged, while the status bar identifies the
failed image/spot/input fit and points the operator to spot distribution and
geometry/color inputs before retrying **Optimize profile**. Measured-MTF model
fitting uses the same contract in its asynchronous warning: the selected saved
curves and model/fit settings are named as the failed inputs, existing
measurements and model parameters are explicitly retained, numerical fitter
detail remains secondary diagnostic context, and the operator is sent back to
measurement selection, capture metadata and fit options before retrying
**Fit measured MTF model**. Slanted-edge **Measure MTF** now follows the
same rule for acquisition failures: the selected edge region and current
measurement/capture settings are identified as the failed inputs, existing
saved measurements/model parameters are explicitly retained, partial
per-channel batches are never accepted, the numerical edge/channel reason is
secondary diagnostic context, and the asynchronous warning tells the operator
to select one clean isolated edge and retry **Measure MTF**. The experimental
one-area Focus **Analyze area** path follows the same preserved-state rule
without adding a routine modal:
failure leaves all MTF/sharpening parameters unchanged, identifies the selected
area plus current geometry/focus settings, preserves worker detail as secondary
diagnostics, and suggests a clearer area or corrected setup before retrying.
Focus-area discovery and joint analysis follow the same rule: failed discovery
changes no saved processing state and accepts no new candidates, while a failed
joint fit preserves the candidate/failed-region
overlays for diagnosis. Both retain the worker's numerical detail only as
secondary context and point the operator back to **Find focus areas** or
**Analyze focus areas** with the Screen/Geometry and validation setup to inspect.
Flat-field reference analysis follows the same contract: a failed white/black
reference request explicitly retains the active correction and all document
parameters, identifies the reference files plus capture gamma/demosaic inputs,
keeps loader/analyzer detail secondary, and uses an asynchronous warning that
points back to **Flat field — Set reference**. Adaptive sharpening also
distinguishes an ordinary cancellation from a genuine numerical failure:
cancellation produces only a status message, while failure explicitly retains
the accepted spatial correction and all document parameters, restores the chart
to that accepted state, identifies the current image/geometry/analysis inputs,
and points back to **Analyze adaptive sharpening** with worker detail kept
secondary. Coordinate refinement follows the same preserved-state rule: a
failed local fit retains the existing coordinate basis, nonlinear correction and
registration points, identifies the scan plus current Screen/capture inputs, and
tells the operator to **Detect screen coordinates** again when the starting basis
is wrong or otherwise retry **Optimize coordinates**. Optimizer detail remains
secondary and the warning is parent-owned/asynchronous. Render/export failure
now follows the same contract: the accepted render snapshot is independent of
the live document, failed/cancelled partial output is removed, renderer/library
detail is preserved through `FileRenderController`, and the asynchronous warning
points to destination writability/free space plus export format/size settings
before retrying **Render to file**.

## Visual style

The alpha-to-beta work should prioritize native, predictable behaviour over ornamental custom
styling.  Qt platform conventions are valuable for museum workstations that may
run Windows, macOS or Linux for years.

Guidelines:

- use standard icons/actions when Qt supplies them;
- avoid hard-coded dark-theme colours in reusable widgets where palette roles
  are sufficient;
- keep spacing/density compact but not cramped;
- reserve accent colour for selection, running state or actionable emphasis;
- charts/overlays may use specialized colour where data semantics require it;
- tooltips should explain physical meaning and units, not merely repeat labels.

`MultiLineTabWidget` currently carries custom tab styling.  Replacing it should
be evaluated later in alpha with real wide/narrow inspector sizes; do not trade
its useful wrapping behaviour for a standard `QTabWidget` that truncates nine
specialist stages.

## Implementation status and remaining alpha sequence

The current alpha baseline implements the original Phase A hardening, the Phase B
workflow summaries/terminology, Phase C module grammar, Phase D navigation
consolidation, and Phase E presets/provenance. Treat those sections below as
maintained invariants rather than a backlog. Phase F now has the versioned
`.cspar` container, cross-platform core/CLI/GUI read-write paths, archive-first
sidecars, and archive-default new saves merged. Remaining alpha work is the
structured-state migration inside that container, equivalent crash-recovery
coverage for archive recovery, and field testing of the finished workflow.

### Phase A — robustness hardening (implemented foundation; keep green)

- fix proven correctness/packaging bugs;
- keep current tab order and architecture;
- document the workflow and state categories;
- expand smoke/sanitizer invariants;
- make errors and task cancellation reliable.

### Phase B — terminology and summaries (implemented foundation)

- add stage labels and one-line process/image-layer/geometry/MTF/profile
  summaries. The persistent Workflow card reports the actual scalar
  analysis/reconstruction source as native grayscale/IR (with wavelength when
  known) or the active simulated RGB mix.
  Negative captures also report whether Contact-copy positive conversion is
  still off or already active, so the persistent state line agrees with the
  next-step handoff into the simulated darkroom. When a `Next:` recommendation names
  one unambiguous inspector stage, the card also offers a compact **Open stage**
  action resolved through that stage's stable semantic key. Choice points,
  toolbar-only actions, file loading, and running operations remain text-only;
- standardize operation wording: Detect, Measure, Fit, Optimize, Reset.
  Adaptive sharpening now uses that user-facing goal consistently in its
  button, long-task progress and stale-input message; the internal displacement
  terminology remains an implementation detail;
- make prerequisites visible. Profile optimization, Focus analyzer, and
  Adaptive sharpening use local fold-aware **Requirement** rows for their main
  gating conditions. Focus analyzer and Adaptive sharpening distinguish a
  missing image from missing screen geometry; both Focus **Analyze area** and
  **Find focus areas** also share the same readiness rule instead of leaving
  disabled or no-op operations unexplained. Obvious image interactions (Digital
  Capture Measure/Crop and
  Sharpness Measure MTF) simply stay disabled until a scan exists, with
  defensive handler guards as a second line of protection. Geometry follows
  the same rule for every compute verb: Detect/Optimize coordinates, automatic
  point discovery and Fit geometry require a live source scan in addition to
  their point/geometry prerequisites. All processing panels now receive a
  pending-aware source getter, so the retained outgoing scan is presented as
  unavailable while asynchronous replacement is active; saved state therefore
  cannot expose an enabled no-op while an image is absent or being replaced;
- add explicit stale/result states where analyses depend on changing inputs.
  Geometry-fit current/failed provenance is tied to the exact source scan as
  well as points/settings/nonlinear mode. Queued fits own that immutable scan;
  image replacement cancels pending publication and clears session fit
  provenance without discarding persisted/manual geometry values.
  The Registration menu's auto-detected patch-center overlay now has explicit
  provenance too: it survives coordinate/mesh refinement because the retained
  map is screen-coordinate evidence projected through current geometry, but a
  source-scan, screen/scanner-type, detection-parameter, gamma, or capture-
  sharpening change clears it from every ordinary view and disables the toggle.
  A dedicated document diagnostic signal keeps map availability and Show/Hide
  synchronized even in inactive tiled/cascaded or detached ordinary views, and
  new/refreshed peer views immediately adopt the current diagnostic; reference
  scans are intentionally excluded.
  Profile calibration follows the same evidence rule: the fitted matrix remains
  saved and may be labelled stale, but per-spot colour matches and average
  DeltaE are session diagnostics tied to the exact source scan + optimizer
  inputs and disappear as soon as those inputs change. Profile spot positions
  and those accepted match results are synchronized to every ordinary peer view;
  the Show profile spots switch remains view-local and the shared Profile
  checkbox follows only the currently inspected view.
  Focus-area rectangles are likewise document-owned diagnostics: candidate,
  selected and held-out-result overlays synchronize to every ordinary peer view
  and clear from all of them when focus inputs become stale; reference scans are
  intentionally excluded. Stored MTF measurement ROI/edge overlays follow the
  same ordinary-view synchronization rule, while Locate remains an active-view
  navigation action and reference scans keep their own source-matched overlay.

### Phase C — module grammar (implemented foundation)

- keep `ParameterPanel` reset/default/modified metadata as the shared
  convention for ordinary saved controls. The original Digital Capture pilot has
  expanded across the processing panels; Reset is disclosed only for values that
  differ from their real `ParameterState` defaults. Explicit sentinels such as
  `0 = not configured/use process default` remain first-class stored defaults
  even when the ordinary numeric editing range starts above zero;
- standardize collapsible Common/Diagnostics/Advanced sections;
- remember expansion state through stable, untranslated section keys.
  All nine processing panels now use this contract: Digital Capture, Tiles,
  Sharpness, Image Layer, Contact Copy, Screen, Geometry, Color, and Profile.
  Explicitly
  folding a section is an application preference restored in subsequently
  created panels, including after restart. Existing open panels keep their own
  local fold state.
  Programmatic folding, document refresh, and process-dependent applicability do
  not overwrite that choice. Color includes all five existing groups and keeps
  the Tone Curve inside Final adjustments. Contact Copy includes all four
  specialist groups; switching simulation off and on does not change their
  saved folds. Digital Capture now groups Source, Resolution and optics,
  Sensor, Spectral wavelengths, and Capture corrections; metadata-derived rows
  keep their logical applicability independent of folding. Sharpness remembers
  all eight existing groups; live focus/adaptive diagnostics cannot punch
  through collapsed sections, and keyed numeric sharpening controls expose the
  same default/modified/Reset presentation piloted in Digital Capture.
  Geometry includes all five existing groups; fit-prerequisite
  messages and visualization charts respect both applicability and the saved
  fold during incremental updates. Finetune Diagnostic Images remains separate
  and is explicitly transient: Geometry and Sharpness expose it only after an
  accepted finetune-producing operation, and the next accepted document-state
  application clears the grid before any producing operation can republish fresh
  diagnostics. This prevents an old optical/registration comparison from
  masquerading as current after later edits or Undo/Redo.
  All nine panels now have explicit section keys; there is no remaining
  initially-expanded compatibility panel in the processing inspector;
- keep Undo coalescing explicit: continuous saved editors use stable parameter
  keys, while unkeyed buttons, canvas clicks, calibration/results and Reset/Clear
  actions are atomic even when repeated rapidly with identical Undo text.
- add consistent per-module reset/bypass only where semantically valid.
  The standard numeric Reset/default presentation now covers straightforward
  saved controls in Digital Capture, Tiles, Sharpness, Color, Image Layer,
  Screen, Geometry, and Contact Copy's simulated-darkroom values. Tiles also
  exercises the context-dependent case: its shared Exposure/Dark point editors
  retarget Reset metadata and the fresh-state default when another tile is
  selected. Coupled calibration
  editors such as Contact Copy's H&D/Richards curve remain deliberately outside
  the generic per-field Reset convention; Contact Copy instead exposes one
  calibration-level Reset that restores the complete characteristic curve
  atomically. Saved checkbox choices follow the same opt-in convention:
  Contact copy simulation resets to bypass/off without discarding its configured
  curve or darkroom values, Sharpness Use measured MTF resets to the fitted
  model, and Image Layer Use simulated RGB resets to the native channel source.

### Phase D — navigation consolidation (implemented foundation)

The inspector now exposes five workflow stages — Capture, Process, Register,
Reconstruct, and Color — while retaining all nine specialist panels and their
stable semantic keys. Programmatic **Open stage** navigation, saved active-panel
state, hidden-panel fallback, detached views, and New View all continue to use
the original document/view ownership model. Keep the five-stage grouping under
alpha field observation; changing it again should be driven by operator evidence,
not by generic editor conventions.

### Phase E — presets/provenance (implemented foundation)

Named application presets are explicitly scoped to Capture, Process,
Reconstruction, or Color and never copy image-specific geometry, crop/tile
state, flat-field/adaptive grids, fitted profile evidence, or measured MTF
curves across documents. Process presets clear incompatible screen-bound
evidence atomically when they change the historical screen type. Geometry,
flat-field, MTF/adaptive sharpening, and profile results expose current/stale
provenance in their owning modules, and **Save Reproducibility Report** exports
the current workflow/provenance summary together with an exact parameter
payload. Keep expanding provenance only where a concrete saved/measured/fitted
distinction remains ambiguous to operators.

### Phase F — project/parameter format modernization

This work is now in progress while the application intentionally remains
`2.0alpha`. The current CSP/`.par` reader remains available indefinitely for
backwards compatibility. New parameter saves use the versioned `.cspar`
container by default, while explicit legacy `.par` export remains available.
The remaining migration is inside the archive: move state from the legacy mirror
to explicitly structured sections without extending the ad-hoc keyword stream.

The format decision is now a libzip-backed `.cspar` archive with a UTF-8
JSON manifest; see [the parameter archive format](parameter-archive-format.md).
JSON is deliberately used as the strict manifest syntax because it is also valid
YAML 1.2 while avoiding a new YAML parser dependency in libcolorscreen and the
CLI. General YAML tags, anchors/aliases, merge keys and implicit scalar typing
therefore never enter the compatibility surface.

The archive keeps ordinary settings/provenance human-readable while allowing
dense geometry meshes, scanner-blur correction grids and sampled curves to live
in typed binary payload entries rather than enormous text arrays. The manifest
identifies every dense payload by stable name, archive path, explicitly sized
endian-qualified element type, dimensions and required/optional status. Schema
version 1 first establishes the durable container/version boundary around an
exact legacy CSP payload; structured domains then migrate incrementally without
breaking old `.par` import.

Whichever syntax/container is chosen, the schema is the compatibility contract:

- one top-level format identifier and integer schema version;
- stable string spellings for enums and named algorithms;
- optional namespaced sections so new GUI/library state can be added without
  another trailing-data convention;
- defined unknown-key behaviour and per-version migrations;
- explicit distinction between persisted inputs, measured/calibrated results,
  provenance and transient diagnostics;
- canonical writer output so round-trip tests produce deterministic files;
- transactional/atomic save semantics identical to the current GUI path;
- fixtures proving legacy `.par` import, current-format round trips, forward
  unknown-key tolerance, and migration of every historical default whose
  semantics changed.

Only after the structured migration, archive-recovery parity, sanitizer/build
gate, and remaining alpha field testing are complete should the project be
considered for a beta version.

## Questions to answer during alpha field testing

Do not guess these from generic Lightroom conventions.  Observe museum operators:

- Do they think in physical-process order or in "fix what looks wrong" order?
- Is screen type normally known before opening the file?
- How often are capture parameters reused across a digitization batch?
- Is RGB+IR routine or exceptional?
- Are geometry and focus fitted once per plate, per scanner session or per tile?
- Which diagnostic plots must stay visible while adjusting another stage?
- Do users want several views of one document for before/after or for different
  coordinate/render modes?
- Which values must be recorded in catalog/provenance systems outside
  Color-Screen?
- What is the most common point at which an operator is unsure what to do next?

Answers should validate or revise the implemented alpha grouping.  The application is specialized
enough that copying another editor's panel order verbatim would be less standard,
not more: the standard behaviour to borrow is consistency, reversibility,
feedback and clear stage ownership.
