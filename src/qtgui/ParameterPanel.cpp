#include "ParameterPanel.h"
#include "../libcolorscreen/include/base.h"
#include "SmartSpinBox.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

ParameterPanel::ParameterPanel(StateGetter stateGetter, StateSetter stateSetter,
                               ImageGetter imageGetter, QWidget *parent,
                               bool useScrollArea)
    : QWidget(parent), m_stateGetter(stateGetter), m_stateSetter(stateSetter),
      m_imageGetter(imageGetter), m_currentGroupForm(nullptr) {
  m_layout = new QVBoxLayout(this);
  m_layout->setContentsMargins(0, 0, 0, 0);

  if (useScrollArea) {
    // Create scroll area for the form
    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setFrameShape(QFrame::NoFrame);

    QWidget *scrollWidget = new QWidget();
    m_form = new QFormLayout(scrollWidget);
    scrollArea->setWidget(scrollWidget);

    m_layout->addWidget(scrollArea);
  } else {
    // No scroll area - layout directly
    m_form = new QFormLayout();
    m_layout->addLayout(m_form);
    m_layout->setSizeConstraint(
        QLayout::SetMinAndMaxSize); // Ensure widget resizes with content
  }
}

ParameterPanel::~ParameterPanel() = default;

void ParameterPanel::updateUI() {
  ParameterState state = m_stateGetter();

  // Update Param Updaters
  for (auto &updater : m_paramUpdaters) {
    updater(state);
  }

  // Update Widget State (Availability)
  for (auto &widgetUpdater : m_widgetStateUpdaters) {
    widgetUpdater();
  }

  // Call virtual method for derived classes
  onParametersRefreshed(state);
}

void ParameterPanel::applyChange(
    std::function<void(ParameterState &)> modifier, const QString &description) {
  ParameterState state = m_stateGetter();
  modifier(state);
  m_stateSetter(state, description);
}

void ParameterPanel::addDoubleParameter(
    const QString &label, double min, double max,
    std::function<double(const ParameterState &)> getter,
    std::function<void(ParameterState &, double)> setter,
    const std::map<double, QString> &specialValues,
    const std::map<double, QString> &quickSelects,
    std::function<bool(double)> validator) {
  SmartSpinBox *spin = new SmartSpinBox();
  spin->setRange(min, max);
  spin->setSingleStep(0.1);
  spin->setSpecialValues(specialValues);

  // Container
  QWidget *container = new QWidget();
  QHBoxLayout *hLayout = new QHBoxLayout(container);
  hLayout->setContentsMargins(0, 0, 0, 0);
  hLayout->addWidget(spin, 1);

  QComboBox *combo = nullptr;
  if (!quickSelects.empty()) {
    combo = new QComboBox();
    for (auto const &[val, text] : quickSelects) {
      combo->addItem(text, val);
    }
    hLayout->addWidget(combo, 0);

    connect(combo, QOverload<int>::of(&QComboBox::activated), this,
            [combo, spin](int index) {
              double val = combo->itemData(index).toDouble();
              spin->setValue(val);
            });
  }

  if (m_currentGroupForm) {
    m_currentGroupForm->addRow(label, container);
  } else {
    m_form->addRow(label, container);
  }

  // Connect changes: UI -> State
  connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
          [this, setter, label](double val) {
            applyChange([setter, val](ParameterState &s) { setter(s, val); }, label);
          });

  // Updater: State -> UI
  m_paramUpdaters.push_back([spin, combo, getter](const ParameterState &state) {
    double val = getter(state);
    spin->blockSignals(true);
    spin->setValue(val);
    spin->blockSignals(false);

    if (combo) {
      combo->blockSignals(true);
      int idx = combo->findData(val);
      if (idx != -1)
        combo->setCurrentIndex(idx);
      else {
        bool found = false;
        // Fuzzy match
        for (int i = 0; i < combo->count(); ++i) {
          if (qAbs(combo->itemData(i).toDouble() - val) < 0.0001) {
            combo->setCurrentIndex(i);
            found = true;
            break;
          }
        }
        if (!found)
          combo->setCurrentIndex(-1);
      }
      combo->blockSignals(false);
    }
  });

  // State Updater: Availability
  if (validator || !specialValues.empty()) {
    m_widgetStateUpdaters.push_back(
        [this, spin, combo, validator, specialValues]() {
          std::shared_ptr<colorscreen::image_data> scan = m_imageGetter();

          for (auto const &[val, text] : specialValues) {
            bool enabled = true;
            if (val == 0.0 && text.contains("ICC")) {
              if (!scan || scan->to_linear[0].empty())
                enabled = false;
            }
            if (validator && !validator(val))
              enabled = false;

            spin->setSpecialValueEnabled(val, enabled);
          }
        });
  }
}

