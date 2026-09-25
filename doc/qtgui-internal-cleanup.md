# Qt GUI internal cleanup and beta-readiness notes

## Purpose

The Qt GUI has reached the point where it is useful as an application rather
than only as a frontend used by its author.  That changes the engineering
priority.  The next failures are less likely to be missing algorithms and more
likely to be ambiguous ownership, stale asynchronous results, surprising undo,
incomplete source packages, and UI helpers whose implicit behaviour is no
longer obvious to a new contributor.

This document records the invariants that should be preserved while the GUI is
prepared for beta.  It is intentionally conservative: it does not propose a
rewrite of a working interface.  The goal is to make future changes local,
testable and unsurprising.

## Current architecture worth preserving

Color-Screen is a non-destructive editor, but its document is richer than a RAW
editor document.  A single scan combines five kinds of state:

1. **Capture state** — demosaicing, scanner/camera metadata, crop/orientation,
   tile normalization, capture MTF and other defects of digitization.
2. **Historical-process state** — physical screen type, strip geometry,
   contact-copy response and other properties of the original photographic
   process.
3. **Reconstruction state** — registration, screen-to-image geometry,
   demosaicing/reconstruction choices and image-layer construction.
4. **Appearance state** — process-colour corrections, viewing-condition model,
   final adjustments and colour-profile fitting.
5. **Presentation/session state** — zoom, pan, active render mode, attached vs.
   detached windows and inspector presentation.

The first four categories belong to the logical document and are represented by
`MainWindow`/`ParameterState` and the associated analysis state.  The fifth
belongs to a view or workspace.  Keeping that boundary is more important than
whether a particular control is implemented by a tab, group box or toolbar.

The existing multiple-document design is a good foundation:

- `ColorScreenApplication` owns the set of logical documents and views.
- `WorkspaceWindow` owns application-level presentation and the `QMdiArea`.
- `MainWindow` owns one document's mutable processing state, undo stack,
  recovery data, workers and task queues.
- ordinary `ImageViewWindow` instances are peer presentations of the same
  document; they do not clone processing state;
- specialized reference views may display another image but still apply
  measurements through the owning source document.

Do not collapse these roles back into a single global window object.  In
particular, presentation changes must never serialize/reconstruct a document as
an implementation shortcut.

## Beta-critical invariants

### Document state has one owner

All processing changes should enter through the document's state setter and
therefore through the undo/dirty-state machinery.  A panel must not retain a
second authoritative copy of a processing parameter.  Widgets may cache values
for display, but `ParameterState` is authoritative.

Analysis results that are not themselves saved parameters should still have an
explicit owner and lifetime.  Candidate focus areas, profile-fit diagnostics,
slanted-edge results and similar data should be document-local unless there is a
clear reason for them to be view-local.

### View state stays view-local

Zoom, pan, render mode, Color/IR presentation, and coordinate-space choice are
examples of view state.  Opening **Window -> New View** must not make those
controls fight through a single shared variable.  Conversely, an edit made from
one ordinary view must update all views because the edited document is shared.

### Workspace chrome is presentation, not document state

Menus, toolbars, the inspector host and the workspace status bar may be moved
between presentations.  Their movement must not determine the lifetime of the
underlying image or processing state.  The existing sanitizer smoke tests are
valuable precisely because Qt can defer destruction of MDI wrappers and docks.
Keep using guarded pointers across event-loop turns.

### Background work is generation-aware

A long-running computation should be understood as operating on a snapshot of
input state.  The completion path must establish that the result still belongs
to the current request/document before applying it.  `TaskQueue` request IDs,
explicit snapshots and cancellation are preferable to reading mutable GUI state
from a worker after it has started.

For every new asynchronous feature answer these questions in code review:

- Which object owns the worker and its progress object?
- What exact input snapshot does the request represent?
- How is an obsolete result rejected?
- What happens when the document closes or the view detaches?
- Does cancellation mean "do not apply the result", "stop computation", or
  both?
- Can a queued signal outlive its receiver, borrowed widget or image data?

### Undo describes user gestures, not timing accidents

Slider drags should coalesce into one undo step, but two different controls
changed quickly are two user actions.  Merge identity must therefore include a
logical operation identity, not only a short wall-clock interval.  This review
changes the current command merger so only commands with the same description
can merge.

Longer term, the description string should become an explicit merge key supplied
by the parameter helper.  That avoids depending on translated display text and
allows two controls with the same visible label in different groups to remain
independent.

### Dirty state is separate from worker/view activity

A document is modified when saved processing state differs from the clean save
point or when recovery state explicitly says it is dirty.  Merely panning,
changing the active tab, running an analysis that produces no accepted result,
or moving a dock must not make the document dirty.

