from pathlib import Path
import re

ROOT = Path("target")


def read(path):
    return (ROOT / path).read_text()


def write(path, text):
    (ROOT / path).write_text(text)


def replace_exact(path, old, new, expected=1):
    text = read(path)
    count = text.count(old)
    if count != expected:
        raise RuntimeError(f"{path}: expected {expected} copies, found {count}: {old[:100]!r}")
    write(path, text.replace(old, new))


def replace_region(path, start_marker, end_marker, old, new, expected=1):
    text = read(path)
    start = text.index(start_marker)
    end = text.index(end_marker, start)
    region = text[start:end]
    count = region.count(old)
    if count != expected:
        raise RuntimeError(
            f"{path}: region {start_marker!r}: expected {expected} copies, found {count}: {old[:100]!r}"
        )
    region = region.replace(old, new)
    write(path, text[:start] + region + text[end:])


def regex_sub(path, pattern, replacement, expected=None, flags=0):
    text = read(path)
    updated, count = re.subn(pattern, replacement, text, flags=flags)
    if expected is not None and count != expected:
        raise RuntimeError(f"{path}: expected {expected} regex replacements, found {count}: {pattern}")
    if expected is None and count == 0:
        raise RuntimeError(f"{path}: regex made no replacements: {pattern}")
    write(path, updated)
    return count


# ---------------------------------------------------------------------------
# ParameterPanel API. Keys are stable parameter identity, not display text.
# ---------------------------------------------------------------------------
path = "src/qtgui/ParameterPanel.h"
replace_exact(
    path,
    "  using StateSetter = std::function<void(const ParameterState &, const QString &)>;",
    "  using StateSetter = std::function<void(const ParameterState &, const QString &,\n"
    "                                         const QString &)>;",
)
replace_exact(
    path,
    "  /*\n    Adds a double parameter row (SpinBox + Optional Combo).\n  */",
    "  /*\n    Adds a double parameter row (SpinBox + Optional Combo).\n\n"
    "    PARAMETERKEY is a stable untranslated identifier for the logical\n"
    "    parameter. Stateful helpers store it as widget metadata and use it as\n"
    "    undo merge identity. Leave it empty while incrementally migrating old\n"
    "    call sites; those retain the historical label-based merge behavior.\n"
    "  */",
)
replace_region(
    path,
    "  void addDoubleParameter(",
    "  /*\n    Adds a slider parameter row (Slider + SpinBox).",
    "                          const QString &tooltip = QString());",
    "                          const QString &tooltip = QString(),\n"
    "                          const QString &parameterKey = QString());",
)
replace_region(
    path,
    "  QWidget *addSliderParameter(",
    "  /*\n    Adds a slider parameter row (Slider + SpinBox) that does not participate in state.",
    "      bool logarithmic = false, const QString &tooltip = QString());",
    "      bool logarithmic = false, const QString &tooltip = QString(),\n"
    "      const QString &parameterKey = QString());",
)
replace_region(
    path,
    "  QComboBox *addEnumParameter(\n      const QString &label, const std::map<int, QString> &options,",
    "  template <typename T>",
    "      const QString &tooltip = QString());",
    "      const QString &tooltip = QString(),\n"
    "      const QString &parameterKey = QString());",
)
replace_region(
    path,
    "  template <typename T>",
    "  template <typename EnumType",
    "      const QString &tooltip = QString()) {",
    "      const QString &tooltip = QString(),\n"
    "      const QString &parameterKey = QString()) {",
)
replace_exact(
    path,
    "    QComboBox *combo = addEnumParameter(label, options, getter, setter, enabledCheck, tooltip);",
    "    QComboBox *combo = addEnumParameter(label, options, getter, setter,\n"
    "                                        enabledCheck, tooltip, parameterKey);",
)
replace_region(
    path,
    "  template <typename EnumType",
    "  QCheckBox *addCheckboxParameter(",
    "      const QString &tooltip = QString()) {",
    "      const QString &tooltip = QString(),\n"
    "      const QString &parameterKey = QString()) {",
)
replace_exact(
    path,
    "    return addEnumParameter(label, Names, Max, getter, setter, enabledCheck, tooltip);",
    "    return addEnumParameter(label, Names, Max, getter, setter, enabledCheck,\n"
    "                            tooltip, parameterKey);",
)
replace_region(
    path,
    "  QCheckBox *addCheckboxParameter(",
    "  QCheckBox *addCheckboxWithReset(",
    "      const QString &tooltip = QString());",
    "      const QString &tooltip = QString(),\n"
    "      const QString &parameterKey = QString());",
)
replace_region(
    path,
    "  QCheckBox *addCheckboxWithReset(",
    "  QPushButton *addButtonParameter(",
    "      const QString &tooltip = QString());",
    "      const QString &tooltip = QString(),\n"
    "      const QString &parameterKey = QString());",
)
replace_region(
    path,
    "  void addCorrelatedRGBParameter(",
    "  QToolButton *addSeparator(",
    "      const QString &tooltip = QString());",
    "      const QString &tooltip = QString(),\n"
    "      const QString &parameterKey = QString());",
)
replace_exact(
    path,
    "  virtual void applyChange(std::function<void(ParameterState &)> modifier, const QString &description = QString());",
    "  virtual void applyChange(std::function<void(ParameterState &)> modifier,\n"
    "                           const QString &description = QString(),\n"
    "                           const QString &parameterKey = QString());",
)