void ParameterPanel::addSliderParameter(
    const QString &label, double min, double max, double scale, int decimals,
    const QString &suffix, const QString &specialValueText,
    std::function<double(const ParameterState &)> getter,
    std::function<void(ParameterState &, double)> setter, double gamma,
    std::function<bool(const ParameterState &)> enabledCheck,
    bool logarithmic) {
  // Container: Slider + SpinBox
  QWidget *container = new QWidget();
  QHBoxLayout *hLayout = new QHBoxLayout(container);
  hLayout->setContentsMargins(0, 0, 0, 0);

  QSlider *slider = new QSlider(Qt::Horizontal);

  // For non-linear, use fixed high resolution range
  const int SLIDER_MAX = 65535;

  if (gamma != 1.0 || logarithmic) {
    slider->setRange(0, SLIDER_MAX);
  } else {
    int minInt = min * scale;
    int maxInt = max * scale;
    slider->setRange(minInt, maxInt);
  }

  QDoubleSpinBox *spin = new QDoubleSpinBox();
  spin->setRange(min, max);
  spin->setDecimals(decimals);
  spin->setSingleStep(1.0 / scale);
  if (!suffix.isEmpty())
    spin->setSuffix(QString(" %1").arg(suffix));
  if (!specialValueText.isEmpty())
    spin->setSpecialValueText(specialValueText);

  hLayout->addWidget(slider, 1); // Slider expands
  hLayout->addWidget(spin, 0);   // SpinBox fixed size

  if (m_currentGroupForm) {
    m_currentGroupForm->addRow(label, container);
  } else {
    m_form->addRow(label, container);
  }

  // Helper to map Slider -> Value
  auto sliderToValue = [min, max, scale, gamma, SLIDER_MAX,
                        logarithmic](int s) -> double {
    if (!logarithmic && gamma == 1.0)
      return (double)s / scale;

    double t = (double)s / SLIDER_MAX; // 0..1

    if (logarithmic) {
      if (min <= 0) {
        // v = (max + 1)^t - 1
        return std::pow(max + 1.0, t) - 1.0;
      } else {
        // v = min * (max/min)^t
        return min * std::pow(max / min, t);
      }
    }

    // v = min + (max-min) * t^gamma
    return min + (max - min) * std::pow(t, gamma);
  };

  // Helper to map Value -> Slider
  auto valueToSlider = [min, max, scale, gamma, SLIDER_MAX,
                        logarithmic](double v) -> int {
    if (!logarithmic && gamma == 1.0)
      return qRound(v * scale);

    double t = 0;
    if (logarithmic) {
      if (min <= 0) {
        // t = log(v + 1) / log(max + 1)
        if (v <= 0)
          t = 0;
        else
          t = std::log(v + 1.0) / std::log(max + 1.0);
      } else {
        // t = log(v/min) / log(max/min)
        if (v <= min)
          t = 0;
        else
          t = std::log(v / min) / std::log(max / min);
      }
    } else {
      // t = ((v - min) / (max - min)) ^ (1/gamma)
      double ratio = (v - min) / (max - min);
      if (ratio <= 0)
        t = 0;
      else if (ratio >= 1)
        t = 1;
      else
        t = std::pow(ratio, 1.0 / gamma);
    }
    return std::clamp((int)qRound(t * SLIDER_MAX), 0, SLIDER_MAX);
  };

  // Synchronization
  connect(slider, &QSlider::valueChanged, this,
          [this, spin, sliderToValue, setter, label](int val) {
            double dVal = sliderToValue(val);
            spin->blockSignals(true);
            spin->setValue(dVal);
            spin->blockSignals(false);

            // Trigger update
            applyChange([setter, dVal](ParameterState &s) { setter(s, dVal); }, label);
          });

  connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
          [slider, valueToSlider](double val) {
            slider->blockSignals(true);
            slider->setValue(valueToSlider(val));
            slider->blockSignals(false);
          });

  // Change
  connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
          [this, setter, label](double val) {
            applyChange([setter, val](ParameterState &s) { setter(s, val); }, label);
          });

  // Updater: State -> UI
  m_paramUpdaters.push_back(
      [slider, spin, getter, valueToSlider](const ParameterState &state) {
        double val = getter(state);
        spin->blockSignals(true);
        spin->setValue(val);
        spin->blockSignals(false);

        slider->blockSignals(true);
        slider->setValue(valueToSlider(val));
        slider->blockSignals(false);
      });

  // Enable Update
  if (enabledCheck) {
    m_widgetStateUpdaters.push_back(
        [this, slider, spin, container, enabledCheck]() {
          ParameterState state = m_stateGetter();
          bool en = enabledCheck(state);
          slider->setEnabled(en);
          spin->setEnabled(en);

          // Disable container? Using setEnabled on container layout items?
          // Actually, disabling the container widget is enough if they were
          // children. But container is just a QWidget holding them. Yes. Wait,
          // m_form->addRow(label, container). If I disable container, the label
          // is NOT disabled. I need to disable label.
          QWidget *labelWidget = m_form->labelForField(container);
          if (labelWidget)
            labelWidget->setEnabled(en);
        });
  }
}

