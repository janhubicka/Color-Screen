#include "ParameterPanel.h"
#include "../libcolorscreen/include/base.h"
#include "SmartSpinBox.h"
#include <QCheckBox>
#include <QDockWidget>
#include <QEvent>
#include <QFont>
#include <QLayout>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QPointer>
#include <QRegularExpression>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QShowEvent>
#include <QToolButton>
#include <QTimer>
#include <utility>
#include <QVBoxLayout>
#include <cmath>
#include <limits>

namespace {

constexpr auto parameterApplicableProperty = "parameterApplicable";
constexpr auto parameterDefaultValueProperty = "parameterDefaultValue";
constexpr auto parameterKeyProperty = "parameterKey";
constexpr auto parameterModifiedProperty = "parameterModified";
constexpr auto parameterSectionExpandedProperty = "parameterSectionExpanded";

/** Attach stable machine-readable PARAMETERKEY metadata to WIDGET. */
void setParameterKey(QWidget *widget, const QString &parameterKey) {
  if (widget && !parameterKey.isEmpty())
    widget->setProperty(parameterKeyProperty, parameterKey);
}

/** Return whether WIDGET is logically applicable to the current panel state. */
bool parameterWidgetApplicable(const QWidget *widget) {
  if (!widget)
    return true;
  const QVariant value = widget->property(parameterApplicableProperty);
  return !value.isValid() || value.toBool();
}

/** One uniform detachable section used by every parameter panel.

    The section, rather than MainWindow or a specialized view, owns the floating
    dock.  It therefore follows the panel into whichever QMainWindow currently
    presents the inspector and can reattach its content without layout probing
    or panel-specific callbacks. */
class DetachableSection final : public QWidget {
public:
  DetachableSection(const QString &title, QWidget *content,
                    std::function<void()> beforeDetach,
                    QWidget *parent = nullptr)
      : QWidget(parent), m_title(title), m_content(content),
        m_beforeDetach(std::move(beforeDetach)) {
    setObjectName(QStringLiteral("DetachableSection"));
    setProperty("detachableTitle", title);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    auto *header = new QWidget(this);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);

    auto *label = new QLabel(title, header);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    headerLayout->addWidget(label);
    headerLayout->addStretch(1);

    m_button = new QPushButton(QIcon::fromTheme("view-restore"), tr("Detach"),
                               header);
    m_button->setObjectName(QStringLiteral("DetachableSectionButton"));
    m_button->setProperty("detachableTitle", title);
    m_button->setFlat(true);
    m_button->setCursor(Qt::PointingHandCursor);
    m_button->setMaximumHeight(24);
    headerLayout->addWidget(m_button);

    m_layout->addWidget(header);
    if (m_content) {
      m_content->setProperty("detachableContentTitle", title);
      m_layout->addWidget(m_content);
    }

    connect(m_button, &QPushButton::clicked, this, [this]() {
      if (m_dock)
        reattach();
      else
        detach();
    });
  }

  ~DetachableSection() override { reattach(false); }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (watched == m_dock.data() && event && event->type() == QEvent::Close) {
      event->ignore();
      reattach();
      return true;
    }
    return QWidget::eventFilter(watched, event);
  }

  void showEvent(QShowEvent *event) override {
    QWidget::showEvent(event);
    // Inspectors move between the workspace, ordinary detached views, and
    // specialized reference views. Keep an already detached dock with the
    // top-level window that currently presents this section.
    QTimer::singleShot(0, this, [this]() { migrateDockToCurrentHost(); });
  }

