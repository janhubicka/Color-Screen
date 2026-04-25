# Color-Screen: qtgui Developer Documentation

This document provides technical details for developing the Qt6-based graphical user interface for Color-Screen.

## Architecture Overview

The GUI is built using **Qt6** and follows a modular panel-based architecture. The main window (`MainWindow`) serves as a shell that integrates multiple functional panels.

### Key Components

- **`MainWindow`**: The top-level window. It manages the lifecycle of the image data, the TaskQueue, and connects signals between panels and the main application logic.
- **`ParameterPanel`**: The abstract base class for all UI panels (e.g., `CapturePanel`, `ColorPanel`, `SharpnessPanel`). It provides a rich set of helper methods to create consistent UI controls that are linked to the application state.
- **`ParameterState`**: A structured object containing all render and project-level parameters. It is used as the single source of truth for the UI.
- **`TaskQueue`**: Manages background worker threads to ensure the UI remains responsive during heavy computations like rendering or optimization.

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

### 2. Requesting a Task via TaskQueue

The `TaskQueue` coordinates when tasks run.

```cpp
// In MainWindow or a controller
taskQueue->requestRender(renderParams, priority);
```

The `TaskQueue` emits signals like `triggerRender` or `progressStarted`, which `MainWindow` listens to to spawn the actual worker threads (usually managed via `QThread`).

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

---

## UI Guidelines

1. **Aesthetics**: Use subtle gradients and consistent spacing. Avoid raw Qt default looks where possible (e.g., use `ParameterPanel`'s consistent layout).
2. **Responsiveness**: Always use background workers for any task taking > 50ms.
3. **Helpfulness**: Always provide tooltips for parameters using the `tooltip` argument in `ParameterPanel` helpers.
4. **Validation**: Use the `enabledCheck` lambdas to disable controls that are not applicable in the current state.
