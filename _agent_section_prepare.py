#!/usr/bin/env python3
"""Apply the reviewed section-preference changes to the exact main checkout."""
from pathlib import Path


def replace_once(path: str, before: str, after: str) -> None:
    """Replace one verified source anchor, refusing a mismatched revision."""
    target = Path(path)
    text = target.read_text()
    count = text.count(before)
    if count != 1:
        raise RuntimeError(f"{path}: expected one anchor, found {count}: {before[:90]}")
    target.write_text(text.replace(before, after, 1))


section_function = r'''/** Add a foldable section, optionally restoring an application preference.

    SECTIONKEY is an untranslated presentation identity, never an undo key.
    Only explicit button activation persists a choice; refresh, applicability,
    and programmatic folding must not overwrite the user's preference. */
QToolButton *ParameterPanel::addSeparator(const QString &title,
                                         const QString &sectionKey) {
  const QString settingsKey = sectionKey.isEmpty()
      ? QString()
      : QStringLiteral("inspector/sections/%1/expanded").arg(sectionKey);
  const bool expanded = settingsKey.isEmpty()
      || QSettings().value(settingsKey, true).toBool();

  QGroupBox *group = new QGroupBox();
  group->setFlat(true);
  group->setStyleSheet(
      "QGroupBox { border: none; margin: 0px; padding: 0px; }");

  // Create a custom title widget with arrow button.
  QWidget *titleWidget = new QWidget();
  QPalette pal = titleWidget->palette();
  pal.setColor(QPalette::Window, pal.color(QPalette::Mid));
  titleWidget->setAutoFillBackground(true);
  titleWidget->setPalette(pal);

  QHBoxLayout *titleLayout = new QHBoxLayout(titleWidget);
  titleLayout->setContentsMargins(4, 4, 4, 4);
  titleLayout->setSpacing(4);

  QToolButton *arrowBtn = new QToolButton();
  arrowBtn->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
  arrowBtn->setStyleSheet(
      "QToolButton { border: none; background: transparent; }");
  arrowBtn->setCheckable(true);
  arrowBtn->setChecked(expanded);
  arrowBtn->setAccessibleName(title);
  arrowBtn->setToolTip(expanded ? tr("Collapse %1").arg(title)
                                : tr("Expand %1").arg(title));
  if (!sectionKey.isEmpty()) {
    arrowBtn->setProperty("sectionKey", sectionKey);
    group->setProperty("sectionKey", sectionKey);
  }

  QLabel *titleLabel = new QLabel(title);
  QFont font = titleLabel->font();
  font.setBold(true);
  titleLabel->setFont(font);

  titleLayout->addWidget(arrowBtn);
  titleLayout->addWidget(titleLabel);
  titleLayout->addStretch();

  QVBoxLayout *groupLayout = new QVBoxLayout();
  groupLayout->setContentsMargins(0, 0, 0, 0);
  groupLayout->setSpacing(0);
  group->setLayout(groupLayout);
  groupLayout->addWidget(titleWidget);

  QFormLayout *groupForm = new QFormLayout();
  groupForm->setProperty(parameterSectionExpandedProperty, expanded);
  m_groupForms.push_back(groupForm);
  groupLayout->addLayout(groupForm);

  // Rows are added after the header. Reapply folding during updateUI(), so
  // restored and dynamically added rows cannot escape a collapsed section.
  // Guard the refresh callback because derived panels may rebuild their UI.
  const QPointer<QToolButton> sectionToggle = arrowBtn;
  const QPointer<QFormLayout> sectionForm = groupForm;
  auto updateVisibility = [sectionToggle, sectionForm, title]() {
    if (!sectionToggle || !sectionForm)
      return;
    const bool checked = sectionToggle->isChecked();
    sectionToggle->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
    sectionToggle->setToolTip(checked ? tr("Collapse %1").arg(title)
                                     : tr("Expand %1").arg(title));
    sectionForm->setProperty(parameterSectionExpandedProperty, checked);

    auto setVisibleRecursive = [](auto self, QLayoutItem *item,
                                  bool visible) -> void {
      if (!item)
        return;
      if (QWidget *widget = item->widget()) {
        widget->setVisible(visible && parameterWidgetApplicable(widget));
      } else if (QLayout *layout = item->layout()) {
        for (int i = 0; i < layout->count(); ++i)
          self(self, layout->itemAt(i), visible);
      }
    };
    for (int i = 0; i < sectionForm->count(); ++i)
      setVisibleRecursive(setVisibleRecursive, sectionForm->itemAt(i), checked);
  };
  connect(arrowBtn, &QToolButton::toggled, this,
          [updateVisibility](bool) { updateVisibility(); });
  if (!settingsKey.isEmpty()) {
    m_widgetStateUpdaters.push_back(updateVisibility);
    connect(arrowBtn, &QToolButton::clicked, this,
            [settingsKey](bool checked) {
              QSettings().setValue(settingsKey, checked);
            });
  }

  m_form->addRow(group);
  m_currentGroupForm = groupForm;
  return arrowBtn;
}
'''

