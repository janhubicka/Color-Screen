#ifndef GEOMETRY_PANEL_H
#define GEOMETRY_PANEL_H

#include "ParameterPanel.h"

class QCheckBox;

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

private:
  void setupUi();
  QCheckBox *m_nonlinearBox = nullptr;
};

#endif // GEOMETRY_PANEL_H
