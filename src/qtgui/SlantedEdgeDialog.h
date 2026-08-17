#pragma once

#include "../libcolorscreen/include/colorscreen.h"
#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QSpinBox;

/** Configure slanted-edge measurement before selecting its image area.
    RGB scans default to measuring all native channels from the same ROI; the
    historical mixed/native image layer remains available explicitly.  */
class SlantedEdgeDialog : public QDialog {
public:
  /** Construct the dialog from DEFAULTS.  HAS_PREVIOUS_MEASUREMENT controls
      whether the new curve may share a capture with the preceding curve.  */
  explicit SlantedEdgeDialog(
      const colorscreen::slanted_edge_parameters &defaults,
      bool hasPreviousMeasurement, bool hasRgb, bool hasInfrared,
      QWidget *parent = nullptr);

  /** Return the complete settings currently entered by the user.  */
  colorscreen::slanted_edge_parameters parameters() const;

  /** Return true when one selected ROI should produce all native channels.  */
  bool measureNativeChannels() const;

private:
  /** Enable acceptance only when required measurement metadata is valid.  */
  void updateValidation();

  QLineEdit *m_nameEdit = nullptr;
  QComboBox *m_sourceCombo = nullptr;
  QDoubleSpinBox *m_wavelengthSpin = nullptr;
  QCheckBox *m_sameCaptureCheck = nullptr;
  QSpinBox *m_oversamplingSpin = nullptr;
  QDoubleSpinBox *m_halfWidthSpin = nullptr;
  QComboBox *m_windowCombo = nullptr;
  QLabel *m_statusLabel = nullptr;
  QPushButton *m_acceptButton = nullptr;
};
