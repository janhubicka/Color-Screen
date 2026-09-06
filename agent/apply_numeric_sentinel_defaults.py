from pathlib import Path

ROOT = Path("target")


def replace_exact(path, old, new, expected=1):
    file_path = ROOT / path
    text = file_path.read_text()
    count = text.count(old)
    if count != expected:
        raise RuntimeError(
            f"{path}: expected {expected} copies, found {count}: {old[:120]!r}"
        )
    file_path.write_text(text.replace(old, new))


# ParameterPanel: a stored sentinel may live below the ordinary numeric interval.
path = "src/qtgui/ParameterPanel.h"
replace_exact(
    path,
    "#include <memory>\n#include <vector>",
    "#include <memory>\n#include <optional>\n#include <vector>",
)
replace_exact(
    path,
    "      bool logarithmic = false, const QString &tooltip = QString(),\n"
    "      const QString &parameterKey = QString(),\n"
    "      bool showDefaultReset = false);",
    "      bool logarithmic = false, const QString &tooltip = QString(),\n"
    "      const QString &parameterKey = QString(),\n"
    "      bool showDefaultReset = false,\n"
    "      std::optional<double> specialMinimumValue = std::nullopt);",
)
replace_exact(
    path,
    "    scale: factor to map double value to integer slider range (e.g. 100 for 0.01\n"
    "    precision). decimals: precision for SpinBox.\n"
    "  */",
    "    scale: factor to map double value to integer slider range (e.g. 100 for 0.01\n"
    "    precision). decimals: precision for SpinBox. SPECIALMINIMUMVALUE is an\n"
    "    optional stored sentinel below MIN, such as 0 meaning not configured.\n"
    "    It gets one separate slider/spinbox position and is never confused with\n"
    "    the first explicit numeric value.\n"
    "  */",
)