path = "src/qtgui/ParameterPanel.cpp"
replace_exact(
    path,
    "constexpr auto parameterApplicableProperty = \"parameterApplicable\";\n"
    "constexpr auto parameterSectionExpandedProperty = \"parameterSectionExpanded\";",
    "constexpr auto parameterApplicableProperty = \"parameterApplicable\";\n"
    "constexpr auto parameterKeyProperty = \"parameterKey\";\n"
    "constexpr auto parameterSectionExpandedProperty = \"parameterSectionExpanded\";\n\n"
    "/** Attach stable machine-readable PARAMETERKEY metadata to WIDGET. */\n"
    "void setParameterKey(QWidget *widget, const QString &parameterKey) {\n"
    "  if (widget && !parameterKey.isEmpty())\n"
    "    widget->setProperty(parameterKeyProperty, parameterKey);\n"
    "}",
)
replace_exact(
    path,
    "void ParameterPanel::applyChange(\n"
    "    std::function<void(ParameterState &)> modifier, const QString &description) {\n"
    "  ParameterState state = m_stateGetter();\n"
    "  modifier(state);\n"
    "  m_stateSetter(state, description);\n"
    "}",
    "void ParameterPanel::applyChange(\n"
    "    std::function<void(ParameterState &)> modifier, const QString &description,\n"
    "    const QString &parameterKey) {\n"
    "  ParameterState state = m_stateGetter();\n"
    "  modifier(state);\n"
    "  m_stateSetter(state, description, parameterKey);\n"
    "}",
)

# addDoubleParameter
replace_region(
    path,
    "void ParameterPanel::addDoubleParameter(",
    "QWidget *ParameterPanel::addSliderParameter(",
    "    std::function<bool(double)> validator,\n    const QString &tooltip) {",
    "    std::function<bool(double)> validator, const QString &tooltip,\n"
    "    const QString &parameterKey) {",
)
replace_region(
    path,
    "void ParameterPanel::addDoubleParameter(",
    "QWidget *ParameterPanel::addSliderParameter(",
    "  hLayout->addWidget(spin, 1);",
    "  hLayout->addWidget(spin, 1);\n"
    "  setParameterKey(container, parameterKey);\n"
    "  setParameterKey(spin, parameterKey);",
)
replace_region(
    path,
    "void ParameterPanel::addDoubleParameter(",
    "QWidget *ParameterPanel::addSliderParameter(",
    "          [this, setter, label](double val) {\n"
    "            applyChange([setter, val](ParameterState &s) { setter(s, val); }, label);\n"
    "          });",
    "          [this, setter, label, parameterKey](double val) {\n"
    "            applyChange([setter, val](ParameterState &s) { setter(s, val); },\n"
    "                        label, parameterKey);\n"
    "          });",
)

