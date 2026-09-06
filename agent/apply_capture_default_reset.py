from pathlib import Path
import re

ROOT = Path("target")


def read(path):
    return (ROOT / path).read_text()


def write(path, text):
    (ROOT / path).write_text(text)


def replace_once(path, old, new):
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected 1 copy, found {count}: {old[:120]!r}")
    write(path, text.replace(old, new, 1))


# ParameterPanel header: opt-in numeric default/reset presentation.
replace_once(
    "src/qtgui/ParameterPanel.h",
    "class QVBoxLayout;\nclass QFormLayout;",
    "class QVBoxLayout;\nclass QHBoxLayout;\nclass QFormLayout;",
)
replace_once(
    "src/qtgui/ParameterPanel.h",
    "                          const QString &tooltip = QString(),\n"
    "                          const QString &parameterKey = QString());",
    "                          const QString &tooltip = QString(),\n"
    "                          const QString &parameterKey = QString(),\n"
    "                          bool showDefaultReset = false);",
)
replace_once(
    "src/qtgui/ParameterPanel.h",
    "      bool logarithmic = false, const QString &tooltip = QString(),\n"
    "      const QString &parameterKey = QString());",
    "      bool logarithmic = false, const QString &tooltip = QString(),\n"
    "      const QString &parameterKey = QString(),\n"
    "      bool showDefaultReset = false);",
)
replace_once(
    "src/qtgui/ParameterPanel.h",
    "  void setParameterApplicability(\n"
    "      QWidget *widget,\n"
    "      std::function<bool(const ParameterState &)> applicableCheck);\n\n",
    "  void setParameterApplicability(\n"
    "      QWidget *widget,\n"
    "      std::function<bool(const ParameterState &)> applicableCheck);\n\n"
    "  /** Add opt-in default/modified/reset presentation to a keyed numeric\n"
    "      FIELD. The reset target comes from a fresh ParameterState so there is\n"
    "      no second table of defaults. Reset is intentionally a separate undo\n"
    "      gesture rather than another update carrying PARAMETERKEY. */\n"
    "  void addNumericDefaultPresentation(\n"
    "      QWidget *field, QHBoxLayout *layout, const QString &label,\n"
    "      const QString &parameterKey, double defaultValue,\n"
    "      std::function<double(const ParameterState &)> getter,\n"
    "      std::function<void(ParameterState &, double)> setter,\n"
    "      double tolerance);\n\n",
)

