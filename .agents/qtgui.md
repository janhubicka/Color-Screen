# Color-Screen: qtgui Developer Documentation

This document provides technical details for developing the Qt6-based graphical user interface for Color-Screen.

## Architecture Overview

The GUI is built using **Qt6** and follows a modular panel-based architecture.
Each document window (`MainWindow`) integrates the image view and functional
panels and is a frontend for `libcolorscreen`; `WorkspaceWindow` is the shared
application shell when documents are attached.

The application is essentially a special-purpose non-destructive image editor.
It uses Qt's standard multiple-document model. `ColorScreenApplication` owns
the document objects, `WorkspaceWindow` provides the application shell and a
`QMdiArea`, and each `MainWindow` owns one image (`m_scan`) and all mutable state
associated with that image. The MDI area uses tabbed view by default and can
switch to tiled or cascaded subwindows. A `MainWindow` may also be detached as
a top-level window; changing presentation must never copy or recreate document
state. Rendering and analysis may be slow, so every document keeps its own
background work and cancellation state.

### Key Components

- **`ColorScreenApplication`**: The application-level document manager. It creates and tracks `MainWindow` instances, routes open requests, manages tab/detached presentation, cycles between documents, and restores per-document crash-recovery sessions.
- **`WorkspaceWindow`**: The primary top-level application shell. Its central `QMdiArea` provides tabbed and subwindow views, while the shell temporarily presents the active document's menu bar, toolbar, and inspector. It owns presentation state only, never image-processing state.
- **`MainWindow`**: One complete image document. It owns the scan, parameters, undo stack, image/navigation widgets, panels, task queues, workers, progress entries, detached docks, and a unique recovery directory. Mutable document state must never be shared between different `MainWindow` instances.
- **`NavigationView`**: This shows the whole image and indicates zoom and position of ImageWidget.
It lets user to effectively move around the image
- **`ImageWidget`**: This displays the image and provides high-performance interaction. It uses a modular architecture for rendering and event handling to manage complex interaction modes (Pan, Select, SetCenter, etc.).
- **`ParameterPanel`**: The abstract base class for all UI panels (e.g., `CapturePanel`, `ColorPanel`, `SharpnessPanel`). It provides a rich set of helper methods to create consistent UI controls that are linked to the application state.
- **`ParameterState`**: A structured object containing all render and project-level parameters. It is used as the single source of truth for the UI.
- **`TaskQueue`**: Manages background worker threads. It utilizes multiple specialized queues (e.g., `m_renderQueue` for image tiles and `m_pointsQueue` for registration overlays) to ensure that heavy computations like point rendering do not block the main GUI thread or interfere with image tile generation.

### Standard Multiple-Document Workspace

Opening an image must not replace an occupied document. All entry points—File
Open (including multi-selection), recent files, positional command-line paths,
and `QFileOpenEvent` from the desktop—route through
`ColorScreenApplication::openFiles()`. The first path may reuse an untouched
empty document; every other path receives a new `MainWindow` and is attached
to the primary workspace by default.

The document boundary is intentionally the entire `MainWindow`. In particular,
each document has independent:

- `image_data`, render/detection/geometry/solver parameters, profile spots, and
  render mode;
- `QUndoStack`, selection/tool state, panels, navigation, and detached docks;
- worker objects, `TaskQueue` instances, progress entries, and render
  cancellation state;
- current image/parameter filenames and a UUID-named recovery directory.

Workspace geometry and recent-file lists remain application preferences in
`QSettings`; they are not document state. Detached-document geometry is also
presentation state. Recent lists are persisted when they change so a
later-closing document cannot overwrite newer entries.

`WorkspaceWindow` must use `QMdiArea`, rather than maintaining a parallel custom
tab implementation. The default view is `QMdiArea::TabbedView`; its internal
`QTabBar` has `autoHide` enabled so no tab strip is shown for a single document.
Tabbed MDI subwindows must remain maximized: an active tab represents the whole
document viewport and must never expose a nested `QMdiSubWindow` frame.
The workspace is a `QMainWindow`: the active document toolbar is installed in
its top toolbar area, therefore document tabs naturally appear below it. The
active document's navigation/parameter column is shown in the shared inspector
dock. The workspace also owns the one visible status bar. The active document's
progress controls are temporarily moved into it and that document's status
messages are mirrored there; inactive documents keep their private status state
hidden until activated. Inactive documents retain their complete processing and
undo state.