# addSliderParameter (stateful only; addSlider remains untouched).
replace_region(
    path,
    "QWidget *ParameterPanel::addSliderParameter(",
    "QWidget* ParameterPanel::addSlider(",
    "    bool logarithmic, const QString &tooltip) {",
    "    bool logarithmic, const QString &tooltip,\n"
    "    const QString &parameterKey) {",
)
replace_region(
    path,
    "QWidget *ParameterPanel::addSliderParameter(",
    "QWidget* ParameterPanel::addSlider(",
    "  hLayout->addWidget(slider, 1); // Slider expands",
    "  setParameterKey(container, parameterKey);\n"
    "  setParameterKey(slider, parameterKey);\n"
    "  setParameterKey(spin, parameterKey);\n\n"
    "  hLayout->addWidget(slider, 1); // Slider expands",
)
replace_region(
    path,
    "QWidget *ParameterPanel::addSliderParameter(",
    "QWidget* ParameterPanel::addSlider(",
    "          [this, spin, sliderToValue, setter, label](int val) {",
    "          [this, spin, sliderToValue, setter, label, parameterKey](int val) {",
)
replace_region(
    path,
    "QWidget *ParameterPanel::addSliderParameter(",
    "QWidget* ParameterPanel::addSlider(",
    "            applyChange([setter, dVal](ParameterState &s) { setter(s, dVal); }, label);",
    "            applyChange([setter, dVal](ParameterState &s) { setter(s, dVal); },\n"
    "                        label, parameterKey);",
)
replace_region(
    path,
    "QWidget *ParameterPanel::addSliderParameter(",
    "QWidget* ParameterPanel::addSlider(",
    "          [this, setter, label](double val) {\n"
    "            applyChange([setter, val](ParameterState &s) { setter(s, val); }, label);\n"
    "          });",
    "          [this, setter, label, parameterKey](double val) {\n"
    "            applyChange([setter, val](ParameterState &s) { setter(s, val); },\n"
    "                        label, parameterKey);\n"
    "          });",
)

# Enum helper.
replace_region(
    path,
    "QComboBox *ParameterPanel::addEnumParameter(",
    "QCheckBox *ParameterPanel::addCheckboxParameter(",
    "    std::function<bool(const ParameterState &)> enabledCheck,\n    const QString &tooltip) {",
    "    std::function<bool(const ParameterState &)> enabledCheck,\n"
    "    const QString &tooltip, const QString &parameterKey) {",
)
replace_region(
    path,
    "QComboBox *ParameterPanel::addEnumParameter(",
    "QCheckBox *ParameterPanel::addCheckboxParameter(",
    "  combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);",
    "  combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);\n"
    "  setParameterKey(combo, parameterKey);",
)
replace_region(
    path,
    "QComboBox *ParameterPanel::addEnumParameter(",
    "QCheckBox *ParameterPanel::addCheckboxParameter(",
    "          [this, combo, setter, label](int index) {\n"
    "            int val = combo->itemData(index).toInt();\n"
    "            applyChange([setter, val](ParameterState &s) { setter(s, val); }, label);\n"
    "          });",
    "          [this, combo, setter, label, parameterKey](int index) {\n"
    "            int val = combo->itemData(index).toInt();\n"
    "            applyChange([setter, val](ParameterState &s) { setter(s, val); },\n"
    "                        label, parameterKey);\n"
    "          });",
)

# Checkbox helper.
replace_region(
    path,
    "QCheckBox *ParameterPanel::addCheckboxParameter(",
    "QCheckBox *ParameterPanel::addCheckboxWithReset(",
    "    std::function<bool(const ParameterState &)> enabledCheck,\n    const QString &tooltip) {",
    "    std::function<bool(const ParameterState &)> enabledCheck,\n"
    "    const QString &tooltip, const QString &parameterKey) {",
)
replace_region(
    path,
    "QCheckBox *ParameterPanel::addCheckboxParameter(",
    "QCheckBox *ParameterPanel::addCheckboxWithReset(",
    "  QLabel *textLabel = new QLabel(label);",
    "  QLabel *textLabel = new QLabel(label);\n"
    "  setParameterKey(container, parameterKey);\n"
    "  setParameterKey(checkbox, parameterKey);",
)
replace_region(
    path,
    "QCheckBox *ParameterPanel::addCheckboxParameter(",
    "QCheckBox *ParameterPanel::addCheckboxWithReset(",
    "  connect(checkbox, &QCheckBox::toggled, this, [this, setter, label](bool checked) {\n"
    "    applyChange([setter, checked](ParameterState &s) { setter(s, checked); }, label);\n"
    "  });",
    "  connect(checkbox, &QCheckBox::toggled, this,\n"
    "          [this, setter, label, parameterKey](bool checked) {\n"
    "    applyChange([setter, checked](ParameterState &s) { setter(s, checked); },\n"
    "                label, parameterKey);\n"
    "  });",
)

