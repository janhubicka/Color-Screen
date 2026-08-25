from pathlib import Path

p = Path('src/qtgui/MainWindow.cpp')
s = p.read_text()


def once(old, new):
    global s
    count = s.count(old)
    if count != 1:
        raise SystemExit(f'expected one MainWindow match, found {count}: {old[:140]!r}')
    s = s.replace(old, new, 1)


once(
    '  explicit InitialSetupGuideDialog(QWidget *parent, bool suggestBayer, bool suggestFStop, bool suggestPitch, bool suggestFill, bool suggestDPI, const colorscreen::image_data *scan)\n',
    '  explicit InitialSetupGuideDialog(QWidget *parent, bool suggestBayer, bool suggestFStop, bool suggestPitch, bool suggestFill, bool suggestDPI, bool suggestWavelengths, const colorscreen::image_data *scan)\n')

old = 'suggestFStop || suggestPitch || suggestFill || suggestDPI'
if s.count(old) != 2:
    raise SystemExit(f'expected two setup-dialog metadata conditions, found {s.count(old)}')
s = s.replace(old, 'suggestFStop || suggestPitch || suggestFill || suggestDPI\n          || suggestWavelengths')

once(
    '          tr("The following camera parameters were automatically detected:"), this);',
    '          tr("The following capture parameters were automatically detected:"), this);')

once(
    '''    if (suggestDPI) {
      m_dpi = new QCheckBox(tr("Set image resolution to %1 PPI").arg(scan->xdpi, 0, 'f', 1), this);
      m_dpi->setChecked(true);
      layout->addWidget(m_dpi);
    }

    auto *buttons = new QDialogButtonBox(
''',
    '''    if (suggestDPI) {
      m_dpi = new QCheckBox(tr("Set image resolution to %1 PPI").arg(scan->xdpi, 0, 'f', 1), this);
      m_dpi->setChecked(true);
      layout->addWidget(m_dpi);
    }

    if (suggestWavelengths) {
      QStringList values;
      static const char *channelNames[] = {"R", "G", "B", "IR"};
      for (int c = 0; c < 4; ++c) {
        const bool present = c < 3 ? scan->has_rgb()
                                   : scan->has_grayscale_or_ir();
        const double wavelength = scan->wavelengths[c];
        if (!present || !colorscreen::my_isfinite(wavelength)
            || wavelength <= 0)
          continue;
        if (scan->has_rgb())
          values << QString("%1 %2 nm")
                        .arg(channelNames[c])
                        .arg(wavelength, 0, 'f', 0);
        else
          values << QString("%1 nm").arg(wavelength, 0, 'f', 0);
      }
      m_wavelengths = new QCheckBox(
          scan->has_rgb()
              ? tr("Set detected channel wavelengths: %1").arg(values.join(", "))
              : tr("Set capture wavelength to %1").arg(values.join(", ")),
          this);
      m_wavelengths->setChecked(true);
      layout->addWidget(m_wavelengths);
    }

    auto *buttons = new QDialogButtonBox(
''')

once(
    '''  bool useDPI() const {
    return m_dpi && m_dpi->isChecked();
  }

private:
''',
    '''  bool useDPI() const {
    return m_dpi && m_dpi->isChecked();
  }

  bool useWavelengths() const {
    return m_wavelengths && m_wavelengths->isChecked();
  }

private:
''')

once(
    '''  QCheckBox *m_fill = nullptr;
  QCheckBox *m_dpi = nullptr;
};
''',
    '''  QCheckBox *m_fill = nullptr;
  QCheckBox *m_dpi = nullptr;
  QCheckBox *m_wavelengths = nullptr;
};
''')

once(
    '''/** Offer the first post-load setup recommendation for a new image.  The guide
   is deliberately conservative and currently changes only demosaicing; EXIF
   metadata import and screen autodetection will be added independently. */
''',
    '''/** Offer conservative post-load setup recommendations for a new image.
   Detected capture metadata is copied only after explicit user confirmation;
   loading an existing parameter file remains authoritative. */
''')

once(
    '''  bool suggestDPI = m_scan->xdpi > 0 &&
      std::abs(m_scan->xdpi - m_rparams.sharpen.scanner_mtf.scan_dpi) > 0.1;

  if (!suggestBayer && !suggestFStop && !suggestPitch && !suggestFill && !suggestDPI)
    return;

  const std::shared_ptr<colorscreen::image_data> guideScan = m_scan;
  auto *dialog = new InitialSetupGuideDialog(
      this, suggestBayer, suggestFStop, suggestPitch, suggestFill,
      suggestDPI, guideScan.get());
''',
    '''  bool suggestDPI = m_scan->xdpi > 0 &&
      std::abs(m_scan->xdpi - m_rparams.sharpen.scanner_mtf.scan_dpi) > 0.1;
  bool suggestWavelengths = false;
  for (int c = 0; c < 4; ++c) {
    const bool present = c < 3 ? m_scan->has_rgb()
                               : m_scan->has_grayscale_or_ir();
    const double wavelength = m_scan->wavelengths[c];
    if (present && colorscreen::my_isfinite(wavelength) && wavelength > 0
        && std::abs(wavelength
                    - m_rparams.sharpen.scanner_mtf.wavelengths[c]) > 0.5)
      suggestWavelengths = true;
  }

  if (!suggestBayer && !suggestFStop && !suggestPitch && !suggestFill
      && !suggestDPI && !suggestWavelengths)
    return;

  const std::shared_ptr<colorscreen::image_data> guideScan = m_scan;
  auto *dialog = new InitialSetupGuideDialog(
      this, suggestBayer, suggestFStop, suggestPitch, suggestFill,
      suggestDPI, suggestWavelengths, guideScan.get());
''')

once(
    '''      [this, dialog, guideScan, suggestBayer, suggestFStop, suggestPitch,
       suggestFill, suggestDPI](int result) {
''',
    '''      [this, dialog, guideScan, suggestBayer, suggestFStop, suggestPitch,
       suggestFill, suggestDPI, suggestWavelengths](int result) {
''')

once(
    '''        if (suggestDPI && dialog->useDPI()) {
          state.rparams.sharpen.scanner_mtf.scan_dpi = guideScan->xdpi;
          changes << tr("image resolution");
        }

        if (changes.isEmpty())
''',
    '''        if (suggestDPI && dialog->useDPI()) {
          state.rparams.sharpen.scanner_mtf.scan_dpi = guideScan->xdpi;
          changes << tr("image resolution");
        }

        if (suggestWavelengths && dialog->useWavelengths()) {
          for (int c = 0; c < 4; ++c) {
            const bool present = c < 3 ? guideScan->has_rgb()
                                       : guideScan->has_grayscale_or_ir();
            const double wavelength = guideScan->wavelengths[c];
            if (present && colorscreen::my_isfinite(wavelength)
                && wavelength > 0)
              state.rparams.sharpen.scanner_mtf.wavelengths[c] = wavelength;
          }
          changes << tr("channel wavelengths");
        }

        if (changes.isEmpty())
''')

p.write_text(s)