QComboBox *ParameterPanel::addEnumParameter(
    const QString &label, const std::map<int, QString> &options,
    std::function<int(const ParameterState &)> getter,
    std::function<void(ParameterState &, int)> setter,
    std::function<bool(const ParameterState &)> enabledCheck) {
  QComboBox *combo = new QComboBox();
  combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  combo->setMinimumContentsLength(10);
  combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  for (auto const &[val, text] : options) {
    combo->addItem(text, val);
  }

  if (m_currentGroupForm) {
    m_currentGroupForm->addRow(label, combo);
  } else {
    m_form->addRow(label, combo);
  }

  // Connect changes: UI -> State
  connect(combo, QOverload<int>::of(&QComboBox::activated), this,
          [this, combo, setter, label](int index) {
            int val = combo->itemData(index).toInt();
            applyChange([setter, val](ParameterState &s) { setter(s, val); }, label);
          });

  // Updater: State -> UI
  m_paramUpdaters.push_back([combo, getter](const ParameterState &state) {
    int val = getter(state);
    combo->blockSignals(true);
    int idx = combo->findData(val);
    if (idx != -1)
      combo->setCurrentIndex(idx);
    combo->blockSignals(false);
  });

  // Enable Update
  if (enabledCheck) {
    m_widgetStateUpdaters.push_back([this, combo, enabledCheck]() {
      ParameterState state = m_stateGetter();
      bool en = enabledCheck(state);
      combo->setEnabled(en);
      QWidget *labelWidget = m_form->labelForField(combo);
      if (labelWidget)
        labelWidget->setEnabled(en);
    });
  }
  return combo;
}

QComboBox *ParameterPanel::addEnumParameter(
    const QString &label, const colorscreen::property_t *names, int max,
    std::function<int(const ParameterState &)> getter,
    std::function<void(ParameterState &, int)> setter,
    std::function<bool(const ParameterState &)> enabledCheck) {
  std::map<int, QString> options;
  for (int i = 0; i < max; ++i) {
    if (names[i].pretty_name && names[i].pretty_name[0]) {
      options[i] = QString::fromUtf8(names[i].pretty_name);
    } else if (names[i].name && names[i].name[0]) {
      options[i] = QString::fromUtf8(names[i].name);
    }
  }

  QComboBox *combo = addEnumParameter(label, options, getter, setter, enabledCheck);
  addEnumTooltips(combo, names, max);
  return combo;
}