When a command accepts an automatically fitted result, that acceptance is a
normal undoable document edit.  The expensive analysis leading to it is not an
undo operation by itself.

## Concrete findings from the beta audit

### Source distributions were incomplete

`src/qtgui/Makefile.am` listed Qt implementation files but omitted seven local
headers and `resources.qrc` from `EXTRA_DIST`.  An in-tree build has all of these
files and therefore does not expose the mistake; a release tarball can fail only
after it has been unpacked elsewhere.  The missing headers were:

- `ColorOptimizerWorker.h`
- `CoordinateTransformer.h`
- `FlowLayout.h`
- `FocusAnalysisWorker.h`
- `HistogramWorker.h`
- `MultiLineTabWidget.h`
- `ScalableImageLabel.h`

The fix adds them and `resources.qrc`, updates the checked-in `Makefile.in`, and
extends `build-aux/check-generated-build-metadata.sh` to reject future Qt GUI
headers/resources missing from `EXTRA_DIST`.

This is a useful pattern for generated-build metadata: check the invariant in a
small deterministic script before spending minutes compiling.

### Undo could merge unrelated controls

`ChangeParametersCommand::id()` intentionally returns one ID for ordinary
parameter changes.  Previously `mergeWith()` then used only a 500 ms interval.
Changing, for example, exposure and a different colour control within that
interval could collapse both into one command whose old state predates the first
change and whose new state follows the second.  One Undo would unexpectedly
remove both edits.

The beta fix keeps drag coalescing but also requires the command descriptions to
match.  This is deliberately small and compatible with existing panel helpers.

### Hidden inspector pages used ancestor visibility as tab state

`MultiLineTabWidget::setTabVisible()` used `QWidget::isVisible()` while looking
for a fallback tab.  `isVisible()` is false when an ancestor (for example the
whole inspector while it is being moved between MDI presentations) is hidden.
That can make every logically enabled tab appear unavailable at the exact moment
a current page is hidden.  The widget now tests explicit hidden state instead
and refuses programmatic selection of a hidden page.

The general lesson is to distinguish **logical availability** from **effective
onscreen visibility**.  The former should not depend on whether an ancestor is
currently mapped.

### Pointer gestures had implicit lifetime

Canvas, navigator and curve interactions historically used independent booleans
or drag indices and assumed that every press would be followed by the expected
release. A lost release, tool switch, hidden/deactivated view, or mismatched
button could therefore leave panning, measurement, rubber-band selection,
registration-point movement, coordinate editing or chart panning logically
active. The navigator also accepted any mouse button as the start of a drag.

Pointer gestures now record their initiating button, refuse to mutate after that
button disappears, and settle all transient state at tool/window/grab
boundaries. Qt's automatic press-to-release mouse grab is used consistently
instead of selectively calling `grabMouse()`. Live edits close their undo
transaction on interruption; uncommitted area/measurement gestures are simply
discarded. The ordinary Qt smoke path contains synthetic lost-release and
tool-switch probes for the primary canvas and interactive curves.

### Dynamic inspector status text resized the canvas

Registration updates change both the persistent Workflow recommendation and
Geometry's threshold/status messages. Those labels used their text width as a
horizontal size hint, so new point batches could make the right inspector ask
the main splitter for more or less width and visibly resize the image canvas.
Dynamic Workflow and Geometry status labels now wrap within the existing
inspector allocation and ignore their horizontal size hint. The registration
visibility checkbox also keeps a stable caption; its changing point count lives
in the tooltip and Workflow summary instead of participating in layout.
Point/solver progress may change text and row height, but it must not move the
user-selected main image/inspector divider. Workspace-churn smoke checks this
sizing policy.

### Screen-coordinate editing required geometry it was supposed to create

The toolbar previously enabled `Screen coordinates` only after
`screen_geometry_configured_p()` was already true. That inverted the manual
fallback workflow: a failed autodetect left no GUI path for entering the initial
linear lattice. The tool now distinguishes screen capability from mapping state.
For a regular screen with the zero-vector geometry sentinel it accepts a center
click followed by a neighboring +X green-dot click. The first click remains
transient; the second atomically creates the center and non-degenerate initial
basis, after which the existing drag/button editing gestures take over.

The coordinate bootstrap now previews its provisional X/Y axes immediately after
the center click. Axis strokes use one black/white cycle per screen period and are
extended in widget space, avoiding the old divide-by-zero failure for perfectly
horizontal or vertical axes.

Measure, Crop and temporary Generic Area tools also expose the pending interaction
directly on the canvas. They accept click-move-click (including wheel zoom between
anchors) as well as the older drag shortcut; Escape or a tool switch discards the
transient first anchor. Add Point and registration Select retain their established
single-click meanings.

