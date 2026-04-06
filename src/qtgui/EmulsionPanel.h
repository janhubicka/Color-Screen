#ifndef EMULSION_PANEL_H
#define EMULSION_PANEL_H

#include "ParameterPanel.h"

class HDCurveWidget;
class QDoubleSpinBox;

class EmulsionPanel : public ParameterPanel {
  Q_OBJECT
public:
  EmulsionPanel(StateGetter stateGetter, StateSetter stateSetter,
                ImageGetter imageGetter, QWidget *parent = nullptr);
  ~EmulsionPanel() override;

private:
  void setupUi();
  void updateSpinBoxes();

  HDCurveWidget *m_hdCurveWidget = nullptr;
  
  QDoubleSpinBox *m_minXSpin = nullptr;
  QDoubleSpinBox *m_minYSpin = nullptr;
  QDoubleSpinBox *m_linear1XSpin = nullptr;
  QDoubleSpinBox *m_linear1YSpin = nullptr;
  QDoubleSpinBox *m_linear2XSpin = nullptr;
  QDoubleSpinBox *m_linear2YSpin = nullptr;
  QDoubleSpinBox *m_maxXSpin = nullptr;
  QDoubleSpinBox *m_maxYSpin = nullptr;

  bool m_updatingSpinBoxes = false;
};

#endif // EMULSION_PANEL_H