**Window → Arrange Images** switches the same live documents between tabbed,
tiled, and cascaded MDI views using `setViewMode()`, `tileSubWindows()`, and
`cascadeSubWindows()`. Keep `QMdiArea::activationOrder` at Qt's default
`CreationOrder`: Qt also uses this order when it tiles and cascades windows, so
`ActivationHistoryOrder` makes spatial placement depend on focus history. A
click in tiled/cascaded view changes only the active document; it must never
move, swap, or re-tile document windows. Color-Screen implements document
cycling separately and does not need MDI activation history for Ctrl+Tab.
Detaching removes the existing `MainWindow` from the MDI
area and reparents that same object as a top-level window; reattaching performs
the inverse operation. Never serialize, clone, or reconstruct a document merely
to change its presentation. Tabs are movable and closable, can be dragged out,
and also support double-click/context-menu detachment. `Ctrl+Tab` and
`Ctrl+Shift+Tab` cycle all documents, whether attached or detached. `Ctrl+N`
creates a new workspace document and `Ctrl+Shift+N` creates a detached empty
window.

`MainWindow` remains the ownership boundary. When embedded, only presentation
widgets are temporarily reparented: its menu actions and toolbar are surfaced by
the workspace and its inspector is placed in the shared inspector stack. Before
detachment or destruction these widgets must be returned to the same
`MainWindow`, so ordinary state saving and teardown continue to work.

Sharpness calibration may use a **slanted-edge reference view**. This is a
specialized `ImageViewWindow` which owns a separately loaded reference scan but
uses the source `MainWindow` as the authoritative parameter/undo/recovery
model. It never owns or suggests another `.par` file. Its inspector keeps the
standard `NavigationView` above a single **Sharpness** tab, and rendering is
limited to **Original digital capture** and **Image layer**. Measurements are
applied through the source document's undoable parameter path. **Reload and
demosaic** reloads both the source scan and every associated slanted-edge
reference from its own filename using the current demosaic mode.

**Window → New View** creates another MDI view of the same document. Ordinary
views present the same complete Navigation + parameter-panel inspector as the
primary view; the document owns one inspector instance and the active ordinary
view merely presents it, so panel state, undo routing, and detached diagnostic
widgets are never duplicated. Navigation and panel tools that act on an image
(crop/area selection, measurement, geometry visualization, and registration
interaction) target the ordinary view currently presenting the inspector.
Detached ordinary views host that same inspector locally while active and return
it to the workspace/primary window on activation changes. Views share the image
and document transforms such as rotation and mirroring, while render mode,
Color/IR choice, zoom, and pan remain view-local. Slanted-edge reference views
remain intentionally specialized and keep their separate Navigation +
Sharpness-only inspector.

Top-level menus follow the conventional order **File, Edit, View,
Registration, Window, Help**. `Window` is the final working menu and owns MDI
arrangement/navigation plus the document list; `Help` is always last and holds
application-wide help/about actions. Keep this order when adding new menus.

Crash recovery is session-aware. `ColorScreenApplication` prompts once and
restores one `MainWindow` per recovery directory. Each `MainWindow` writes and
removes only its own payload, so closing one image cannot erase another image's
recovery state. Slanted-edge reference filenames are stored in the owning
document's recovery directory and recreated as attached specialized reference
views after that document restores. Ordinary New Views, detached/attached
presentation, zoom, pan, Color/IR selection, and view-local render modes are
deliberately not recovery state. Reference metadata is rewritten atomically and
is not rewritten while a saved list is being replayed, so reopening the first
reference cannot truncate later entries. The legacy single-document recovery
slot is migrated without removing its source files until the complete copy
succeeds.

---

## Coding Style

