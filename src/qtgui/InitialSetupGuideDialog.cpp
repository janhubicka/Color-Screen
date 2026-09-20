#include "InitialSetupGuideDialog.h"

#include "ScreenPanel.h"
#include "../libcolorscreen/include/base.h"
#include "../libcolorscreen/include/imagedata.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

InitialSetupGuideDialog::InitialSetupGuideDialog(
    QWidget *parent, bool suggestCaptureType, bool looksMonochrome,
    bool suggestBayer, bool suggestFStop, bool suggestPitch, bool suggestFill,
    bool suggestDPI, bool suggestWavelengths,
    const colorscreen::image_data *scan, CaptureType initialCaptureType,
    colorscreen::scr_type initialScreenType)
    : QDialog(parent), m_initialCaptureType(initialCaptureType) {
  setWindowTitle(tr("Suggested image setup"));
  setModal(true);

  auto *layout = new QVBoxLayout(this);

  if (suggestCaptureType) {
    auto *intro = new QLabel(
        tr("Choose what kind of material this image captures. The capture "
           "type determines which restoration workflow is applicable."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    m_captureType = new QComboBox(this);
    m_captureType->setObjectName(QStringLiteral("InitialCaptureTypeCombo"));
    for (int i = 0; i < (int)colorscreen::render_parameters::capture_max; ++i) {
      const auto capture = static_cast<CaptureType>(i);
      bool show = capture == colorscreen::render_parameters::capture_unknown;
      if (looksMonochrome) {
        // RGB may only be a Bayer-container detail here. Offer the
        // monochrome-through-screen paths, which reconstruct a regular screen
        // geometrically, but not RGB screen-color detection paths.
        show = show ||
               capture == colorscreen::render_parameters::capture_transparency ||
               capture == colorscreen::render_parameters::capture_negative ||
               capture == colorscreen::render_parameters::capture_plain_image;
      } else if (scan) {
        show = show ||
               colorscreen::render_parameters::capture_type_compatible_p(
                   capture, scan);
      }
      if (show) {
        const auto &property =
            colorscreen::render_parameters::capture_properties[i];
        m_captureType->addItem(QString::fromUtf8(property.pretty_name), i);
        if (property.help && property.help[0])
          m_captureType->setItemData(m_captureType->count() - 1,
                                     QString::fromUtf8(property.help),
                                     Qt::ToolTipRole);
      }
    }
    m_captureType->setCurrentIndex(m_captureType->findData(
        (int)colorscreen::render_parameters::capture_unknown));
    m_captureType->setToolTip(
        tr("Choose Unknown if you are not sure yet. Color-Screen will keep "
           "the restoration workflow conservative until this is known."));
    layout->addWidget(m_captureType);
  }

  // Screen setup follows the physical capture choice. Monochrome captures
  // cannot identify the process from screen colors, so they expose only
  // regular screens and require one before automatic lattice detection.
  // RGB captures with the screen visible can identify common processes
  // directly, so the selector stays hidden and autodetection remains usable.
  m_screenTypeRow = new QWidget(this);
  m_screenTypeRow->setObjectName(QStringLiteral("InitialScreenTypeRow"));
  auto *screenRowLayout = new QHBoxLayout(m_screenTypeRow);
  screenRowLayout->setContentsMargins(0, 0, 0, 0);
  screenRowLayout->addWidget(new QLabel(tr("Original color screen:"), m_screenTypeRow));
  m_screenType = createScreenTypeComboBox(
      m_screenTypeRow, true, tr("Choose regular screen type…"));
  m_screenType->setObjectName(QStringLiteral("InitialScreenTypeCombo"));
  const int initialScreenIndex = m_screenType->findData((int)initialScreenType);
  if (initialScreenIndex >= 0)
    m_screenType->setCurrentIndex(initialScreenIndex);
  screenRowLayout->addWidget(m_screenType, 1);
  layout->addWidget(m_screenTypeRow);

  m_preferredColorModel = new QCheckBox(this);
  m_preferredColorModel->setObjectName(
      QStringLiteral("InitialPreferredColorModelCheck"));
  m_preferredColorModel->setChecked(true);
  layout->addWidget(m_preferredColorModel);

  m_autoDetectScreen = new QCheckBox(
      tr("Automatically detect the screen after applying setup"), this);
  m_autoDetectScreen->setObjectName(
      QStringLiteral("InitialAutoDetectScreenCheck"));
  m_autoDetectScreen->setChecked(true);
  layout->addWidget(m_autoDetectScreen);

  if (m_captureType) {
    connect(m_captureType, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { updateScreenSetupControls(); });
  }
  connect(m_screenType, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int) { updateScreenSetupControls(); });
  updateScreenSetupControls();

  const bool hasFollowingSuggestions =
      suggestBayer || suggestFStop || suggestPitch || suggestFill ||
      suggestDPI || suggestWavelengths;
  const bool hasSetupSection =
      suggestCaptureType ||
      colorscreen::render_parameters::capture_has_screen_p(initialCaptureType);
  if (hasSetupSection && hasFollowingSuggestions) {
    auto *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);
  }

  if (suggestBayer) {
    auto *intro = new QLabel(
        tr("This appears to be a monochromatic capture made with a Bayer-filter camera."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    m_monochromeBayer =
        new QCheckBox(tr("Reload with Bayer-filter compensation"), this);
    m_monochromeBayer->setObjectName(
        QStringLiteral("InitialMonochromeBayerCheck"));
    m_monochromeBayer->setChecked(true);
    layout->addWidget(m_monochromeBayer);

    if (suggestFStop || suggestPitch || suggestFill || suggestDPI ||
        suggestWavelengths) {
      auto *line = new QFrame(this);
      line->setFrameShape(QFrame::HLine);
      line->setFrameShadow(QFrame::Sunken);
      layout->addWidget(line);
    }
  }

  if (suggestFStop || suggestPitch || suggestFill || suggestDPI ||
      suggestWavelengths) {
    auto *intro = new QLabel(
        tr("The following capture parameters were automatically detected:"),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
  }

  if (suggestFStop) {
    m_fstop = new QCheckBox(
        tr("Set nominal f-stop to f/%1").arg(scan->f_stop, 0, 'f', 1), this);
    m_fstop->setChecked(true);
    layout->addWidget(m_fstop);
  }

  if (suggestPitch) {
    int divisor = scan->width > 0 ? scan->width : 1;
    double sensorWidth = divisor * scan->pixel_pitch / 1000.0;
    QString sensorName = getSensorName(sensorWidth);
    m_pitch = new QCheckBox(
        tr("Set sensor pixel pitch to %1 μm (Sensor size: %2)")
            .arg(scan->pixel_pitch, 0, 'f', 2)
            .arg(sensorName),
        this);
    m_pitch->setChecked(true);
    layout->addWidget(m_pitch);
  }

  if (suggestFill) {
    m_fill = new QCheckBox(
        tr("Set sensor fill factor to %1")
            .arg(scan->sensor_fill_factor, 0, 'f', 3),
        this);
    m_fill->setChecked(true);
    layout->addWidget(m_fill);
  }

  if (suggestDPI) {
    m_dpi = new QCheckBox(
        tr("Set image resolution to %1 PPI").arg(scan->xdpi, 0, 'f', 1), this);
    m_dpi->setChecked(true);
    layout->addWidget(m_dpi);
  }

  if (suggestWavelengths) {
    QStringList values;
    static const char *channelNames[] = {"R", "G", "B", "IR"};
    for (int c = 0; c < 4; ++c) {
      const bool present = c < 3 ? scan->has_rgb() : scan->has_grayscale_or_ir();
      const double wavelength = scan->wavelengths[c];
      if (!present || !colorscreen::my_isfinite(wavelength) || wavelength <= 0)
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
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons->button(QDialogButtonBox::Ok)
      ->setText(suggestBayer ? tr("Apply and reload") : tr("Apply"));
  buttons->button(QDialogButtonBox::Cancel)->setText(tr("Not now"));
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);
}

InitialSetupGuideDialog::CaptureType
InitialSetupGuideDialog::selectedCaptureType() const {
  if (!m_captureType)
    return m_initialCaptureType;
  return static_cast<CaptureType>(m_captureType->currentData().toInt());
}

/** Return the original regular screen selected for a monochrome capture. */
colorscreen::scr_type InitialSetupGuideDialog::selectedScreenType() const {
  if (!m_screenType)
    return colorscreen::NoScreen;
  return static_cast<colorscreen::scr_type>(
      m_screenType->currentData().toInt());
}

/** Return whether the preferred dye model for the chosen screen is accepted. */
bool InitialSetupGuideDialog::usePreferredColorModel() const {
  return m_preferredColorModel && !m_preferredColorModel->isHidden() &&
         m_preferredColorModel->isEnabled() &&
         m_preferredColorModel->isChecked();
}

/** Return whether accepted setup should launch screen autodetection. */
bool InitialSetupGuideDialog::automaticallyDetectScreen() const {
  return m_autoDetectScreen && !m_autoDetectScreen->isHidden() &&
         m_autoDetectScreen->isEnabled() && m_autoDetectScreen->isChecked();
}

/** Refresh screen controls after capture or screen selection changes. */
void InitialSetupGuideDialog::updateScreenSetupControls() {
  if (!m_screenTypeRow || !m_screenType || !m_autoDetectScreen)
    return;

  const CaptureType capture = selectedCaptureType();
  const bool usesScreen =
      colorscreen::render_parameters::capture_has_screen_p(capture);
  const bool regularRequired =
      colorscreen::render_parameters::capture_requires_regular_screen_p(capture);
  const bool screenColorsVisible =
      colorscreen::render_parameters::capture_supports_screen_detection_p(
          capture);
  const bool regularSelected =
      colorscreen::screen_has_regular_geometry_p(selectedScreenType());

  m_screenTypeRow->setVisible(regularRequired);

  colorscreen::render_parameters preferred;
  const bool hasPreferredModel =
      regularSelected && preferred.auto_color_model(selectedScreenType());
  if (hasPreferredModel) {
    const QString modelName = QString::fromUtf8(
        colorscreen::render_parameters::color_model_properties[
            preferred.color_model]
            .pretty_name);
    if (m_preferredColorModel->isHidden())
      m_preferredColorModel->setChecked(true);
    m_preferredColorModel->setText(
        tr("Use preferred color model: %1").arg(modelName));
    m_preferredColorModel->setToolTip(
        tr("Use the dye/color model recommended for the selected historical "
           "screen process."));
  }
  m_preferredColorModel->setVisible(regularRequired && hasPreferredModel);

  m_autoDetectScreen->setVisible(usesScreen);
  m_autoDetectScreen->setEnabled(
      usesScreen &&
      (screenColorsVisible || (regularRequired && regularSelected)));

  if (screenColorsVisible) {
    m_autoDetectScreen->setToolTip(
        tr("The screen colors are visible in this capture, so Color-Screen can "
           "identify common screen types automatically."));
  } else if (regularRequired && !regularSelected) {
    m_autoDetectScreen->setToolTip(
        tr("Choose the original regular color screen before enabling "
           "automatic detection."));
  } else if (regularRequired) {
    m_autoDetectScreen->setToolTip(
        tr("Detect the selected regular screen lattice and registration "
           "points automatically."));
  } else {
    m_autoDetectScreen->setToolTip(QString());
  }

  // These controls appear after the dialog has already been laid out when the
  // capture/screen choice changes. Recompute the top-level size so the newly
  // visible checkboxes receive their own rows rather than being clipped into
  // the following capture-metadata controls.
  QTimer::singleShot(0, this, [this]() {
    if (layout()) {
      layout()->invalidate();
      layout()->activate();
    }
    adjustSize();
  });
}

bool InitialSetupGuideDialog::useMonochromeBayerCorrection() const {
  return m_monochromeBayer && m_monochromeBayer->isChecked();
}

bool InitialSetupGuideDialog::useFStop() const {
  return m_fstop && m_fstop->isChecked();
}

bool InitialSetupGuideDialog::usePixelPitch() const {
  return m_pitch && m_pitch->isChecked();
}

bool InitialSetupGuideDialog::useFillFactor() const {
  return m_fill && m_fill->isChecked();
}

bool InitialSetupGuideDialog::useDPI() const {
  return m_dpi && m_dpi->isChecked();
}

bool InitialSetupGuideDialog::useWavelengths() const {
  return m_wavelengths && m_wavelengths->isChecked();
}

QString InitialSetupGuideDialog::getSensorName(double widthMm) const {
  struct Preset {
    const char *name;
    double width;
  };
  const Preset presets[] = {
      {"PhaseOne 54.0mm", 54.0},
      {"PhaseOne 53.7mm", 53.7},
      {"PhaseOne 53.4mm", 53.4},
      {"Medium Format 43.8mm", 43.8},
      {"Full Frame (36mm)", 36.0},
      {"APS-H (28.3mm)", 28.3},
      {"APS-C (23.0mm)", 23.0},
      {"Micro Four Thirds (17.3mm)", 17.3},
      {"1-inch (13.2mm)", 13.2},
      {"1/1.7-inch (7.6mm)", 7.6},
      {"1/2.5-inch (5.76mm)", 5.76},
  };
  for (const auto &preset : presets)
    if (std::abs(widthMm - preset.width) < 1.0)
      return QString::fromUtf8(preset.name);
  return tr("Unknown, %1 mm width").arg(widthMm, 0, 'f', 1);
}
