#ifndef GEOMETRY_PANEL_H
#define GEOMETRY_PANEL_H

#include "ParameterPanel.h"
#include "../libcolorscreen/include/finetune.h"
#include <QDoubleSpinBox>

class QCheckBox;
class QLabel;
class DeformationChartWidget;
class FinetuneImagesPanel;

namespace colorscreen {
struct finetune_result;
}

class GeometryPanel : public ParameterPanel {
  Q_OBJECT
public:
  GeometryPanel(StateGetter stateGetter, StateSetter stateSetter,
                ImageGetter imageGetter, QWidget *parent = nullptr);
  ~GeometryPanel() override;

signals:
  void optimizeRequested(bool autoChecked);
  void nonlinearToggled(bool checked);

  void centerOnRequested(const colorscreen::point_t &p);

  void heatmapToleranceChanged(double tol);
  void exaggerateChanged(double ex);
  void maxArrowLengthChanged(double len);
  void automaticallyAddPointsRequested(const colorscreen::finetune_area_parameters &params);
  void automaticallyAddPointsInAreaRequested(const colorscreen::finetune_area_parameters &params);
  void autodetectCoordinatesRequested();
  void optimizeCoordinatesRequested();

public:
  bool isAutoEnabled() const;
  bool isNonlinearEnabled() const;
  void updateRegistrationPointInfo(const ParameterState &state);
  void updateDeformationChart();
  void updateFinetuneImages(const colorscreen::finetune_result& result);
  /** Show document-owned geometry-fit freshness/result status. */
  void setFitStatus(const QString &status);
  void setRegistrationPointsVisible(bool visible);
  void setNonlinearChecked(bool checked);
  QCheckBox *autoOptimizeCheckBox() const { return m_autoOptimizeBox; }
  QCheckBox *registrationPointsCheckBox() const { return m_showRegistrationPointsBox; }
  colorscreen::finetune_area_parameters finetuneAreaParams() const { return m_finetuneAreaParams; }

protected:
  void onParametersRefreshed(const ParameterState &state) override { 
      updateRegistrationPointInfo(state);
      updateDeformationChart(); 
  }

private:
  void setupUi();
  bool m_nonlinearEnabled = false;
  DeformationChartWidget *m_deformationChart = nullptr;
  DeformationChartWidget *m_lensChart = nullptr;
  DeformationChartWidget *m_perspectiveChart = nullptr;
  DeformationChartWidget *m_nonlinearChart = nullptr;
  FinetuneImagesPanel *m_finetuneImagesPanel = nullptr;

  QVBoxLayout *m_chartContainer = nullptr;
  QVBoxLayout *m_lensChartContainer = nullptr;
  QVBoxLayout *m_perspectiveChartContainer = nullptr;
  QVBoxLayout *m_nonlinearChartContainer = nullptr;
  QVBoxLayout *m_finetuneImagesContainer = nullptr;
  QSlider *m_heatmapToleranceSlider = nullptr;
  QWidget *m_exaggerateSliderContainer = nullptr;
  QWidget *m_maxArrowLengthSliderContainer = nullptr;

  QCheckBox *m_showRegistrationPointsBox = nullptr;
  QPushButton *m_autodetectCoordinatesButton = nullptr;
  QPushButton *m_optimizeButton = nullptr;
  QCheckBox *m_autoOptimizeBox = nullptr;
  QCheckBox *m_lensCb = nullptr;
  QCheckBox *m_tiltCb = nullptr;
  QCheckBox *m_nlCb = nullptr;

  QLabel *m_lensMessageLabel = nullptr;
  QLabel *m_tiltMessageLabel = nullptr;
  QLabel *m_nonlinearMessageLabel = nullptr;
  QLabel *m_optimizationMessageLabel = nullptr;
  QLabel *m_fitStatusLabel = nullptr;
  colorscreen::finetune_area_parameters m_finetuneAreaParams;
  QDoubleSpinBox *m_gridWidthSpin = nullptr;
  QDoubleSpinBox *m_gridHeightSpin = nullptr;
};

#endif // GEOMETRY_PANEL_H
