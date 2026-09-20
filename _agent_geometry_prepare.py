#!/usr/bin/env python3
"""Apply the Geometry section patch to an exact, separately checked-out main."""
from pathlib import Path
import subprocess

root = Path.cwd()
transport = Path(__file__).resolve().parent

def replace(path, old, new):
    """Replace one verified source fragment, refusing a stale baseline."""
    file = root / path
    text = file.read_text()
    if text.count(old) != 1:
        raise SystemExit(f'{path}: expected exactly one occurrence of {old!r}')
    file.write_text(text.replace(old, new))

g = 'src/qtgui/GeometryPanel.cpp'
for title, key in [('Registration points', 'geometry.registration_points'),
                   ('Automatic registration', 'geometry.automatic_registration'),
                   ('Geometry fit', 'geometry.fit'),
                   ('Final image orientation', 'geometry.final_orientation'),
                   ('Visualization', 'geometry.visualization')]:
    replace(g, f'addSeparator("{title}")',
            f'addSeparator("{title}",\n               QStringLiteral("{key}"))')
replace(g, '  m_autoOptimizeBox->setChecked(true);', '''  // Initial presentation is not a document edit or a request to run the fit.
  {
    const QSignalBlocker blocker(m_autoOptimizeBox);
    m_autoOptimizeBox->setChecked(true);
  }''')
replace(g, '  auto configureDynamicStatusLabel = [](QLabel *label,',
        '  auto configureDynamicStatusLabel = [this, addToPanel](QLabel *label,')
replace(g, '''    label->setVisible(false);
  };

  m_optimizationMessageLabel''', '''    label->setVisible(false);
    // Prerequisites belong to Geometry fit, including when it is collapsed.
    addToPanel(label);
    setParameterApplicability(label, [label](const ParameterState &) {
      return !label->text().isEmpty();
    });
  };

  m_optimizationMessageLabel''')
for label in ['m_optimizationMessageLabel', 'm_lensMessageLabel',
              'm_tiltMessageLabel', 'm_nonlinearMessageLabel']:
    replace(g, f'  m_form->addRow({label});\n', '')
replace(g, '''  auto setupChart =
      [this, addToPanel](DeformationChartWidget *&chart,
                         QVBoxLayout *&containerLayout,
                         const QString &title) {''', '''  auto hasGeometry = [this](const ParameterState &state) {
    auto scan = m_imageGetter();
    return scan && scan->width > 0 && scan->height > 0
        && colorscreen::screen_geometry_configured_p(state.scrToImg);
  };
  auto setupChart =
      [this, addToPanel](DeformationChartWidget *&chart,
                         QVBoxLayout *&containerLayout,
                         const QString &title, const QString &objectName,
                         std::function<bool(const ParameterState &)> applicable) {''')
replace(g, '''        containerLayout->addWidget(detachable);
        addToPanel(container);
      };''', '''        containerLayout->addWidget(detachable);
        container->setObjectName(objectName);
        addToPanel(container);
        setParameterApplicability(container, applicable);
      };''')
replace(g, '''  setupChart(m_lensChart, m_lensChartContainer, "Lens Correction");
  setupChart(m_perspectiveChart, m_perspectiveChartContainer, "Perspective");
  setupChart(m_nonlinearChart, m_nonlinearChartContainer,
             "Nonlinear transformation");''', '''  setupChart(m_lensChart, m_lensChartContainer, "Lens Correction",
             QStringLiteral("GeometryLensChartRow"),
             [hasGeometry](const ParameterState &state) {
               return hasGeometry(state)
                   && !state.scrToImg.lens_correction.is_noop();
             });
  setupChart(m_perspectiveChart, m_perspectiveChartContainer, "Perspective",
             QStringLiteral("GeometryPerspectiveChartRow"),
             [hasGeometry](const ParameterState &state) {
               return hasGeometry(state)
                   && (std::abs(state.scrToImg.tilt_x) > 1e-6
                       || std::abs(state.scrToImg.tilt_y) > 1e-6);
             });
  setupChart(m_nonlinearChart, m_nonlinearChartContainer,
             "Nonlinear transformation",
             QStringLiteral("GeometryNonlinearChartRow"),
             [hasGeometry](const ParameterState &state) {
               return hasGeometry(state) && state.scrToImg.mesh_trans != nullptr;
             });''')
replace(g, '  m_chartContainer = new QVBoxLayout(container);',
        '''  container->setObjectName(QStringLiteral("GeometryFinalChartRow"));
  m_chartContainer = new QVBoxLayout(container);''')
replace(g, '''  addToPanel(container);

  updateUI();''', '''  addToPanel(container);
  setParameterApplicability(container, hasGeometry);

  updateUI();''')
replace(g, 'void GeometryPanel::updateRegistrationPointInfo(const ParameterState &state) {',
        '''/** Refresh prerequisites from STATE without changing section preferences. */
void GeometryPanel::updateRegistrationPointInfo(const ParameterState &state) {''')
replace(g, '''          label->show();
      } else {
          label->hide();
      }
  };''', '''      } else {
          label->clear();
      }
  };''')
