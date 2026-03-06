#pragma once

#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/render-type-parameters.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
#include <QDialog>

class QComboBox;
class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QSlider;
class QLabel;

class RenderDialog : public QDialog {
  Q_OBJECT
public:
  explicit RenderDialog(
      const colorscreen::render_type_parameters &rtparams,
      const colorscreen::render_parameters &rparams,
      const colorscreen::scr_to_img_parameters &scrParams,
      const colorscreen::image_data *scan,
      bool isDng,
      QWidget *parent = nullptr);

  // Results filled after exec() == Accepted
  colorscreen::render_type_parameters renderTypeParams() const;
  colorscreen::render_parameters::output_profile_t outputProfile() const;
  bool hdr() const;
  int depth() const;
  colorscreen::render_to_file_params::output_geometry geometry() const;
  int antialias() const;

  // Size results (0 = use default)
  double scale() const;
  double screenScale() const;
  int outputWidth() const;
  int outputHeight() const;

private slots:
  void onGeometryChanged(int index);
  void onModeChanged(int index);
  void updateSizePreview();
  void updateControlStates();

private:
  bool screenGeometryActive() const;

  // Stored copies for complete_rendered_file_parameters calls
  colorscreen::render_type_parameters m_rtparams;
  colorscreen::scr_to_img_parameters  m_scrParams;
  const colorscreen::image_data      *m_scan;

  QComboBox *m_modeCombo     = nullptr;
  QComboBox *m_profileCombo  = nullptr;
  QCheckBox *m_hdrCheck      = nullptr;
  QComboBox *m_depthCombo    = nullptr;
  QComboBox *m_geometryCombo = nullptr;
  QSpinBox  *m_antialiasSpin = nullptr;

  // Non-DNG controls wrapper
  QWidget *m_nonDngWidget = nullptr;

  // Size controls
  QDoubleSpinBox *m_scaleSpin       = nullptr;
  QSlider        *m_scaleSlider     = nullptr;
  QDoubleSpinBox *m_screenScaleSpin = nullptr;
  QSlider        *m_screenScaleSlider = nullptr;
  QWidget        *m_screenScaleRow  = nullptr; // to show/hide
  QSpinBox       *m_widthSpin       = nullptr;
  QSpinBox       *m_heightSpin      = nullptr;
  QLabel         *m_sizePreviewLabel = nullptr;

  bool m_updatingSliders = false; // re-entrancy guard
};
