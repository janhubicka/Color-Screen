#include "ContactCopyPanel.h"
#include "HDCurveWidget.h"
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include "HistogramWorker.h"
#include "../libcolorscreen/include/sensitivity.h"

ContactCopyPanel::ContactCopyPanel(StateGetter stateGetter, StateSetter stateSetter,
                             ImageGetter imageGetter, QWidget *parent)
    : ParameterPanel(stateGetter, stateSetter, imageGetter, parent) {
  qRegisterMetaType<std::vector<uint64_t>>("std::vector<uint64_t>");
  qRegisterMetaType<HistogramRequestData>("HistogramRequestData");

  m_worker = new HistogramWorker(m_imageGetter());
  m_worker->moveToThread(&m_workerThread);

  connect(&m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
  connect(&m_taskQueue, &TaskQueue::triggerRender, this, &ContactCopyPanel::onTriggerHistogram);
  connect(m_worker, &HistogramWorker::finished, this, &ContactCopyPanel::onHistogramFinished);

  m_workerThread.start();

  setupUi();
}

ContactCopyPanel::~ContactCopyPanel() {
    m_workerThread.quit();
    m_workerThread.wait();
}

void ContactCopyPanel::setupUi() {
  addCheckboxParameter(
      "Contact copy simulation",
      [](const ParameterState &s) { return s.rparams.contact_copy.simulate; },
      [](ParameterState &s, bool v) { s.rparams.contact_copy.simulate = v; },
      nullptr, "Enable physics-based simulation of photographic contact printing. This models the response of a photographic glass plate to the light transmitted through the digitized color screen.");

  addSeparator("Film characteristics");
  m_hdCurveWidget = new HDCurveWidget();
  
  QComboBox *modeCombo = new QComboBox();
  m_modeCombo = modeCombo;
  modeCombo->addItem("Exposure + Density (H&D)", (int)colorscreen::hd_axis_hd);
  modeCombo->addItem("Gamma 2.2", (int)colorscreen::hd_axis_gamma22);
  modeCombo->addItem("Gamma 1.0 (Linear)", (int)colorscreen::hd_axis_gamma10);
  auto *dmLabel = new QLabel("Display mode");
  dmLabel->setToolTip("Select the units for the horizontal axis of the H&D curve (log exposure or gamma-corrected values).");
  modeCombo->setToolTip("Select the units for the horizontal axis of the H&D curve (log exposure or gamma-corrected values).");
  m_currentGroupForm->addRow(dmLabel, modeCombo);
  connect(modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, modeCombo](int){
      m_hdCurveWidget->setDisplayMode((colorscreen::hd_axis_type)modeCombo->currentData().toInt());
      updateUI();
  });

  QComboBox *presetCombo = new QComboBox();
  m_presetCombo = presetCombo;
  presetCombo->addItem("Custom", -1);
  for (int i = 0; i < colorscreen::film_sensitivity::hd_curves_max; ++i) {
      presetCombo->addItem(QString::fromUtf8(colorscreen::film_sensitivity::hd_curves_properties[i].pretty_name), i);
  }
  auto *pLabel = new QLabel("Presets");
  pLabel->setToolTip("Select from a variety of historical photographic emulsion characteristics (e.g., glass plate negatives or positives).");
  presetCombo->setToolTip("Select from a variety of historical photographic emulsion characteristics (e.g., glass plate negatives or positives).");
  m_currentGroupForm->addRow(pLabel, presetCombo);

  connect(presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, presetCombo](int index) {
      int presetIdx = presetCombo->itemData(index).toInt();
      if (presetIdx >= 0) {
          const auto &preset = colorscreen::film_sensitivity::hd_curves_properties[presetIdx];
          applyChange([preset](ParameterState &s) {
              s.rparams.contact_copy.emulsion_characteristic_curve = preset.params;
          }, "Apply characteristic curve preset");
      }
  });

  // Wrap it nicely to center or align it
  QWidget *chartWrapper = new QWidget();
  m_hdCurveContainer = new QVBoxLayout(chartWrapper);
  m_hdCurveContainer->setContentsMargins(0, 0, 0, 0);
  
  QWidget *detachableCurve = createDetachableSection("H&D Curve", m_hdCurveWidget, [this]() {
      emit detachHDCurveRequested(m_hdCurveWidget);
  });
  m_hdCurveContainer->addWidget(detachableCurve);
  
  m_gammaLabel = new QLabel();
  m_gammaLabel->setAlignment(Qt::AlignCenter);
  // Optional: make it look a bit more premium
  m_gammaLabel->setStyleSheet("color: #888; font-style: italic; margin-top: 4px;");
  m_hdCurveContainer->addWidget(m_gammaLabel);
  
  if (m_currentGroupForm)
      m_currentGroupForm->addRow(chartWrapper);
  else
      m_form->addRow(chartWrapper);

  // Connection from HDCurveWidget -> State
  connect(m_hdCurveWidget, &HDCurveWidget::parametersChanged, this, [this](const colorscreen::hd_curve_parameters &params) {
      if (m_updatingSpinBoxes) return;
      
      applyChange(
          [params](ParameterState &s) {
              s.rparams.contact_copy.emulsion_characteristic_curve = params;
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
                  s.rparams.contact_copy.emulsion_characteristic_curve = p;
              },
              "Modify emulsion curve"
          );
      });
  };

  createSpinBox(m_minXSpin); createSpinBox(m_minYSpin);
  createSpinBox(m_linear1XSpin); createSpinBox(m_linear1YSpin);
  createSpinBox(m_linear2XSpin); createSpinBox(m_linear2YSpin);
  createSpinBox(m_maxXSpin); createSpinBox(m_maxYSpin);
  addSeparator("H&D Richards model parameters");

  auto addRichardsSlider = [this](const QString &label, const QString &tooltip, 
                                 std::function<double(const colorscreen::richards_curve_parameters &)> getter,
                                 std::function<void(colorscreen::richards_curve_parameters &, double)> setter,
                                 double min = -10.0, double max = 10.0, bool logarithmic = false) {
      addSliderParameter(
          label, min, max, 100.0, 3, "", "",
          [getter](const ParameterState &s) {
              return getter(colorscreen::hd_to_richards_curve_parameters(s.rparams.contact_copy.emulsion_characteristic_curve));
          },
          [this, setter](ParameterState &s, double v) {
              auto rp_old = colorscreen::hd_to_richards_curve_parameters(s.rparams.contact_copy.emulsion_characteristic_curve);
              auto rp_new = rp_old;
              setter(rp_new, v);
              s.rparams.contact_copy.emulsion_characteristic_curve.adjust_richards(rp_old, rp_new);
          },
          1.0, [](const ParameterState &s) { return s.rparams.contact_copy.simulate; }, logarithmic);
      
      // Set tooltips on label and field
      QFormLayout *layout = m_currentGroupForm ? m_currentGroupForm : m_form;
      if (layout->count() >= 2) {
          QWidget *field = layout->itemAt(layout->count() - 1)->widget();
          QWidget *labelW = layout->itemAt(layout->count() - 2)->widget();
          if (field) { field->setToolTip(tooltip); m_richardsWidgets.push_back(field); }
          if (labelW) { labelW->setToolTip(tooltip); m_richardsWidgets.push_back(labelW); }
      }
  };

  addRichardsSlider("Minimal density (A)", "Lower asymptote (minimal density). Represents the base fog level.", 
                    [](const auto& r) { return r.A; }, [](auto& r, double v) { r.A = v; });
  addRichardsSlider("Maximal density (K)", "Upper asymptote (maximal density). Represents the saturation level.", 
                    [](const auto& r) { return r.K; }, [](auto& r, double v) { r.K = v; });
  addRichardsSlider("Slope (B)", "Growth rate (slope). Controls the steepness of the linear region.", 
                    [](const auto& r) { return r.B; }, [](auto& r, double v) { r.B = v; }, -100.0, 100.0, false);
  addRichardsSlider("Offset (M)", "Horizontal offset. Center point of the linear region.", 
                    [](const auto& r) { return r.M; }, [](auto& r, double v) { r.M = v; });
  addRichardsSlider("Asymmetry (\u03BD)", "Asymmetry parameter. Controls where the curve inflection occurs (\u03BD=1 is symmetric).", 
                    [](const auto& r) { return r.v; }, [](auto& r, double v) { r.v = v; }, 0.01, 10.0, true);

  addCheckboxParameter(
      "Inverse mode",
      [](const ParameterState &s) { 
          return colorscreen::hd_to_richards_curve_parameters(s.rparams.contact_copy.emulsion_characteristic_curve).is_inverse; 
      },
      [this](ParameterState &s, bool v) {
          auto &p = s.rparams.contact_copy.emulsion_characteristic_curve;
          std::swap(p.minx, p.miny);
          std::swap(p.linear1x, p.linear1y);
          std::swap(p.linear2x, p.linear2y);
          std::swap(p.maxx, p.maxy);
          p.sort_by_x();
      },
      [](const ParameterState &s) { return s.rparams.contact_copy.simulate; });
  
  QFormLayout *layout = m_currentGroupForm ? m_currentGroupForm : m_form;
  if (layout->count() >= 1) {
      QWidget *invField = layout->itemAt(layout->count() - 1)->widget();
      if (invField) {
          invField->setToolTip("Invert the characteristic curve. This is used to mathematically reverse a physical laboratory copy step (e.g., to reconstruct the linear image).");
          m_richardsWidgets.push_back(invField);
      }
  }

  addSeparator("H&D Coordinate points (manual entry)");

  auto addRow = [this](const QString &label, QWidget *w1, QWidget *w2) {
      QWidget *row = new QWidget();
      QHBoxLayout *l = new QHBoxLayout(row);
      l->setContentsMargins(0, 0, 0, 0);
      l->setSpacing(4);
      l->addStretch();
      l->addWidget(new QLabel("X:"));
      l->addWidget(w1);
      l->addSpacing(8);
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

  addSeparator("Simulated darkroom");

  addSliderParameter(
      "Preflash", 0.0, 100.0, 100.0, 2, "", "",
      [](const ParameterState &s) { return s.rparams.contact_copy.preflash; },
      [](ParameterState &s, double v) { s.rparams.contact_copy.preflash = v; },
      1.0, [](const ParameterState &s) { return s.rparams.contact_copy.simulate; });
  {
      QFormLayout *layout = m_currentGroupForm ? m_currentGroupForm : m_form;
      if (layout->count() >= 2) {
          layout->itemAt(layout->count() - 1)->widget()->setToolTip("Simulates a uniform low-intensity exposure applied to the entire plate before printing. This \"lifts\" the shadows and effectively reduces the overall contrast.");
          layout->itemAt(layout->count() - 2)->widget()->setToolTip("Simulates a uniform low-intensity exposure applied to the entire plate before printing. This \"lifts\" the shadows and effectively reduces the overall contrast.");
      }
  }

  addSliderParameter(
      "Enlarger exposure", 0.0, 100.0, 100.0, 2, "", "",
      [](const ParameterState &s) { return s.rparams.contact_copy.exposure; },
      [](ParameterState &s, double v) { s.rparams.contact_copy.exposure = v; },
      3.0, [](const ParameterState &s) { return s.rparams.contact_copy.simulate; }, true);
  {
      QFormLayout *layout = m_currentGroupForm ? m_currentGroupForm : m_form;
      if (layout->count() >= 2) {
          layout->itemAt(layout->count() - 1)->widget()->setToolTip("The duration and intensity of the simulated enlarger light hitting the copy plate. Adjust this to control the overall brightness (density) of the reconstructed image.");
          layout->itemAt(layout->count() - 2)->widget()->setToolTip("The duration and intensity of the simulated enlarger light hitting the copy plate. Adjust this to control the overall brightness (density) of the reconstructed image.");
      }
  }

  addSliderParameter(
      "Density boost", 0.0, 100.0, 100.0, 2, "", "",
      [](const ParameterState &s) { return s.rparams.contact_copy.boost; },
      [](ParameterState &s, double v) { s.rparams.contact_copy.boost = v; },
      3.0, [](const ParameterState &s) { return s.rparams.contact_copy.simulate; });
  {
      QFormLayout *layout = m_currentGroupForm ? m_currentGroupForm : m_form;
      if (layout->count() >= 2) {
          layout->itemAt(layout->count() - 1)->widget()->setToolTip("Adjusts the maximum reachable density of the simulated emulsion.");
          layout->itemAt(layout->count() - 2)->widget()->setToolTip("Adjusts the maximum reachable density of the simulated emulsion.");
      }
  }

  // Sync state to UI
  m_paramUpdaters.push_back([this](const ParameterState &s) {
      bool sim = s.rparams.contact_copy.simulate;
      m_hdCurveWidget->setEnabled(sim);
      if (m_presetCombo) m_presetCombo->setEnabled(sim);
      if (m_modeCombo) m_modeCombo->setEnabled(sim);
      m_minXSpin->setEnabled(sim);
      m_minYSpin->setEnabled(sim);
      m_linear1XSpin->setEnabled(sim);
      m_linear1YSpin->setEnabled(sim);
      m_linear2XSpin->setEnabled(sim);
      m_linear2YSpin->setEnabled(sim);
      m_maxXSpin->setEnabled(sim);
      m_maxYSpin->setEnabled(sim);

      if (!(s.rparams.contact_copy.emulsion_characteristic_curve == m_hdCurveWidget->getParameters())) {
          m_hdCurveWidget->blockSignals(true);
          m_hdCurveWidget->setParameters(s.rparams.contact_copy.emulsion_characteristic_curve);
          m_hdCurveWidget->blockSignals(false);
          updateSpinBoxes();
      }

      m_hdCurveWidget->setDensityBoost(s.rparams.contact_copy.boost);
      
      // Update Richards widgets enablement
      bool canRichards = s.rparams.contact_copy.emulsion_characteristic_curve.is_valid_for_richards_curve();
      for (QWidget *w : m_richardsWidgets) {
          if (w) w->setEnabled(sim && canRichards);
      }

      // Update preset combo
      if (m_presetCombo) {
          int foundIdx = 0; // "Custom"
          for (int i = 0; i < colorscreen::film_sensitivity::hd_curves_max; ++i) {
              if (s.rparams.contact_copy.emulsion_characteristic_curve == colorscreen::film_sensitivity::hd_curves_properties[i].params) {
                  foundIdx = i + 1;
                  break;
              }
          }
          m_presetCombo->blockSignals(true);
          m_presetCombo->setCurrentIndex(foundIdx);
          m_presetCombo->blockSignals(false);
      }

      double minY = m_hdCurveWidget->minY();
      double maxY = m_hdCurveWidget->maxY();
      
      const auto &p = s.rparams.contact_copy.emulsion_characteristic_curve;
      double dx = p.linear2x - p.linear1x;
      if (std::abs(dx) > 1e-6) {
          double gamma = (p.linear2y - p.linear1y) / dx;
          m_gammaLabel->setText(QString("Characteristic Curve Gamma: %1").arg(gamma, 0, 'f', 2));
      } else {
          m_gammaLabel->setText("Characteristic Curve Gamma: ---");
      }

      if (maxY > minY) {
          colorscreen::render_parameters mut_rparams = s.rparams;
          colorscreen::hd_axis_type axisType = m_hdCurveWidget->getDisplayMode();
              
          auto colors = colorscreen::hd_y_to_rgb(mut_rparams, 400, minY, maxY, s.scrToImg.type != colorscreen::Random ? colorscreen::patch_proportions(s.scrToImg.type, &mut_rparams) : (colorscreen::rgbdata){1.0/3, 1.0/3, 1.0/3}, axisType);
          m_hdCurveWidget->setHDColors(colors, minY, maxY);

          if (m_imageGetter() && m_hdCurveWidget->isVisible() && m_hdCurveWidget->isEnabled()) {
              HistogramRequestData data;
              data.params = mut_rparams;
              data.steps = 256;
              data.minX = m_hdCurveWidget->minX();
              data.maxX = m_hdCurveWidget->maxX();
              data.axisType = axisType;
              
              m_taskQueue.requestRender(QVariant::fromValue(data));
          }
      } else {
          m_taskQueue.cancelAll();
          m_hdCurveWidget->setHistogram({}, 0, 0);
      }
  });

  updateUI();
}