private:
  struct WidgetPresentation {
    QPointer<QWidget> widget;
    QSize minimumSize;
    QSize maximumSize;
    QSizePolicy sizePolicy;
    Qt::Alignment alignment;
  };

  QMainWindow *currentHost() const {
    return qobject_cast<QMainWindow *>(window());
  }

  void snapshotPresentation() {
    m_presentation.clear();
    if (!m_content)
      return;

    QList<QWidget *> widgets = m_content->findChildren<QWidget *>();
    widgets.prepend(m_content);
    for (QWidget *widget : widgets) {
      WidgetPresentation state;
      state.widget = widget;
      state.minimumSize = widget->minimumSize();
      state.maximumSize = widget->maximumSize();
      state.sizePolicy = widget->sizePolicy();
      if (QWidget *parent = widget->parentWidget()) {
        if (QLayout *layout = parent->layout()) {
          const int index = layout->indexOf(widget);
          if (index >= 0 && layout->itemAt(index))
            state.alignment = layout->itemAt(index)->alignment();
        }
      }
      m_presentation.push_back(state);
    }
  }

  void restorePresentation() {
    for (const WidgetPresentation &state : std::as_const(m_presentation)) {
      QWidget *widget = state.widget.data();
      if (!widget)
        continue;
      widget->setMinimumSize(state.minimumSize);
      widget->setMaximumSize(state.maximumSize);
      widget->setSizePolicy(state.sizePolicy);
      if (QWidget *parent = widget->parentWidget()) {
        if (QLayout *layout = parent->layout())
          layout->setAlignment(widget, state.alignment);
      }
    }
    m_presentation.clear();
  }

  void detach() {
    if (!m_content || m_dock)
      return;

    QMainWindow *host = currentHost();
    if (!host)
      return;

    snapshotPresentation();
    if (m_beforeDetach)
      m_beforeDetach();

    static quint64 serial = 0;
    QString key = m_title;
    key.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9]+")));
    if (key.isEmpty())
      key = QStringLiteral("Panel");

    auto *dock = new QDockWidget(m_title, host);
    dock->setObjectName(QStringLiteral("DetachedPanelDock_%1_%2")
                            .arg(key)
                            .arg(++serial));
    dock->setProperty("detachablePanel", true);
    dock->setProperty("detachableTitle", m_title);
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);
    dock->setFeatures(QDockWidget::DockWidgetClosable |
                      QDockWidget::DockWidgetMovable |
                      QDockWidget::DockWidgetFloatable);
    dock->installEventFilter(this);
    connect(dock, &QObject::destroyed, this, [this]() {
      m_dock.clear();
      if (m_content && m_content->parentWidget() != this) {
        m_content->setParent(this);
        m_layout->addWidget(m_content);
        m_content->show();
        restorePresentation();
      }
      updateButton(false);
    });

    m_dock = dock;
    host->addDockWidget(Qt::RightDockWidgetArea, dock);
    dock->setWidget(m_content);
    dock->setFloating(true);
    if (m_content->sizeHint().isValid())
      dock->resize(m_content->sizeHint().expandedTo(QSize(320, 220)));
    dock->show();
    dock->raise();
    updateButton(true);
  }

  void reattach(bool restoreSizing = true) {
    QDockWidget *dock = m_dock.data();
    if (!dock) {
      if (m_content && m_content->parentWidget() != this) {
        m_content->setParent(this);
        m_layout->addWidget(m_content);
        m_content->show();
      }
      if (restoreSizing)
        restorePresentation();
      updateButton(false);
      return;
    }

    dock->removeEventFilter(this);
    if (QMainWindow *host = qobject_cast<QMainWindow *>(dock->parentWidget()))
      host->removeDockWidget(dock);

    if (m_content) {
      m_content->setParent(this);
      m_layout->addWidget(m_content);
      m_content->show();
    }
    dock->setWidget(nullptr);
    m_dock.clear();
    dock->hide();
    dock->deleteLater();

    if (restoreSizing)
      restorePresentation();
    updateButton(false);
  }

  void migrateDockToCurrentHost() {
    QDockWidget *dock = m_dock.data();
    QMainWindow *host = currentHost();
    if (!dock || !host || dock->parentWidget() == host)
      return;

    if (QMainWindow *oldHost =
            qobject_cast<QMainWindow *>(dock->parentWidget()))
      oldHost->removeDockWidget(dock);
    dock->setParent(host);
    host->addDockWidget(Qt::RightDockWidgetArea, dock);
    dock->setFloating(true);
    dock->show();
    dock->raise();
  }

  void updateButton(bool detached) {
    if (!m_button)
      return;
    m_button->setText(detached ? tr("Reattach") : tr("Detach"));
    m_button->setToolTip(detached ? tr("Return this panel to the inspector")
                                  : tr("Show this panel in a floating dock"));
  }

  QString m_title;
  QPointer<QWidget> m_content;
  std::function<void()> m_beforeDetach;
  QVBoxLayout *m_layout = nullptr;
  QPushButton *m_button = nullptr;
  QPointer<QDockWidget> m_dock;
  std::vector<WidgetPresentation> m_presentation;
};

} // namespace

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
    std::function<void(ParameterState &)> modifier, const QString &description,
    const QString &parameterKey) {
  ParameterState state = m_stateGetter();
  modifier(state);
  m_stateSetter(state, description, parameterKey);
}