Canvas navigation now follows the Capture One-style temporary Hand convention: hold
Space to pan with the left mouse button without changing tools or losing a pending
anchor. The permanent Pan tool uses the same open/closed hand cursor language.
Temporary area prompts carry the operation-specific text supplied by MainWindow, and
right-click shares Escape's pending-anchor cancellation path where right-click has no
active tool meaning.

Manual base-coordinate editing is deliberately hidden after an accepted
control-point geometry fit or nonlinear mesh takes ownership of the mapping. The
axis lock and coordinate-finetune actions are similarly scoped to the active
manual tool. This keeps the normal path — autodetect, inspect/refine, add control
points, fit/autosolve — visually linear while retaining a usable fallback.

### Automatic registration obscured the result and could stall before lens fitting

The persistent Workflow hint now treats the combined **Detect screen** run as
one operation while it is active. Coordinate detection publishes a stable
wait/Cancel hint, and its handoff to incremental full-image point discovery
switches that to wait/Stop. Intermediate point batches and automatic geometry
updates may refresh diagnostics but cannot cycle the next-step recommendation.
The session marker follows the weak progress identity so an old coordinate-stage
completion cannot clear a newer point-discovery stage.

The combined **Detect screen** path used to arm **Add Point** after finding the
initial lattice. Add Point deliberately turns the green registration overlay on,
so a successful automatic run could finish on a reconstructed colour image that
was almost completely covered by points. Detection now leaves the current canvas
tool and overlay visibility alone. Workflow instead explains how to show/hide
points and use Select/Add Point, recognizes an already-selected reconstruction
mode, and points to **Screen -> Swap screen colors**. The redundant Digital
Capture "try luck" detector is gone, and the swap action now lives with Screen
detection rather than Geometry.

The opening/setup path now also converges on one post-detection recommendation
dialog. The initial monochrome screen selector reuses the Screen panel's rendered
pattern icons and offers a preferred dye model when known. Once automatic
geometry is available—whether from RGB screen identification or B&W
known-screen detection—the same screen calibration as Digital Capture's
**Resolution from screen** computes a candidate PPI. Preferred dyes and PPI are
independent checked suggestions; neither is silently accepted if the
recommendation dialog is closed. The setup-dialog smoke additionally checks that
dynamic screen rows do not overlap the following Bayer control.

Large lens-distorted scans also exposed a bootstrap problem: ordinary flood-fill
can stall while the trusted point cloud is still too local for global lens
parameters to be identifiable. Full-image automatic discovery now uses one
private recovery pass when lens fitting is requested but lacks coverage. For
non-Dufay screens it first forces a conservative two-term radial lens fit from
the trusted points; Dufay-like screens retain the temporary nonlinear mesh
because their film geometry eventually needs that model. Either provisional map
is only a search aid. Newly discovered points stay private until an ordinary
lens solve succeeds; only that speculative suffix is pruned against the final
mapping, coverage is checked again, and the global model is re-solved. A
failed bootstrap publishes neither the temporary mesh nor its speculative points.
User-selected nonlinear correction and selected-area point discovery keep their
previous semantics.

### Profile auto-optimization retriggered on every state refresh

`ProfilePanel::onParametersRefreshed()` said it auto-triggered when profile spots
changed, but it only tested that Auto optimize was enabled and at least one spot
existed.  Applying an optimizer result changes render parameters, which refreshes
the panel, so the result could immediately request another optimization even
though the spots were unchanged.  Other unrelated edits could do the same.

The panel now snapshots the profile-spot coordinates and auto-runs only when the
spot set actually changes.  Manual **Optimize color** remains unchanged.

### Workflow now names the active image layer

The persistent Workflow card includes an explicit Image layer line. It
distinguishes a captured native grayscale/infrared plane from an RGB-derived
scalar layer and shows the current RGB weights (plus whether dark offsets are
set) when simulation is active. The line is derived from the same
`render_parameters` and image capabilities used by Image Layer itself, so it
is presentation only: there is no duplicated processing state or additional
Undo identity. Like the other live workflow labels it ignores horizontal size
hints and wraps inside the existing inspector allocation. Workspace churn checks
both the derived source classification and splitter sizing.

## Panel framework cleanup priorities

`ParameterPanel` is high-leverage code: a small mistake in a helper is copied to
many screens.  Changes here deserve focused tests before broad visual cleanup.

### P0 — before beta

- Keep `EXTRA_DIST` and generated build metadata checked automatically.
- Keep sanitizer GUI smoke coverage for document/view/dock lifetime churn.
- Keep pointer-gesture interruption probes in the ordinary Qt smoke path;
  no drag may depend on receiving an ideal release sequence.