The `qtgui` component follows a **Qt-like style**:
- **File Extensions**: Use `.cpp` and `.h`.
- **Naming**: Use `CamelCase` for classes and `camelCase` for methods and variables.
- **Signals/Slots**: Use the modern `connect()` syntax with lambdas or member function pointers.
- **Documentation**: Document methods in the header files using standard Doxygen-style comments.
- **Implementation Comments**: Each method implementation should have a comment block explaining its purpose and parameters in the implementation file.

---

## Creating a New Panel

To create a new panel, inherit from `ParameterPanel`. This base class simplifies UI construction and state synchronization.

### 1. Basic Structure

```cpp
class MyNewPanel : public ParameterPanel {
    Q_OBJECT
public:
    MyNewPanel(StateGetter stateGetter, StateSetter stateSetter, 
               ImageGetter imageGetter, QWidget *parent = nullptr)
        : ParameterPanel(stateGetter, stateSetter, imageGetter, parent) {
        setupUi();
    }

private:
    void setupUi() {
        // Add controls here
    }
};
```

### 2. Adding Controls

Use the protected helper methods in `ParameterPanel` to add widgets. These methods automatically handle:
- Creating labels and layout rows.
- Synchronizing the UI with the application state.
- Providing "Undo/Redo" support via the `description` parameter in `applyChange`.

Example: Adding a slider for a double value.

```cpp
addSliderParameter(
    "My Parameter", 0.0, 100.0, 10.0, 2, "units", "default",
    [](const ParameterState &s) { return s.myValue; },
    [](ParameterState &s, double v) { s.myValue = v; },
    1.0,      // Gamma (non-linear distribution)
    nullptr,  // Enabled check lambda
    false,    // Logarithmic scale
    "This parameter controls X." // Tooltip
);
```

### 3. State Management

- **Getter Lambda**: Returns the current value from the `ParameterState`.
- **Setter Lambda**: Modifies the `ParameterState` with the new value.
- **`applyChange`**: Behind the scenes, the helper methods call `applyChange`, which triggers a global state update and UI refresh.

---

## Background Tasks and Concurrency

Never perform heavy computations in the UI thread. The `qtgui` component uses three distinct concurrency patterns based on task lifecycle:

### 1. Progressive Updates (TaskQueue)
The `TaskQueue` implements a **two-task scheme** for long-running computations where progressive results are desirable.
- **When to Use**: Situations where a user changes parameters rapidly (e.g., dragging a slider) and needs visual feedback as soon as possible, but it is acceptable to cancel an in-flight computation if even fresher parameters arrive.
- **Examples**: Image tile rendering, registration point overlays, geometry solver, color optimizer.
- **Behavior**: New requests automatically cancel or supersede pending/active tasks in the same queue.

### 2. One-Shot Cancellable Tasks
Tasks that run in the background and report a final result (or series of intermediate results).
- **When to Use**: Computations that should work with the freshest data; if the data changes significantly, the old task should be cancelled and a new one started.
- **Implementation**: Can use `QThread + moveToThread` (if custom signal/slot communication is needed) or `QtConcurrent::run` (for simple functional tasks).
- **Examples**: FinetuneWorker, DetectScreenWorker, FlatFieldWorker, FocusAnalysisWorker, AdaptiveSharpeningWorker, area-based computations (white balance, auto levels).
- **Behavior**: Tracked via `progress_info` for manual or automatic cancellation.

### 3. Independent Exports (Render to File)
- **When to Use**: Tasks that are independent of ongoing UI parameter tweaks once started and should run to completion.
- **Examples**: `onRender()` (rendering to a final file).
- **Behavior**: Not automatically cancelled by state changes; usually requires explicit user confirmation to abort.

---

### Implementation Details

Inherit from `WorkerBase` or `QObject` to implement a specific task. Use `QThread` to move the worker off the main thread.

```cpp
class MyWorker : public QObject {
    Q_OBJECT
public:
    void run() {
        while (!m_progress->cancelled()) {
            // Perform chunk of work...
            emit partialResultReady(data);
            
            // Check throttling logic here
            if (shouldUpdateGui()) {
                emit progressUpdated(m_progress);
            }
        }
        emit finished(true);
    }
};
```

### 2. TaskQueue and runAsync

The `TaskQueue` coordinates when tasks run and provides a specialized `runAsync` API for non-blocking operations that need to return results to the GUI thread.

