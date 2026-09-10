from pathlib import Path
import subprocess


def replace(path, old, new, count=1):
    p = Path(path)
    text = p.read_text()
    found = text.count(old)
    if found != count:
        raise RuntimeError(f"{path}: expected {count} matches, found {found}: {old[:100]!r}")
    p.write_text(text.replace(old, new, count))

replace(
    'src/qtgui/ParameterPanel.h',
    '''  QCheckBox *addCheckboxParameter(
      const QString &label, std::function<bool(const ParameterState &)> getter,
''',
    '''  /** Add a stateful checkbox row. ENABLEDCHECK controls enablement only;
      use setParameterApplicability() when the concept should disappear. */
  QCheckBox *addCheckboxParameter(
      const QString &label, std::function<bool(const ParameterState &)> getter,
''')
replace(
    'src/qtgui/ParameterPanel.h',
    '''  QCheckBox *addCheckboxWithReset(
      const QString &label, std::function<bool(const ParameterState &)> getter,
''',
    '''  /** Add a stateful checkbox with an explicit Reset action. ENABLEDCHECK
      has the same enable-only semantics as every other parameter helper. */
  QCheckBox *addCheckboxWithReset(
      const QString &label, std::function<bool(const ParameterState &)> getter,
''')

old_checkbox_enablement = '''  // Enable/Visibility Update
  if (enabledCheck) {
    m_widgetStateUpdaters.push_back([this, container, enabledCheck]() {
      ParameterState state = m_stateGetter();
      bool visible = enabledCheck(state);
      container->setVisible(visible);
    });
  }
'''
new_checkbox_enablement = '''  // Enablement is independent of applicability/visibility. Disabling the
  // container keeps the inline label (and Reset control, when present) in the
  // same prerequisite state as the checkbox itself.
  if (enabledCheck) {
    m_widgetStateUpdaters.push_back([this, container, enabledCheck]() {
      const bool enabled = enabledCheck(m_stateGetter());
      container->setEnabled(enabled);
    });
  }
'''
replace('src/qtgui/ParameterPanel.cpp', old_checkbox_enablement,
        new_checkbox_enablement, count=2)

replace(
    'src/qtgui/ImageLayerPanel.cpp',
    '''  m_form->addRow(m_ignoreInfraredCheck);

  connect(m_ignoreInfraredCheck, &QCheckBox::toggled, this, [this](bool checked) {
''',
    '''  m_form->addRow(m_ignoreInfraredCheck);
  setParameterApplicability(
      m_ignoreInfraredCheck, [this](const ParameterState &) {
        const auto img = m_imageGetter();
        return img && img->has_rgb() && img->has_grayscale_or_ir();
      });

  connect(m_ignoreInfraredCheck, &QCheckBox::toggled, this, [this](bool checked) {
''')
replace(
    'src/qtgui/ImageLayerPanel.cpp',
    '''        const bool hasRgb = img && img->has_rgb();
        const bool hasNativeLayer = img && img->has_grayscale_or_ir();
        const bool hasSourceChoice = hasRgb && hasNativeLayer;
        const bool simulatedActive = enableSimulated(state);
        if (m_ignoreInfraredCheck)
          m_ignoreInfraredCheck->setVisible(hasSourceChoice);
        if (simulatedSection)
          simulatedSection->setVisible(simulatedActive);
''',
    '''        const bool simulatedActive = enableSimulated(state);
        if (simulatedSection)
          simulatedSection->setVisible(simulatedActive);
''')

replace(
    'src/qtgui/GeometryPanel.cpp',
    '''  addCheckboxParameter(
      "Mirror final image",
      [](const ParameterState &s) { return s.scrToImg.final_mirror; },
      [](ParameterState &s, bool v) { s.scrToImg.final_mirror = v; },
      hasFinalGeometry,
      "Mirror the final-coordinate image horizontally before Final rotation. "
      "This is part of geometry and is saved in the parameter file.");
''',
    '''  QCheckBox *finalMirrorCheck = addCheckboxParameter(
      "Mirror final image",
      [](const ParameterState &s) { return s.scrToImg.final_mirror; },
      [](ParameterState &s, bool v) { s.scrToImg.final_mirror = v; },
      hasFinalGeometry,
      "Mirror the final-coordinate image horizontally before Final rotation. "
      "This is part of geometry and is saved in the parameter file.");
  finalMirrorCheck->setObjectName(QStringLiteral("GeometryFinalMirrorCheck"));
''')

