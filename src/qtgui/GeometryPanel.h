#ifndef GEOMETRY_PANEL_H
#define GEOMETRY_PANEL_H

#include "ParameterPanel.h"

class QCheckBox;
class DeformationChartWidget;

class GeometryPanel : public ParameterPanel {
  Q_OBJECT
public:
  GeometryPanel(StateGetter stateGetter, StateSetter stateSetter,
                ImageGetter imageGetter, QWidget *parent = nullptr);
  ~GeometryPanel() override;

signals:
  void optimizeRequested(bool autoChecked);
  void nonlinearToggled(bool checked);
  void detachDeformationChartRequested(QWidget *widget);
  void detachLensChartRequested(QWidget *widget);
  void detachPerspectiveChartRequested(QWidget *widget);
  void detachNonlinearChartRequested(QWidget *widget);

public:
  bool isAutoEnabled() const;
  bool isNonlinearEnabled() const;
  void updateDeformationChart();
  void reattachDeformationChart(QWidget *widget);
  void reattachLensChart(QWidget *widget);
  void reattachPerspectiveChart(QWidget *widget);
  void reattachNonlinearChart(QWidget *widget);

private:
  void setupUi();
  bool m_nonlinearEnabled = false;
  DeformationChartWidget *m_deformationChart = nullptr;
  DeformationChartWidget *m_lensChart = nullptr;
  DeformationChartWidget *m_perspectiveChart = nullptr;
  DeformationChartWidget *m_nonlinearChart = nullptr;

  QVBoxLayout *m_chartContainer = nullptr;
  QVBoxLayout *m_lensChartContainer = nullptr;
  QVBoxLayout *m_perspectiveChartContainer = nullptr;
  QVBoxLayout *m_nonlinearChartContainer = nullptr;
  QSlider *m_heatmapToleranceSlider = nullptr;
};

#endif // GEOMETRY_PANEL_H