void ContactCopyPanel::updateSpinBoxes() {
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

void ContactCopyPanel::onTriggerHistogram(int reqId, std::shared_ptr<colorscreen::progress_info> progress, const QVariant &userData) {
    if (!userData.canConvert<HistogramRequestData>()) {
        m_taskQueue.reportFinished(reqId, false);
        return;
    }

    HistogramRequestData data = userData.value<HistogramRequestData>();
    m_worker->setScan(m_imageGetter());
    
    QMetaObject::invokeMethod(m_worker, "compute", Qt::QueuedConnection,
                             Q_ARG(int, reqId),
                             Q_ARG(colorscreen::render_parameters, data.params),
                             Q_ARG(int, data.steps),
                             Q_ARG(double, data.minX),
                             Q_ARG(double, data.maxX),
                             Q_ARG(colorscreen::hd_axis_type, data.axisType),
                             Q_ARG(std::shared_ptr<colorscreen::progress_info>, progress));
}

void ContactCopyPanel::onHistogramFinished(int reqId, std::vector<uint64_t> data, double minx, double maxx, bool success) {
    m_taskQueue.reportFinished(reqId, success);
    
    if (success && reqId > m_lastHistogramReqId) {
        m_lastHistogramReqId = reqId;
        m_hdCurveWidget->setHistogram(data, minx, maxx);
    }
}

void ContactCopyPanel::reattachHDCurve(QWidget *widget) {
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
