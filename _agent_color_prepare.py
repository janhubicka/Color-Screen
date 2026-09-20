#!/usr/bin/env python3
"""Apply only the reviewed Color/Contact Copy section-preference changes."""
from pathlib import Path
import subprocess
root = Path.cwd()
transport = Path(__file__).resolve().parent

def replace(path, old, new):
    p = root / path
    s = p.read_text()
    assert s.count(old) == 1, (path, old, s.count(old))
    p.write_text(s.replace(old, new))

for title, key in [('Adjustments in process color space','color.process'),('Backlight','color.backlight'),('Screen dyes','color.dyes'),('Viewing conditions correction','color.viewing'),('Final adjustments','color.final')]:
    space = ' ' if title == 'Final adjustments' else ''
    replace('src/qtgui/ColorPanel.cpp', f'  addSeparator{space}("{title}");', f'  addSeparator("{title}",\n               QStringLiteral("{key}"));')
replace('src/qtgui/ColorPanel.cpp', '  m_form->addRow(toneCurveSection);', '''  // The curve is part of Final adjustments, not a separate top-level row.
  // Keep the detachable wrapper inside the section so folding includes it.
  m_currentGroupForm->addRow(toneCurveSection);''')
replace('src/qtgui/ContactCopyPanel.cpp', '''  auto addSimulationSection = [this](const QString &title,
                                     const QString &objectName) {
    addSeparator(title);''', '''  auto addSimulationSection = [this](const QString &title,
                                     const QString &objectName,
                                     const QString &sectionKey) {
    addSeparator(title, sectionKey);''')
for object_name, key in [('ContactCopyFilmCharacteristicsGroup','contact_copy.film'),('ContactCopyRichardsGroup','contact_copy.richards'),('ContactCopyManualPointsGroup','contact_copy.manual_points'),('ContactCopyDarkroomGroup','contact_copy.darkroom')]:
    replace('src/qtgui/ContactCopyPanel.cpp', f'      QStringLiteral("{object_name}"));', f'      QStringLiteral("{object_name}"),\n      QStringLiteral("{key}"));')
replace('src/qtgui/ContactCopyPanel.cpp', "  // user's previous presentation.\n", "  // user's previous presentation. Explicit keys also restore this choice in\n  // new panels; enabling the simulation never overwrites the preference.\n")
replace('src/qtgui/main.cpp', '#include "ColorScreenApplication.h"\n', '#include "ColorScreenApplication.h"\n#include "ColorPanel.h"\n#include "ContactCopyPanel.h"\n')
replace('src/qtgui/main.cpp', '#include <QFileInfo>\n', '#include <QFileInfo>\n#include <QFormLayout>\n#include <QGroupBox>\n')
marker = '/** Exercise beta-critical non-rendering UI/document invariants. */'
replace('src/qtgui/main.cpp', marker, (transport / '_agent_color_smoke.cpp').read_text() + marker)
replace('src/qtgui/main.cpp', '      || !parameterSectionPreferencesSmoke())', '      || !parameterSectionPreferencesSmoke()\n      || !colorSectionPreferencesSmoke())')
replace('.agents/qtgui.md', 'The Screen and Image Layer panels are the first migrated callers. Empty keys', '''Screen, Image Layer, Color, and Contact Copy now use explicit section keys.
Color's five sections and Contact Copy's four specialist sections keep their
process/simulation applicability separate from restored folding. The Tone Curve
detachable wrapper belongs inside Final adjustments, so folding that section
also hides its attached curve without changing the dock lifecycle. Empty keys''')
p = root / '.agents/qtgui.md'
p.write_text(p.read_text() + '''
The beta-invariant smoke also constructs the real Color and Contact Copy panels
under a temporary QSettings organization/domain/application identity, restoring
the original identity on every exit. It checks all nine keys, alternating saved
folds, recreation, independent live inspectors, applicability changes, the nested
spectral chart, and the attached Tone Curve. This probe runs before document
creation without pumping GUI events; it must never alter operator preferences
or invoke the document state setter.
''')
replace('doc/qtgui-workflow-roadmap.md', '''  Screen and Image Layer are the first migrated panels: explicitly folding a
  section is an application preference restored in subsequently created panels,
  including after restart. Existing open panels keep their own local fold state.''', '''  Screen, Image Layer, Color, and Contact Copy now use this contract: explicitly
  folding a section is an application preference restored in subsequently
  created panels, including after restart. Existing open panels keep their own
  local fold state.''')
replace('doc/qtgui-workflow-roadmap.md', '''  not overwrite that choice. Other panels keep the previous initially-expanded
  behavior until their sections receive explicit keys;''', '''  not overwrite that choice. Color includes all five existing groups and keeps
  the Tone Curve inside Final adjustments. Contact Copy includes all four
  specialist groups; switching simulation off and on does not change their
  saved folds. Other panels keep the previous initially-expanded behavior until
  their sections receive explicit keys;''')
replace('doc/qtgui-internal-cleanup.md', '''  saved parameter/Undo identity. Screen and Image Layer opt into application-
  preference persistence; only user activation writes settings. New panels
  restore the last explicit choice, while existing panels retain their local''', '''  saved parameter/Undo identity. Screen, Image Layer, Color, and Contact Copy
  opt into application-preference persistence; only user activation writes
  settings. New panels restore the last explicit choice, while existing panels
  retain their local''')
replace('doc/qtgui-internal-cleanup.md', '''  applicability, programmatic changes, and zero document setter calls.
  Image Layer''', '''  applicability, programmatic changes, and zero document setter calls. The next
  real-panel probe covers all five Color and four Contact Copy sections under
  an isolated settings identity, including multiple live panels and reopening.
  Color's Tone Curve wrapper now belongs to Final adjustments instead of the
  outer form, preventing that editor from escaping the section's fold. Nested
  spectral-chart applicability and simulation-dependent Contact Copy groups
  remain independent of the remembered presentation.
  Image Layer''')
subprocess.run(['git', 'diff', '--check'], check=True)
subprocess.run(['bash', 'build-aux/check-generated-build-metadata.sh'], check=True)
