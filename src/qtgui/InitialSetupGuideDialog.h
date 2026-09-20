#pragma once

#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
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
                                   const colorscreen::image_data *scan,
                                   CaptureType initialCaptureType,
                                   colorscreen::scr_type initialScreenType);

  CaptureType selectedCaptureType() const;
  colorscreen::scr_type selectedScreenType() const;
  bool usePreferredColorModel() const;
  bool automaticallyDetectScreen() const;
  bool useMonochromeBayerCorrection() const;
  bool useFStop() const;
  bool usePixelPitch() const;
  bool useFillFactor() const;
  bool useDPI() const;
  bool useWavelengths() const;

private:
  QString getSensorName(double widthMm) const;
  void updateScreenSetupControls();

  CaptureType m_initialCaptureType =
      colorscreen::render_parameters::capture_unknown;
  QComboBox *m_captureType = nullptr;
  QWidget *m_screenTypeRow = nullptr;
  QComboBox *m_screenType = nullptr;
  QCheckBox *m_preferredColorModel = nullptr;
  QCheckBox *m_autoDetectScreen = nullptr;
  QCheckBox *m_monochromeBayer = nullptr;
  QCheckBox *m_fstop = nullptr;
  QCheckBox *m_pitch = nullptr;
  QCheckBox *m_fill = nullptr;
  QCheckBox *m_dpi = nullptr;
  QCheckBox *m_wavelengths = nullptr;
};