# Checkbox with Reset. Reset deliberately remains a one-shot description-keyed
# command so clicking Reset does not merge into the preceding continuous edit.
replace_region(
    path,
    "QCheckBox *ParameterPanel::addCheckboxWithReset(",
    "QPushButton *ParameterPanel::addButtonParameter(",
    "    std::function<bool(const ParameterState &)> enabledCheck,\n    const QString &tooltip) {",
    "    std::function<bool(const ParameterState &)> enabledCheck,\n"
    "    const QString &tooltip, const QString &parameterKey) {",
)
replace_region(
    path,
    "QCheckBox *ParameterPanel::addCheckboxWithReset(",
    "QPushButton *ParameterPanel::addButtonParameter(",
    "  QLabel *textLabel = new QLabel(label);",
    "  QLabel *textLabel = new QLabel(label);\n"
    "  setParameterKey(container, parameterKey);\n"
    "  setParameterKey(checkbox, parameterKey);",
)
replace_region(
    path,
    "QCheckBox *ParameterPanel::addCheckboxWithReset(",
    "QPushButton *ParameterPanel::addButtonParameter(",
    "  QPushButton *resetBtn = new QPushButton(\"Reset\");",
    "  QPushButton *resetBtn = new QPushButton(\"Reset\");\n"
    "  setParameterKey(resetBtn, parameterKey);",
)
replace_region(
    path,
    "QCheckBox *ParameterPanel::addCheckboxWithReset(",
    "QPushButton *ParameterPanel::addButtonParameter(",
    "  connect(checkbox, &QCheckBox::toggled, this, [this, setter, label](bool checked) {\n"
    "    applyChange([setter, checked](ParameterState &s) { setter(s, checked); }, label);\n"
    "  });",
    "  connect(checkbox, &QCheckBox::toggled, this,\n"
    "          [this, setter, label, parameterKey](bool checked) {\n"
    "    applyChange([setter, checked](ParameterState &s) { setter(s, checked); },\n"
    "                label, parameterKey);\n"
    "  });",
)

# Correlated RGB helper.
replace_region(
    path,
    "void ParameterPanel::addCorrelatedRGBParameter(",
    "QToolButton *ParameterPanel::addSeparator(",
    "    std::function<bool(const ParameterState &)> enabledCheck,\n    const QString &tooltip) {",
    "    std::function<bool(const ParameterState &)> enabledCheck,\n"
    "    const QString &tooltip, const QString &parameterKey) {",
)
replace_region(
    path,
    "void ParameterPanel::addCorrelatedRGBParameter(",
    "QToolButton *ParameterPanel::addSeparator(",
    "  linkCheck->setChecked(true);",
    "  linkCheck->setChecked(true);\n"
    "  setParameterKey(linkCheck, parameterKey);",
)
replace_region(
    path,
    "void ParameterPanel::addCorrelatedRGBParameter(",
    "QToolButton *ParameterPanel::addSeparator(",
    "    hLayout->addWidget(slider, 1);",
    "    setParameterKey(container, parameterKey);\n"
    "    setParameterKey(slider, parameterKey);\n"
    "    setParameterKey(spin, parameterKey);\n\n"
    "    hLayout->addWidget(slider, 1);",
)
replace_region(
    path,
    "void ParameterPanel::addCorrelatedRGBParameter(",
    "QToolButton *ParameterPanel::addSeparator(",
    "  auto handleValueChange = [this, channels, linkCheck, getter, setter,\n"
    "                            scale, label](int changedIdx, double newVal) {",
    "  auto handleValueChange = [this, channels, linkCheck, getter, setter,\n"
    "                            scale, label, parameterKey](int changedIdx,\n"
    "                                                       double newVal) {",
)
replace_region(
    path,
    "void ParameterPanel::addCorrelatedRGBParameter(",
    "QToolButton *ParameterPanel::addSeparator(",
    "    applyChange([setter, next](ParameterState &state) { setter(state, next); }, label);",
    "    applyChange([setter, next](ParameterState &state) { setter(state, next); },\n"
    "                label, parameterKey);",
)