#### runAsync Pattern
This pattern is ideal for tasks like rendering overlays or performing quick background math:

```cpp
m_pointsQueue.runAsync(
    [=](colorscreen::progress_info *p) mutable {
        // Worker code - Runs on a background thread
        // e.g., Render 10,000 points into a QImage
        return result; 
    },
    [this](ResultType result) {
        // Done callback - Runs on the GUI thread
        // Safely update UI or store results
        update();
    }
);
```

#### Dual-Queue Architecture
To prevent UI "stutters" during complex interactions, `ImageWidget` uses two parallel queues:
- **`m_renderQueue`**: Handles the heavy lifting of image tile rendering and demosaicing.
- **`m_pointsQueue`**: Handles registration point overlays and simulated position updates.

Running these in separate queues ensures that the registration points can be re-rendered instantly (e.g., during a drag or zoom) without waiting for the main image render to catch up.

### 3. Iterative Workers and Throttling

When a worker provides incremental updates (e.g., finding registration points in a loop), follow these best practices to maintain UI responsiveness:

- **Signal Throttling**: Avoid sending signals for every tiny change. Implement a throttling mechanism based on time (e.g., every 5 seconds) or progress percentage (e.g., 10% change).
- **Partial Updates**: Workers should emit signals like `pointsReady` or `intermediateResult` while continuing their work.
- **Final Sync**: Always emit a final update when the worker finishes to ensure no pending data is lost.
- **Undo Integration**: Each emitted signal that modifies the `ParameterState` should be pushed to the `QUndoStack` in `MainWindow`.

### 4. Cancellation

Workers must periodically check `m_progress->cancelled()` and exit gracefully. In `MainWindow`, ensure the worker's thread is tracked so it can be requested to stop when the user clicks "Cancel" in the progress bar.

---

## Viewport and Coordinate Systems

The `ImageWidget` handles mapping between three main coordinate systems:
1.  **Scan Coordinates**: Raw pixel indices of the input image.
2.  **Transformed Coordinates**: Coordinates after rotation, cropping, and mirroring.
3.  **Widget Coordinates**: Screen pixels relative to the `ImageWidget` top-left.

### Non-Blocking Overlay Rendering

To handle thousands of registration points without blocking the UI, `ImageWidget` uses a **Composite Overlay Model** paired with **Visibility Culling**:

- **Pre-rendered Overlay**: Registration points are rendered into a `QImage` in the background via `TaskQueue`.
- **Compositing**: The `paintEvent` draws this `QImage` on top of the main pixmap using a simple bit-blit or scaled draw.
- **Interpolation**: While a new overlay is being rendered, the current overlay is stretched in real-time.
- **Style**: Registration points use a distinct style and are culled if too close to each other.
- **Visibility Culling**: Always check visibility flags (e.g., `m_showRegistrationPoints`) before scheduling background tasks. If visibility is toggled off, cancel any active background rendering tasks (`m_pointsQueue.cancelAll()`) to save CPU resources.

---

## Modular Event Handling in ImageWidget

To maintain a clean and extensible viewer, monolithic event handlers must be decomposed into specific helper methods.

### 1. Drawing Helpers (`paintEvent`)
The `paintEvent` should only coordinate high-level drawing. Specific overlay logic belongs in `draw...` methods:
- `drawPointsOverlay(QPainter &p)`
- `drawProfileSpots(QPainter &p)`
- `drawScreenCoordinateSystem(QPainter &p)`
- `drawMeasurement(QPainter &p)`

### 2. Interaction Handlers (`mouse...Event`)
Mouse interaction logic is delegated based on `InteractionMode`. This ensures that complex tools like `SetCenterMode` do not pollute the core `PanMode` logic.

**Naming Convention:** `handle[Mode][Event]`
- `handleSetCenterPress(QMouseEvent *event)`
- `handleSelectMove(QMouseEvent *event)`
- `handleAreaRelease(QMouseEvent *event)`

**Implementation Rules:**
- Keep the main event handler (`mousePressEvent`) as a simple switch/if-else block.
- Always accept or ignore events appropriately in the handlers to maintain event propagation.
- Grab/Release the mouse explicitly in handlers that require persistent dragging (e.g., coordinate axis manipulation).

