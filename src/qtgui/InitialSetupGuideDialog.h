#pragma once

#include "../libcolorscreen/include/render-parameters.h"
#include <QDialog>

class QCheckBox;
class QComboBox;

namespace colorscreen {
class image_data;
}

/** Small extensible guide shown after an image is opened without parameter
    data. Later setup recommendations can be added without changing document
    load/reload orchestration in MainWindow. */
class InitialSetupGuideDialog final : public QDialog {
public:
  using CaptureType =
      decltype(colorscreen::render_parameters::capture_unknown);

  explicit InitialSetupGuideDialog(QWidget *parent, bool suggestCaptureType,
                                   bool looksMonochrome, bool suggestBayer,
                                   bool suggestFStop, bool suggestPitch,
                                   bool suggestFill, bool suggestDPI,
                                   bool suggestWavelengths,
                                   const colorscreen::image_data *scan);

  CaptureType selectedCaptureType() const;
  bool useMonochromeBayerCorrection() const;
  bool useFStop() const;
  bool usePixelPitch() const;
  bool useFillFactor() const;
  bool useDPI() const;
  bool useWavelengths() const;

private:
  QString getSensorName(double widthMm) const;

  QComboBox *m_captureType = nullptr;
  QCheckBox *m_monochromeBayer = nullptr;
  QCheckBox *m_fstop = nullptr;
  QCheckBox *m_pitch = nullptr;
  QCheckBox *m_fill = nullptr;
  QCheckBox *m_dpi = nullptr;
  QCheckBox *m_wavelengths = nullptr;
};