void ParameterPanel::addEnumTooltips(QComboBox *combo, const colorscreen::property_t *names, int max) {
  for (int i = 0; i < combo->count(); ++i) {
    int val = combo->itemData(i).toInt();
    if (val >= 0 && val < max) {
      const char *help = names[val].help;
      if (help && help[0]) {
        combo->setItemData(i, QString::fromUtf8(help), Qt::ToolTipRole);
      }
    }
  }
}

void ParameterPanel::addCheckboxParameter(
    const QString &label, std::function<bool(const ParameterState &)> getter,
    std::function<void(ParameterState &, bool)> setter,
    std::function<bool(const ParameterState &)> enabledCheck) {
  // Create container with label on left, checkbox on right
  QWidget *container = new QWidget();
  QHBoxLayout *hLayout = new QHBoxLayout(container);
  hLayout->setContentsMargins(0, 0, 0, 0);

  QCheckBox *checkbox = new QCheckBox();
  QLabel *textLabel = new QLabel(label);

  hLayout->addWidget(checkbox, 0);  // Checkbox fixed size on left
  hLayout->addWidget(textLabel, 1); // Label expands to fill space

  // Add to form (single column - container spans both label and field)
  if (m_currentGroupForm) {
    m_currentGroupForm->addRow(container);
  } else {
    m_form->addRow(container);
  }

  // Connect changes: UI -> State
  connect(checkbox, &QCheckBox::toggled, this, [this, setter, label](bool checked) {
    applyChange([setter, checked](ParameterState &s) { setter(s, checked); }, label);
  });

  // Updater: State -> UI
  m_paramUpdaters.push_back([checkbox, getter](const ParameterState &state) {
    bool val = getter(state);
    checkbox->blockSignals(true);
    checkbox->setChecked(val);
    checkbox->blockSignals(false);
  });

  // Enable/Visibility Update
  if (enabledCheck) {
    m_widgetStateUpdaters.push_back([this, container, enabledCheck]() {
      ParameterState state = m_stateGetter();
      bool visible = enabledCheck(state);
      container->setVisible(visible);
    });
  }
}