/** Add quiet modified/default/reset UI for one keyed numeric parameter. */
void ParameterPanel::addNumericDefaultPresentation(
    QWidget *field, QHBoxLayout *layout, const QString &label,
    const QString &parameterKey, double defaultValue,
    std::function<double(const ParameterState &)> getter,
    std::function<void(ParameterState &, double)> setter,
    double tolerance) {
  Q_ASSERT(field);
  Q_ASSERT(layout);
  Q_ASSERT(!parameterKey.isEmpty());

  field->setProperty(parameterDefaultValueProperty, defaultValue);
  field->setProperty(parameterModifiedProperty, false);

  auto *resetButton = new QToolButton(field);
  resetButton->setObjectName(QStringLiteral("ParameterResetButton"));
  resetButton->setText(tr("Reset"));
  resetButton->setAutoRaise(true);
  resetButton->setToolTip(tr("Reset %1 to its default value").arg(label));
  resetButton->setProperty(parameterKeyProperty, parameterKey);
  resetButton->setProperty(parameterDefaultValueProperty, defaultValue);
  resetButton->setProperty(parameterModifiedProperty, false);
  resetButton->hide();
  layout->addWidget(resetButton, 0);

  QWidget *labelWidget = nullptr;
  if (m_currentGroupForm)
    labelWidget = m_currentGroupForm->labelForField(field);
  if (!labelWidget && m_form)
    labelWidget = m_form->labelForField(field);
  if (!labelWidget) {
    for (QFormLayout *form : m_groupForms) {
      labelWidget = form ? form->labelForField(field) : nullptr;
      if (labelWidget)
        break;
    }
  }

  const QFont normalFont = labelWidget ? labelWidget->font() : QFont();
  QFont modifiedFont = normalFont;
  if (modifiedFont.weight() < QFont::DemiBold)
    modifiedFont.setWeight(QFont::DemiBold);
  if (labelWidget) {
    labelWidget->setProperty(parameterDefaultValueProperty, defaultValue);
    labelWidget->setProperty(parameterModifiedProperty, false);
  }

  connect(resetButton, &QToolButton::clicked, this,
          [this, setter, defaultValue, label]() {
            applyChange(
                [setter, defaultValue](ParameterState &state) {
                  setter(state, defaultValue);
                },
                tr("Reset %1").arg(label), QString());
          });

  m_paramUpdaters.push_back(
      [field, labelWidget, resetButton, getter, defaultValue, tolerance,
       normalFont, modifiedFont](const ParameterState &state) {
        const double value = getter(state);
        bool modified = value != defaultValue;
        if (std::isfinite(value) && std::isfinite(defaultValue))
          modified = std::abs(value - defaultValue) > tolerance;

        field->setProperty(parameterModifiedProperty, modified);
        resetButton->setProperty(parameterModifiedProperty, modified);
        resetButton->setVisible(modified);
        if (labelWidget) {
          labelWidget->setProperty(parameterModifiedProperty, modified);
          labelWidget->setFont(modified ? modifiedFont : normalFont);
        }
      });
}