# ParameterPanel implementation.
replace_once(
    "src/qtgui/ParameterPanel.cpp",
    "#include <QEvent>\n#include <QLayout>",
    "#include <QEvent>\n#include <QFont>\n#include <QLayout>",
)
replace_once(
    "src/qtgui/ParameterPanel.cpp",
    "#include <QVBoxLayout>\n\nnamespace {",
    "#include <QVBoxLayout>\n#include <cmath>\n\nnamespace {",
)
replace_once(
    "src/qtgui/ParameterPanel.cpp",
    "constexpr auto parameterApplicableProperty = \"parameterApplicable\";\n"
    "constexpr auto parameterKeyProperty = \"parameterKey\";\n"
    "constexpr auto parameterSectionExpandedProperty = \"parameterSectionExpanded\";",
    "constexpr auto parameterApplicableProperty = \"parameterApplicable\";\n"
    "constexpr auto parameterDefaultValueProperty = \"parameterDefaultValue\";\n"
    "constexpr auto parameterKeyProperty = \"parameterKey\";\n"
    "constexpr auto parameterModifiedProperty = \"parameterModified\";\n"
    "constexpr auto parameterSectionExpandedProperty = \"parameterSectionExpanded\";",
)
replace_once(
    "src/qtgui/ParameterPanel.cpp",
    "void ParameterPanel::applyChange(\n"
    "    std::function<void(ParameterState &)> modifier, const QString &description,\n"
    "    const QString &parameterKey) {\n"
    "  ParameterState state = m_stateGetter();\n"
    "  modifier(state);\n"
    "  m_stateSetter(state, description, parameterKey);\n"
    "}\n\n"
    "void ParameterPanel::addDoubleParameter(",
    "void ParameterPanel::applyChange(\n"
    "    std::function<void(ParameterState &)> modifier, const QString &description,\n"
    "    const QString &parameterKey) {\n"
    "  ParameterState state = m_stateGetter();\n"
    "  modifier(state);\n"
    "  m_stateSetter(state, description, parameterKey);\n"
    "}\n\n"
    "/** Add quiet modified/default/reset UI for one keyed numeric parameter. */\n"
    "void ParameterPanel::addNumericDefaultPresentation(\n"
    "    QWidget *field, QHBoxLayout *layout, const QString &label,\n"
    "    const QString &parameterKey, double defaultValue,\n"
    "    std::function<double(const ParameterState &)> getter,\n"
    "    std::function<void(ParameterState &, double)> setter,\n"
    "    double tolerance) {\n"
    "  Q_ASSERT(field);\n"
    "  Q_ASSERT(layout);\n"
    "  Q_ASSERT(!parameterKey.isEmpty());\n\n"
    "  field->setProperty(parameterDefaultValueProperty, defaultValue);\n"
    "  field->setProperty(parameterModifiedProperty, false);\n\n"
    "  auto *resetButton = new QToolButton(field);\n"
    "  resetButton->setObjectName(QStringLiteral(\"ParameterResetButton\"));\n"
    "  resetButton->setText(tr(\"Reset\"));\n"
    "  resetButton->setAutoRaise(true);\n"
    "  resetButton->setToolTip(tr(\"Reset %1 to its default value\").arg(label));\n"
    "  resetButton->setProperty(parameterKeyProperty, parameterKey);\n"
    "  resetButton->setProperty(parameterDefaultValueProperty, defaultValue);\n"
    "  resetButton->setProperty(parameterModifiedProperty, false);\n"
    "  resetButton->hide();\n"
    "  layout->addWidget(resetButton, 0);\n\n"
    "  QWidget *labelWidget = nullptr;\n"
    "  if (m_currentGroupForm)\n"
    "    labelWidget = m_currentGroupForm->labelForField(field);\n"
    "  if (!labelWidget && m_form)\n"
    "    labelWidget = m_form->labelForField(field);\n"
    "  if (!labelWidget) {\n"
    "    for (QFormLayout *form : m_groupForms) {\n"
    "      labelWidget = form ? form->labelForField(field) : nullptr;\n"
    "      if (labelWidget)\n"
    "        break;\n"
    "    }\n"
    "  }\n\n"
    "  const QFont normalFont = labelWidget ? labelWidget->font() : QFont();\n"
    "  QFont modifiedFont = normalFont;\n"
    "  if (modifiedFont.weight() < QFont::DemiBold)\n"
    "    modifiedFont.setWeight(QFont::DemiBold);\n"
    "  if (labelWidget) {\n"
    "    labelWidget->setProperty(parameterDefaultValueProperty, defaultValue);\n"
    "    labelWidget->setProperty(parameterModifiedProperty, false);\n"
    "  }\n\n"
    "  connect(resetButton, &QToolButton::clicked, this,\n"
    "          [this, setter, defaultValue, label]() {\n"
    "            applyChange(\n"
    "                [setter, defaultValue](ParameterState &state) {\n"
    "                  setter(state, defaultValue);\n"
    "                },\n"
    "                tr(\"Reset %1\").arg(label), QString());\n"
    "          });\n\n"
    "  m_paramUpdaters.push_back(\n"
    "      [field, labelWidget, resetButton, getter, defaultValue, tolerance,\n"
    "       normalFont, modifiedFont](const ParameterState &state) {\n"
    "        const double value = getter(state);\n"
    "        bool modified = value != defaultValue;\n"
    "        if (std::isfinite(value) && std::isfinite(defaultValue))\n"
    "          modified = std::abs(value - defaultValue) > tolerance;\n\n"
    "        field->setProperty(parameterModifiedProperty, modified);\n"
    "        resetButton->setProperty(parameterModifiedProperty, modified);\n"
    "        resetButton->setVisible(modified);\n"
    "        if (labelWidget) {\n"
    "          labelWidget->setProperty(parameterModifiedProperty, modified);\n"
    "          labelWidget->setFont(modified ? modifiedFont : normalFont);\n"
    "        }\n"
    "      });\n"
    "}\n\n"
    "void ParameterPanel::addDoubleParameter(",
)
replace_once(
    "src/qtgui/ParameterPanel.cpp",
    "    std::function<bool(double)> validator, const QString &tooltip,\n"
    "    const QString &parameterKey) {",
    "    std::function<bool(double)> validator, const QString &tooltip,\n"
    "    const QString &parameterKey, bool showDefaultReset) {",
)
replace_once(
    "src/qtgui/ParameterPanel.cpp",
    "  if (m_currentGroupForm) {\n"
    "    m_currentGroupForm->addRow(label, container);\n"
    "  } else {\n"
    "    m_form->addRow(label, container);\n"
    "  }\n\n"
    "  // Connect changes: UI -> State\n"
    "  connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,",
    "  if (m_currentGroupForm) {\n"
    "    m_currentGroupForm->addRow(label, container);\n"
    "  } else {\n"
    "    m_form->addRow(label, container);\n"
    "  }\n\n"
    "  if (showDefaultReset) {\n"
    "    Q_ASSERT(!parameterKey.isEmpty());\n"
    "    const double defaultValue = getter(ParameterState());\n"
    "    addNumericDefaultPresentation(container, hLayout, label, parameterKey,\n"
    "                                  defaultValue, getter, setter, 1e-12);\n"
    "  }\n\n"
    "  // Connect changes: UI -> State\n"
    "  connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,",
)
replace_once(
    "src/qtgui/ParameterPanel.cpp",
    "    bool logarithmic, const QString &tooltip,\n"
    "    const QString &parameterKey) {",
    "    bool logarithmic, const QString &tooltip,\n"
    "    const QString &parameterKey, bool showDefaultReset) {",
)
# The slider addRow block is textually identical to the earlier double block;
# operate only within addSliderParameter's function region.
text = read("src/qtgui/ParameterPanel.cpp")
start = text.index("QWidget *ParameterPanel::addSliderParameter(")
end = text.index("QWidget* ParameterPanel::addSlider(", start)
region = text[start:end]
old = (
    "  if (m_currentGroupForm) {\n"
    "    m_currentGroupForm->addRow(label, container);\n"
    "  } else {\n"
    "    m_form->addRow(label, container);\n"
    "  }\n\n"
    "  // Helper to map Slider -> Value"
)
if region.count(old) != 1:
    raise RuntimeError(f"slider addRow region expected once, found {region.count(old)}")
