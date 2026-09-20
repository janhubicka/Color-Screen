/** Exercise persisted folding in the real Color and Contact Copy panels. */
bool colorSectionPreferencesSmoke() {
  auto fail = [](const QString &reason) {
    qCritical() << "Color/Contact Copy section smoke failed:" << reason;
    return false;
  };

  // Real panels use their production keys. Isolate the complete QSettings
  // identity instead of overwriting any existing operator preferences. This
  // probe runs before document windows are created and never pumps GUI events.
  QTemporaryDir temporary;
  if (!temporary.isValid())
    return fail(QStringLiteral("could not allocate isolated settings identity"));
  struct SettingsIdentityGuard {
    QString organization = QCoreApplication::organizationName();
    QString domain = QCoreApplication::organizationDomain();
    QString application = QCoreApplication::applicationName();
    /** Remove the probe's settings and restore the application's identity. */
    ~SettingsIdentityGuard() {
      QSettings settings;
      settings.clear();
      settings.sync();
      QCoreApplication::setOrganizationName(organization);
      QCoreApplication::setOrganizationDomain(domain);
      QCoreApplication::setApplicationName(application);
    }
  } settingsGuard;
  const QString identity = QStringLiteral("ColorScreenSmoke-%1")
      .arg(QFileInfo(temporary.path()).fileName());
  QCoreApplication::setOrganizationName(identity);
  QCoreApplication::setOrganizationDomain(identity + QStringLiteral(".invalid"));
  QCoreApplication::setApplicationName(QStringLiteral("PanelSections"));

  using RenderParameters = colorscreen::render_parameters;
  ParameterState state;
  state.rparams.capture_type = RenderParameters::capture_transparency_with_screen;
  state.rparams.color_model = RenderParameters::color_model_scan;
  state.rparams.contact_copy.simulate = true;
  const ParameterState initialState = state;
  int documentEdits = 0;
  auto getState = [&state]() { return state; };
  auto setState = [&documentEdits](const ParameterState &, const QString &,
                                  const QString &) { ++documentEdits; };
  auto noImage = []() { return std::shared_ptr<colorscreen::image_data>(); };

  auto settingKey = [](const QString &key) {
    return QStringLiteral("inspector/sections/%1/expanded").arg(key);
  };
  auto toggleFor = [](ParameterPanel &panel, const QString &key) {
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
  auto groupFor = [](ParameterPanel &panel, const QString &key) {
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
  auto verify = [&](ParameterPanel &panel, const QStringList &keys,
                    const std::vector<bool> &expanded) {
    int keyedButtons = 0;
    for (auto *button : panel.findChildren<QToolButton *>())
      if (button->property("sectionKey").isValid())
        ++keyedButtons;
    if (keyedButtons != keys.size())
      return fail(QStringLiteral("incomplete or duplicate panel section keys"));
    for (int i = 0; i < keys.size(); ++i) {
      auto *button = toggleFor(panel, keys[i]);
      auto *group = groupFor(panel, keys[i]);
      if (!button || !group || button->isChecked() != expanded[i]
          || button->property("parameterKey").isValid()
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

  /** Describe the two production panels and their complete section identities. */
  struct PanelProbe {
    QStringList keys;
    std::function<std::unique_ptr<ParameterPanel>()> create;
  };
  const std::vector<PanelProbe> probes = {
      {{QStringLiteral("color.process"), QStringLiteral("color.backlight"),
        QStringLiteral("color.dyes"), QStringLiteral("color.viewing"),
        QStringLiteral("color.final")},
       [&]() { return std::make_unique<ColorPanel>(getState, setState, noImage); }},
      {{QStringLiteral("contact_copy.film"),
        QStringLiteral("contact_copy.richards"),
        QStringLiteral("contact_copy.manual_points"),
        QStringLiteral("contact_copy.darkroom")},
       [&]() {
         return std::make_unique<ContactCopyPanel>(getState, setState, noImage);
       }}};

  for (const PanelProbe &probe : probes) {
    auto first = probe.create();
    std::vector<bool> expected(probe.keys.size(), true);
    if (!verify(*first, probe.keys, expected))
      return false;
    for (int i = 0; i < probe.keys.size(); ++i) {
      if (QSettings().contains(settingKey(probe.keys[i])))
        return fail(QStringLiteral("construction persisted a default choice"));
      // Alternate folds to catch accidental reuse of one section identity.
      if (i % 2 == 0) {
        toggleFor(*first, probe.keys[i])->click();
        expected[i] = false;
      }
    }
    first->updateUI();
    if (!verify(*first, probe.keys, expected) || state != initialState)
      return fail(QStringLiteral("folding changed rows or document parameters"));
    {
      auto second = probe.create();
      if (!verify(*second, probe.keys, expected))
        return false;

      // A programmatic fold remains local, including after applicability
      // changes. The second inspector and saved user choice must not follow it.
      toggleFor(*first, probe.keys[0])->setChecked(true);
      state.rparams.capture_type = RenderParameters::capture_plain_image;
      state.rparams.contact_copy.simulate = false;
      first->updateUI();
      second->updateUI();
      for (const QString &key : probe.keys) {
        if (key.startsWith(QStringLiteral("contact_copy."))
            || key == QStringLiteral("color.dyes")
            || key == QStringLiteral("color.viewing")) {
          auto *group = groupFor(*second, key);
          if (!group->isHidden()
              || group->property("parameterApplicable").toBool())
            return fail(QStringLiteral("inapplicable section was shown: %1")
                            .arg(key));
        }
      }
      state = initialState;
      first->updateUI();
      second->updateUI();
      auto firstExpected = expected;
      firstExpected[0] = true;
      if (!verify(*first, probe.keys, firstExpected)
          || !verify(*second, probe.keys, expected)
          || QSettings().value(settingKey(probe.keys[0]), true).toBool())
        return fail(QStringLiteral("refresh lost independent fold preferences"));
      for (const QString &key : probe.keys) {
        if (groupFor(*second, key)->isHidden())
          return fail(QStringLiteral("applicable section did not return: %1")
                          .arg(key));
      }

      if (auto *color = qobject_cast<ColorPanel *>(second.get())) {
        auto *finalGroup = groupFor(*color, QStringLiteral("color.final"));
        auto *curve = color->findChild<ToneCurveWidget *>(
            QStringLiteral("ColorToneCurveWidget"));
        if (!curve || !finalGroup->isAncestorOf(curve)
            || curve->isVisibleTo(finalGroup))
          return fail(QStringLiteral("tone curve escaped Final adjustments"));
        toggleFor(*color, QStringLiteral("color.final"))->click();
        expected[4] = true;
        if (!curve->isVisibleTo(finalGroup))
          return fail(QStringLiteral("expanded Final adjustments lost its curve"));

        auto *spectra = color->findChild<QWidget *>(
            QStringLiteral("ColorSpectralChartRow"));
        if (!spectra || !spectra->isHidden())
          return fail(QStringLiteral("matrix-only model exposed spectral chart"));
        state.rparams.color_model =
            RenderParameters::color_model_dufay_color_cinematography_spectra;
        color->updateUI();
        if (!spectra->property("parameterApplicable").toBool()
            || !spectra->isHidden())
          return fail(QStringLiteral("newly applicable chart escaped saved fold"));
        toggleFor(*color, QStringLiteral("color.dyes"))->click();
        expected[2] = true;
        if (spectra->isHidden())
          return fail(QStringLiteral("expanded dyes lost the applicable chart"));
        state = initialState;
        color->updateUI();
        if (!spectra->isHidden())
          return fail(QStringLiteral("expanded dyes resurrected inapplicable chart"));
      }

      // An explicit choice in the new inspector affects future panels only.
      toggleFor(*second, probe.keys[1])->click();
      expected[1] = false;
      first->updateUI();
      second->updateUI();
      if (!toggleFor(*first, probe.keys[1])->isChecked()
          || !verify(*second, probe.keys, expected))
        return fail(QStringLiteral("one inspector changed another's fold state"));
    }
    auto reopened = probe.create();
    if (!verify(*reopened, probe.keys, expected))
      return false;
  }
  if (documentEdits != 0 || state != initialState)
    return fail(QStringLiteral("section preferences entered document/Undo state"));
  return true;
}