panel_path = Path('src/qtgui/ParameterPanel.cpp')
panel_text = panel_path.read_text()
start = panel_text.index('QToolButton *ParameterPanel::addSeparator(const QString &title) {')
end = panel_text.index('/** Register logical applicability for the form row containing WIDGET. */', start)
panel_path.write_text(panel_text[:start] + section_function + '\n' + panel_text[end:])
replace_once(str(panel_path), '#include <QScrollArea>\n', '#include <QScrollArea>\n#include <QSettings>\n')
replace_once('src/qtgui/ParameterPanel.h',
             '  QToolButton *addSeparator(const QString &title);',
             '''  /** Add a foldable section. A non-empty, stable untranslated SECTIONKEY
      remembers explicit user folding in application settings. Empty keys keep
      the historical initially-expanded, nonpersistent behavior. Section keys
      are presentation metadata, not saved parameter or undo identities.
      Call updateUI() after populating rows to apply restored folding. */
  QToolButton *addSeparator(const QString &title,
                           const QString &sectionKey = QString());''')
for title, key in [('Screen pattern', 'screen.pattern'),
                   ('Reconstruction', 'screen.reconstruction'),
                   ('Pre-demosaic denoising', 'screen.denoise.pre'),
                   ('Post-demosaic denoising', 'screen.denoise.post')]:
    replace_once('src/qtgui/ScreenPanel.cpp',
                 f'addSeparator("{title}")',
                 f'addSeparator("{title}",\n                   QStringLiteral("{key}"))')
replace_once('src/qtgui/ImageLayerPanel.cpp',
             'addSeparator(tr("Simulated image layer from RGB"));',
             '''addSeparator(tr("Simulated image layer from RGB"),
                   QStringLiteral("image_layer.simulated_rgb"));''')