# ---------------------------------------------------------------------------
# MainWindow: the key reaches QUndoCommand while menu text remains readable.
# ---------------------------------------------------------------------------
path = "src/qtgui/MainWindow.h"
replace_exact(
    path,
    "  void applySharedDocumentState(const ParameterState &state,\n"
    "                                const QString &description);",
    "  void applySharedDocumentState(const ParameterState &state,\n"
    "                                const QString &description,\n"
    "                                const QString &parameterKey = QString());",
)
replace_exact(
    path,
    "  void changeParameters(const ParameterState &newState, const QString &description = QString());",
    "  void changeParameters(const ParameterState &newState,\n"
    "                        const QString &description = QString(),\n"
    "                        const QString &parameterKey = QString());",
)

path = "src/qtgui/MainWindow.cpp"
replace_exact(
    path,
    "/** Undo command that captures a full ParameterState snapshot before and after\n"
    "   a change.  Successive commands with the same description within a 500 ms\n"
    "   window are merged into a single undo step so that one slider drag produces\n"
    "   one entry.  Different controls must remain separate even when changed\n"
    "   quickly, otherwise undo can silently skip an intermediate user action. */",
    "/** Undo command that captures a full ParameterState snapshot before and after\n"
    "   a change. Successive commands with the same stable parameter key within a\n"
    "   500 ms window merge so one slider drag produces one entry. During gradual\n"
    "   migration, an empty key falls back to the historical description-based\n"
    "   identity. Human-visible Undo text is always kept separate from the key. */",
)
replace_exact(
    path,
    "  ChangeParametersCommand(MainWindow *window, const ParameterState &oldState,\n"
    "                          const ParameterState &newState,\n"
    "                          const QString &description = QString())\n"
    "      : m_window(window), m_oldState(oldState), m_newState(newState),\n"
    "        m_description(description) {\n"
    "    setText(description.isEmpty() ? \"Change Parameters\" : description);\n"
    "    m_timestamp = QDateTime::currentMSecsSinceEpoch();\n"
    "  }",
    "  ChangeParametersCommand(MainWindow *window, const ParameterState &oldState,\n"
    "                          const ParameterState &newState,\n"
    "                          const QString &description = QString(),\n"
    "                          const QString &parameterKey = QString())\n"
    "      : m_window(window), m_oldState(oldState), m_newState(newState),\n"
    "        m_mergeKey(parameterKey.isEmpty() ? description : parameterKey) {\n"
    "    setText(description.isEmpty() ? \"Change Parameters\" : description);\n"
    "    m_timestamp = QDateTime::currentMSecsSinceEpoch();\n"
    "  }",
)
replace_exact(
    path,
    "    // Qt never attempts to merge commands whose id is -1.  Calls without a\n"
    "    // logical description are therefore conservative one-shot undo entries.\n"
    "    return m_description.isEmpty() ? -1 : 1;",
    "    // Qt never attempts to merge commands whose id is -1. Calls without a\n"
    "    // parameter key or legacy description are conservative one-shot edits.\n"
    "    return m_mergeKey.isEmpty() ? -1 : 1;",
)
replace_exact(
    path,
    "    // QUndoStack uses id() only as a coarse filter.  The description is the\n"
    "    // stable logical identity supplied by ParameterPanel (normally the field\n"
    "    // label), so do not merge two different edits merely because they happened\n"
    "    // close together.\n"
    "    if (cmd->m_description != m_description)\n"
    "      return false;",
    "    // QUndoStack uses id() only as a coarse filter. The stable key, not the\n"
    "    // translated/display label, determines whether two updates are one edit.\n"
    "    if (cmd->m_mergeKey != m_mergeKey)\n"
    "      return false;",
)
replace_exact(path, "  QString m_description;", "  QString m_mergeKey;")
count = regex_sub(
    path,
    r"\[this\]\(const ParameterState &s, const QString &desc\) \{\n\s*changeParameters\(s, desc\);\n\s*\}",
    "[this](const ParameterState &s, const QString &desc,\n"
    "                             const QString &parameterKey) {\n"
    "                         changeParameters(s, desc, parameterKey);\n"
    "                       }",
)
if count < 7:
    raise RuntimeError(f"MainWindow.cpp: expected >=7 panel setter lambdas, got {count}")
