# Color-Screen: qtgui Developer Documentation

This document provides technical details for developing the Qt6-based graphical user interface for Color-Screen.

## Architecture Overview

The GUI is built using **Qt6** and follows a modular panel-based architecture.
The main window (`MainWindow`) serves as a shell that integrates multiple
functional panels.  It is a frontend for `libcolorscreen`.

The application is essentially a special purpose non-destructive image editor.
Main functionality is loading an image `m_scan` along with parameters saved in
`ParameterState`. Main window consists of standard menu, toolbar, statusbar and
three main components: `ImageWIdget`, `Navigationview` and Panels. The status
bar is integrated with libcolorscreen's `progress_info` which makes it possible
to visualise progress on tasks and subtasks as well as cancel individual tasks.
Rendering and analysis may be slow and it is important to keep GUI responsive.

### Key Components

- **`MainWindow`**: The top-level window. It manages the lifecycle of the image data, the TaskQueue, and connects signals between panels and the main application logic.
- **`NavigationView`**: This shows the whole image and indicates zoom and position of ImageWidget.
It lets user to effectively move around the image
- **`ImageWidget`**: This displays the image and lets user to quickly pan and zoom in it,
edit control points, select areas etc.
- **`ParameterPanel`**: The abstract base class for all UI panels (e.g., `CapturePanel`, `ColorPanel`, `SharpnessPanel`). It provides a rich set of helper methods to create consistent UI controls that are linked to the application state.
- **`ParameterState`**: A structured object containing all render and project-level parameters. It is used as the single source of truth for the UI.
- **`TaskQueue`**: Manages background worker threads. It utilizes multiple specialized queues (e.g., `m_renderQueue` for image tiles and `m_pointsQueue` for registration overlays) to ensure that heavy computations like point rendering do not block the main GUI thread or interfere with image tile generation.

---

## Coding Style

The `qtgui` component follows a **Qt-like style**:
- **File Extensions**: Use `.cpp` and `.h`.
- **Naming**: Use `CamelCase` for classes and `camelCase` for methods and variables.
- **Signals/Slots**: Use the modern `connect()` syntax with lambdas or member function pointers.
- **Documentation**: Document methods in the header files using standard Doxygen-style comments.

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

Never perform heavy computations in the UI thread. Use the `TaskQueue` and `WorkerBase` system.
`TaskQueue` should be used when running multiple threads is desired for user response when parameters are
changed rapidly (such as rendering the image). While ohter tasks that should be run only once can be
handled separately.

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

To handle thousands of registration points without blocking the UI, `ImageWidget` uses a **Composite Overlay Model**:

- **Pre-rendered Overlay**: Registration points are rendered into a `QImage` in the background.
- **Compositing**: The `paintEvent` simply draws this `QImage` on top of the main pixmap.
- **Interpolation**: While a new overlay is being rendered (e.g., during a zoom), the current overlay is stretched or translated in real-time to maintain visual alignment, providing 60fps feedback even while the background compute is catching up.
- **Style**: Registration points use a distinct style (Source: circle outline, Target: filled disk) and are automatically culled if they are too close to each other to reduce visual noise.

### Smooth Transitions

Use `smoothFitToView()` or `smoothZoomTo()` for automated view changes.

- **`m_panAnimationActive`**: Set this flag when you want the view to smoothly pan towards `m_exploreTargetX/Y`. 
- **`m_zoomFocusCenter`**: When true, the view zooms towards the center of the screen.
- **Absolute Targets**: Always set `m_exploreTargetX/Y` to absolute image coordinates to ensure the animation converges correctly even during concurrent zooming.

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
Avoid code duplication

### 6. Documentation
- **Document function**: Add block comments to function
- **Document design decisions**: Keep comments in the source which helps later understanding of the design of inidvidual parts.