smoke_function = r'''/** Verify that section preferences stay independent of document state. */
bool parameterSectionPreferencesSmoke() {
  auto fail = [](const char *reason) {
    qCritical() << "Section-preference smoke failed:" << reason;
    return false;
  };

  // Touch only a unique test subtree, including on an early failure.
  QTemporaryDir temporary;
  if (!temporary.isValid())
    return fail("could not allocate unique settings keys");
  const QString keyRoot = QStringLiteral("smoke.%1")
      .arg(QFileInfo(temporary.path()).fileName());
  const QString settingsRoot =
      QStringLiteral("inspector/sections/") + keyRoot;
  struct SettingsCleanup {
    QString root;
    /** Remove only preferences belonging to this smoke invocation. */
    ~SettingsCleanup() { QSettings().remove(root); }
  } cleanup{settingsRoot};
  const QString firstKey = keyRoot + QStringLiteral("/first");
  const QString secondKey = keyRoot + QStringLiteral("/second");
  const QString firstSetting = settingsRoot + QStringLiteral("/first/expanded");
  const QString secondSetting = settingsRoot + QStringLiteral("/second/expanded");

  ParameterState state;
  int documentEdits = 0;
  auto getState = [&state]() { return state; };
  auto setState = [&state, &documentEdits](const ParameterState &next,
                                          const QString &, const QString &) {
    state = next;
    ++documentEdits;
  };
  auto noImage = []() { return std::shared_ptr<colorscreen::image_data>(); };
  auto addRow = [](CheckboxSemanticsProbe &panel) {
    QCheckBox *field = panel.addCheckboxParameter(
        QStringLiteral("Saved value"),
        [](const ParameterState &s) { return s.rparams.scan_mirror; },
        [](ParameterState &s, bool value) { s.rparams.scan_mirror = value; });
    return field->parentWidget();
  };

  {
    CheckboxSemanticsProbe panel(getState, setState, noImage, nullptr, false);
    QToolButton *first = panel.addSeparator(QStringLiteral("Repeated title"),
                                           firstKey);
    QWidget *firstRow = addRow(panel);
    QToolButton *second = panel.addSeparator(QStringLiteral("Repeated title"),
                                            secondKey);
    QWidget *secondRow = addRow(panel);
    bool rowApplicable = false;
    panel.setParameterApplicability(secondRow,
        [&rowApplicable](const ParameterState &) { return rowApplicable; });
    panel.updateUI();
    if (!first->isChecked() || !second->isChecked() || firstRow->isHidden()
        || !secondRow->isHidden())
      return fail("initial folding or row applicability is incorrect");
    if (QSettings().contains(firstSetting) || QSettings().contains(secondSetting))
      return fail("initial refresh persisted a choice the user did not make");
    if (first->property("sectionKey").toString() != firstKey
        || first->property("parameterKey").isValid()
        || first->accessibleName() != QStringLiteral("Repeated title"))
      return fail("section identity/accessibility was confused with parameters");

    first->click();
    panel.updateUI();
    if (!firstRow->isHidden() || first->arrowType() != Qt::RightArrow
        || QSettings().value(firstSetting, true).toBool()
        || QSettings().contains(secondSetting))
      return fail("user folding did not persist independently by stable key");

    // Repeated captions and inapplicable rows must not share fold state.
    second->click();
    second->click();
    panel.updateUI();
    if (!secondRow->isHidden() || !second->isChecked()
        || !QSettings().value(secondSetting, false).toBool())
      return fail("expansion resurrected an inapplicable row");
    rowApplicable = true;
    panel.updateUI();
    if (secondRow->isHidden())
      return fail("applicable row did not return in an expanded section");

    // Programmatic changes and a hidden/reparented inspector are not choices
    // to copy into subsequently opened documents.
    first->setChecked(true);
    QWidget *firstGroup = first->parentWidget()->parentWidget();
    bool sectionApplicable = false;
    panel.setParameterApplicability(firstGroup,
        [&sectionApplicable](const ParameterState &) {
          return sectionApplicable;
        });
    panel.updateUI();
    if (!firstGroup->isHidden() || !first->isChecked()
        || QSettings().value(firstSetting, true).toBool())
      return fail("programmatic folding/applicability overwrote the preference");
    sectionApplicable = true;
    panel.updateUI();
    if (firstGroup->isHidden() || firstRow->isHidden())
      return fail("whole-section applicability lost local expansion state");
  }

  {
    CheckboxSemanticsProbe restored(getState, setState, noImage, nullptr, false);
    QToolButton *first = restored.addSeparator(QStringLiteral("Renamed title"),
                                              firstKey);
    QWidget *firstRow = addRow(restored);
    restored.updateUI();
    if (first->isChecked() || !firstRow->isHidden()
        || first->arrowType() != Qt::RightArrow)
      return fail("recreated/renamed section did not restore collapsed state");
    QWidget *lateRow = addRow(restored);
    state.rparams.scan_mirror = true;
    restored.updateUI();
    if (!lateRow->isHidden() || !firstRow->isHidden())
      return fail("late rows or document refresh escaped a collapsed section");

    QToolButton *second = restored.addSeparator(QStringLiteral("Renamed title"),
                                               secondKey);
    QWidget *secondRow = addRow(restored);
    QToolButton *unkeyed = restored.addSeparator(QStringLiteral("Renamed title"));
    QWidget *unkeyedRow = addRow(restored);
    restored.updateUI();
    if (!second->isChecked() || secondRow->isHidden()
        || !unkeyed->isChecked() || unkeyedRow->isHidden())
      return fail("same-caption or unkeyed sections inherited another key");

    const QStringList settingsBefore = QSettings().allKeys();
    unkeyed->click();
    restored.updateUI();
    if (!unkeyedRow->isHidden() || QSettings().allKeys() != settingsBefore)
      return fail("unkeyed folding wrote application settings");

    first->click();
    restored.updateUI();
    if (firstRow->isHidden() || lateRow->isHidden()
        || !QSettings().value(firstSetting, false).toBool())
      return fail("explicit re-expansion did not replace the saved choice");
  }

  CheckboxSemanticsProbe reopened(getState, setState, noImage, nullptr, false);
  QToolButton *first = reopened.addSeparator(QStringLiteral("Third title"),
                                            firstKey);
  QWidget *row = addRow(reopened);
  reopened.updateUI();
  if (!first->isChecked() || row->isHidden())
    return fail("latest user expansion was not restored");
  if (documentEdits != 0)
    return fail("section preferences entered the document/Undo setter");
  return true;
}

'''
replace_once('src/qtgui/main.cpp',
             '/** Exercise beta-critical non-rendering UI/document invariants. */\n',
             smoke_function + '/** Exercise beta-critical non-rendering UI/document invariants. */\n')