replace_exact(
    path,
    "  QAction *undoAction = m_undoStack->createUndoAction(this, tr(\"&Undo\"));\n"
    "  undoAction->setIcon(QIcon::fromTheme(\"edit-undo-symbolic\"));",
    "  QAction *undoAction = m_undoStack->createUndoAction(this, tr(\"&Undo\"));\n"
    "  undoAction->setObjectName(QStringLiteral(\"UndoParametersAction\"));\n"
    "  undoAction->setIcon(QIcon::fromTheme(\"edit-undo-symbolic\"));",
)
replace_exact(
    path,
    "/** Apply shared document parameters changed by a secondary/specialized view. */\n"
    "void MainWindow::applySharedDocumentState(const ParameterState &state,\n"
    "                                          const QString &description) {\n"
    "  changeParameters(state, description);\n"
    "}",
    "/** Apply shared document parameters changed by a secondary/specialized view. */\n"
    "void MainWindow::applySharedDocumentState(const ParameterState &state,\n"
    "                                          const QString &description,\n"
    "                                          const QString &parameterKey) {\n"
    "  changeParameters(state, description, parameterKey);\n"
    "}",
)
regex_sub(
    path,
    r"void MainWindow::changeParameters\(const ParameterState &newState,\n\s+const QString &description\) \{",
    "void MainWindow::changeParameters(const ParameterState &newState,\n"
    "                                  const QString &description,\n"
    "                                  const QString &parameterKey) {",
    expected=1,
)
replace_exact(
    path,
    "  m_undoStack->push(\n      new ChangeParametersCommand(this, currentState, newState, description));",
    "  m_undoStack->push(new ChangeParametersCommand(\n"
    "      this, currentState, newState, description, parameterKey));",
)
text = read(path)
text = text.replace(
    "   The DESCRIPTION string appears in the Edit > Undo/Redo menu text.\n"
    "   Successive calls with the same DESCRIPTION within 500 ms are merged by the\n"
    "   command's mergeWith() into a single undo step.  */",
    "   DESCRIPTION appears in the Edit > Undo/Redo menu text. PARAMETERKEY is a\n"
    "   stable machine-readable merge identity; when empty, DESCRIPTION preserves\n"
    "   the historical behavior for controls not migrated yet. */",
)
write(path, text)

# Secondary/reference inspectors forward keys to their source document.
path = "src/qtgui/ImageViewWindow.cpp"
replace_exact(
    path,
    "      [this](const ParameterState &state, const QString &description) {\n"
    "        if (m_document)\n"
    "          m_document->applySharedDocumentState(state, description);\n"
    "      },",
    "      [this](const ParameterState &state, const QString &description,\n"
    "             const QString &parameterKey) {\n"
    "        if (m_document)\n"
    "          m_document->applySharedDocumentState(state, description, parameterKey);\n"
    "      },",
)

# These panels bypass applyChange for a few custom rows. Supply an empty key to
# preserve their exact historical undo behavior until they are migrated.
path = "src/qtgui/TilesPanel.cpp"
replace_exact(
    path,
    "        m_stateSetter(s, tr(\"Toggle tile %1,%2\").arg(gx).arg(gy));",
    "        m_stateSetter(s, tr(\"Toggle tile %1,%2\").arg(gx).arg(gy),\n"
    "                      QString());",
)
path = "src/qtgui/ImageLayerPanel.cpp"
replace_exact(
    path,
    "    m_stateSetter(s, tr(\"Use simulated RGB image layer %1\")\n"
    "                        .arg(checked ? tr(\"on\") : tr(\"off\")));",
    "    m_stateSetter(s, tr(\"Use simulated RGB image layer %1\")\n"
    "                        .arg(checked ? tr(\"on\") : tr(\"off\")),\n"
    "                  QString());",
)

# Derived panels keep their refresh hooks when keyed helpers call applyChange.
for panel in ("SharpnessPanel", "ColorPanel"):
    path = f"src/qtgui/{panel}.h"
    replace_exact(
        path,
        "  void applyChange(std::function<void(ParameterState &)> modifier, const QString &description = QString()) override;",
        "  void applyChange(std::function<void(ParameterState &)> modifier,\n"
        "                   const QString &description = QString(),\n"
        "                   const QString &parameterKey = QString()) override;",
    )
    path = f"src/qtgui/{panel}.cpp"
    replace_exact(
        path,
        f"void {panel}::applyChange(\n"
        "    std::function<void(ParameterState &)> modifier, const QString &description) {\n"
        "  ParameterPanel::applyChange(modifier, description);",
        f"void {panel}::applyChange(\n"
        "    std::function<void(ParameterState &)> modifier, const QString &description,\n"
        "    const QString &parameterKey) {\n"
        "  ParameterPanel::applyChange(modifier, description, parameterKey);",
    )