new = (
    "  if (m_currentGroupForm) {\n"
    "    m_currentGroupForm->addRow(label, container);\n"
    "  } else {\n"
    "    m_form->addRow(label, container);\n"
    "  }\n\n"
    "  if (showDefaultReset) {\n"
    "    Q_ASSERT(!parameterKey.isEmpty());\n"
    "    const double defaultValue = getter(ParameterState());\n"
    "    const double tolerance =\n"
    "        scale > 0 ? 0.5 / scale : std::numeric_limits<double>::epsilon();\n"
    "    addNumericDefaultPresentation(container, hLayout, label, parameterKey,\n"
    "                                  defaultValue, getter, setter, tolerance);\n"
    "  }\n\n"
    "  // Helper to map Slider -> Value"
)
region = region.replace(old, new, 1)
write("src/qtgui/ParameterPanel.cpp", text[:start] + region + text[end:])
replace_once(
    "src/qtgui/ParameterPanel.cpp",
    "#include <cmath>\n",
    "#include <cmath>\n#include <limits>\n",
)

# Digital Capture pilot. Wavelengths are intentionally excluded: their stored
# default sentinel (0) is represented through the slider minimum/special text.
replace_once(
    "src/qtgui/CapturePanel.cpp",
    "        nullptr,\n"
    "        \"Gamma correction applied to the input scan.\"\n"
    "    );",
    "        nullptr,\n"
    "        \"Gamma correction applied to the input scan.\",\n"
    "        QStringLiteral(\"capture.gamma\"), true\n"
    "    );",
)
replace_once(
    "src/qtgui/CapturePanel.cpp",
    "        }, 1.0, nullptr, false, \"Scanner or camera resolution in Pixels Per Inch (PPI). Crucial for MTF-based sharpening.\");",
    "        }, 1.0, nullptr, false,\n"
    "        \"Scanner or camera resolution in Pixels Per Inch (PPI). Crucial for MTF-based sharpening.\",\n"
    "        QStringLiteral(\"capture.mtf.scan_dpi\"), true);",
)
replace_once(
    "src/qtgui/CapturePanel.cpp",
    "        }, 1.0, nullptr, false, \"Lens aperture used during capture. Affects diffraction part of the MTF model used for sharpening.\");",
    "        }, 1.0, nullptr, false,\n"
    "        \"Lens aperture used during capture. Affects diffraction part of the MTF model used for sharpening.\",\n"
    "        QStringLiteral(\"capture.mtf.f_stop\"), true);",
)
replace_once(
    "src/qtgui/CapturePanel.cpp",
    "        }, 1.0, nullptr, false, \"Physical distance between centers of adjacent pixels on the sensor.\");",
    "        }, 1.0, nullptr, false,\n"
    "        \"Physical distance between centers of adjacent pixels on the sensor.\",\n"
    "        QStringLiteral(\"capture.mtf.pixel_pitch\"), true);",
)
replace_once(
    "src/qtgui/CapturePanel.cpp",
    "        }, 1.0, nullptr, false, \"The fraction of the pixel area that is sensitive to light. Affects sensor MTF used for sharpening.\");",
    "        }, 1.0, nullptr, false,\n"
    "        \"The fraction of the pixel area that is sensitive to light. Affects sensor MTF used for sharpening.\",\n"
    "        QStringLiteral(\"capture.mtf.sensor_fill_factor\"), true);",
)

