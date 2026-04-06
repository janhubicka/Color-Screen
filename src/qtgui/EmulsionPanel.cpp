#include "EmulsionPanel.h"
#include "HDCurveWidget.h"
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

EmulsionPanel::EmulsionPanel(StateGetter stateGetter, StateSetter stateSetter,
                             ImageGetter imageGetter, QWidget *parent)
    : ParameterPanel(stateGetter, stateSetter, imageGetter, parent) {
  setupUi();
}

EmulsionPanel::~EmulsionPanel() = default;

void EmulsionPanel::setupUi() {
  addSeparator("Film characteristics");

  // Create the interactive curve widget
  m_hdCurveWidget = new HDCurveWidget();
  
  // Wrap it nicely to center or align it
  QWidget *chartWrapper = new QWidget();
  m_hdCurveContainer = new QVBoxLayout(chartWrapper);
  m_hdCurveContainer->setContentsMargins(0, 0, 0, 0);
  
  QWidget *detachableCurve = createDetachableSection("H&D Curve", m_hdCurveWidget, [this]() {
      emit detachHDCurveRequested(m_hdCurveWidget);
  });
  m_hdCurveContainer->addWidget(detachableCurve);
  
  if (m_currentGroupForm)
      m_currentGroupForm->addRow(chartWrapper);
  else
      m_form->addRow(chartWrapper);

  // Connection from HDCurveWidget -> State
  connect(m_hdCurveWidget, &HDCurveWidget::parametersChanged, this, [this](const colorscreen::hd_curve_parameters &params) {
      if (m_updatingSpinBoxes) return;
      
      applyChange(
          [params](ParameterState &s) {
              s.rparams.emulsion_characteristic_curve = params;
          },
          "Modify emulsion curve"
      );
      updateSpinBoxes();
  });

  // Setup manual spinboxes for the points
  auto createSpinBox = [this](QDoubleSpinBox*& spin) {
      spin = new QDoubleSpinBox();
      spin->setRange(-10.0, 10.0);
      spin->setSingleStep(0.1);
      spin->setDecimals(2);
      
      connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
          if (m_updatingSpinBoxes) return;
          
          colorscreen::hd_curve_parameters p = m_hdCurveWidget->getParameters();
          p.minx = m_minXSpin->value();
          p.miny = m_minYSpin->value();
          p.linear1x = m_linear1XSpin->value();
          p.linear1y = m_linear1YSpin->value();
          p.linear2x = m_linear2XSpin->value();
          p.linear2y = m_linear2YSpin->value();
          p.maxx = m_maxXSpin->value();
          p.maxy = m_maxYSpin->value();
          
          m_hdCurveWidget->setParameters(p);
          
          applyChange(
              [p](ParameterState &s) {
                  s.rparams.emulsion_characteristic_curve = p;
              },
              "Modify emulsion curve"
          );
      });
  };

  createSpinBox(m_minXSpin); createSpinBox(m_minYSpin);
  createSpinBox(m_linear1XSpin); createSpinBox(m_linear1YSpin);
  createSpinBox(m_linear2XSpin); createSpinBox(m_linear2YSpin);
  createSpinBox(m_maxXSpin); createSpinBox(m_maxYSpin);

  auto addRow = [this](const QString &label, QWidget *w1, QWidget *w2) {
      QWidget *row = new QWidget();
      QHBoxLayout *l = new QHBoxLayout(row);
      l->setContentsMargins(0, 0, 0, 0);
      l->addWidget(new QLabel("X:"));
      l->addWidget(w1);
      l->addWidget(new QLabel("Y:"));
      l->addWidget(w2);
      
      if (m_currentGroupForm)
          m_currentGroupForm->addRow(label, row);
      else
          m_form->addRow(label, row);
  };

  addRow("Min point", m_minXSpin, m_minYSpin);
  addRow("Linear start", m_linear1XSpin, m_linear1YSpin);
  addRow("Linear end", m_linear2XSpin, m_linear2YSpin);
  addRow("Max point", m_maxXSpin, m_maxYSpin);

  // Sync state to UI
  m_paramUpdaters.push_back([this](const ParameterState &s) {
      if (s.rparams.emulsion_characteristic_curve == m_hdCurveWidget->getParameters())
          return;
      m_hdCurveWidget->blockSignals(true);
      m_hdCurveWidget->setParameters(s.rparams.emulsion_characteristic_curve);
      m_hdCurveWidget->blockSignals(false);
      updateSpinBoxes();
  });

  updateUI();
}

void EmulsionPanel::updateSpinBoxes() {
    m_updatingSpinBoxes = true;
    colorscreen::hd_curve_parameters p = m_hdCurveWidget->getParameters();
    
    m_minXSpin->setValue(p.minx);
    m_minYSpin->setValue(p.miny);
    
    m_linear1XSpin->setValue(p.linear1x);
    m_linear1YSpin->setValue(p.linear1y);
    
    m_linear2XSpin->setValue(p.linear2x);
    m_linear2YSpin->setValue(p.linear2y);
    
    m_maxXSpin->setValue(p.maxx);
    m_maxYSpin->setValue(p.maxy);
    m_updatingSpinBoxes = false;
}

void EmulsionPanel::reattachHDCurve(QWidget *widget) {
    if (widget != m_hdCurveWidget)
        return;
        
    if (m_hdCurveContainer && m_hdCurveContainer->count() > 0) {
        QWidget *section = m_hdCurveContainer->itemAt(0)->widget();
        if (section && section->layout()) {
            QLayoutItem *item = section->layout()->takeAt(section->layout()->count() - 1);
            if (item) {
                if (item->widget()) delete item->widget();
                delete item;
            }
            section->layout()->addWidget(widget);
            widget->show();
            
            if (section->layout()->count() > 0) {
                QLayoutItem *headerItem = section->layout()->itemAt(0);
                if (headerItem && headerItem->widget()) {
                    headerItem->widget()->show();
                }
            }
        }
    }
}