void ParameterPanel::addDoubleParameter(
    const QString &label, double min, double max,
    std::function<double(const ParameterState &)> getter,
    std::function<void(ParameterState &, double)> setter,
    const std::map<double, QString> &specialValues,
    const std::map<double, QString> &quickSelects,
    std::function<bool(double)> validator, const QString &tooltip,
    const QString &parameterKey, bool showDefaultReset) {
  SmartSpinBox *spin = new SmartSpinBox();
  spin->setRange(min, max);
  spin->setSingleStep(0.1);
  spin->setSpecialValues(specialValues);

  if (!tooltip.isEmpty())
    spin->setToolTip(tooltip);

  // Container
  QWidget *container = new QWidget();
  QHBoxLayout *hLayout = new QHBoxLayout(container);
  hLayout->setContentsMargins(0, 0, 0, 0);
  hLayout->addWidget(spin, 1);
  setParameterKey(container, parameterKey);
  setParameterKey(spin, parameterKey);

  QComboBox *combo = nullptr;
  if (!quickSelects.empty()) {
    combo = new QComboBox();
    if (!tooltip.isEmpty())
      combo->setToolTip(tooltip);
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

  if (showDefaultReset) {
    Q_ASSERT(!parameterKey.isEmpty());
    const double defaultValue = getter(ParameterState());
    addNumericDefaultPresentation(container, hLayout, label, parameterKey,
                                  defaultValue, getter, setter, 1e-12);
  }

  // Connect changes: UI -> State
  connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
          [this, setter, label, parameterKey](double val) {
            applyChange([setter, val](ParameterState &s) { setter(s, val); },
                        label, parameterKey);
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

QWidget *ParameterPanel::addSliderParameter(
    const QString &label, double min, double max, double scale, int decimals,
    const QString &suffix, const QString &specialValueText,
    std::function<double(const ParameterState &)> getter,
    std::function<void(ParameterState &, double)> setter, double gamma,
    std::function<bool(const ParameterState &)> enabledCheck,
    bool logarithmic, const QString &tooltip,
    const QString &parameterKey, bool showDefaultReset) {
  // Container: Slider + SpinBox
  QWidget *container = new QWidget();
  QHBoxLayout *hLayout = new QHBoxLayout(container);
  hLayout->setContentsMargins(0, 0, 0, 0);

  QSlider *slider = new QSlider(Qt::Horizontal);
  if (!tooltip.isEmpty())
    slider->setToolTip(tooltip);

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

  setParameterKey(container, parameterKey);
  setParameterKey(slider, parameterKey);
  setParameterKey(spin, parameterKey);

  hLayout->addWidget(slider, 1); // Slider expands
  hLayout->addWidget(spin, 0);   // SpinBox fixed size

  if (m_currentGroupForm) {
    m_currentGroupForm->addRow(label, container);
  } else {
    m_form->addRow(label, container);
  }

  if (showDefaultReset) {
    Q_ASSERT(!parameterKey.isEmpty());
    const double defaultValue = getter(ParameterState());
    const double tolerance =
        scale > 0 ? 0.5 / scale : std::numeric_limits<double>::epsilon();
    addNumericDefaultPresentation(container, hLayout, label, parameterKey,
                                  defaultValue, getter, setter, tolerance);
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
          [this, spin, sliderToValue, setter, label, parameterKey](int val) {
            double dVal = sliderToValue(val);
            spin->blockSignals(true);
            spin->setValue(dVal);
            spin->blockSignals(false);

            // Trigger update
            applyChange([setter, dVal](ParameterState &s) { setter(s, dVal); },
                        label, parameterKey);
          });

  connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
          [slider, valueToSlider](double val) {
            slider->blockSignals(true);
            slider->setValue(valueToSlider(val));
            slider->blockSignals(false);
          });

  // Change
  connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
          [this, setter, label, parameterKey](double val) {
            applyChange([setter, val](ParameterState &s) { setter(s, val); },
                        label, parameterKey);
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
  return container;
}

QWidget* ParameterPanel::addSlider(
    const QString &label, double min, double max, double scale, int decimals,
    const QString &suffix, const QString &specialValueText,
    double initialValue,
    std::function<void(double)> onChanged, double gamma,
    bool logarithmic, const QString &tooltip) {
  // Container: Slider + SpinBox
  QWidget *container = new QWidget();
  QHBoxLayout *hLayout = new QHBoxLayout(container);
  hLayout->setContentsMargins(0, 0, 0, 0);

  QSlider *slider = new QSlider(Qt::Horizontal);
  if (!tooltip.isEmpty())
    slider->setToolTip(tooltip);

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
          [spin, sliderToValue, onChanged](int val) {
            double dVal = sliderToValue(val);
            spin->blockSignals(true);
            spin->setValue(dVal);
            spin->blockSignals(false);

            if (onChanged)
              onChanged(dVal);
          });

  connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
          [slider, valueToSlider, onChanged](double val) {
            slider->blockSignals(true);
            slider->setValue(valueToSlider(val));
            slider->blockSignals(false);

            if (onChanged)
              onChanged(val);
          });

  // Initial Value
  spin->setValue(initialValue);
  slider->setValue(valueToSlider(initialValue));

  return container;
}

QComboBox *ParameterPanel::addEnumParameter(
    const QString &label, const std::map<int, QString> &options,
    std::function<int(const ParameterState &)> getter,
    std::function<void(ParameterState &, int)> setter,
    std::function<bool(const ParameterState &)> enabledCheck,
    const QString &tooltip, const QString &parameterKey) {
  QComboBox *combo = new QComboBox();
  if (!tooltip.isEmpty())
    combo->setToolTip(tooltip);
  combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  combo->setMinimumContentsLength(10);
  combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setParameterKey(combo, parameterKey);
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
          [this, combo, setter, label, parameterKey](int index) {
            int val = combo->itemData(index).toInt();
            applyChange([setter, val](ParameterState &s) { setter(s, val); },
                        label, parameterKey);
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


QCheckBox *ParameterPanel::addCheckboxParameter(
    const QString &label, std::function<bool(const ParameterState &)> getter,
    std::function<void(ParameterState &, bool)> setter,
    std::function<bool(const ParameterState &)> enabledCheck,
    const QString &tooltip, const QString &parameterKey) {
  // Create container with label on left, checkbox on right
  QWidget *container = new QWidget();
  QHBoxLayout *hLayout = new QHBoxLayout(container);
  hLayout->setContentsMargins(0, 0, 0, 0);

  QCheckBox *checkbox = new QCheckBox();
  QLabel *textLabel = new QLabel(label);
  setParameterKey(container, parameterKey);
  setParameterKey(checkbox, parameterKey);

  if (!tooltip.isEmpty()) {
    checkbox->setToolTip(tooltip);
    textLabel->setToolTip(tooltip);
  }

  hLayout->addWidget(checkbox, 0);  // Checkbox fixed size on left
  hLayout->addWidget(textLabel, 1); // Label expands to fill space

  // Add to form (single column - container spans both label and field)
  if (m_currentGroupForm) {
    m_currentGroupForm->addRow(container);
  } else {
    m_form->addRow(container);
  }

  // Connect changes: UI -> State
  connect(checkbox, &QCheckBox::toggled, this,
          [this, setter, label, parameterKey](bool checked) {
    applyChange([setter, checked](ParameterState &s) { setter(s, checked); },
                label, parameterKey);
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
  return checkbox;
}

QCheckBox *ParameterPanel::addCheckboxWithReset(
    const QString &label, std::function<bool(const ParameterState &)> getter,
    std::function<void(ParameterState &, bool)> setter,
    std::function<void(ParameterState &)> resetAction,
    std::function<bool(const ParameterState &)> enabledCheck,
    const QString &tooltip, const QString &parameterKey) {
  // Create container with label on left, checkbox on right
  QWidget *container = new QWidget();
  QHBoxLayout *hLayout = new QHBoxLayout(container);
  hLayout->setContentsMargins(0, 0, 0, 0);

  QCheckBox *checkbox = new QCheckBox();
  QLabel *textLabel = new QLabel(label);
  setParameterKey(container, parameterKey);
  setParameterKey(checkbox, parameterKey);

  if (!tooltip.isEmpty()) {
    checkbox->setToolTip(tooltip);
    textLabel->setToolTip(tooltip);
  }
  QPushButton *resetBtn = new QPushButton("Reset");
  setParameterKey(resetBtn, parameterKey);
  resetBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

  hLayout->addWidget(checkbox, 0);  // Checkbox fixed size on left
  hLayout->addWidget(textLabel, 1); // Label expands to fill space
  hLayout->addWidget(resetBtn, 0);  // Reset button fixed size

  // Add to form (single column - container spans both label and field)
  if (m_currentGroupForm) {
    m_currentGroupForm->addRow(container);
  } else {
    m_form->addRow(container);
  }

  // Connect changes: UI -> State
  connect(checkbox, &QCheckBox::toggled, this,
          [this, setter, label, parameterKey](bool checked) {
    applyChange([setter, checked](ParameterState &s) { setter(s, checked); },
                label, parameterKey);
  });

  // Connect Reset Button
  connect(resetBtn, &QPushButton::clicked, this, [this, resetAction, label]() {
    applyChange(resetAction, QString("Reset %1").arg(label));
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
  return checkbox;
}

QPushButton *ParameterPanel::addButtonParameter(
    const QString &label, const QString &text, std::function<void()> onClicked,
    std::function<bool(const ParameterState &)> enabledCheck,
    const QString &tooltip) {
  QPushButton *button = new QPushButton(text);
  if (!tooltip.isEmpty())
    button->setToolTip(tooltip);
  if (m_currentGroupForm) {
    m_currentGroupForm->addRow(label, button);
  } else {
    m_form->addRow(label, button);
  }

  // Connect clicks
  connect(button, &QPushButton::clicked, this, [onClicked]() {
    if (onClicked)
      onClicked();
  });

  // Enable/Visibility Update
  if (enabledCheck) {
    m_widgetStateUpdaters.push_back([this, button, enabledCheck]() {
      ParameterState state = m_stateGetter();
      bool en = enabledCheck(state);
      button->setEnabled(en);
      QWidget *labelWidget = m_form->labelForField(button);
      if (labelWidget)
        labelWidget->setEnabled(en);
    });
  }
  return button;
}

QPushButton *ParameterPanel::addToggleButtonParameter(
    const QString &label, const QString &text,
    std::function<void(bool)> onToggled,
    std::function<bool(const ParameterState &)> getter,
    std::function<bool(const ParameterState &)> enabledCheck,
    const QString &tooltip) {
  QPushButton *button = new QPushButton(text);
  button->setCheckable(true);
  if (!tooltip.isEmpty())
    button->setToolTip(tooltip);
  if (m_currentGroupForm) {
    m_currentGroupForm->addRow(label, button);
  } else {
    m_form->addRow(label, button);
  }

  // Connect clicks
  connect(button, &QPushButton::toggled, this, [onToggled](bool checked) {
    if (onToggled)
      onToggled(checked);
  });

  // Updater: State -> UI
  if (getter) {
      m_paramUpdaters.push_back([button, getter](const ParameterState &state) {
        bool val = getter(state);
        button->blockSignals(true);
        button->setChecked(val);
        button->blockSignals(false);
      });
  }

  // Enable/Visibility Update
  if (enabledCheck) {
    m_widgetStateUpdaters.push_back([this, button, enabledCheck]() {
      ParameterState state = m_stateGetter();
      bool en = enabledCheck(state);
      button->setEnabled(en);
      QWidget *labelWidget = m_form->labelForField(button);
      if (labelWidget)
        labelWidget->setEnabled(en);
    });
  }
  return button;
}

void ParameterPanel::addCorrelatedRGBParameter(
    const QString &label, double min, double max, double scale, int decimals,
    const QString &suffix,
    std::function<colorscreen::rgbdata(const ParameterState &)> getter,
    std::function<void(ParameterState &, const colorscreen::rgbdata &)> setter,
    std::function<bool(const ParameterState &)> enabledCheck,
    const QString &tooltip, const QString &parameterKey) {

  // 1. Link Checkbox
  QCheckBox *linkCheck = new QCheckBox("Link channels");
  linkCheck->setChecked(true);
  setParameterKey(linkCheck, parameterKey);

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
    if (!tooltip.isEmpty()) {
      slider->setToolTip(tooltip);
      spin->setToolTip(tooltip);
    }

    setParameterKey(container, parameterKey);
    setParameterKey(slider, parameterKey);
    setParameterKey(spin, parameterKey);

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
                            scale, label, parameterKey](int changedIdx,
                                                       double newVal) {
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

    applyChange([setter, next](ParameterState &state) { setter(state, next); },
                label, parameterKey);

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
  groupForm->setProperty(parameterSectionExpandedProperty, true);
  m_groupForms.push_back(groupForm);
  groupLayout->addLayout(groupForm);

  // Connect arrow button to toggle visibility
  connect(arrowBtn, &QToolButton::toggled, [arrowBtn, groupForm](bool checked) {
    arrowBtn->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
    groupForm->setProperty(parameterSectionExpandedProperty, checked);

    auto setVisibleRecursive = [](QLayoutItem *item, bool visible) {
        auto recurse = [](auto self, QLayoutItem *itm, bool vis) -> void {
            if (!itm) return;
            if (itm->widget()) {
                QWidget *widget = itm->widget();
                widget->setVisible(vis && parameterWidgetApplicable(widget));
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

/** Register logical applicability for the form row containing WIDGET. */
void ParameterPanel::setParameterApplicability(
    QWidget *widget,
    std::function<bool(const ParameterState &)> applicableCheck) {
  if (!widget || !applicableCheck)
    return;

  QFormLayout *form = nullptr;
  QWidget *rowWidget = nullptr;
  auto locateInForm = [](QFormLayout *candidateForm, QWidget *candidate) {
    if (!candidateForm || !candidate)
      return false;
    int row = -1;
    QFormLayout::ItemRole role = QFormLayout::FieldRole;
    candidateForm->getWidgetPosition(candidate, &row, &role);
    return row >= 0 && role != QFormLayout::LabelRole;
  };

  for (QWidget *candidate = widget; candidate && candidate != this;
       candidate = candidate->parentWidget()) {
    if (locateInForm(m_form, candidate)) {
      form = m_form;
      rowWidget = candidate;
      break;
    }
    for (QFormLayout *groupForm : m_groupForms) {
      if (locateInForm(groupForm, candidate)) {
        form = groupForm;
        rowWidget = candidate;
        break;
      }
    }
    if (form)
      break;
  }

  if (!form || !rowWidget)
    return;

  QWidget *labelWidget = form->labelForField(rowWidget);
  m_widgetStateUpdaters.push_back(
      [this, form, rowWidget, labelWidget,
       applicableCheck = std::move(applicableCheck)]() {
        const bool applicable = applicableCheck(m_stateGetter());
        const QVariant expandedValue =
            form->property(parameterSectionExpandedProperty);
        const bool sectionExpanded =
            !expandedValue.isValid() || expandedValue.toBool();

        rowWidget->setProperty(parameterApplicableProperty, applicable);
        rowWidget->setVisible(applicable && sectionExpanded);
        if (labelWidget) {
          labelWidget->setProperty(parameterApplicableProperty, applicable);
          labelWidget->setVisible(applicable && sectionExpanded);
        }
      });
}

QWidget *
ParameterPanel::createDetachableSection(
    const QString &title, QWidget *content,
    std::function<void()> beforeDetach) {
  return new DetachableSection(title, content, std::move(beforeDetach), this);
}


void ParameterPanel::endGroup() {
  m_currentGroupForm = nullptr;
}