---

## Smooth Transitions

Use `smoothFitToView()` or `smoothZoomTo()` for automated view changes. These rely on a high-frequency `exploreTick` timer.

- **`m_panAnimationActive`**: Set this flag when you want the view to smoothly pan towards `m_exploreTargetX/Y`. 
- **`m_zoomFocusCenter`**: When true, the view zooms towards the center of the screen.
- **Absolute Targets**: Always set `m_exploreTargetX/Y` to absolute image coordinates to ensure the animation converges correctly even during concurrent zooming.

### Interaction Modes and Tool State Management

The application supports several interaction modes (e.g., Pan, Select, Add Point, Set Center, Crop). Some of these are "persistent" (selected from the toolbar) while others are "temporary" (triggered by a specific action and returning to the previous tool once finished).

#### Persistent vs. Temporary Modes
- **Persistent Tools**: Pan, Select, Add Point, Set Center. These are managed via `QActionGroup` in the toolbar.
- **Temporary Modes**: `CropMode`, `GenericAreaMode`. These are usually triggered by buttons in panels (e.g., "Set by neutral area" or "Crop").

#### State Persistence Logic
To maintain a smooth user workflow, `MainWindow` tracks the user's active tool using `saveInteractionMode()` and `restoreInteractionMode()`:
- **`saveInteractionMode()`**: Captures the current mode before entering a temporary mode. It avoids saving `GenericAreaMode` to prevent state corruption.
- **`restoreInteractionMode()`**: Returns to the previously saved mode. Crucially, it synchronizes the toolbar state by updating the `checked` property of the corresponding `QAction`.

#### Toolbar Synchronization
The UI uses a **signal-based synchronization** pattern. `MainWindow` connects to `ImageWidget::interactionModeChanged` to ensure that any programmatic change to the interaction mode is reflected in the toolbar. 
- When the mode changes, `MainWindow` blocks signals on the toolbar actions, updates their `checked` state, and then unblocks signals to avoid infinite recursion.

---

## Undo/Redo Architecture

The application uses a centralized `QUndoStack` in `MainWindow` to track all parameter changes.

### 1. applyChange Helper
Most state modifications should go through `ParameterPanel::applyChange`. This method:
1. Captures the current `ParameterState`.
2. Applies a modifier lambda to create the new state.
3. Passes the new state and a human-readable description to `MainWindow::m_stateSetter`.
4. `MainWindow` then creates a `SetParametersCommand` and pushes it to the undo stack.

### 2. Command Merging
To prevent the undo stack from becoming bloated by slider movements, the `SetParametersCommand` supports merging. Rapid successive changes with the same description (e.g., "Adjust Gamma") are merged into a single undo step.

---

## Progress and Status Bar

`MainWindow` provides a multi-tasking progress tracking system integrated into the status bar.

### 1. Registering Progress
When starting a background task, create a `std::shared_ptr<colorscreen::progress_info>` and pass it to `MainWindow::addProgress()`.
- **Progress Container**: The status bar displays a progress bar, a status label, and navigation buttons (`Prev`/`Next`) if multiple tasks are active.
- **Auto-Selection**: By default, the status bar shows the task with the lowest progress percentage (longest remaining time).
- **Manual Override**: Users can cycle through active tasks using the arrows in the status bar.

### 2. Cancellation
Clicking the "X" button in the progress container calls `onCancelClicked()`, which triggers `progress_info::cancel()`. Workers must check this flag to exit safely.

---

## Custom Panels and Detachable Sections
If a panel contains a complex widget (like a chart) that a user might want to see in a separate window or dock, use `createDetachableSection`.

```cpp
m_chart = new MyChartWidget();
QWidget *section = createDetachableSection("Chart View", m_chart, [this]() {
    emit chartDetached(m_chart);
});
m_form->addRow(section);
```

The parent panel should implement a "reattach" mechanism to handle when the user closes the dock widget.

## Widget Visibility Implementation

To handle optional UI sections that should only appear when data is available (e.g., diagnostic images or analysis charts), follow this pattern:

1. **Wrap in a Container**: Wrap the detachable section in a `QWidget` wrapper with a `QVBoxLayout`.
2. **Initial Hide**: Hide the wrapper in `setupUi`.
3. **Add to Row**: Add the wrapper (not the internal widget) to the form row.
4. **Conditional Show**: Show the wrapper when data becomes available (e.g., in `onParametersRefreshed` or specialized update methods).

Example:
```cpp
// In setupUi
m_wrapper = new QWidget();
m_container = new QVBoxLayout(m_wrapper);
m_container->setContentsMargins(0, 0, 0, 0);

QWidget *detachable = createDetachableSection("Title", m_contentWidget, [this](){ ... });
m_container->addWidget(detachable);
m_wrapper->hide();
m_form->addRow(m_wrapper);

// In update method
void MyPanel::updateData(const Data &data) {
    m_contentWidget->setData(data);
    m_wrapper->show();
}
```


---

## UI Guidelines

1. **Aesthetics**: Use subtle gradients and consistent spacing. Avoid raw Qt default looks where possible (e.g., use `ParameterPanel`'s consistent layout).
2. **Responsiveness**: Always use background workers for any task taking > 50ms.
3. **Helpfulness**: Always provide tooltips for parameters using the `tooltip` argument in `ParameterPanel` helpers.
4. **Validation**: Use the `enabledCheck` lambdas to disable controls that are not applicable in the current state.

---

## Maintaining UI Integrity and Encapsulation

To ensure the codebase remains maintainable as the number of panels and UI variants grows, follow these architectural principles:

### 1. Enforce Panel Encapsulation
Panels should be the "source of truth" for their own internal widget states. 
- **Avoid `findChild`**: `MainWindow` should ideally not reach into panels using `findChild<T>("objectName")` to call `setEnabled()` or `setVisible()`.
- **Prefer Public API**: If `MainWindow` needs to trigger a state update in a panel, call a public method (e.g., `updateRegistrationPointInfo()`).
- **Internal Logic**: Logic for disabling a checkbox based on point counts should live inside the panel, usually triggered by `onParametersRefreshed`.

### 2. Centralize Logic Thresholds
Never hardcode business logic constants (like "3 points needed for optimization") in the UI layer.
- **Library as Source**: Always use static methods from `libcolorscreen` (e.g., `colorscreen::solver_parameters::min_points(type)`) to determine thresholds. Suggest updates to libcolorsreen API in the plan.

### 3. Standardized Object Naming
When `findChild` is unavoidable (e.g., for global shortcuts or synchronization between distinct UI modules):
- **Document Names**: Use a consistent naming convention (e.g., `lowerCamelCase` with a descriptive suffix like `Box` or `Btn`).
- **Sync across Variants**: Ensure all variants of a panel (e.g., `flp/GeometryPanel` and `GeometryPanel`) use the exact same `objectName` for corresponding controls.

### 4. Explicit Feedback
When a control is disabled due to missing data (like registration points):
- **Explain Why**: Instead of just graying out the control, provide a companion `QLabel` explaining the requirement (e.g., "5 additional points needed for lens correction").
- **Real-time Updates**: Ensure these labels update immediately as the state changes (e.g., as the user adds points in the viewer).

### 5. Favor Composition over Duplication
Avoid code duplication by extracting shared UI patterns into helper methods in `MainWindow`.

### 6. Use Centralized Helpers
To maintain consistency across different UI actions, use the following standardized helpers in `MainWindow`:
- **`runAreaComputation()`**: Use this for any task that involves: 
    1. Status bar instruction.
    2. Area selection on the image.
    3. Background computation with progress tracking.
    4. Pushing an undoable parameter change.
- **`loadParameterFile()`**: Use this for all parameter loading operations (from dialogs, recent files, or drag-and-drop). It ensures consistent state reset, UI refresh, and undo history management.

### 7. Documentation
- **Document function**: Add block comments to functions using Doxygen-style (`/** ... */`) to allow for automated documentation generation.
- **Document design decisions**: Keep comments in the source which helps later understanding of the design of individual parts.
- **Sync with .agents**: Ensure that any major architectural changes (like new threading patterns or global helpers) are reflected in this document.