replace(
    'src/qtgui/main.cpp',
    '''#include "MultiLineTabWidget.h"
#include "ImageViewWindow.h"
''',
    '''#include "MultiLineTabWidget.h"
#include "ParameterPanel.h"
#include "ImageViewWindow.h"
''')
replace(
    'src/qtgui/main.cpp',
    '''class PointerSmokeToneCurve final : public ToneCurveWidget {
public:
  using ToneCurveWidget::ToneCurveWidget;
  QPointF plotPoint(double x, double y) const { return plotToWidget(x, y); }
};

''',
    '''class PointerSmokeToneCurve final : public ToneCurveWidget {
public:
  using ToneCurveWidget::ToneCurveWidget;
  QPointF plotPoint(double x, double y) const { return plotToWidget(x, y); }
};

/** Expose checkbox helpers so their common enabled/applicable contract can be
    checked without relying on a particular processing panel. */
class CheckboxSemanticsProbe final : public ParameterPanel {
public:
  using ParameterPanel::ParameterPanel;
  using ParameterPanel::addCheckboxParameter;
  using ParameterPanel::addCheckboxWithReset;
};

''')
replace(
    'src/qtgui/main.cpp',
    '''  tabs.setTabVisible(secondTab, true);
  tabs.setCurrentIndex(secondTab);
  if (tabs.currentIndex() != secondTab)
    return fail("logically visible tab stayed unavailable under hidden ancestor");

  // Focus analysis is now a plain background helper. Missing input must fail
''',
    '''  tabs.setTabVisible(secondTab, true);
  tabs.setCurrentIndex(secondTab);
  if (tabs.currentIndex() != secondTab)
    return fail("logically visible tab stayed unavailable under hidden ancestor");

  // Checkbox enabledCheck must match the rest of ParameterPanel: a missing
  // prerequisite disables a still-visible row. Logical disappearance is an
  // explicit setParameterApplicability() decision, never an overloaded meaning
  // of the helper argument.
  ParameterState checkboxProbeState;
  bool checkboxPrerequisite = false;
  CheckboxSemanticsProbe checkboxProbe(
      [&checkboxProbeState]() { return checkboxProbeState; },
      [&checkboxProbeState](const ParameterState &state, const QString &,
                            const QString &) { checkboxProbeState = state; },
      []() { return std::shared_ptr<colorscreen::image_data>(); }, nullptr,
      false);
  auto checkboxEnabled = [&checkboxPrerequisite](const ParameterState &) {
    return checkboxPrerequisite;
  };
  QCheckBox *plainCheckbox = checkboxProbe.addCheckboxParameter(
      QStringLiteral("Plain checkbox"),
      [](const ParameterState &state) { return state.rparams.scan_mirror; },
      [](ParameterState &state, bool value) { state.rparams.scan_mirror = value; },
      checkboxEnabled);
  QCheckBox *resetCheckbox = checkboxProbe.addCheckboxWithReset(
      QStringLiteral("Reset checkbox"),
      [](const ParameterState &state) { return state.scrToImg.final_mirror; },
      [](ParameterState &state, bool value) { state.scrToImg.final_mirror = value; },
      [](ParameterState &state) { state.scrToImg.final_mirror = false; },
      checkboxEnabled);
  checkboxProbe.updateUI();
  if (plainCheckbox->isHidden() || resetCheckbox->isHidden() ||
      plainCheckbox->isEnabled() || resetCheckbox->isEnabled())
    return fail("checkbox enabledCheck still changed visibility semantics");
  checkboxPrerequisite = true;
  checkboxProbe.updateUI();
  if (plainCheckbox->isHidden() || resetCheckbox->isHidden() ||
      !plainCheckbox->isEnabled() || !resetCheckbox->isEnabled())
    return fail("checkbox enabledCheck did not restore enablement");

  // Focus analysis is now a plain background helper. Missing input must fail
''')

