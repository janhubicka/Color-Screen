#pragma once

#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/render-type-parameters.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
#include <QDialog>

class QComboBox;
class QCheckBox;
class QSpinBox;

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

private:
  void onModeChanged(int index);

  QComboBox *m_modeCombo = nullptr;
  QComboBox *m_profileCombo = nullptr;
  QCheckBox *m_hdrCheck = nullptr;
  QComboBox *m_depthCombo = nullptr;
  QComboBox *m_geometryCombo = nullptr;
  QSpinBox  *m_antialiasSpin = nullptr;

  // Non-DNG controls wrapper widget (hidden for DNG)
  QWidget   *m_nonDngWidget = nullptr;

  colorscreen::render_type_parameters m_rtparams;
};