void ParameterPanel::addCorrelatedRGBParameter(
    const QString &label, double min, double max, double scale, int decimals,
    const QString &suffix,
    std::function<colorscreen::rgbdata(const ParameterState &)> getter,
    std::function<void(ParameterState &, const colorscreen::rgbdata &)> setter,
    std::function<bool(const ParameterState &)> enabledCheck) {

  // 1. Link Checkbox
  QCheckBox *linkCheck = new QCheckBox("Link channels");
  linkCheck->setChecked(true);

  // 2. Three channels
  struct Channel {
    QSlider *slider;
    QDoubleSpinBox *spin;
  };
  std::vector<Channel> channels;
  QStringList names = {"Red", "Green", "Blue"};

  for (int i = 0; i < 3; ++i) {
    QWidget *container = new QWidget();
    QHBoxLayout *hLayout = new QHBoxLayout(container);
    hLayout->setContentsMargins(0, 0, 0, 0);

    QSlider *slider = new QSlider(Qt::Horizontal);
    slider->setRange(min * scale, max * scale);

    QDoubleSpinBox *spin = new QDoubleSpinBox();
    spin->setRange(min, max);
    spin->setDecimals(decimals);
    spin->setSingleStep(1.0 / scale);
    if (!suffix.isEmpty())
      spin->setSuffix(QString(" %1").arg(suffix));

    hLayout->addWidget(slider, 1);
    hLayout->addWidget(spin, 0);

    if (m_currentGroupForm)
      m_currentGroupForm->addRow(QString("%1 %2").arg(names[i]).arg(label),
                                 container);
    else
      m_form->addRow(QString("%1 %2").arg(names[i]).arg(label), container);

    channels.push_back({slider, spin});

    // Internal Sync for each channel
    connect(slider, &QSlider::valueChanged, this, [spin, scale](int val) {
      spin->blockSignals(true);
      spin->setValue((double)val / scale);
      spin->blockSignals(false);
    });
    connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [slider, scale](double val) {
              slider->blockSignals(true);
              slider->setValue(qRound(val * scale));
              slider->blockSignals(false);
            });
  }

  // Link Checkbox row
  if (m_currentGroupForm)
    m_currentGroupForm->addRow("", linkCheck);
  else
    m_form->addRow("", linkCheck);

  // Interaction Logic
  auto handleValueChange = [this, channels, linkCheck, getter, setter,
                            scale, label](int changedIdx, double newVal) {
    ParameterState s = m_stateGetter();
    colorscreen::rgbdata current = getter(s);
    double oldVal = current[changedIdx];
    double delta = newVal - oldVal;

    colorscreen::rgbdata next = current;
    next[changedIdx] = newVal;

    if (linkCheck->isChecked()) {
      for (int i = 0; i < 3; ++i) {
        if (i != changedIdx) {
          next[i] += delta;
        }
      }
    }

    applyChange([setter, next](ParameterState &state) { setter(state, next); }, label);

    // Optimistic UI update for linked sliders
    if (linkCheck->isChecked()) {
      for (int i = 0; i < 3; ++i) {
        if (i != changedIdx) {
          channels[i].spin->blockSignals(true);
          channels[i].spin->setValue(next[i]);
          channels[i].spin->blockSignals(false);

          channels[i].slider->blockSignals(true);
          channels[i].slider->setValue(qRound(next[i] * scale));
          channels[i].slider->blockSignals(false);
        }
      }
    }
  };

  // Connect user interaction
  for (int i = 0; i < 3; ++i) {
    int idx = i;
    connect(channels[i].spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [handleValueChange, idx](double v) {
              handleValueChange(idx, v);
            });
    connect(channels[i].slider, &QSlider::valueChanged, this,
            [handleValueChange, idx, scale](int v) {
              handleValueChange(idx, (double)v / scale);
            });
  }

  // Updater State -> UI
  m_paramUpdaters.push_back([channels, getter, scale](const ParameterState &s) {
    colorscreen::rgbdata v = getter(s);
    for (int i = 0; i < 3; ++i) {
      channels[i].spin->blockSignals(true);
      channels[i].spin->setValue(v[i]);
      channels[i].spin->blockSignals(false);

      channels[i].slider->blockSignals(true);
      channels[i].slider->setValue(qRound(v[i] * scale));
      channels[i].slider->blockSignals(false);
    }
  });

  // Enablement
  if (enabledCheck) {
    m_widgetStateUpdaters.push_back(
        [this, channels, linkCheck, enabledCheck, label]() {
          ParameterState s = m_stateGetter();
          bool en = enabledCheck(s);
          linkCheck->setEnabled(en);
          for (auto &c : channels) {
            c.slider->setEnabled(en);
            c.spin->setEnabled(en);
            QWidget *labelWidget = m_form->labelForField(c.slider->parentWidget());
            if (labelWidget)
              labelWidget->setEnabled(en);
          }
        });
  }
}

