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

### 1. Implementing a Worker

Inherit from `WorkerBase` to implement a specific task.

```cpp
class MyWorker : public colorscreen::WorkerBase {
    Q_OBJECT
public:
    void run() override {
        // Perform computation...
        // Use reportProgress(fraction, message)
        emit finished();
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

---

## Detachable Sections

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