- Keep the ordinary Qt smoke regression for undo merge identity: repeated
  updates with one stable parameter key merge, while a different key remains a
  separate action even when the human-readable Undo text is identical. The probe
  also verifies the resulting Undo/Redo state sequence.
- Keep the ordinary Qt smoke regression for `MultiLineTabWidget` logical
  visibility while an ancestor is hidden: fallback and programmatic selection
  must use explicit per-tab hidden state rather than effective onscreen visibility.
- Ensure every one-shot worker completion is request/generation checked before
  applying state.
- Keep application-owned modal objects parent-owned and asynchronous.  The
  explicit dialog/message-box/context-menu `exec()` sites have been converted
  to `open()`/`popup()` with receiver-bound continuations, and the deterministic
  Qt GUI source/build-metadata audit rejects new secondary event loops in CI.
  The top-level `QApplication::exec()` and `QDrag::exec()` remain intentional
  exceptions.

### P1 — early beta

- Give every stateful parameter helper an explicit machine-readable key.
  `ParameterPanel` now accepts an optional stable `parameterKey`, stores it
  on the field widget, and uses it for undo merge identity independently of
  the human Undo description. Unconverted controls deliberately fall back to
  their historical label-based identity. Screen strip-width controls and the
  measured-MTF selector are the first migrated users; continue assigning keys
  as panels gain reset/default/modified metadata. Keyed numeric helpers can
  now opt into that presentation without a parallel defaults table: a fresh
  `ParameterState` supplies the reset target, the label is emphasized while
  modified, and Reset is hidden at the default. Digital Capture is the first
  pilot (gamma, resolution, f-stop, pixel pitch and sensor fill factor).
  Numeric sentinel defaults are state, not presentation: preserve values
  such as `0 = not configured/use process default` exactly. `ParameterPanel`
  gives a below-range sentinel its own slider/spinbox position; capture MTF
  wavelengths and process strip widths exercise this rule. Values whose
  sentinel is already inside the ordinary range (for example Geometry's
  `0 = Auto`) need no special range handling. The Screen panel is now the
  first complete panel migration: every stateful helper plus its custom screen
  selector has a `screen.*` key. In particular, the pre- and post-demosaic
  denoising stages use distinct keys despite intentionally repeated captions
  such as `Denoise Mode`, `Strength`, `Patch Radius`, and `Search Radius`.
  Workspace churn drives the two real `Strength` spin boxes back-to-back and
  verifies that one Undo reverts only the later stage; this catches regression
  to visible-text merge identity rather than merely checking metadata. Geometry
  is the second complete panel migration: persistent fit policy, scanner/camera
  geometry, and final-orientation controls use `geometry.*` keys. Panel-local
  **Auto fit geometry** and **Nonlinear corrections** request/presentation state
  deliberately remain unkeyed; a parameter key must identify saved document
  state rather than merely every widget produced by a parameter helper. Image
  Layer is the third complete migration: the simulated/native source choice and
  six RGB mixing values use `image_layer.*` keys, while Set by dark area, Set by
  neutral area, and Set by infrared channel remain unkeyed operation controls.
  Tiles is the fourth complete migration and exercises a different invariant:
  its shared Exposure/Dark point editors change saved target with the selected
  stitch tile, so `ParameterKeyGetter` resolves them to coordinate-specific
  `tiles.<x>.<y>.*` keys at edit time. The same shared editors now use dynamic
  default/modified/Reset metadata: selecting another tile retargets the Reset
  key and recomputes the fresh-state default before presentation or Reset.
  Per-tile enable checkboxes use the same namespace, while the current-tile
  selector remains unkeyed presentation state. The beta smoke edits Exposure on
  two tiles back-to-back, verifies Reset follows the selected tile, checks that
  Reset changes only that tile, then verifies Reset and both edits remain three
  independent Undo gestures. Workspace churn continues to check the other
  document/operation-state ownership boundaries.
- Subsequent complete stable-key migrations are Digital Capture (fifth), Contact Copy (sixth), Color (seventh), and Sharpness (eighth). Capture's derived Sensor width shares the pixel-pitch identity while its rotation assumption remains presentation-only. Contact Copy gives each H&D editing surface its own gesture identity. Color adds `color.*` keys for every saved editor, including custom whitepoint/tone-curve widgets. Correlated RGB controls expand their base key to per-channel `.red/.green/.blue` identities; Link channels and Color's area/chart-view controls remain deliberately unkeyed. Sharpness uses `sharpness.*` for saved MTF/deconvolution controls and target-specific `sharpness.measurements.<index>.*` identities for repeated measurement metadata rows; chart presentation, measurement navigation/actions, and focus-analysis setup remain unkeyed. Workspace smoke verifies both the complete Sharpness key set and that adjacent name edits on two measurements require two Undo steps.
- Prefer `QSignalBlocker` for temporary signal suppression. Central
  `ParameterPanel` synchronization and ordinary refresh/update paths now use
  scoped blockers throughout the parameter panels, document window and
  secondary image views. The remaining direct `blockSignals()` call is the
  intentional undo-stack teardown suppression, which is lifetime-specific.