QToolButton *ParameterPanel::addSeparator(const QString &title) {
  QGroupBox *group = new QGroupBox();
  group->setFlat(true);
  group->setStyleSheet(
      "QGroupBox { border: none; margin: 0px; padding: 0px; }");

  // Create a custom title widget with arrow button
  QWidget *titleWidget = new QWidget();

  // Use palette for theme-aware coloring
  QPalette pal = titleWidget->palette();
  pal.setColor(QPalette::Window, pal.color(QPalette::Mid));
  titleWidget->setAutoFillBackground(true);
  titleWidget->setPalette(pal);

  QHBoxLayout *titleLayout = new QHBoxLayout(titleWidget);
  titleLayout->setContentsMargins(4, 4, 4, 4);
  titleLayout->setSpacing(4);

  QToolButton *arrowBtn = new QToolButton();
  arrowBtn->setArrowType(Qt::DownArrow);
  arrowBtn->setStyleSheet(
      "QToolButton { border: none; background: transparent; }");
  arrowBtn->setCheckable(true);
  arrowBtn->setChecked(true);

  QLabel *titleLabel = new QLabel(title);
  QFont font = titleLabel->font();
  font.setBold(true);
  titleLabel->setFont(font);

  titleLayout->addWidget(arrowBtn);
  titleLayout->addWidget(titleLabel);
  titleLayout->addStretch();

  QVBoxLayout *groupLayout = new QVBoxLayout();
  groupLayout->setContentsMargins(0, 0, 0, 0);
  groupLayout->setSpacing(0);
  group->setLayout(groupLayout);

  groupLayout->addWidget(titleWidget);

  QFormLayout *groupForm = new QFormLayout();
  groupLayout->addLayout(groupForm);

  // Connect arrow button to toggle visibility
  connect(arrowBtn, &QToolButton::toggled, [arrowBtn, groupForm](bool checked) {
    arrowBtn->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);

    auto setVisibleRecursive = [](QLayoutItem *item, bool visible) {
        auto recurse = [](auto self, QLayoutItem *itm, bool vis) -> void {
            if (!itm) return;
            if (itm->widget()) {
                itm->widget()->setVisible(vis);
            } else if (itm->layout()) {
                QLayout *subLayout = itm->layout();
                for (int j = 0; j < subLayout->count(); ++j) {
                    self(self, subLayout->itemAt(j), vis);
                }
            }
        };
        recurse(recurse, item, visible);
    };

    // Hide all widgets in the form layout
    for (int i = 0; i < groupForm->rowCount(); ++i) {
      QLayoutItem *labelItem = groupForm->itemAt(i, QFormLayout::LabelRole);
      QLayoutItem *fieldItem = groupForm->itemAt(i, QFormLayout::FieldRole);
      // Check SpanningRole as well just in case, though usually FieldRole covers it in 2-arg addRow
      QLayoutItem *spanningItem = groupForm->itemAt(i, QFormLayout::SpanningRole);

      setVisibleRecursive(labelItem, checked);
      setVisibleRecursive(fieldItem, checked);
      if (spanningItem && spanningItem != fieldItem && spanningItem != labelItem) {
          setVisibleRecursive(spanningItem, checked);
      }
    }
  });

  m_form->addRow(group);
  m_currentGroupForm = groupForm;

  return arrowBtn; // Return the toggle button
}

QWidget *
ParameterPanel::createDetachableSection(const QString &title, QWidget *content,
                                        std::function<void()> onDetach) {
  QWidget *container = new QWidget();
  QVBoxLayout *layout = new QVBoxLayout(container);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  // Header
  QWidget *header = new QWidget();
  QHBoxLayout *headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(0, 0, 0, 0);

  QLabel *label = new QLabel(title);
  QFont f = label->font();
  f.setBold(true);
  label->setFont(f);
  headerLayout->addWidget(label);

  headerLayout->addStretch(1);

  QPushButton *detachBtn =
      new QPushButton(QIcon::fromTheme("view-restore"), "Detach");
  detachBtn->setFlat(true);
  detachBtn->setCursor(Qt::PointingHandCursor);
  detachBtn->setMaximumHeight(24);

  headerLayout->addWidget(detachBtn);

  layout->addWidget(header);
  layout->addWidget(content);

  connect(detachBtn, &QPushButton::clicked, this,
          [onDetach, container, title, header]() {
            if (onDetach)
              onDetach();

            // Remove content from layout (it is reparented by Dock anyway)
            // Add placeholder
            if (container->layout()->count() > 1) { // Header + Content
              container->layout()->takeAt(1);       // Remove content item
            }

            QWidget *placeholder = new QWidget();
            placeholder->setVisible(false);
            container->layout()->addWidget(placeholder);

            header->hide();
          });

  return container;
}