# Workspace smoke: discover generic reset controls by stable key and test the
# complete modified -> Reset -> Undo -> Undo gesture.
replace_once(
    "src/qtgui/WorkspaceChurnSmoke.cpp",
    "QAction *undoParametersAction = first->findChild<QAction *>(\n"
    "    QStringLiteral(\"UndoParametersAction\"));\n",
    "QAction *undoParametersAction = first->findChild<QAction *>(\n"
    "    QStringLiteral(\"UndoParametersAction\"));\n"
    "const QList<QToolButton *> parameterResetButtons =\n"
    "    inspector->findChildren<QToolButton *>(\n"
    "        QStringLiteral(\"ParameterResetButton\"));\n"
    "auto findParameterResetButton =\n"
    "    [&parameterResetButtons](const QString &parameterKey) {\n"
    "      for (QToolButton *button : parameterResetButtons)\n"
    "        if (button && button->property(\"parameterKey\").toString() ==\n"
    "                          parameterKey)\n"
    "          return button;\n"
    "      return static_cast<QToolButton *>(nullptr);\n"
    "    };\n"
    "QToolButton *resolutionResetButton = findParameterResetButton(\n"
    "    QStringLiteral(\"capture.mtf.scan_dpi\"));\n"
    "QWidget *resolutionField =\n"
    "    resolutionResetButton ? resolutionResetButton->parentWidget() : nullptr;\n",
)
replace_once(
    "src/qtgui/WorkspaceChurnSmoke.cpp",
    "        if (!redStripWidth || !greenStripWidth ||\n"
    "            redStripWidth->property(\"parameterKey\").toString() !=\n"
    "                QStringLiteral(\"screen.red_strip_width\") ||\n"
    "            greenStripWidth->property(\"parameterKey\").toString() !=\n"
    "                QStringLiteral(\"screen.green_strip_width\")) {\n"
    "          fail(QStringLiteral(\n"
    "              \"Workspace churn lost stable parameter-key metadata\"));\n"
    "          return;\n"
    "        }\n\n",
    "        if (!redStripWidth || !greenStripWidth ||\n"
    "            redStripWidth->property(\"parameterKey\").toString() !=\n"
    "                QStringLiteral(\"screen.red_strip_width\") ||\n"
    "            greenStripWidth->property(\"parameterKey\").toString() !=\n"
    "                QStringLiteral(\"screen.green_strip_width\")) {\n"
    "          fail(QStringLiteral(\n"
    "              \"Workspace churn lost stable parameter-key metadata\"));\n"
    "          return;\n"
    "        }\n\n"
    "        const QStringList captureDefaultKeys = {\n"
    "            QStringLiteral(\"capture.gamma\"),\n"
    "            QStringLiteral(\"capture.mtf.scan_dpi\"),\n"
    "            QStringLiteral(\"capture.mtf.f_stop\"),\n"
    "            QStringLiteral(\"capture.mtf.pixel_pitch\"),\n"
    "            QStringLiteral(\"capture.mtf.sensor_fill_factor\")};\n"
    "        for (const QString &key : captureDefaultKeys) {\n"
    "          QToolButton *button = findParameterResetButton(key);\n"
    "          if (!button || !button->property(\"parameterDefaultValue\").isValid()) {\n"
    "            fail(QStringLiteral(\n"
    "                     \"Workspace churn lost capture default/reset metadata for %1\")\n"
    "                     .arg(key));\n"
    "            return;\n"
    "          }\n"
    "        }\n"
    "        if (!resolutionResetButton || !resolutionField) {\n"
    "          fail(QStringLiteral(\n"
    "              \"Workspace churn lost the resolution default/reset row\"));\n"
    "          return;\n"
    "        }\n\n",
)
# Insert the reset gesture immediately after the existing same-key coalescing test.
needle = (
    "        undoParametersAction->trigger();\n"
    "        if (first->documentStateSnapshot() != undoBaseline) {\n"
    "          fail(QStringLiteral(\n"
    "              \"Undo failed to coalesce repeated updates for one parameter key\"));\n"
    "          return;\n"
    "        }\n\n"
    "        // Folding the guide is presentation state only."
)
replacement = (
    "        undoParametersAction->trigger();\n"
    "        if (first->documentStateSnapshot() != undoBaseline) {\n"
    "          fail(QStringLiteral(\n"
    "              \"Undo failed to coalesce repeated updates for one parameter key\"));\n"
    "          return;\n"
    "        }\n\n"
    "        // A visible Reset is progressive disclosure: it appears only\n"
    "        // after a keyed value differs from its real ParameterState default.\n"
    "        // Reset is a separate undo gesture so one Undo restores the value\n"
    "        // immediately before Reset, and a second Undo restores the baseline.\n"
    "        const ParameterState resetBaseline = first->documentStateSnapshot();\n"
    "        const ParameterState defaults;\n"
    "        const double defaultResolution =\n"
    "            defaults.rparams.sharpen.scanner_mtf.scan_dpi;\n"
    "        double modifiedResolution = defaultResolution + 4321.0;\n"
    "        if (std::abs(modifiedResolution -\n"
    "                     resetBaseline.rparams.sharpen.scanner_mtf.scan_dpi) <\n"
    "            0.1)\n"
    "          modifiedResolution = defaultResolution + 3210.0;\n"
    "        ParameterState modifiedCapture = resetBaseline;\n"
    "        modifiedCapture.rparams.sharpen.scanner_mtf.scan_dpi =\n"
    "            modifiedResolution;\n"
    "        first->applySharedDocumentState(\n"
    "            modifiedCapture, QStringLiteral(\"Resolution\"),\n"
    "            QStringLiteral(\"capture.mtf.scan_dpi\"));\n"
    "        if (std::abs(first->documentStateSnapshot()\n"
    "                         .rparams.sharpen.scanner_mtf.scan_dpi -\n"
    "                     modifiedResolution) >\n"
    "                0.01 ||\n"
    "            resolutionResetButton->isHidden() ||\n"
    "            !resolutionField->property(\"parameterModified\").toBool()) {\n"
    "          fail(QStringLiteral(\n"
    "              \"Modified capture parameter did not expose Reset state\"));\n"
    "          return;\n"
    "        }\n"
    "        resolutionResetButton->click();\n"
    "        if (std::abs(first->documentStateSnapshot()\n"
    "                         .rparams.sharpen.scanner_mtf.scan_dpi -\n"
    "                     defaultResolution) >\n"
    "                0.01 ||\n"
    "            !resolutionResetButton->isHidden() ||\n"
    "            resolutionField->property(\"parameterModified\").toBool()) {\n"
    "          fail(QStringLiteral(\n"
    "              \"Capture Reset did not restore and hide the default state\"));\n"
    "          return;\n"
    "        }\n"
    "        undoParametersAction->trigger();\n"
    "        if (std::abs(first->documentStateSnapshot()\n"
    "                         .rparams.sharpen.scanner_mtf.scan_dpi -\n"
    "                     modifiedResolution) >\n"
    "                0.01 ||\n"
    "            resolutionResetButton->isHidden() ||\n"
    "            !resolutionField->property(\"parameterModified\").toBool()) {\n"
    "          fail(QStringLiteral(\n"
    "              \"Undo of capture Reset did not restore modified state\"));\n"
    "          return;\n"
    "        }\n"
    "        undoParametersAction->trigger();\n"
    "        if (first->documentStateSnapshot() != resetBaseline) {\n"
    "          fail(QStringLiteral(\n"
    "              \"Capture default/reset smoke did not restore its baseline\"));\n"
    "          return;\n"
    "        }\n\n"
    "        // Folding the guide is presentation state only."
)
replace_once("src/qtgui/WorkspaceChurnSmoke.cpp", needle, replacement)

