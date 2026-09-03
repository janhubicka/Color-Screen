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
beta does not need a wholesale reorder.  The first goal should be to make the
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
**Profile calibration**.  Auto optimize should mean "rerun when calibration
spots change", not "rerun after every unrelated parameter refresh".  The beta
hardening patch corrects that behaviour.

### Stage 8 — Output

Rendering/export is currently a command/dialog rather than a parameter-panel
stage.  That is reasonable and conventional.

For beta, the render dialog should make these items explicit:

- output coordinate/image plane;
- crop and dimensions/resampling;
- output colour profile/encoding;
- bit depth and file format;
- whether any output-specific sharpening exists;
- destination and overwrite policy.

Do not put export-only state into the document unless users need reproducible
saved export recipes.  If recipes are later added, model them explicitly rather
than silently turning the last export dialog state into image-processing state.

## Suggested inspector organization

### Low-risk beta presentation

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

### Preferred post-beta presentation

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
This can be added later through `ParameterPanel` once reset metadata is explicit.
Do not implement ad-hoc reset gestures panel by panel.

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
prevented from publishing. Measured MTF model fitting now follows the same
document-level presentation model across the main and reference inspectors; its
measurement ROI/edge metadata is persistent provenance but deliberately does
not stale the numerical model. Profile calibration now follows the same rule:
the exact screen/render/spot snapshot gates publication, accepted fits retain
their average DeltaE as quality provenance, and later edits make the profile
stale without discarding its coefficients. Profiles loaded from `.par` remain
available but are labelled as having unverified session freshness.

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
image/reference, wavelength, residual/error where available.

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
The beta patch fixes this exact issue in `MultiLineTabWidget`.

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
unchanged. Try a central raster area or verify the process type."

## Visual style

The beta should prioritize native, predictable behaviour over ornamental custom
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
be evaluated after beta with real wide/narrow inspector sizes; do not trade its
useful wrapping behaviour for a standard `QTabWidget` that truncates nine
specialist stages.

## Proposed implementation sequence

### Phase A — beta hardening (now)

- fix proven correctness/packaging bugs;
- keep current tab order and architecture;
- document the workflow and state categories;
- expand smoke/sanitizer invariants;
- make errors and task cancellation reliable.

### Phase B — terminology and summaries

- add stage labels and one-line process/geometry/MTF/profile summaries;
- standardize operation wording: Detect, Measure, Fit, Optimize, Reset;
- make prerequisites visible;
- add explicit stale/result states where analyses depend on changing inputs.

### Phase C — module grammar

- teach `ParameterPanel` reset/default/modified metadata;
- standardize collapsible Common/Diagnostics/Advanced sections;
- remember expansion state;
- add consistent per-module reset/bypass only where semantically valid.

### Phase D — navigation consolidation

- use field observations to decide whether nine tabs should become five workflow
  stages;
- preserve all specialized panels behind those stages;
- keep New View/detach semantics unchanged while changing inspector navigation.

### Phase E — presets/provenance

- scoped capture/process/reconstruction/output presets;
- provenance for measured/fitted/default values;
- optional reproducibility report useful for archival workflows.

## Questions to answer with real beta users

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

Answers should drive the post-beta regrouping.  The application is specialized
enough that copying another editor's panel order verbatim would be less standard,
not more: the standard behaviour to borrow is consistency, reversibility,
feedback and clear stage ownership.
