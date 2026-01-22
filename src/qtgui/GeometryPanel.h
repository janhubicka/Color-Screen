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

public:
  bool isAutoEnabled() const;
  bool isNonlinearEnabled() const;
  void updateDeformationChart();

private:
  void setupUi();
  QCheckBox *m_nonlinearBox = nullptr;
  DeformationChartWidget *m_deformationChart = nullptr;
  QSlider *m_heatmapToleranceSlider = nullptr;
};

#endif // GEOMETRY_PANEL_H