# Contributor and roadmap notes.
replace_once(
    ".agents/qtgui.md",
    "    \"example.my_parameter\"       // Stable parameter key\n"
    ");",
    "    \"example.my_parameter\",      // Stable parameter key\n"
    "    true                         // Show Reset only while modified\n"
    ");",
)
replace_once(
    ".agents/qtgui.md",
    "- **`applyChange`**: Behind the scenes, the helper methods call `applyChange`, which triggers a global state update and UI refresh. Stateful helpers accept an optional stable, untranslated `parameterKey`; use it for new or actively maintained controls so Undo merging and future Reset/default metadata do not depend on the visible label.\n",
    "- **`applyChange`**: Behind the scenes, the helper methods call `applyChange`, which triggers a global state update and UI refresh. Stateful helpers accept an optional stable, untranslated `parameterKey`; use it for new or actively maintained controls so Undo merging and Reset/default metadata do not depend on the visible label. Keyed numeric helpers can opt into the standard default presentation: the label becomes semibold and a small Reset control appears only while the value differs from a fresh `ParameterState` default. Reset remains a separate undo gesture.\n",
)
replace_once(
    "doc/qtgui-internal-cleanup.md",
    "  as panels gain reset/default/modified metadata.\n",
    "  as panels gain reset/default/modified metadata. Keyed numeric helpers can\n"
    "  now opt into that presentation without a parallel defaults table: a fresh\n"
    "  `ParameterState` supplies the reset target, the label is emphasized while\n"
    "  modified, and Reset is hidden at the default. Digital Capture is the first\n"
    "  pilot (gamma, resolution, f-stop, pixel pitch and sensor fill factor).\n",
)
replace_once(
    "doc/qtgui-workflow-roadmap.md",
    "- teach `ParameterPanel` reset/default/modified metadata;\n",
    "- teach `ParameterPanel` reset/default/modified metadata; the first numeric\n"
    "  pilot is active in Digital Capture, with Reset disclosed only for values\n"
    "  that differ from their real `ParameterState` defaults;\n",
)
