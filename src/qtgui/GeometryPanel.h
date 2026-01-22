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

public:
  bool isAutoEnabled() const;
  bool isNonlinearEnabled() const;
  void updateDeformationChart();
  void reattachDeformationChart(QWidget *widget);

private:
  void setupUi();
  QCheckBox *m_nonlinearBox = nullptr;
  DeformationChartWidget *m_deformationChart = nullptr;
  QVBoxLayout *m_chartContainer = nullptr;
  QSlider *m_heatmapToleranceSlider = nullptr;
};

#endif // GEOMETRY_PANEL_H