# First real migrations use controls that already have stable object identities.
path = "src/qtgui/ScreenPanel.cpp"
replace_exact(
    path,
    "      \"Relative width of the red filter strips for line-screen processes like \"\n"
    "      \"Joly or Dufaycolor.\");",
    "      \"Relative width of the red filter strips for line-screen processes like \"\n"
    "      \"Joly or Dufaycolor.\",\n"
    "      QStringLiteral(\"screen.red_strip_width\"));",
)
replace_exact(
    path,
    "      \"Relative width of the green filter strips for line-screen processes \"\n"
    "      \"like Joly or Dufaycolor.\");",
    "      \"Relative width of the green filter strips for line-screen processes \"\n"
    "      \"like Joly or Dufaycolor.\",\n"
    "      QStringLiteral(\"screen.green_strip_width\"));",
)
path = "src/qtgui/SharpnessPanel.cpp"
replace_exact(
    path,
    "      nullptr,\n"
    "      \"Use a measured MTF curve directly instead of the fitted analytical physical or fallback model.\");",
    "      nullptr,\n"
    "      \"Use a measured MTF curve directly instead of the fitted analytical physical or fallback model.\",\n"
    "      QStringLiteral(\"sharpness.capture.use_measured_mtf\"));",
)

# ---------------------------------------------------------------------------
# Smoke coverage: key metadata plus the actual QUndoStack merge behavior.
# ---------------------------------------------------------------------------
path = "src/qtgui/WorkspaceChurnSmoke.cpp"
replace_exact(path, "#include <QComboBox>\n", "#include <QAction>\n#include <QComboBox>\n")
replace_exact(
    path,
    "QCheckBox *mtfUseMeasured = inspector->findChild<QCheckBox *>(\n"
    "    QStringLiteral(\"MtfUseMeasuredCheck\"));",
    "QCheckBox *mtfUseMeasured = inspector->findChild<QCheckBox *>(\n"
    "    QStringLiteral(\"MtfUseMeasuredCheck\"));\n"
    "QWidget *redStripWidth = inspector->findChild<QWidget *>(\n"
    "    QStringLiteral(\"ScreenRedStripWidth\"));\n"
    "QWidget *greenStripWidth = inspector->findChild<QWidget *>(\n"
    "    QStringLiteral(\"ScreenGreenStripWidth\"));\n"
    "QAction *undoParametersAction = first->findChild<QAction *>(\n"
    "    QStringLiteral(\"UndoParametersAction\"));",
)
replace_exact(
    path,
    "        scannerCameraToggle->setChecked(scannerPropertiesWereExpanded);\n\n"
    "        // Folding the guide is presentation state only.",
    "        scannerCameraToggle->setChecked(scannerPropertiesWereExpanded);\n\n"
    "        if (!redStripWidth || !greenStripWidth ||\n"
    "            redStripWidth->property(\"parameterKey\").toString() !=\n"
    "                QStringLiteral(\"screen.red_strip_width\") ||\n"
    "            greenStripWidth->property(\"parameterKey\").toString() !=\n"
    "                QStringLiteral(\"screen.green_strip_width\")) {\n"
    "          fail(QStringLiteral(\n"
    "              \"Workspace churn lost stable parameter-key metadata\"));\n"
    "          return;\n"
    "        }\n\n"
    "        // Exercise undo identity directly. Two edits deliberately use the\n"
    "        // same human description: different keys must keep them separate,\n"
    "        // while repeated updates carrying one key must coalesce.\n"
    "        if (!undoParametersAction) {\n"
    "          fail(QStringLiteral(\"Workspace churn lost the Undo action\"));\n"
    "          return;\n"
    "        }\n"
    "        const ParameterState undoBaseline = first->documentStateSnapshot();\n"
    "        ParameterState firstKeyEdit = undoBaseline;\n"
    "        firstKeyEdit.rparams.brightness = undoBaseline.rparams.brightness + 0.25;\n"
    "        first->applySharedDocumentState(\n"
    "            firstKeyEdit, QStringLiteral(\"Undo key smoke\"),\n"
    "            QStringLiteral(\"smoke.brightness\"));\n"
    "        ParameterState secondKeyEdit = first->documentStateSnapshot();\n"
    "        secondKeyEdit.rparams.scan_mirror = !undoBaseline.rparams.scan_mirror;\n"
    "        first->applySharedDocumentState(\n"
    "            secondKeyEdit, QStringLiteral(\"Undo key smoke\"),\n"
    "            QStringLiteral(\"smoke.scan_mirror\"));\n"
    "        undoParametersAction->trigger();\n"
    "        ParameterState afterDifferentKeyUndo = first->documentStateSnapshot();\n"
    "        if (afterDifferentKeyUndo.rparams.brightness !=\n"
    "                firstKeyEdit.rparams.brightness ||\n"
    "            afterDifferentKeyUndo.rparams.scan_mirror !=\n"
    "                undoBaseline.rparams.scan_mirror) {\n"
    "          fail(QStringLiteral(\n"
    "              \"Undo merged distinct parameter keys sharing one label\"));\n"
    "          return;\n"
    "        }\n"
    "        undoParametersAction->trigger();\n"
    "        if (first->documentStateSnapshot() != undoBaseline) {\n"
    "          fail(QStringLiteral(\n"
    "              \"Undo did not restore the baseline after distinct keyed edits\"));\n"
    "          return;\n"
    "        }\n\n"
    "        ParameterState sameKeyEdit1 = undoBaseline;\n"
    "        sameKeyEdit1.rparams.brightness = undoBaseline.rparams.brightness + 0.5;\n"
    "        first->applySharedDocumentState(\n"
    "            sameKeyEdit1, QStringLiteral(\"Undo key smoke\"),\n"
    "            QStringLiteral(\"smoke.brightness\"));\n"
    "        ParameterState sameKeyEdit2 = first->documentStateSnapshot();\n"
    "        sameKeyEdit2.rparams.brightness = undoBaseline.rparams.brightness + 0.75;\n"
    "        first->applySharedDocumentState(\n"
    "            sameKeyEdit2, QStringLiteral(\"Undo key smoke\"),\n"
    "            QStringLiteral(\"smoke.brightness\"));\n"
    "        undoParametersAction->trigger();\n"
    "        if (first->documentStateSnapshot() != undoBaseline) {\n"
    "          fail(QStringLiteral(\n"
    "              \"Undo failed to coalesce repeated updates for one parameter key\"));\n"
    "          return;\n"
    "        }\n\n"
    "        // Folding the guide is presentation state only.",
)