path = "src/qtgui/ParameterPanel.cpp"
replace_exact(
    path,
    'constexpr auto parameterModifiedProperty = "parameterModified";\n'
    'constexpr auto parameterSectionExpandedProperty = "parameterSectionExpanded";',
    'constexpr auto parameterModifiedProperty = "parameterModified";\n'
    'constexpr auto parameterSpecialStateValueProperty = "parameterSpecialStateValue";\n'
    'constexpr auto parameterSectionExpandedProperty = "parameterSectionExpanded";',
)
marker = '''/** Return whether WIDGET is logically applicable to the current panel state. */
bool parameterWidgetApplicable(const QWidget *widget) {
  if (!widget)
    return true;
  const QVariant value = widget->property(parameterApplicableProperty);
  return !value.isValid() || value.toBool();
}
'''
insert = marker + '''
/** Numeric editor with one explicit stored sentinel below its ordinary range.

    QDoubleSpinBox normally ties specialValueText() to minimum().  Extending the
    internal range down to the sentinel would therefore also make all values
    between the sentinel and the real numeric minimum editable.  This editor
    keeps that interval unavailable while still letting the sentinel round-trip
    exactly. */
class ParameterSliderSpinBox final : public QDoubleSpinBox {
public:
  using QDoubleSpinBox::QDoubleSpinBox;

  void setSeparatedMinimum(double specialValue, double regularMinimum) {
    Q_ASSERT(specialValue < regularMinimum);
    m_specialValue = specialValue;
    m_regularMinimum = regularMinimum;
    m_hasSeparatedMinimum = true;
  }

protected:
  QValidator::State validate(QString &input, int &pos) const override {
    const QValidator::State baseState = QDoubleSpinBox::validate(input, pos);
    if (!m_hasSeparatedMinimum || baseState != QValidator::Acceptable)
      return baseState;
    if (!specialValueText().isEmpty() &&
        input.trimmed() == specialValueText())
      return QValidator::Acceptable;

    const double parsed = QDoubleSpinBox::valueFromText(input);
    if (near(parsed, m_specialValue) || parsed >= m_regularMinimum)
      return QValidator::Acceptable;

    // Keep partial input such as "4" valid while the user is typing "400",
    // but never commit a value from the sentinel-to-numeric gap.
    return QValidator::Intermediate;
  }

  void stepBy(int steps) override {
    if (!m_hasSeparatedMinimum || steps == 0) {
      QDoubleSpinBox::stepBy(steps);
      return;
    }

    const double current = value();
    if (steps > 0 && near(current, m_specialValue)) {
      setValue(m_regularMinimum);
      --steps;
    } else if (steps < 0 && near(current, m_regularMinimum)) {
      setValue(m_specialValue);
      ++steps;
    }
    if (steps == 0)
      return;

    QDoubleSpinBox::stepBy(steps);
    const double stepped = value();
    if (stepped > m_specialValue && stepped < m_regularMinimum)
      setValue(steps > 0 ? m_regularMinimum : m_specialValue);
  }

private:
  bool near(double a, double b) const {
    return qAbs(a - b) <= qMax(1e-12, singleStep() * 0.5);
  }

  bool m_hasSeparatedMinimum = false;
  double m_specialValue = 0;
  double m_regularMinimum = 0;
};
'''
replace_exact(path, marker, insert)
replace_exact(
    path,
    "    std::function<void(ParameterState &, double)> setter, double gamma,\n"
    "    std::function<bool(const ParameterState &)> enabledCheck,\n"
    "    bool logarithmic, const QString &tooltip,\n"
    "    const QString &parameterKey, bool showDefaultReset) {",
    "    std::function<void(ParameterState &, double)> setter, double gamma,\n"
    "    std::function<bool(const ParameterState &)> enabledCheck,\n"
    "    bool logarithmic, const QString &tooltip,\n"
    "    const QString &parameterKey, bool showDefaultReset,\n"
    "    std::optional<double> specialMinimumValue) {",
)
replace_exact(
    path,
    "  // Container: Slider + SpinBox\n"
    "  QWidget *container = new QWidget();\n"
    "  QHBoxLayout *hLayout = new QHBoxLayout(container);\n"
    "  hLayout->setContentsMargins(0, 0, 0, 0);\n\n"
    "  QSlider *slider = new QSlider(Qt::Horizontal);",
    "  Q_ASSERT(!specialMinimumValue.has_value() ||\n"
    "           *specialMinimumValue <= min);\n"
    "  const bool hasSeparatedSpecialMinimum =\n"
    "      specialMinimumValue.has_value() && *specialMinimumValue < min;\n"
    "  const double specialStateValue =\n"
    "      specialMinimumValue.value_or(min);\n\n"
    "  // Container: Slider + SpinBox\n"
    "  QWidget *container = new QWidget();\n"
    "  QHBoxLayout *hLayout = new QHBoxLayout(container);\n"
    "  hLayout->setContentsMargins(0, 0, 0, 0);\n\n"
    "  QSlider *slider = new QSlider(Qt::Horizontal);",
)
replace_exact(
    path,
    "  if (gamma != 1.0 || logarithmic) {\n"
    "    slider->setRange(0, SLIDER_MAX);\n"
    "  } else {\n"
    "    int minInt = min * scale;\n"
    "    int maxInt = max * scale;\n"
    "    slider->setRange(minInt, maxInt);\n"
    "  }\n\n"
    "  QDoubleSpinBox *spin = new QDoubleSpinBox();\n"
    "  spin->setRange(min, max);",
    "  const int regularLinearSliderMin = (int)(min * scale);\n"
    "  const int regularLinearSliderMax = (int)(max * scale);\n"
    "  const bool nonlinearSlider = gamma != 1.0 || logarithmic;\n"
    "  const int regularSliderMin =\n"
    "      nonlinearSlider ? (hasSeparatedSpecialMinimum ? 1 : 0)\n"
    "                      : regularLinearSliderMin;\n"
    "  const int specialSliderPosition =\n"
    "      nonlinearSlider ? 0 : regularLinearSliderMin - 1;\n"
    "  if (nonlinearSlider)\n"
    "    slider->setRange(0, SLIDER_MAX);\n"
    "  else\n"
    "    slider->setRange(hasSeparatedSpecialMinimum ? specialSliderPosition\n"
    "                                                : regularLinearSliderMin,\n"
    "                     regularLinearSliderMax);\n\n"
    "  auto *spin = new ParameterSliderSpinBox();\n"
    "  if (hasSeparatedSpecialMinimum) {\n"
    "    spin->setRange(specialStateValue, max);\n"
    "    spin->setSeparatedMinimum(specialStateValue, min);\n"
    "  } else {\n"
    "    spin->setRange(min, max);\n"
    "  }",
)
replace_exact(
    path,
    "  setParameterKey(container, parameterKey);\n"
    "  setParameterKey(slider, parameterKey);\n"
    "  setParameterKey(spin, parameterKey);\n\n"
    "  hLayout->addWidget(slider, 1);",
    "  setParameterKey(container, parameterKey);\n"
    "  setParameterKey(slider, parameterKey);\n"
    "  setParameterKey(spin, parameterKey);\n"
    "  if (specialMinimumValue.has_value()) {\n"
    "    container->setProperty(parameterSpecialStateValueProperty,\n"
    "                           *specialMinimumValue);\n"
    "    slider->setProperty(parameterSpecialStateValueProperty,\n"
    "                        *specialMinimumValue);\n"
    "    spin->setProperty(parameterSpecialStateValueProperty,\n"
    "                      *specialMinimumValue);\n"
    "  }\n\n"
    "  hLayout->addWidget(slider, 1);",
)
replace_exact(
    path,
    "  auto sliderToValue = [min, max, scale, gamma, SLIDER_MAX,\n"
    "                        logarithmic](int s) -> double {\n"
    "    if (!logarithmic && gamma == 1.0)\n"
    "      return (double)s / scale;\n\n"
    "    double t = (double)s / SLIDER_MAX; // 0..1\n",
    "  auto sliderToValue =\n"
    "      [min, max, scale, gamma, SLIDER_MAX, logarithmic,\n"
    "       hasSeparatedSpecialMinimum, specialStateValue,\n"
    "       regularSliderMin, specialSliderPosition](int s) -> double {\n"
    "    if (hasSeparatedSpecialMinimum && s == specialSliderPosition)\n"
    "      return specialStateValue;\n"
    "    if (!logarithmic && gamma == 1.0)\n"
    "      return (double)s / scale;\n\n"
    "    const double t =\n"
    "        (double)(s - regularSliderMin) / (SLIDER_MAX - regularSliderMin);\n",
)
replace_exact(
    path,
    "  auto valueToSlider = [min, max, scale, gamma, SLIDER_MAX,\n"
    "                        logarithmic](double v) -> int {\n"
    "    if (!logarithmic && gamma == 1.0)\n"
    "      return qRound(v * scale);\n\n"
    "    double t = 0;",
    "  auto valueToSlider =\n"
    "      [min, max, scale, gamma, SLIDER_MAX, logarithmic,\n"
    "       hasSeparatedSpecialMinimum, specialStateValue, regularSliderMin,\n"
    "       regularLinearSliderMax, specialSliderPosition](double v) -> int {\n"
    "    if (hasSeparatedSpecialMinimum &&\n"
    "        qAbs(v - specialStateValue) <= 1e-12)\n"
    "      return specialSliderPosition;\n"
    "    if (!logarithmic && gamma == 1.0) {\n"
    "      const int mapped = qRound(v * scale);\n"
    "      return hasSeparatedSpecialMinimum\n"
    "                 ? std::clamp(mapped, regularSliderMin,\n"
    "                              regularLinearSliderMax)\n"
    "                 : mapped;\n"
    "    }\n\n"
    "    double t = 0;",
)
replace_exact(
    path,
    "    return std::clamp((int)qRound(t * SLIDER_MAX), 0, SLIDER_MAX);\n"
    "  };",
    "    return std::clamp(\n"
    "        (int)qRound(t * (SLIDER_MAX - regularSliderMin) +\n"
    "                    regularSliderMin),\n"
    "        regularSliderMin, SLIDER_MAX);\n"
    "  };",
    expected=1,
)