replace(
    'src/qtgui/WorkspaceChurnSmoke.cpp',
    '''QToolButton *imageLayerToggle = inspector->findChild<QToolButton *>(
    QStringLiteral("SimulatedImageLayerToggle"));
QWidget *mtfUseMeasuredRow =
''',
    '''QToolButton *imageLayerToggle = inspector->findChild<QToolButton *>(
    QStringLiteral("SimulatedImageLayerToggle"));
QCheckBox *imageLayerSourceChoice = inspector->findChild<QCheckBox *>(
    QStringLiteral("ImageLayerUseSimulatedRgbCheck"));
QCheckBox *finalMirrorCheck = inspector->findChild<QCheckBox *>(
    QStringLiteral("GeometryFinalMirrorCheck"));
QWidget *mtfUseMeasuredRow =
''')
replace(
    'src/qtgui/WorkspaceChurnSmoke.cpp',
    '''        const bool imageLayerWasExpanded = imageLayerToggle->isChecked();
        imageLayerToggle->setChecked(false);
''',
    '''        const bool imageLayerSourceApplicable =
            imageLayerImage && imageLayerImage->has_rgb() &&
            imageLayerImage->has_grayscale_or_ir();
        const QVariant imageLayerSourceApplicability =
            imageLayerSourceChoice
                ? imageLayerSourceChoice->property("parameterApplicable")
                : QVariant();
        if (!imageLayerSourceChoice ||
            !imageLayerSourceApplicability.isValid() ||
            imageLayerSourceApplicability.toBool() != imageLayerSourceApplicable ||
            imageLayerSourceChoice->isHidden() == imageLayerSourceApplicable) {
          fail(QStringLiteral(
              "Image Layer source choice bypassed row applicability semantics"));
          return;
        }

        const bool imageLayerWasExpanded = imageLayerToggle->isChecked();
        imageLayerToggle->setChecked(false);
''')
replace(
    'src/qtgui/WorkspaceChurnSmoke.cpp',
    '''        imageLayerToggle->setChecked(imageLayerWasExpanded);

        if (!redStripWidth || !greenStripWidth ||
''',
    '''        imageLayerToggle->setChecked(imageLayerWasExpanded);

        // enabledCheck must never mean visibility. Final orientation remains
        // discoverable before screen geometry exists, but teaches the missing
        // prerequisite by being disabled; configuring a valid basis enables it.
        if (!finalMirrorCheck) {
          fail(QStringLiteral("Workspace churn lost final mirror checkbox"));
          return;
        }
        const ParameterState checkboxSemanticsBaseline =
            first->documentStateSnapshot();
        ParameterState withoutFinalGeometry = checkboxSemanticsBaseline;
        withoutFinalGeometry.scrToImg.type = colorscreen::Dufay;
        withoutFinalGeometry.scrToImg.mesh_trans = nullptr;
        withoutFinalGeometry.scrToImg.coordinate1 = {0, 0};
        withoutFinalGeometry.scrToImg.coordinate2 = {0, 0};
        first->applyState(withoutFinalGeometry);
        if (finalMirrorCheck->isHidden() || finalMirrorCheck->isEnabled()) {
          fail(QStringLiteral(
              "Checkbox prerequisite hid final orientation instead of disabling it"));
          return;
        }
        ParameterState withFinalGeometry = withoutFinalGeometry;
        withFinalGeometry.scrToImg.coordinate1 = {10, 0};
        withFinalGeometry.scrToImg.coordinate2 = {0, 10};
        first->applyState(withFinalGeometry);
        if (finalMirrorCheck->isHidden() || !finalMirrorCheck->isEnabled()) {
          fail(QStringLiteral(
              "Checkbox prerequisite did not enable final orientation"));
          return;
        }
        first->applyState(checkboxSemanticsBaseline);

        if (!redStripWidth || !greenStripWidth ||
''')

replace(
    '.agents/qtgui.md',
    '''4. **Validation**: Use `enabledCheck` when a control should stay visible but temporarily unavailable because a prerequisite is missing. Use `setParameterApplicability()` when the control's concept does not apply to the current image/process.''',
    '''4. **Validation**: Use `enabledCheck` when a control should stay visible but temporarily unavailable because a prerequisite is missing. This is now uniform across sliders, enums, buttons, and both checkbox helpers. Use `setParameterApplicability()` when the control's concept does not apply to the current image/process; do not reintroduce checkbox-specific hide-on-disable behavior.''')

replace(
    'doc/qtgui-internal-cleanup.md',
    '''  functions.  `ParameterPanel::setParameterApplicability()` now establishes
  this distinction for form rows and composes it with section folding; Screen
  pattern rows and measured-MTF controls are the first users.  Continue moving
  panel-specific visibility predicates to this API as the individual panels are
  simplified.
''',
    '''  functions.  `ParameterPanel::setParameterApplicability()` now establishes
  this distinction for form rows and composes it with section folding; Screen
  pattern rows and measured-MTF controls were the first users. Both checkbox
  helpers now follow the same enable-only `enabledCheck` contract as sliders,
  enums and buttons. Image Layer's native-vs-simulated source choice uses explicit
  row applicability instead of a direct `setVisible()` repair, while Geometry's
  final-mirror prerequisite remains visible and disabled until screen geometry
  exists. Lightweight and workspace smoke probes cover both checkbox helper
  variants, row applicability metadata, and the visible-disabled transition.
  Continue moving remaining panel-specific visibility predicates to this API as
  the individual panels are simplified.
''')

expected = {
    '.agents/qtgui.md',
    'doc/qtgui-internal-cleanup.md',
    'src/qtgui/GeometryPanel.cpp',
    'src/qtgui/ImageLayerPanel.cpp',
    'src/qtgui/ParameterPanel.cpp',
    'src/qtgui/ParameterPanel.h',
    'src/qtgui/WorkspaceChurnSmoke.cpp',
    'src/qtgui/main.cpp',
}
changed = set(subprocess.check_output(['git', 'diff', '--name-only', 'HEAD'], text=True).splitlines())
if changed != expected:
    raise RuntimeError(f'unexpected changed files: {changed ^ expected}')
subprocess.run(['git', 'diff', 'HEAD', '--check'], check=True)
print('checkbox semantics patch ready:', ', '.join(sorted(changed)))