replace_once('src/qtgui/main.cpp',
             '  if (!backgroundThreadRegistryShutdownSmoke())\n    return false;\n',
             '''  if (!backgroundThreadRegistryShutdownSmoke()
      || !parameterSectionPreferencesSmoke())
    return false;
''')

replace_once('doc/qtgui-workflow-roadmap.md',
             '- remember expansion state;',
             '''- remember expansion state through stable, untranslated section keys.
  Screen and Image Layer are the first migrated panels: explicitly folding a
  section is an application preference restored in subsequently created panels,
  including after restart. Existing open panels keep their own local fold state.
  Programmatic folding, document refresh, and process-dependent applicability do
  not overwrite that choice. Other panels keep the previous initially-expanded
  behavior until their sections receive explicit keys;''')
replace_once('doc/qtgui-internal-cleanup.md',
             '''- Consolidate repeated slider value mapping in one tested utility instead of
  duplicating linear/gamma/logarithmic conversions in stateful and stateless
  slider helpers.''',
             '''- Keep linear/gamma/logarithmic slider conversions centralized in
  `SliderValueMapping`; do not reintroduce separate mapping formulas in stateful
  and stateless helpers.''')
replace_once('doc/qtgui-internal-cleanup.md',
             '''- Keep group folding as presentation state only: a collapsed/expanded section
  must compose with each row's logical applicability instead of overwriting it.''',
             '''- Keep group folding as presentation state only: a collapsed/expanded section
  must compose with each row's logical applicability instead of overwriting it.
  `addSeparator()` now accepts an optional stable `sectionKey`, distinct from
  saved parameter/Undo identity. Screen and Image Layer opt into application-
  preference persistence; only user activation writes settings. New panels
  restore the last explicit choice, while existing panels retain their local
  presentation. Refresh re-applies folding to rows added after the header, and
  guarded callbacks tolerate UI rebuilds. The beta smoke covers recreation with
  renamed/repeated captions, key separation, unkeyed compatibility, dynamic rows,
  applicability, programmatic changes, and zero document setter calls.''')
with Path('.agents/qtgui.md').open('a') as out:
    out.write('''

## Persistent inspector section preferences

`ParameterPanel::addSeparator(title, sectionKey)` optionally remembers folding
in `QSettings` under `inspector/sections/<sectionKey>/expanded`. Supply a stable,
untranslated key scoped to the panel (for example `screen.denoise.pre`), never
an image filename, translated title, row index, or saved `parameterKey`.
The Screen and Image Layer panels are the first migrated callers. Empty keys
retain the old initially-expanded, nonpersistent behavior.

Only `QToolButton::clicked` persists a preference. Programmatic `setChecked()`,
parameter refresh, and logical applicability must not write preferences or
invoke the document state setter. A newly created panel restores the latest
explicit user choice; existing panels retain their local presentation, so
opening or closing another document cannot reset it. These preferences are not
`.par`, recovery, dirty, or Undo state.

Populate the section and call `updateUI()` before showing it. The keyed-section refresh
callback reapplies folding to all rows, including rows added after restoration,
and composes it with `parameterApplicable`. Keep the existing layout/parent
contract; section applicability still addresses the outer group. The header's
`sectionKey` metadata is separate from processing keys, and its accessible name
and Expand/Collapse tooltip describe the section. Beta smoke uses a unique,
cleaned-up settings subtree and tests restoration, key isolation, applicability,
late rows, and the absence of document edits.
''')
print('Applied section preferences, five keyed sections, smoke coverage, and tracking updates.')