# Capture wavelengths: stored zero is an explicit 'not configured' state, not
# an explicit 380 nm wavelength. Give it its own slider position and Reset UI.
path = "src/qtgui/CapturePanel.cpp"
for old_text, new_text in [
    ("default (600nm)", "default (600 nm)"),
    ("default (530nm)", "default (530 nm)"),
    ("default (450nm)", "default (450 nm)"),
    ("default (750nm)", "default (750 nm)"),
    ("default (550nm)", "default (550 nm)"),
]:
    text = (ROOT / path).read_text()
    if old_text in text:
        (ROOT / path).write_text(text.replace(old_text, new_text))

wavelength_replacements = [
    (
        '        }, 1.0, nullptr, false, "Wavelength in nanometers used for MTF modeling of diffraction for the red channel.");',
        '        }, 1.0, nullptr, false, "Wavelength in nanometers used for MTF modeling of diffraction for the red channel.",\n'
        '        QStringLiteral("capture.mtf.wavelength.red"), true, 0.0);',
    ),
    (
        '        }, 1.0, nullptr, false, "Wavelength in nanometers used for MTF modeling of diffraction for the green channel.");',
        '        }, 1.0, nullptr, false, "Wavelength in nanometers used for MTF modeling of diffraction for the green channel.",\n'
        '        QStringLiteral("capture.mtf.wavelength.green"), true, 0.0);',
    ),
    (
        '        }, 1.0, nullptr, false, "Wavelength in nanometers used for MTF modeling of diffraction for the blue channel.");',
        '        }, 1.0, nullptr, false, "Wavelength in nanometers used for MTF modeling of diffraction for the blue channel.",\n'
        '        QStringLiteral("capture.mtf.wavelength.blue"), true, 0.0);',
    ),
    (
        '        }, 1.0, nullptr, false, "Wavelength in nanometers used for MTF modeling of diffraction for the scalar or infrared channel.");',
        '        }, 1.0, nullptr, false, "Wavelength in nanometers used for MTF modeling of diffraction for the scalar or infrared channel.",\n'
        '        QStringLiteral("capture.mtf.wavelength.scalar"), true, 0.0);',
    ),
]
for old, new in wavelength_replacements:
    replace_exact(path, old, new)

