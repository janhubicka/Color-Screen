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

### Profile auto-optimization retriggered on every state refresh

`ProfilePanel::onParametersRefreshed()` said it auto-triggered when profile spots
changed, but it only tested that Auto optimize was enabled and at least one spot
existed.  Applying an optimizer result changes render parameters, which refreshes
the panel, so the result could immediately request another optimization even
though the spots were unchanged.  Other unrelated edits could do the same.

The panel now snapshots the profile-spot coordinates and auto-runs only when the
spot set actually changes.  Manual **Optimize color** remains unchanged.

## Panel framework cleanup priorities

`ParameterPanel` is high-leverage code: a small mistake in a helper is copied to
many screens.  Changes here deserve focused tests before broad visual cleanup.

### P0 — before beta

- Keep `EXTRA_DIST` and generated build metadata checked automatically.
- Keep sanitizer GUI smoke coverage for document/view/dock lifetime churn.
- Add a focused test for undo merge identity (same-control drag merges;
  different controls do not).
- Add a focused test for `MultiLineTabWidget` visibility while an ancestor is
  hidden.
- Ensure every one-shot worker completion is request/generation checked before
  applying state.
- Audit modal dialogs for parent destruction and avoid stack-owned child QObjects
  entering nested event loops.

### P1 — early beta

- Give every stateful parameter helper an explicit machine-readable key.  Use
  that key for undo merge identity, optional reset/bypass state and diagnostics.
- Replace ad-hoc `blockSignals(true/false)` pairs with `QSignalBlocker`, reducing
  paths that accidentally leave a widget blocked after an early return.
- Make group visibility semantics explicit.  A collapsed group should not force
  a child visible when that child is logically unavailable for the current
  process or image type.
- Introduce small helper objects for one-shot operations: prerequisites,
  description, progress/cancel policy, result validation and apply callback.
  Many current panel buttons repeat this lifecycle.
- Separate "enabled" from "applicable/visible" in helper APIs.  Greyed controls
  are useful when they teach a prerequisite; hidden controls are useful when a
  whole concept is meaningless for the current process.  A lambda called
  `enabledCheck` should not silently mean different things in different helper
  functions.

### P2 — maintenance refactoring

- Split very large source files by responsibility rather than by arbitrary line
  count.  `MainWindow` should retain document coordination, while operation
  implementations can move into focused controllers/services once interfaces
  are stable.
- Give long-lived analysis state explicit structs rather than parallel member
  variables.
- Consolidate repeated slider value mapping in one tested utility instead of
  duplicating linear/gamma/logarithmic conversions in stateful and stateless
  slider helpers.
- Replace remaining UI lookups by `objectName` with typed panel APIs whenever
  practical.

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
