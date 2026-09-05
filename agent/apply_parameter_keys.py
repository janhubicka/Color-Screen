from pathlib import Path
import re

ROOT = Path("target")


def read(path):
    return (ROOT / path).read_text()


def write(path, text):
    (ROOT / path).write_text(text)


def replace_exact(path, old, new, count=1):
    text = read(path)
    actual = text.count(old)
    if actual != count:
        raise RuntimeError(f"{path}: expected {count} copies, found {actual}: {old[:80]!r}")
    write(path, text.replace(old, new, count))


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
# ParameterPanel: make stable parameter identity an explicit helper concept.
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
    "                          std::function<bool(double)> validator = nullptr,\n"
    "                          const QString &tooltip = QString());",
    "                          std::function<bool(double)> validator = nullptr,\n"
    "                          const QString &tooltip = QString(),\n"
    "                          const QString &parameterKey = QString());",
)
replace_exact(
    path,
    "      bool logarithmic = false, const QString &tooltip = QString());",
    "      bool logarithmic = false, const QString &tooltip = QString(),\n"
    "      const QString &parameterKey = QString());",
    count=1,
)
replace_exact(
    path,
    "      std::function<bool(const ParameterState &)> enabledCheck = nullptr,\n"
    "      const QString &tooltip = QString());\n\n  template <typename T>",
    "      std::function<bool(const ParameterState &)> enabledCheck = nullptr,\n"
    "      const QString &tooltip = QString(),\n"
    "      const QString &parameterKey = QString());\n\n  template <typename T>",
)
replace_exact(
    path,
    "      std::function<bool(const ParameterState &)> enabledCheck = nullptr,\n"
    "      const QString &tooltip = QString()) {\n",
    "      std::function<bool(const ParameterState &)> enabledCheck = nullptr,\n"
    "      const QString &tooltip = QString(),\n"
    "      const QString &parameterKey = QString()) {\n",
    count=2,
)
replace_exact(
    path,
    "    QComboBox *combo = addEnumParameter(label, options, getter, setter, enabledCheck, tooltip);",
    "    QComboBox *combo = addEnumParameter(label, options, getter, setter,\n"
    "                                        enabledCheck, tooltip, parameterKey);",
)
replace_exact(
    path,
    "    return addEnumParameter(label, Names, Max, getter, setter, enabledCheck, tooltip);",
    "    return addEnumParameter(label, Names, Max, getter, setter, enabledCheck,\n"
    "                            tooltip, parameterKey);",
)
# Checkbox and checkbox-with-reset declarations have the same tail.
replace_exact(
    path,
    "      std::function<bool(const ParameterState &)> enabledCheck = nullptr,\n"
    "      const QString &tooltip = QString());",
    "      std::function<bool(const ParameterState &)> enabledCheck = nullptr,\n"
    "      const QString &tooltip = QString(),\n"
    "      const QString &parameterKey = QString());",
    count=2,
)
replace_exact(
    path,
    "      std::function<bool(const ParameterState &)> enabledCheck = nullptr,\n"
    "      const QString &tooltip = QString());\n\n  QToolButton *addSeparator",
    "      std::function<bool(const ParameterState &)> enabledCheck = nullptr,\n"
    "      const QString &tooltip = QString(),\n"
    "      const QString &parameterKey = QString());\n\n  QToolButton *addSeparator",
)
replace_exact(
    path,
    "  virtual void applyChange(std::function<void(ParameterState &)> modifier, const QString &description = QString());",
    "  virtual void applyChange(std::function<void(ParameterState &)> modifier,\n"
    "                           const QString &description = QString(),\n"
    "                           const QString &parameterKey = QString());",
)
# Document the contract next to the helper API instead of relying on call-site lore.
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
replace_exact(
    path,
    "    std::function<bool(double)> validator,\n"
    "    const QString &tooltip) {",
    "    std::function<bool(double)> validator, const QString &tooltip,\n"
    "    const QString &parameterKey) {",
)
replace_exact(
    path,
    "  hLayout->addWidget(spin, 1);\n\n  QComboBox *combo = nullptr;",
    "  hLayout->addWidget(spin, 1);\n"
    "  setParameterKey(container, parameterKey);\n"
    "  setParameterKey(spin, parameterKey);\n\n"
    "  QComboBox *combo = nullptr;",
)
replace_exact(
    path,
    "          [this, setter, label](double val) {\n"
    "            applyChange([setter, val](ParameterState &s) { setter(s, val); }, label);\n"
    "          });",
    "          [this, setter, label, parameterKey](double val) {\n"
    "            applyChange([setter, val](ParameterState &s) { setter(s, val); },\n"
    "                        label, parameterKey);\n"
    "          });",
    count=1,
)
replace_exact(
    path,
    "    bool logarithmic, const QString &tooltip) {\n  // Container: Slider + SpinBox",
    "    bool logarithmic, const QString &tooltip,\n"
    "    const QString &parameterKey) {\n  // Container: Slider + SpinBox",
    count=1,
)
replace_exact(
    path,
    "  if (!specialValueText.isEmpty())\n"
    "    spin->setSpecialValueText(specialValueText);\n\n"
    "  hLayout->addWidget(slider, 1); // Slider expands",
    "  if (!specialValueText.isEmpty())\n"
    "    spin->setSpecialValueText(specialValueText);\n\n"
    "  setParameterKey(container, parameterKey);\n"
    "  setParameterKey(slider, parameterKey);\n"
    "  setParameterKey(spin, parameterKey);\n\n"
    "  hLayout->addWidget(slider, 1); // Slider expands",
    count=1,
)
replace_exact(
    path,
    "          [this, spin, sliderToValue, setter, label](int val) {",
    "          [this, spin, sliderToValue, setter, label, parameterKey](int val) {",
)
replace_exact(
    path,
    "            applyChange([setter, dVal](ParameterState &s) { setter(s, dVal); }, label);",
    "            applyChange([setter, dVal](ParameterState &s) { setter(s, dVal); },\n"
    "                        label, parameterKey);",
)
replace_exact(
    path,
    "          [this, setter, label](double val) {\n"
    "            applyChange([setter, val](ParameterState &s) { setter(s, val); }, label);\n"
    "          });",
    "          [this, setter, label, parameterKey](double val) {\n"
    "            applyChange([setter, val](ParameterState &s) { setter(s, val); },\n"
    "                        label, parameterKey);\n"
    "          });",
    count=1,
)
replace_exact(
    path,
    "    std::function<bool(const ParameterState &)> enabledCheck,\n"
    "    const QString &tooltip) {\n  QComboBox *combo = new QComboBox();",
    "    std::function<bool(const ParameterState &)> enabledCheck,\n"
    "    const QString &tooltip, const QString &parameterKey) {\n"
    "  QComboBox *combo = new QComboBox();",
)
replace_exact(
    path,
    "  combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);\n"
    "  for (auto const &[val, text] : options) {",
    "  combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);\n"
    "  setParameterKey(combo, parameterKey);\n"
    "  for (auto const &[val, text] : options) {",
)
replace_exact(
    path,
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
# Two checkbox implementations share the same signature tail.
replace_exact(
    path,
    "    std::function<bool(const ParameterState &)> enabledCheck,\n"
    "    const QString &tooltip) {\n  // Create container with label on left, checkbox on right",
    "    std::function<bool(const ParameterState &)> enabledCheck,\n"
    "    const QString &tooltip, const QString &parameterKey) {\n"
    "  // Create container with label on left, checkbox on right",
    count=2,
)
replace_exact(
    path,
    "  QCheckBox *checkbox = new QCheckBox();\n"
    "  QLabel *textLabel = new QLabel(label);",
    "  QCheckBox *checkbox = new QCheckBox();\n"
    "  QLabel *textLabel = new QLabel(label);\n"
    "  setParameterKey(container, parameterKey);\n"
    "  setParameterKey(checkbox, parameterKey);",
    count=2,
)
replace_exact(
    path,
    "  connect(checkbox, &QCheckBox::toggled, this, [this, setter, label](bool checked) {\n"
    "    applyChange([setter, checked](ParameterState &s) { setter(s, checked); }, label);\n"
    "  });",
    "  connect(checkbox, &QCheckBox::toggled, this,\n"
    "          [this, setter, label, parameterKey](bool checked) {\n"
    "    applyChange([setter, checked](ParameterState &s) { setter(s, checked); },\n"
    "                label, parameterKey);\n"
    "  });",
    count=2,
)
replace_exact(
    path,
    "  QPushButton *resetBtn = new QPushButton(\"Reset\");\n"
    "  resetBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);",
    "  QPushButton *resetBtn = new QPushButton(\"Reset\");\n"
    "  resetBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);\n"
    "  setParameterKey(resetBtn, parameterKey);",
)
# Reset remains a distinct one-shot undo gesture rather than merging into a drag.
replace_exact(
    path,
    "  connect(resetBtn, &QPushButton::clicked, this, [this, resetAction, label]() {\n"
    "    applyChange(resetAction, QString(\"Reset %1\").arg(label));\n"
    "  });",
    "  connect(resetBtn, &QPushButton::clicked, this, [this, resetAction, label]() {\n"
    "    applyChange(resetAction, QString(\"Reset %1\").arg(label));\n"
    "  });",
)
replace_exact(
    path,
    "    std::function<bool(const ParameterState &)> enabledCheck,\n"
    "    const QString &tooltip) {\n\n  // 1. Link Checkbox",
    "    std::function<bool(const ParameterState &)> enabledCheck,\n"
    "    const QString &tooltip, const QString &parameterKey) {\n\n"
    "  // 1. Link Checkbox",
)
replace_exact(
    path,
    "  QCheckBox *linkCheck = new QCheckBox(\"Link channels\");\n"
    "  linkCheck->setChecked(true);",
    "  QCheckBox *linkCheck = new QCheckBox(\"Link channels\");\n"
    "  linkCheck->setChecked(true);\n"
    "  setParameterKey(linkCheck, parameterKey);",
)
replace_exact(
    path,
    "    if (!tooltip.isEmpty()) {\n"
    "      slider->setToolTip(tooltip);\n"
    "      spin->setToolTip(tooltip);\n"
    "    }\n\n"
    "    hLayout->addWidget(slider, 1);",
    "    if (!tooltip.isEmpty()) {\n"
    "      slider->setToolTip(tooltip);\n"
    "      spin->setToolTip(tooltip);\n"
    "    }\n"
    "    setParameterKey(container, parameterKey);\n"
    "    setParameterKey(slider, parameterKey);\n"
    "    setParameterKey(spin, parameterKey);\n\n"
    "    hLayout->addWidget(slider, 1);",
)
replace_exact(
    path,
    "  auto handleValueChange = [this, channels, linkCheck, getter, setter,\n"
    "                            scale, label](int changedIdx, double newVal) {",
    "  auto handleValueChange = [this, channels, linkCheck, getter, setter,\n"
    "                            scale, label, parameterKey](int changedIdx,\n"
    "                                                       double newVal) {",
)
replace_exact(
    path,
    "    applyChange([setter, next](ParameterState &state) { setter(state, next); }, label);",
    "    applyChange([setter, next](ParameterState &state) { setter(state, next); },\n"
    "                label, parameterKey);",
)

# ---------------------------------------------------------------------------
# MainWindow: carry the key to the undo command and keep menu text human-only.
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
# All document-owned panel state setters are adjacent lambdas with this shape.
count = regex_sub(
    path,
    r"\[this\]\(const ParameterState &s, const QString &desc\) \{\n(?P<i>\s*)changeParameters\(s, desc\);\n(?P=i)\}",
    lambda m: "[this](const ParameterState &s, const QString &desc,\n"
              + m.group("i") + "       const QString &parameterKey) {\n"
              + m.group("i") + "changeParameters(s, desc, parameterKey);\n"
              + m.group("i") + "}",
    expected=None,
)
if count < 7:
    raise RuntimeError(f"MainWindow.cpp: expected at least seven panel setter lambdas, got {count}")
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
# Change the central push path without disturbing surrounding state bookkeeping.
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
# Refresh the nearby contract comment if present.
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

# Secondary/reference inspectors must forward the key into the source document.
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

# A few panels write through StateSetter directly rather than applyChange.
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

# Preserve derived-panel refresh behavior when a helper supplies a key.
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

# First real helper migrations: existing stable Screen rows and measured-MTF mode.
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
# Smoke regression: same label/different keys must remain two undo gestures,
# while two updates with the same key must coalesce.
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

# Tracking/docs: record that the foundation exists and show future callers how
# to choose stable keys without coupling behavior to translated UI text.
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
