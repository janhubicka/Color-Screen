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
    std::function<void(ParameterState &)> modifier) {
  ParameterState state = m_stateGetter();
  modifier(state);
  m_stateSetter(state);
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
          [this, setter](double val) {
            applyChange([setter, val](ParameterState &s) { setter(s, val); });
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
    std::function<bool(const ParameterState &)> enabledCheck) {
  // Container: Slider + SpinBox
  QWidget *container = new QWidget();
  QHBoxLayout *hLayout = new QHBoxLayout(container);
  hLayout->setContentsMargins(0, 0, 0, 0);

  QSlider *slider = new QSlider(Qt::Horizontal);

  // For non-linear, use fixed high resolution range
  const int SLIDER_MAX = 10000;

  if (gamma != 1.0) {
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
  auto sliderToValue = [min, max, scale, gamma, SLIDER_MAX](int s) -> double {
    if (gamma == 1.0)
      return (double)s / scale;
    double t = (double)s / SLIDER_MAX; // 0..1
    // v = min + (max-min) * t^gamma
    return min + (max - min) * std::pow(t, gamma);
  };

  // Helper to map Value -> Slider
  auto valueToSlider = [min, max, scale, gamma, SLIDER_MAX](double v) -> int {
    if (gamma == 1.0)
      return qRound(v * scale);
    // t = ((v - min) / (max - min)) ^ (1/gamma)
    double ratio = (v - min) / (max - min);
    if (ratio < 0)
      ratio = 0;
    if (ratio > 1)
      ratio = 1;
    return qRound(std::pow(ratio, 1.0 / gamma) * SLIDER_MAX);
  };

  // Synchronization
  connect(slider, &QSlider::valueChanged, this,
          [this, spin, sliderToValue, setter](int val) {
            double dVal = sliderToValue(val);
            spin->blockSignals(true);
            spin->setValue(dVal);
            spin->blockSignals(false);

            // Trigger update
            applyChange([setter, dVal](ParameterState &s) { setter(s, dVal); });
          });

  connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
          [slider, valueToSlider](double val) {
            slider->blockSignals(true);
            slider->setValue(valueToSlider(val));
            slider->blockSignals(false);
          });

  // Change
  connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
          [this, setter](double val) {
            applyChange([setter, val](ParameterState &s) { setter(s, val); });
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

void ParameterPanel::addEnumParameter(
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
          [this, combo, setter](int index) {
            int val = combo->itemData(index).toInt();
            applyChange([setter, val](ParameterState &s) { setter(s, val); });
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
  connect(checkbox, &QCheckBox::toggled, this, [this, setter](bool checked) {
    applyChange([setter, checked](ParameterState &s) { setter(s, checked); });
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

    // Hide all widgets in the form layout
    for (int i = 0; i < groupForm->rowCount(); ++i) {
      QLayoutItem *labelItem = groupForm->itemAt(i, QFormLayout::LabelRole);
      QLayoutItem *fieldItem = groupForm->itemAt(i, QFormLayout::FieldRole);

      if (labelItem && labelItem->widget()) {
        labelItem->widget()->setVisible(checked);
      }
      if (fieldItem && fieldItem->widget()) {
        fieldItem->widget()->setVisible(checked);
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