replace(g, '          m_lensMessageLabel->show();\n', '')
replace(g, '''  }
}

bool GeometryPanel::isNonlinearEnabled()''', '''  }
  // Point batches also reach this method without a complete parameter refresh.
  // Apply the new message applicability without re-entering updateUI().
  updateWidgetStates();
}

bool GeometryPanel::isNonlinearEnabled()''')
replace(g, 'void GeometryPanel::updateDeformationChart() {',
        '''/** Refresh chart data and applicability, preserving the local fold state. */
void GeometryPanel::updateDeformationChart() {''')
file = root / g
text = file.read_text()
start = text.index('  bool showLens = hasGeometry')
end = text.index('  if (!hasGeometry) {', start)
text = text[:start] + '''  // Direct chart refreshes (including section expansion) use the same row
  // applicability as a full refresh. Never show a row through a saved fold.
  updateWidgetStates();

''' + text[end:]
file.write_text(text)
replace('src/qtgui/ParameterPanel.h', '''protected:
  /*''', '''protected:
  /** Refresh registered widget availability and folding only. Incremental
      presentation updates can use this without applying parameter values or
      recursively invoking onParametersRefreshed(). */
  void updateWidgetStates();

  /*''')
replace('src/qtgui/ParameterPanel.cpp', '''  // Update Widget State (Availability)
  for (auto &widgetUpdater : m_widgetStateUpdaters) {
    widgetUpdater();
  }

  // Call virtual method''', '''  updateWidgetStates();

  // Call virtual method''')
replace('src/qtgui/ParameterPanel.cpp', 'void ParameterPanel::applyChange(\n', '''/** Refresh presentation callbacks without re-entering parameter refresh. */
void ParameterPanel::updateWidgetStates() {
  for (auto &widgetUpdater : m_widgetStateUpdaters)
    widgetUpdater();
}

void ParameterPanel::applyChange(
''')
main = root / 'src/qtgui/main.cpp'
text = main.read_text()
for header in ['"GeometryPanel.h"', '"../libcolorscreen/include/imagedata.h"', '<QLabel>', '<algorithm>']:
    directive = '#include ' + header + '\n'
    if directive not in text:
        text = directive + text
main.write_text(text)
marker = '/** Exercise beta-critical non-rendering UI/document invariants. */'
replace('src/qtgui/main.cpp', marker,
        (transport / '_agent_geometry_smoke.cpp').read_text() + '\n' + marker)
replace('src/qtgui/main.cpp', '      || !colorSectionPreferencesSmoke())',
        '      || !colorSectionPreferencesSmoke()\n      || !geometrySectionPreferencesSmoke())')
replace('.agents/qtgui.md', 'Screen, Image Layer, Color, and Contact Copy now use explicit section keys.',
        'Screen, Image Layer, Color, Contact Copy, and Geometry now use explicit\nsection keys.')
with (root / '.agents/qtgui.md').open('a') as f:
    f.write('''
Geometry's five section preferences follow the same contract. Its four fit
prerequisite messages are rows inside Geometry fit, applicable only while their
message is nonempty. All four Visualization chart rows register applicability
independently of the section fold. Incremental point/chart updates call
`ParameterPanel::updateWidgetStates()` to replay presentation callbacks only;
this must not invoke parameter updaters or `onParametersRefreshed()` recursively.
Do not restore direct `show()`/`setVisible()` calls for these rows. The separate
Finetune Diagnostic Images section and the shared chart dock lifecycle are
unchanged. Geometry's real-panel smoke checks recreation, independent inspectors,
point-count/coverage messages, absent or changing geometry, attached chart rows,
and zero document-setter/fit requests under an isolated settings identity.
''')
replace('doc/qtgui-workflow-roadmap.md',
        '  Screen, Image Layer, Color, and Contact Copy now use this contract: explicitly\n',
        '  Screen, Image Layer, Color, Contact Copy, and Geometry use this contract:\n  explicitly\n')
replace('doc/qtgui-workflow-roadmap.md', '''  saved folds. Other panels keep the previous initially-expanded behavior until
  their sections receive explicit keys;''', '''  saved folds. Geometry includes all five existing groups; fit-prerequisite
  messages and visualization charts respect both applicability and the saved
  fold during incremental updates. Finetune Diagnostic Images remains separate.
  Other panels keep the previous initially-expanded behavior until their
  sections receive explicit keys;''')
replace('doc/qtgui-internal-cleanup.md', '''  saved parameter/Undo identity. Screen, Image Layer, Color, and Contact Copy
  opt into application-preference persistence;''', '''  saved parameter/Undo identity. Screen, Image Layer, Color, Contact Copy, and
  Geometry opt into application-preference persistence;''')
replace('doc/qtgui-internal-cleanup.md', '''  remain independent of the remembered presentation.
  Image Layer's''', '''  remain independent of the remembered presentation.
  Geometry's five sections now follow the same contract. Its fit-status labels
  belong inside Geometry fit rather than the outer form. Message presence and
  all four chart rows use registered applicability, including point batches
  and explicit chart refreshes between complete parameter refreshes. The shared
  `updateWidgetStates()` presentation-only helper avoids recursion into the
  derived refresh hook. Geometry's initial Auto fit checkbox synchronization is
  signal-blocked, so construction does not issue a redundant document setter.
  Real-panel smoke covers remembered/independent folds, point thresholds, lens
  coverage, missing and changing geometry, chart applicability, and no document
  edits or automatic fit requests. Existing chart detachment stays unchanged.
  Image Layer's''')
subprocess.run(['git', 'diff', '--check'], check=True)
subprocess.run(['bash', 'build-aux/check-generated-build-metadata.sh'], check=True)