- Finish the ordinary saved-checkbox Reset rollout without conflating it with
  session state. Contact Copy's simulation master, Sharpness Use measured MTF,
  and Image Layer Use simulated RGB image layer use fresh-`ParameterState`
  default/modified presentation. Resetting the Contact Copy master is a
  non-destructive bypass: subordinate curve and darkroom values remain stored.
  Geometry Auto fit/nonlinear-request and Sharpness focus-analyzer option
  checkboxes remain session/presentation state and intentionally have no saved
  default Reset.
- Numeric rows that opt into standard Reset share one centralized
  double-click gesture. `ParameterPanel::updateUI()` lazily discovers standard
  numeric Reset rows, installs the panel event filter on the form label and
  numeric value editor (including the spin box's internal editor), and invokes
  the same Reset action only while the row is modified and enabled. Enums,
  checkboxes, calibration-level Clear/Reset actions, and session/operation
  controls deliberately retain their ordinary double-click behavior.
- Keep generic numeric Reset scoped to independent document values. Contact Copy
  now opts Preflash, Enlarger exposure, and Density boost into the standard
  fresh-`ParameterState` default/modified presentation. Its H&D graph, manual
  coordinate points, Richards parameters, inverse mode, and presets all edit one
  coupled characteristic-curve calibration and intentionally do not get
  per-field generic Reset controls. The Film characteristics section instead
  provides one Reset characteristic curve action that replaces the whole curve
  with the fresh-ParameterState default in one undoable edit.
- Keep group folding as presentation state only: a collapsed/expanded section
  must compose with each row's logical applicability instead of overwriting it.
  `addSeparator()` now accepts an optional stable `sectionKey`, distinct from
  saved parameter/Undo identity. Screen, Image Layer, Color, Contact Copy, and
  Geometry opt into application-preference persistence; only user activation writes
  settings. New panels restore the last explicit choice, while existing panels
  retain their local
  presentation. Refresh re-applies folding to rows added after the header, and
  guarded callbacks tolerate UI rebuilds. The beta smoke covers recreation with
  renamed/repeated captions, key separation, unkeyed compatibility, dynamic rows,
  applicability, programmatic changes, and zero document setter calls. The next
  real-panel probe covers all five Color and four Contact Copy sections under
  an isolated settings identity, including multiple live panels and reopening.
  Color's Tone Curve wrapper now belongs to Final adjustments instead of the
  outer form, preventing that editor from escaping the section's fold. Nested
  spectral-chart applicability and simulation-dependent Contact Copy groups
  remain independent of the remembered presentation.
  Digital Capture now follows the same section contract with five groups:
  Source, Resolution and optics, Sensor, Spectral wavelengths, and Capture
  corrections. Its detected/EXIF metadata rows use the shared imperative
  row-applicability helper so a metadata refresh cannot reopen a collapsed
  section. Sharpness now gives stable keys to all eight existing sections.
  Numeric MTF/deconvolution controls opt into the same real-default metadata
  and Reset affordance as Digital Capture. Finetune and adaptive diagnostic
  rows keep derived availability separate from folding; starting live adaptive
  analysis marks its chart applicable without forcing the section open.
  Geometry's five sections now follow the same contract. Its fit-status labels
  belong inside Geometry fit rather than the outer form. Message presence and
  all four chart rows use registered applicability, including point batches
  and explicit chart refreshes between complete parameter refreshes. The shared
  `updateWidgetStates()` presentation-only helper avoids recursion into the
  derived refresh hook. Geometry's initial Auto fit checkbox synchronization is
  signal-blocked, so construction does not issue a redundant document setter.
  Real-panel smoke covers remembered/independent folds, point thresholds, lens
  coverage, missing and changing geometry, chart applicability, and no document
  edits or automatic fit requests. Existing chart detachment stays unchanged.
  Image Layer's infrared-only calibration action now uses
  `setParameterApplicability()` rather than a direct-visibility repair callback,
  and workspace-churn smoke covers collapse/expand resurrection explicitly.
- `MainWindow::OneShotOperation` now provides the first shared one-shot
  lifecycle: prerequisites, progress description, start/cleanup UI callbacks,
  replace-on-new-request cancellation plus TaskQueue publication ownership,
  final result validation and an apply callback. Area-based parameter
  computations (white balance, auto
  levels, image-layer calibration and slanted-edge measurement) were the first
  migrated users. Flat-field analysis now follows the same lifecycle and its
  former QObject/QThread/generation wrapper has been reduced to a synchronous
  background helper. Point-based focus analysis now uses the same lifecycle as
  well: the old QObject/QThread/generation wrapper is gone, and an exact
  image/`ParameterState` snapshot gate prevents stale MTF values from being
  applied after an unrelated edit. These operations publish only while their
  captured prerequisites are still current; any accepted document edit
  cancels them immediately. Workspace-churn smoke verifies that a racing
  cancelled completion performs cleanup but cannot publish. `TaskQueue::runAsync`
  now constructs its `QRunnable` fully before handing it to `QThreadPool`; this
  avoids QtConcurrent's inline construct-and-submit path, which newer TSan runs
  reported as a vptr construction/execution race. The worker closure is also
  published and taken under an explicit `std::mutex`: Qt's thread-pool submission
  is thread-safe, but TSan does not model it as a C++ happens-before edge for
  arbitrary runnable members. The explicit handoff prevents the pool thread from
  racing the submitting thread while a captured `std::function` is moved into
  place. Completion still uses a `QFutureWatcher` backed by `QPromise`, so
  destroying the owning queue safely disconnects GUI publication while the
  cooperative worker winds down.
  Single-area registration finetune now follows the same final-result lifecycle:
  its helper returns only points produced from the captured solver/render/geometry
  snapshot, and exact scan/`ParameterState` plus finetune-control validation
  prevents those points from being appended after an intervening edit or a
  change to the operation's spacing/tolerance controls. The progressive
  `FinetuneMisregisteredWorker` remains a dedicated `QThread` because its
  point and geometry batches are intentionally visible and undoable while it is
  still running. Full-image and selected-area discovery now share one launcher
  and one lifecycle state: generation, exact progress identity and source scan
  stay fixed, while `expectedState` advances before each accepted worker-owned
  point/geometry edit. Any other state change, Undo, image replacement or newer
  discovery request cancels the worker; queued batches, current-point callbacks
  and old completion paths all use the same ownership gate. Stop still preserves
  batches already accepted into ordinary Undo history. Adaptive sharpening uses
  the analogous *immutable* progressive ownership rule: generation, exact
  progress identity, source scan and full input snapshot gate every coarse/dense
  chart signal as well as the final correction. Starting another analysis or
  editing its inputs cancels the old request before queued cells can repaint the
  chart; abandoned live data are replaced by the accepted document correction.
  Keep migrating custom one-shot `QThread` workers incrementally where their
  intermediate signal requirements allow the same policy without obscuring the
  worker API. Final-result screen-type detection
  now follows this rule too: the detector owns its diagnostic screen map through
  RAII, exact scan/`ParameterState` validation gates worker completion, and the
  asynchronous dye-model confirmation remains tied to that same baseline. A
  document edit or newer final-result operation dismisses the obsolete prompt,
  preventing a valid old detection from being accepted into newer state. The
  former DetectScreen QObject/QThread generation wrapper and unused cached mesh
  member are therefore gone.
  Coordinate autodetection and local coordinate refinement now also use these
  snapshot/publication rules. Their shared persistent QObject/QThread and both
  manual request counters are removed. The known-screen Detect Screen path
  captures its optional automatic-point-finding continuation in the detection
  request rather than a mutable window flag, so cancellation or a newer direct
  coordinate request cannot inherit another request's continuation. Detection
  retains its dedicated Cancel row through an optional one-shot progress title.
  Refinement applies a copied state through `changeParameters()` before updating
  diagnostics; the old live-state mutation caused `changeParameters()` to see
  a no-op and omit the undo/dirty transition. Workspace-churn smoke checks the
  exact coordinate-edit Undo/Redo state and dedicated progress-row cleanup.
  Automatic focus-area discovery and multi-area fitting now also share that
  lifecycle, retaining their dedicated Cancel rows and input-model/validation
  settings. Their numerical work lives in `FocusAnalysisWorker`, with immutable
  inputs and structured missing-input/cancellation/exception results. Cancelled
  or stale runs cannot restore old candidate rectangles or partial diagnostics
  after a document edit. Successful joint fits still need explicit Apply, and
  the approval dialog remains bound to the original scan and full state until
  acceptance. Edits (even if later undone), image/parameter replacement, close,
  or a newer final-result request dismiss obsolete focus/detection approvals.
  Beta smoke covers missing/pre-cancelled helper inputs; workspace churn covers
  both production cancellation paths, late approval after input restoration,
  prompt supersession, task-row cleanup, and exact Apply/Undo/Redo state.
  Slanted-edge measurement in an external reference view now also enters the
  owning document's one-shot queue. The old independent watcher could restore
  a whole stale `ParameterState` and had no registered Cancel control. Reference
  batches now append only their measurements, after checking the source scan,
  reference scan, complete document snapshot and live request ownership. All
  requested channels must succeed before any are appended. Closing/reloading a
  reference cancels its own progress handle, not a newer document operation;
  old completions cannot reset a newer request's controls. The start callback
  exposes that request-local progress handle and the lifecycle entry point is
  shared with guarded secondary-view callers. Workspace churn exercises a real
  synthetic RGB edge, batch failure, Undo/Redo, stale-result rejection, reload,
  replacement, reference close and progress cleanup.
  Measured-MTF model fitting now completes this group of simple final-result
  migrations. Sharpness panels own only the editable setup dialog; the source
  `MainWindow` owns computation, one-shot cancellation, the dedicated Cancel
  row, Current/Stale/Failed provenance, result publication and the accepted
  undo edit. The setup dialog itself is snapshot-bound, so accepting controls
  copied from an older document state does not launch expensive obsolete work.
  The former panel-local `TaskQueue` and the reference-view destructor repair
  for abandoned fits are gone. An explicit request-progress identity prevents
  a cancelled old completion from clearing a newer fit after external parameter
  replacement, and a newer final-result operation cancels model fitting through
  the same queue. Workspace churn covers a real successful objective evaluation,
  exact Apply/Undo/Redo state, edit-and-restore cancellation, validator failure,
  stale dialog acceptance, supersession and progress-row cleanup.
  Geometry/color optimizer queues and workers that intentionally publish
  intermediate results retain their separate lifecycle contracts; do not force
  them through `OneShotOperation` merely to remove another queue.
- Workflow guidance now has a conservative navigation affordance: when
  `Next:` points to one specific inspector stage, **Open stage** resolves that
  destination through the stable semantic tab key and records the click as the
  user's preferred inspector panel. Recommendations that intentionally present a
  choice (for example RGB screen-colour detection versus Geometry), toolbar Mode
  changes, file loading, and active background operations remain text-only.
  Workspace churn checks both an ambiguous hidden-button case and real
  Screen-stage navigation/persistence.
- Separate "enabled" from "applicable/visible" in helper APIs.  Greyed controls
  are useful when they teach a prerequisite; hidden controls are useful when a
  whole concept is meaningless for the current process.  A lambda called
  `enabledCheck` should not silently mean different things in different helper
  functions.  `ParameterPanel::setParameterApplicability()` now establishes
  this distinction for form rows and composes it with section folding; Screen
  pattern rows and measured-MTF controls were the first users. Both checkbox
  helpers now follow the same enable-only `enabledCheck` contract as sliders,
  enums and buttons. Image Layer's native-vs-simulated source choice uses explicit
  row applicability instead of a direct `setVisible()` repair, while Geometry's
  final-mirror prerequisite remains visible and disabled until screen geometry
  exists. Lightweight and workspace smoke probes cover both checkbox helper
  variants, row applicability metadata, and the visible-disabled transition.
  Whole `addSeparator()` sections now use the same contract too: Image Layer's
  simulated-RGB mixer, all Contact Copy specialist sections, and Color's
  historical-process sections no longer maintain parallel direct-visibility
  repair paths. The nested Color spectral-chart row is explicitly applicable
  only to spectra-based dye models, while chart refresh only updates its data.
  Contact Copy schedules its histogram after applicability/folding updates so
  disabled or collapsed sections still avoid background work. Lightweight and
  workspace smoke tests verify that hiding a section does not overwrite its
  independent fold state and that the migrated real sections expose consistent
  `parameterApplicable` metadata. Remaining direct `setVisible()` calls in these
  panels are presentation/status mechanics rather than logical parameter rows.

### P2 — maintenance refactoring

- Split very large source files by responsibility rather than by arbitrary line
  count. `MainWindow` retains document-specific snapshots, UI decisions and
  publication. The first physical split is now complete:
  `MainWindowAnalysis.cpp` contains registration discovery, screen detection
  and recommendations, adaptive-sharpening analysis, and coordinate
  autodetection/refinement. `MainWindowDocument.cpp` now contains image and
  parameter open/save/recent handling, post-load setup, close/save policy,
  window persistence, recovery files, and transactional parameter loading.
  The same `MainWindow` object still owns all state and policy; these are
  physical responsibility splits, not new controller/ownership layers. Together
  they remove more than two thousand lines from the original translation unit.
  Stable replaceable one-shot queue/publication mechanics live in
  `OneShotOperationController`; transient/dedicated task presentation,
  switching, delayed visibility and row bookkeeping live in
  `DocumentProgressController`; accepted file-render jobs, future/watcher
  lifetime, cancellation identity and incomplete-output cleanup live in
  `FileRenderController`, which also joins active render workers and removes
  incomplete outputs during document teardown. Ad-hoc QThread workers that
  publish incremental results are tracked and joined by
  `BackgroundThreadRegistry`, including the BlockingQueuedConnection-safe
  MetaCall drain required by misregistered-point workers. MainWindow still owns
  workspace/focus policy plus save-path/render-settings dialogs and document
  snapshots. Continue moving cohesive infrastructure behind focused
  controllers/services when the interface is similarly stable.
  Keep secondary build manifests in lockstep with the maintained Automake
  executable manifest. The standalone Qt CMake target is now resynchronized
  with all current GUI sources, headers and resources (including split
  `MainWindow` translation units) and with the static libcolorscreen image-I/O
  dependencies it must link explicitly.
- Give long-lived analysis state explicit structs rather than parallel member
  variables. Profile-calibration, geometry-fit, measured-MTF fit, automatic
  multi-area focus analysis, progressive adaptive sharpening, and progressive
  registration discovery now each live in one lifecycle struct instead of
  parallel request/result/presentation members. Adaptive sharpening groups its
  generation, immutable baseline, scan, and progress identity because both live
  chart cells and the final correction share that ownership. Registration
  discovery groups generation, scan, progress identity and the evolving
  expected document state because accepted worker batches are themselves
  undoable edits. Reference views likewise group the mutex-published
  reference-load handoff and each reference-MTF request. Keep applying this
  pattern when another analysis has coupled session-local members that are
  always saved, cleared, or restored together.
- Keep linear/gamma/logarithmic slider conversions centralized in
  `SliderValueMapping`; do not reintroduce separate mapping formulas in stateful
  and stateless helpers.
- Production panel reach-through by `objectName` is now eliminated.
  `MainWindow` uses typed panel APIs; remaining `findChild`/`objectName`
  lookups are smoke-test introspection or `WorkspaceWindow::documentTabBar()`,
  where Qt exposes no typed `QMdiArea` tab-bar accessor. Keep `objectName`
  for stable automation/smoke identities, not production panel control.

## Robustness checklist for new features

### Parameters

- Is the value saved in the correct project/parameter format?
- Does loading old data give a documented default?
- Does changing it mark the document dirty?
- Is reset undoable?
- Does the UI update with signals blocked?
- Does it invalidate only the caches/render stages that depend on it?

### One-shot analyses

- Are prerequisites visible before starting?
- Is work outside the GUI thread?
- Is progress meaningful and cancellable?
- Is the request tied to an immutable input snapshot or generation?
- Is the result checked before use?
- Is applying the accepted result one undoable edit?

### Views and windows

- Does the feature work in the primary view, an ordinary New View, and a
  detached view where applicable?
- Does a specialized reference view intentionally support or reject it?
- Can closing a view destroy a widget still borrowed by the document inspector?
- Are deferred Qt deletions guarded with `QPointer` across event-loop turns?
- Does the shared workspace status bar continue to route to the active top-level
  presentation without changing document focus?

### File operations

- Does opening a second file create a second document rather than replacing an
  occupied one?
- Does the last presentation obey save vetoes?
- Does **File -> Exit** include detached presentations?
- Are recovery writes atomic and document-local?
- Can an interrupted save leave the previous usable parameter/project file in
  place?

## Tests that should form the beta gate

The beta gate should not depend only on algorithmic `make check`.  A useful
minimum is:

1. optimized Linux build and full `make check`;
2. `make distcheck`, including reconstruction from the produced tarball;
3. macOS and Windows production builds;
4. ASan/UBSan GUI smoke on supported platforms;
5. TSan/Archer coverage for Qt paths that the platform runtime supports;
6. the completion-driven multi-document workspace churn smoke;
7. save/load/recovery round trips for a representative parameter file;
8. a small fixture test for each nontrivial parameter-widget mapping;
9. manual workflow passes for at least one regular screen, one line screen and
   one RGB+IR or monochrome capture.

For manual beta checks, record not only whether the final image looks right but
whether the operator can tell what stage is being edited, why a control is
unavailable, whether an automatic operation is still running, and what Undo is
about to undo.

## Changes that should *not* be mixed into a beta-hardening patch

Avoid a simultaneous rewrite to a new widget toolkit/layout model, a new project
file format, a new rendering graph and a new undo architecture.  All may be
reasonable eventually, but changing them together destroys the value of the
existing sanitizer and smoke-test history.

Prefer incremental seams:

- first name processing stages and control semantics in documentation;
- then add common metadata/API to existing panels;
- then standardize one stage at a time;
- only then consider replacing `MultiLineTabWidget` or reorganizing the
  inspector shell if the remaining UX problem justifies it.

That path keeps the current application useful while making every iteration
more conventional and easier to maintain.