# Screen strip-width zero has the same semantic meaning, but it is already the
# numeric minimum, so the standard special-value presentation is sufficient.
path = "src/qtgui/ScreenPanel.cpp"
replace_exact(
    path,
    '      "Red Strip Width", 0.0, 1.0, 100.0, 2, "", "",',
    '      "Red Strip Width", 0.0, 1.0, 100.0, 2, "", "process default",',
)
replace_exact(
    path,
    '      QStringLiteral("screen.red_strip_width"));',
    '      QStringLiteral("screen.red_strip_width"), true);',
)
replace_exact(
    path,
    '      "Green Strip Width", 0.0, 1.0, 100.0, 2, "", "",',
    '      "Green Strip Width", 0.0, 1.0, 100.0, 2, "", "process default",',
)
replace_exact(
    path,
    '      QStringLiteral("screen.green_strip_width"));',
    '      QStringLiteral("screen.green_strip_width"), true);',
)

# Smoke: verify zero is represented as a distinct sentinel and Reset round-trips
# it without confusing explicit 380 nm with the default state.
path = "src/qtgui/WorkspaceChurnSmoke.cpp"
replace_exact(
    path,
    "#include <QDebug>\n#include <QEvent>",
    "#include <QDebug>\n#include <QDoubleSpinBox>\n#include <QEvent>",
)
replace_exact(
    path,
    "QToolButton *resolutionResetButton = findParameterResetButton(\n"
    "    QStringLiteral(\"capture.mtf.scan_dpi\"));\n"
    "QWidget *resolutionField =\n"
    "    resolutionResetButton ? resolutionResetButton->parentWidget() : nullptr;",
    "QToolButton *resolutionResetButton = findParameterResetButton(\n"
    "    QStringLiteral(\"capture.mtf.scan_dpi\"));\n"
    "QWidget *resolutionField =\n"
    "    resolutionResetButton ? resolutionResetButton->parentWidget() : nullptr;\n"
    "QToolButton *redWavelengthResetButton = findParameterResetButton(\n"
    "    QStringLiteral(\"capture.mtf.wavelength.red\"));\n"
    "QWidget *redWavelengthField = redWavelengthResetButton\n"
    "                                  ? redWavelengthResetButton->parentWidget()\n"
    "                                  : nullptr;\n"
    "QDoubleSpinBox *redWavelengthSpin =\n"
    "    redWavelengthField\n"
    "        ? redWavelengthField->findChild<QDoubleSpinBox *>()\n"
    "        : nullptr;",
)
replace_exact(
    path,
    "            QStringLiteral(\"capture.mtf.pixel_pitch\"),\n"
    "            QStringLiteral(\"capture.mtf.sensor_fill_factor\")};",
    "            QStringLiteral(\"capture.mtf.pixel_pitch\"),\n"
    "            QStringLiteral(\"capture.mtf.sensor_fill_factor\"),\n"
    "            QStringLiteral(\"capture.mtf.wavelength.red\"),\n"
    "            QStringLiteral(\"capture.mtf.wavelength.green\"),\n"
    "            QStringLiteral(\"capture.mtf.wavelength.blue\"),\n"
    "            QStringLiteral(\"capture.mtf.wavelength.scalar\")};",
)
replace_exact(
    path,
    "        if (!resolutionResetButton || !resolutionField) {\n"
    "          fail(QStringLiteral(\n"
    "              \"Workspace churn lost the resolution default/reset row\"));\n"
    "          return;\n"
    "        }",
    "        if (!resolutionResetButton || !resolutionField ||\n"
    "            !redWavelengthResetButton || !redWavelengthField ||\n"
    "            !redWavelengthSpin ||\n"
    "            redWavelengthField->property(\"parameterSpecialStateValue\")\n"
    "                    .toDouble() != 0.0) {\n"
    "          fail(QStringLiteral(\n"
    "              \"Workspace churn lost numeric default/sentinel rows\"));\n"
    "          return;\n"
    "        }",
)
needle = '''        if (first->documentStateSnapshot() != resetBaseline) {
          fail(QStringLiteral(
              "Capture default/reset smoke did not restore its baseline"));
          return;
        }

        // Folding the guide is presentation state only. It must not hide the
'''
replacement = '''        if (first->documentStateSnapshot() != resetBaseline) {
          fail(QStringLiteral(
              "Capture default/reset smoke did not restore its baseline"));
          return;
        }

        // Zero wavelength means not explicitly configured. It must round-trip
        // as zero and occupy a distinct UI position from an explicit 380 nm,
        // even though 380 nm is the ordinary numeric minimum.
        const ParameterState wavelengthBaseline = first->documentStateSnapshot();
        if (wavelengthBaseline.rparams.sharpen.scanner_mtf.wavelengths[0] != 0 ||
            redWavelengthSpin->value() != 0 ||
            !redWavelengthSpin->text().contains(
                QStringLiteral("default"), Qt::CaseInsensitive) ||
            !redWavelengthResetButton->isHidden() ||
            redWavelengthField->property("parameterModified").toBool()) {
          fail(QStringLiteral(
              "Capture wavelength zero sentinel was not represented as default"));
          return;
        }

        ParameterState explicitWavelength = wavelengthBaseline;
        explicitWavelength.rparams.sharpen.scanner_mtf.wavelengths[0] = 380;
        first->applySharedDocumentState(
            explicitWavelength, QStringLiteral("Red wavelength"),
            QStringLiteral("capture.mtf.wavelength.red"));
        if (first->documentStateSnapshot()
                    .rparams.sharpen.scanner_mtf.wavelengths[0] != 380 ||
            redWavelengthSpin->value() != 380 ||
            redWavelengthSpin->text().contains(
                QStringLiteral("default"), Qt::CaseInsensitive) ||
            redWavelengthResetButton->isHidden() ||
            !redWavelengthField->property("parameterModified").toBool()) {
          fail(QStringLiteral(
              "Explicit minimum wavelength was confused with default sentinel"));
          return;
        }

        redWavelengthResetButton->click();
        if (first->documentStateSnapshot()
                    .rparams.sharpen.scanner_mtf.wavelengths[0] != 0 ||
            redWavelengthSpin->value() != 0 ||
            !redWavelengthResetButton->isHidden() ||
            redWavelengthField->property("parameterModified").toBool()) {
          fail(QStringLiteral(
              "Wavelength Reset did not restore explicit zero sentinel"));
          return;
        }
        undoParametersAction->trigger();
        if (first->documentStateSnapshot()
                    .rparams.sharpen.scanner_mtf.wavelengths[0] != 380 ||
            redWavelengthSpin->value() != 380 ||
            redWavelengthResetButton->isHidden()) {
          fail(QStringLiteral(
              "Undo of wavelength Reset did not restore explicit minimum"));
          return;
        }
        undoParametersAction->trigger();
        if (first->documentStateSnapshot() != wavelengthBaseline) {
          fail(QStringLiteral(
              "Wavelength sentinel smoke did not restore its baseline"));
          return;
        }

        // Folding the guide is presentation state only. It must not hide the
'''
replace_exact(path, needle, replacement)