# Documentation/tracking.
path = "doc/qtgui-internal-cleanup.md"
replace_exact(
    path,
    "- Give every stateful parameter helper an explicit machine-readable key.  Use\n"
    "  that key for undo merge identity, optional reset/bypass state and diagnostics.",
    "- Give every stateful parameter helper an explicit machine-readable key.\n"
    "  `ParameterPanel` now accepts an optional stable `parameterKey`, stores it\n"
    "  on the field widget, and uses it for undo merge identity independently of\n"
    "  the human Undo description. Unconverted controls deliberately fall back to\n"
    "  their historical label-based identity. Screen strip-width controls and the\n"
    "  measured-MTF selector are the first migrated users; continue assigning keys\n"
    "  as panels gain reset/default/modified metadata.",
)
path = ".agents/qtgui.md"
replace_exact(
    path,
    "    \"This parameter controls X.\" // Tooltip\n"
    ");",
    "    \"This parameter controls X.\", // Tooltip\n"
    "    \"example.my_parameter\"       // Stable parameter key\n"
    ");",
)
replace_exact(
    path,
    "- **`applyChange`**: Behind the scenes, the helper methods call `applyChange`, which triggers a global state update and UI refresh.",
    "- **`applyChange`**: Behind the scenes, the helper methods call `applyChange`, which triggers a global state update and UI refresh. Stateful helpers accept an optional stable, untranslated `parameterKey`; use it for new or actively maintained controls so Undo merging and future Reset/default metadata do not depend on the visible label.",
)

print("parameter-key migration prepared")
