/** Exercise persisted Geometry folds and incremental diagnostic visibility. */
bool geometrySectionPreferencesSmoke() {
  auto fail = [](const QString &reason) {
    qCritical() << "Geometry section smoke failed:" << reason;
    return false;
  };

  // Production section keys must never overwrite operator preferences. Run
  // before document creation, with no event pumping under this test identity.
  QTemporaryDir temporary;
  if (!temporary.isValid())
    return fail(QStringLiteral("could not allocate isolated settings identity"));
  struct SettingsIdentityGuard {
    QString organization = QCoreApplication::organizationName();
    QString domain = QCoreApplication::organizationDomain();
    QString application = QCoreApplication::applicationName();
    /** Remove only test settings and restore the original application identity. */
    ~SettingsIdentityGuard() {
      QSettings settings;
      settings.clear();
      settings.sync();
      QCoreApplication::setOrganizationName(organization);
      QCoreApplication::setOrganizationDomain(domain);
      QCoreApplication::setApplicationName(application);
    }
  } settingsGuard;
  const QString identity = QStringLiteral("ColorScreenGeometrySmoke-%1")
      .arg(QFileInfo(temporary.path()).fileName());
  QCoreApplication::setOrganizationName(identity);
  QCoreApplication::setOrganizationDomain(identity + QStringLiteral(".invalid"));
  QCoreApplication::setApplicationName(QStringLiteral("GeometrySections"));

  using Solver = colorscreen::solver_parameters;
  ParameterState state;
  state.scrToImg.type = colorscreen::Dufay;
  state.scrToImg.scanner_type = colorscreen::fixed_lens;
  const ParameterState initialState = state;
  std::shared_ptr<colorscreen::image_data> scan;
  int documentEdits = 0;
  int fitRequests = 0;
  auto getState = [&]() { return state; };
  auto setState = [&](const ParameterState &, const QString &, const QString &) {
    ++documentEdits;
  };
  auto getImage = [&]() { return scan; };
  auto createPanel = [&]() {
    auto panel = std::make_unique<GeometryPanel>(getState, setState, getImage);
    QObject::connect(panel.get(), &GeometryPanel::optimizeRequested,
                     panel.get(), [&](bool) { ++fitRequests; });
    return panel;
  };
  const QStringList keys = {
      QStringLiteral("geometry.registration_points"),
      QStringLiteral("geometry.automatic_registration"),
      QStringLiteral("geometry.fit"),
      QStringLiteral("geometry.final_orientation"),
      QStringLiteral("geometry.visualization")};
  auto settingKey = [](const QString &key) {
    return QStringLiteral("inspector/sections/%1/expanded").arg(key);
  };
  auto toggleFor = [](GeometryPanel &panel, const QString &key) {
    QToolButton *result = nullptr;
    for (auto *button : panel.findChildren<QToolButton *>()) {
      if (button->property("sectionKey").toString() == key) {
        if (result)
          return static_cast<QToolButton *>(nullptr);
        result = button;
      }
    }
    return result;
  };
  auto groupFor = [](GeometryPanel &panel, const QString &key) {
    QGroupBox *result = nullptr;
    for (auto *group : panel.findChildren<QGroupBox *>()) {
      if (group->property("sectionKey").toString() == key) {
        if (result)
          return static_cast<QGroupBox *>(nullptr);
        result = group;
      }
    }
    return result;
  };
  auto verify = [&](GeometryPanel &panel, const std::vector<bool> &expanded) {
    int keyedButtons = 0;
    for (auto *button : panel.findChildren<QToolButton *>())
      if (button->property("sectionKey").isValid())
        ++keyedButtons;
    if (keyedButtons != keys.size())
      return fail(QStringLiteral("incomplete or duplicate section keys"));
    for (int i = 0; i < keys.size(); ++i) {
      auto *button = toggleFor(panel, keys[i]);
      auto *group = groupFor(panel, keys[i]);
      if (!button || !group || button->isChecked() != expanded[i]
          || button->property("parameterKey").isValid()
          || button->accessibleName().isEmpty()
          || !group->layout() || group->layout()->count() != 2)
        return fail(QStringLiteral("incorrect section state: %1").arg(keys[i]));
      auto *form = qobject_cast<QFormLayout *>(
          group->layout()->itemAt(1)->layout());
      if (!form || form->rowCount() == 0)
        return fail(QStringLiteral("missing section rows: %1").arg(keys[i]));
      auto rowsMatch = [](auto self, QLayoutItem *item, bool open) -> bool {
        if (QWidget *widget = item->widget()) {
          const QVariant applicable = widget->property("parameterApplicable");
          return !widget->isHidden()
              == (open && (!applicable.isValid() || applicable.toBool()));
        }
        if (QLayout *layout = item->layout())
          for (int row = 0; row < layout->count(); ++row)
            if (!self(self, layout->itemAt(row), open))
              return false;
        return true;
      };
      for (int row = 0; row < form->count(); ++row)
        if (!rowsMatch(rowsMatch, form->itemAt(row), expanded[i]))
          return fail(QStringLiteral("row escaped section folding: %1")
                          .arg(keys[i]));
    }
    return true;
  };
  const QStringList messages = {
      QStringLiteral("GeometryOptimizationMessage"),
      QStringLiteral("GeometryLensMessage"),
      QStringLiteral("GeometryTiltMessage"),
      QStringLiteral("GeometryNonlinearMessage")};
  auto verifyMessages = [&](GeometryPanel &panel,
                            const std::vector<bool> &applicable) {
    auto *group = groupFor(panel, keys[2]);
    auto *toggle = toggleFor(panel, keys[2]);
    if (!group || !toggle)
      return fail(QStringLiteral("missing fit section"));
    for (int i = 0; i < messages.size(); ++i) {
      auto *label = panel.findChild<QLabel *>(messages[i]);
      if (!label || !group->isAncestorOf(label)
          || label->property("parameterApplicable").toBool() != applicable[i]
          || !label->text().isEmpty() != applicable[i]
          || !label->isHidden() != (applicable[i] && toggle->isChecked()))
        return fail(QStringLiteral("incorrect prerequisite row: %1")
                        .arg(messages[i]));
    }
    return true;
  };
  const QStringList charts = {
      QStringLiteral("GeometryLensChartRow"),
      QStringLiteral("GeometryPerspectiveChartRow"),
      QStringLiteral("GeometryNonlinearChartRow"),
      QStringLiteral("GeometryFinalChartRow")};
  auto verifyCharts = [&](GeometryPanel &panel,
                          const std::vector<bool> &applicable) {
    auto *group = groupFor(panel, keys[4]);
    auto *toggle = toggleFor(panel, keys[4]);
    if (!group || !toggle)
      return fail(QStringLiteral("missing visualization section"));
    for (int i = 0; i < charts.size(); ++i) {
      auto *row = panel.findChild<QWidget *>(charts[i]);
      if (!row || !group->isAncestorOf(row)
          || !row->property("parameterApplicable").isValid()
          || row->property("parameterApplicable").toBool() != applicable[i]
          || !row->isHidden() != (applicable[i] && toggle->isChecked()))
        return fail(QStringLiteral("incorrect chart row: %1").arg(charts[i]));
    }
    return true;
  };

  auto first = createPanel();
  std::vector<bool> expected(keys.size(), true);
  if (!verify(*first, expected)
      || !verifyMessages(*first, {true, true, true, true})
      || !verifyCharts(*first, {false, false, false, false}))
    return false;
  for (int i = 0; i < keys.size(); ++i) {
    if (QSettings().contains(settingKey(keys[i])))
      return fail(QStringLiteral("construction persisted a default choice"));
    if (i % 2 == 0) {
      toggleFor(*first, keys[i])->click();
      expected[i] = false;
    }
  }
  first->updateUI();
  auto second = createPanel();
  if (!verify(*first, expected) || !verify(*second, expected))
    return false;

  // A programmatic fold stays local and never overwrites the saved preference.
  toggleFor(*first, keys[2])->setChecked(true);
  auto firstExpected = expected;
  firstExpected[2] = true;
  first->updateUI();
  second->updateUI();
  if (!verify(*first, firstExpected) || !verify(*second, expected)
      || QSettings().value(settingKey(keys[2]), true).toBool()
      || !verifyMessages(*second, {true, true, true, true}))
    return fail(QStringLiteral("programmatic folding changed another inspector"));

  const int enoughPoints = std::max({Solver::min_points(state.scrToImg.type),
      Solver::min_lens_points(state.scrToImg.type),
      Solver::min_perspective_points(state.scrToImg.type),
      Solver::min_mesh_points(state.scrToImg.type)});
  auto addLocalPoints = [&]() {
    for (int i = 0; i < enoughPoints; ++i)
      state.solver.points.push_back({{1, 1}, {1, 1}, Solver::green});
  };
  addLocalPoints();
  // Exercise the incremental API, not just the complete updateUI() path.
  second->updateRegistrationPointInfo(state);
  if (!verifyMessages(*second, {false, false, false, false}))
    return false;
  toggleFor(*second, keys[2])->click();
  expected[2] = true;
  if (!verifyMessages(*second, {false, false, false, false}))
    return fail(QStringLiteral("expansion resurrected satisfied prerequisites"));
  state.solver.points.clear();
  second->updateRegistrationPointInfo(state);
  if (!verifyMessages(*second, {true, true, true, true}))
    return fail(QStringLiteral("incremental point removal lost prerequisites"));
  toggleFor(*second, keys[2])->click();
  expected[2] = false;

  // Enough clustered points still require a coverage warning, even while folded.
  scan = std::make_shared<colorscreen::image_data>();
  scan->width = 80;
  scan->height = 64;
  state.scrToImg.center = {40, 32};
  state.scrToImg.coordinate1 = {4, 0};
  state.scrToImg.coordinate2 = {0, 4};
  addLocalPoints();
  second->updateRegistrationPointInfo(state);
  if (!verifyMessages(*second, {false, true, false, false}))
    return fail(QStringLiteral("lens coverage warning ignored folding"));
  state.solver.points.push_back({{0, 0}, {0, 0}, Solver::green});
  state.solver.points.push_back({{79, 63}, {20, 16}, Solver::green});
  second->updateRegistrationPointInfo(state);
  if (!verifyMessages(*second, {false, true, false, false}))
    return fail(QStringLiteral("outliers incorrectly cleared the coverage warning"));

  // Coverage uses the central 90% span, not a bounding box. Replace the
  // cluster by a distributed grid instead of relying on isolated outliers.
  state.solver.points.clear();
  const int gridRows = (enoughPoints + 9) / 10;
  for (int i = 0; i < enoughPoints; ++i) {
    const double x = (i % 10) * 79.0 / 9;
    const double y = (i / 10) * 63.0 / (gridRows - 1);
    state.solver.points.push_back({{x, y}, {x / 4, y / 4}, Solver::green});
  }
  second->updateRegistrationPointInfo(state);
  if (!verifyMessages(*second, {false, false, false, false}))
    return fail(QStringLiteral("sufficient lens coverage retained its warning"));
  const ParameterState withGeometry = state;

  second->updateDeformationChart();
  if (!verifyCharts(*second, {false, false, false, true}))
    return fail(QStringLiteral("new geometry escaped a remembered chart fold"));
  state.scrToImg.lens_correction.kr[1] = 0.01;
  state.scrToImg.tilt_x = 1;
  second->updateDeformationChart();
  if (!verifyCharts(*second, {true, true, false, true}))
    return false;
  toggleFor(*second, keys[4])->click();
  expected[4] = true;
  if (!verifyCharts(*second, {true, true, false, true}))
    return fail(QStringLiteral("expansion lost applicable diagnostic charts"));
  second->updateUI();
  if (!verify(*second, expected))
    return false;
  state = withGeometry;
  second->updateDeformationChart();
  if (!verifyCharts(*second, {false, false, false, true}))
    return fail(QStringLiteral("removed corrections left their charts visible"));
  state.scrToImg.coordinate1 = {0, 0};
  state.scrToImg.coordinate2 = {0, 0};
  second->updateDeformationChart();
  toggleFor(*second, keys[4])->click();
  toggleFor(*second, keys[4])->click();
  if (!verifyCharts(*second, {false, false, false, false}))
    return fail(QStringLiteral("expansion resurrected charts without geometry"));
  state = withGeometry;
  scan.reset();
  second->updateDeformationChart();
  if (!verifyCharts(*second, {false, false, false, false}))
    return fail(QStringLiteral("image removal left diagnostic charts visible"));

  state = initialState;
  second->updateUI();
  for (int i = 0; i < keys.size(); ++i) {
    if (toggleFor(*second, keys[i])->isChecked())
      toggleFor(*second, keys[i])->click();
    expected[i] = false;
  }
  QSettings().sync();
  first->updateUI();
  second->updateUI();
  if (!verify(*first, firstExpected) || !verify(*second, expected))
    return fail(QStringLiteral("one inspector changed another's local folds"));
  second.reset();
  auto reopened = createPanel();
  if (!verify(*reopened, expected)
      || !verifyMessages(*reopened, {true, true, true, true})
      || !verifyCharts(*reopened, {false, false, false, false}))
    return fail(QStringLiteral("recreated panel lost remembered folds"));
  for (const QString &key : keys)
    if (QSettings().value(settingKey(key), true).toBool())
      return fail(QStringLiteral("refresh overwrote an explicit fold preference"));
  if (documentEdits != 0 || fitRequests != 0 || state != initialState)
    return fail(QStringLiteral("folding or refresh entered document/fit state"));
  return true;
}