# Contributor/tracking docs: make the semantic distinction explicit.
path = ".agents/qtgui.md"
replace_exact(
    path,
    "- **`applyChange`**: Behind the scenes, the helper methods call `applyChange`, which triggers a global state update and UI refresh. Stateful helpers accept an optional stable, untranslated `parameterKey`; use it for new or actively maintained controls so Undo merging and Reset/default metadata do not depend on the visible label. Keyed numeric helpers can opt into the standard default presentation: the label becomes semibold and a small Reset control appears only while the value differs from a fresh `ParameterState` default. Reset remains a separate undo gesture.",
    "- **`applyChange`**: Behind the scenes, the helper methods call `applyChange`, which triggers a global state update and UI refresh. Stateful helpers accept an optional stable, untranslated `parameterKey`; use it for new or actively maintained controls so Undo merging and Reset/default metadata do not depend on the visible label. Keyed numeric helpers can opt into the standard default presentation: the label becomes semibold and a small Reset control appears only while the value differs from a fresh `ParameterState` default. Reset remains a separate undo gesture. Preserve explicit sentinel values such as `0 = not configured/process default`: when a sentinel lies below the ordinary numeric interval, pass it as the helper's separate special minimum instead of clamping it to the first explicit value.",
)

path = "doc/qtgui-internal-cleanup.md"
replace_exact(
    path,
    "  pilot (gamma, resolution, f-stop, pixel pitch and sensor fill factor).",
    "  pilot (gamma, resolution, f-stop, pixel pitch and sensor fill factor).\n"
    "  Numeric sentinel defaults are state, not presentation: preserve values\n"
    "  such as `0 = not configured/use process default` exactly. `ParameterPanel`\n"
    "  gives a below-range sentinel its own slider/spinbox position; capture MTF\n"
    "  wavelengths and process strip widths exercise this rule. Values whose\n"
    "  sentinel is already inside the ordinary range (for example Geometry's\n"
    "  `0 = Auto`) need no special range handling.",
)

path = "doc/qtgui-workflow-roadmap.md"
replace_exact(
    path,
    "  pilot is active in Digital Capture, with Reset disclosed only for values\n"
    "  that differ from their real `ParameterState` defaults;",
    "  pilot is active in Digital Capture, with Reset disclosed only for values\n"
    "  that differ from their real `ParameterState` defaults. Explicit sentinels\n"
    "  such as `0 = not configured/use process default` remain first-class stored\n"
    "  defaults even when the ordinary numeric editing range starts above zero;",
)
