#include "ParameterPanel.h"
#include "../libcolorscreen/include/base.h"
#include "SmartSpinBox.h"
#include <QSignalBlocker>
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
#include <QSettings>
#include <QSlider>
#include <QShowEvent>
#include <QToolButton>
#include <QTimer>
#include <utility>
#include <QVBoxLayout>
#include <cmath>
#include <limits>
#include <optional>

namespace {

constexpr auto parameterApplicableProperty = "parameterApplicable";
constexpr auto parameterDefaultValueProperty = "parameterDefaultValue";
constexpr auto parameterKeyProperty = "parameterKey";
constexpr auto parameterModifiedProperty = "parameterModified";
constexpr auto parameterSpecialStateValueProperty = "parameterSpecialStateValue";
constexpr auto parameterSectionExpandedProperty = "parameterSectionExpanded";

/** Shared conversion between a numeric value and its slider position.

    Stateful and stateless parameter helpers must expose identical linear,
    gamma, and logarithmic geometry. A separated stored sentinel reserves one
    slider position below the regular range without changing that mapping. */
class SliderValueMapping {
public:
  static constexpr int nonlinearSliderMaximum = 65535;

  SliderValueMapping(double minimum, double maximum, double scale, double gamma,
                     bool logarithmic,
                     std::optional<double> specialMinimumValue = std::nullopt)
      : m_minimum(minimum), m_maximum(maximum), m_scale(scale),
        m_gamma(gamma), m_logarithmic(logarithmic),
        m_hasSeparatedSpecialMinimum(specialMinimumValue.has_value() &&
                                     *specialMinimumValue < minimum),
        m_specialStateValue(specialMinimumValue.value_or(minimum)),
        m_regularLinearSliderMin(static_cast<int>(minimum * scale)),
        m_regularLinearSliderMax(static_cast<int>(maximum * scale)),
        m_nonlinear(gamma != 1.0 || logarithmic),
        m_regularSliderMin(m_nonlinear
                               ? (m_hasSeparatedSpecialMinimum ? 1 : 0)
                               : m_regularLinearSliderMin),
        m_specialSliderPosition(m_nonlinear ? 0
                                            : m_regularLinearSliderMin - 1) {
    Q_ASSERT(scale > 0);
    Q_ASSERT(!specialMinimumValue.has_value() ||
             *specialMinimumValue <= minimum);
  }

  bool hasSeparatedSpecialMinimum() const {
    return m_hasSeparatedSpecialMinimum;
  }
  double specialStateValue() const { return m_specialStateValue; }

  int sliderMinimum() const {
    if (m_nonlinear)
      return 0;
    return m_hasSeparatedSpecialMinimum ? m_specialSliderPosition
                                        : m_regularLinearSliderMin;
  }

  int sliderMaximum() const {
    return m_nonlinear ? nonlinearSliderMaximum : m_regularLinearSliderMax;
  }

  double sliderToValue(int sliderValue) const {
    if (m_hasSeparatedSpecialMinimum &&
        sliderValue == m_specialSliderPosition)
      return m_specialStateValue;
    if (!m_nonlinear)
      return static_cast<double>(sliderValue) / m_scale;

    const double t = static_cast<double>(sliderValue - m_regularSliderMin) /
                     (nonlinearSliderMaximum - m_regularSliderMin);
    if (m_logarithmic) {
      if (m_minimum <= 0)
        return std::pow(m_maximum + 1.0, t) - 1.0;
      return m_minimum * std::pow(m_maximum / m_minimum, t);
    }

    return m_minimum +
           (m_maximum - m_minimum) * std::pow(t, m_gamma);
  }

  int valueToSlider(double value) const {
    if (m_hasSeparatedSpecialMinimum &&
        qAbs(value - m_specialStateValue) <= 1e-12)
      return m_specialSliderPosition;
    if (!m_nonlinear) {
      const int mapped = qRound(value * m_scale);
      return m_hasSeparatedSpecialMinimum
                 ? std::clamp(mapped, m_regularSliderMin,
                              m_regularLinearSliderMax)
                 : mapped;
    }

    double t = 0;
    if (m_logarithmic) {
      if (m_minimum <= 0) {
        if (value > 0)
          t = std::log(value + 1.0) / std::log(m_maximum + 1.0);
      } else if (value > m_minimum) {
        t = std::log(value / m_minimum) /
            std::log(m_maximum / m_minimum);
      }
    } else {
      const double ratio = (value - m_minimum) / (m_maximum - m_minimum);
      if (ratio >= 1)
        t = 1;
      else if (ratio > 0)
        t = std::pow(ratio, 1.0 / m_gamma);
    }

    return std::clamp(
        qRound(t * (nonlinearSliderMaximum - m_regularSliderMin) +
               m_regularSliderMin),
        m_regularSliderMin, nonlinearSliderMaximum);
  }

private:
  double m_minimum;
  double m_maximum;
  double m_scale;
  double m_gamma;
  bool m_logarithmic;
  bool m_hasSeparatedSpecialMinimum;
  double m_specialStateValue;
  int m_regularLinearSliderMin;
  int m_regularLinearSliderMax;
  bool m_nonlinear;
  int m_regularSliderMin;
  int m_specialSliderPosition;
};

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

/** Numeric editor with one explicit stored sentinel below its ordinary range.

    QDoubleSpinBox normally ties specialValueText() to minimum().  Extending the
    internal range down to the sentinel would therefore also make all values
    between the sentinel and the real numeric minimum editable.  This editor
    keeps that interval unavailable while still letting the sentinel round-trip
    exactly. */
class ParameterSliderSpinBox final : public QDoubleSpinBox {
public:
  using QDoubleSpinBox::QDoubleSpinBox;

  void setSeparatedMinimum(double specialValue, double regularMinimum) {
    Q_ASSERT(specialValue < regularMinimum);
    m_specialValue = specialValue;
    m_regularMinimum = regularMinimum;
    m_hasSeparatedMinimum = true;
  }

protected:
  QValidator::State validate(QString &input, int &pos) const override {
    const QValidator::State baseState = QDoubleSpinBox::validate(input, pos);
    if (!m_hasSeparatedMinimum || baseState != QValidator::Acceptable)
      return baseState;
    if (!specialValueText().isEmpty() &&
        input.trimmed() == specialValueText())
      return QValidator::Acceptable;

    const double parsed = QDoubleSpinBox::valueFromText(input);
    if (near(parsed, m_specialValue) || parsed >= m_regularMinimum)
      return QValidator::Acceptable;

    // Keep partial input such as "4" valid while the user is typing "400",
    // but never commit a value from the sentinel-to-numeric gap.
    return QValidator::Intermediate;
  }

  void stepBy(int steps) override {
    if (!m_hasSeparatedMinimum || steps == 0) {
      QDoubleSpinBox::stepBy(steps);
      return;
    }

    const double current = value();
    if (steps > 0 && near(current, m_specialValue)) {
      setValue(m_regularMinimum);
      --steps;
    } else if (steps < 0 && near(current, m_regularMinimum)) {
      setValue(m_specialValue);
      ++steps;
    }
    if (steps == 0)
      return;

    QDoubleSpinBox::stepBy(steps);
    const double stepped = value();
    if (stepped > m_specialValue && stepped < m_regularMinimum)
      setValue(steps > 0 ? m_regularMinimum : m_specialValue);
  }

private:
  bool near(double a, double b) const {
    return qAbs(a - b) <= qMax(1e-12, singleStep() * 0.5);
  }

  bool m_hasSeparatedMinimum = false;
  double m_specialValue = 0;
  double m_regularMinimum = 0;
};

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

  updateWidgetStates();

  // Call virtual method for derived classes
  onParametersRefreshed(state);
}

/** Refresh presentation callbacks without re-entering parameter refresh. */
void ParameterPanel::updateWidgetStates() {
  for (auto &widgetUpdater : m_widgetStateUpdaters)
    widgetUpdater();
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
    QSignalBlocker signalBlocker1(spin);
    spin->setValue(val);
    signalBlocker1.unblock();

    if (combo) {
      QSignalBlocker signalBlocker2(combo);
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
      signalBlocker2.unblock();
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

ParameterPanel::SliderWidgets ParameterPanel::addSliderParameterControls(
    const QString &label, double min, double max, double scale, int decimals,
    const QString &suffix, const QString &specialValueText,
    std::function<double(const ParameterState &)> getter,
    std::function<void(ParameterState &, double)> setter, double gamma,
    std::function<bool(const ParameterState &)> enabledCheck,
    bool logarithmic, const QString &tooltip,
    const QString &parameterKey, bool showDefaultReset,
    std::optional<double> specialMinimumValue,
    ParameterKeyGetter parameterKeyGetter) {
  Q_ASSERT(parameterKey.isEmpty() || !parameterKeyGetter);
  Q_ASSERT(!showDefaultReset || !parameterKeyGetter);
  Q_ASSERT(!specialMinimumValue.has_value() ||
           *specialMinimumValue <= min);

  const auto resolvedParameterKey = [parameterKey, parameterKeyGetter]() {
    return parameterKeyGetter ? parameterKeyGetter() : parameterKey;
  };
  const SliderValueMapping valueMapping(min, max, scale, gamma, logarithmic,
                                        specialMinimumValue);
  const bool hasSeparatedSpecialMinimum =
      valueMapping.hasSeparatedSpecialMinimum();
  const double specialStateValue = valueMapping.specialStateValue();

  // Container: Slider + SpinBox
  QWidget *container = new QWidget();
  QHBoxLayout *hLayout = new QHBoxLayout(container);
  hLayout->setContentsMargins(0, 0, 0, 0);

  QSlider *slider = new QSlider(Qt::Horizontal);
  if (!tooltip.isEmpty())
    slider->setToolTip(tooltip);

  slider->setRange(valueMapping.sliderMinimum(), valueMapping.sliderMaximum());

  auto *spin = new ParameterSliderSpinBox();
  if (hasSeparatedSpecialMinimum) {
    spin->setRange(specialStateValue, max);
    spin->setSeparatedMinimum(specialStateValue, min);
  } else {
    spin->setRange(min, max);
  }
  spin->setDecimals(decimals);
  spin->setSingleStep(1.0 / scale);
  if (!suffix.isEmpty())
    spin->setSuffix(QString(" %1").arg(suffix));
  if (!specialValueText.isEmpty())
    spin->setSpecialValueText(specialValueText);

  const QString initialParameterKey = resolvedParameterKey();
  setParameterKey(container, initialParameterKey);
  setParameterKey(slider, initialParameterKey);
  setParameterKey(spin, initialParameterKey);
  if (specialMinimumValue.has_value()) {
    container->setProperty(parameterSpecialStateValueProperty,
                           *specialMinimumValue);
    slider->setProperty(parameterSpecialStateValueProperty,
                        *specialMinimumValue);
    spin->setProperty(parameterSpecialStateValueProperty,
                      *specialMinimumValue);
  }

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

  // Synchronization
  connect(slider, &QSlider::valueChanged, this,
          [this, spin, valueMapping, setter, label,
           resolvedParameterKey](int val) {
            double dVal = valueMapping.sliderToValue(val);
            QSignalBlocker signalBlocker3(spin);
            spin->setValue(dVal);
            signalBlocker3.unblock();

            // Trigger update
            applyChange([setter, dVal](ParameterState &s) { setter(s, dVal); },
                        label, resolvedParameterKey());
          });

  connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
          [slider, valueMapping](double val) {
            QSignalBlocker signalBlocker4(slider);
            slider->setValue(valueMapping.valueToSlider(val));
            signalBlocker4.unblock();
          });

  // Change
  connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
          [this, setter, label, resolvedParameterKey](double val) {
            applyChange([setter, val](ParameterState &s) { setter(s, val); },
                        label, resolvedParameterKey());
          });

  // Updater: State -> UI
  m_paramUpdaters.push_back(
      [slider, spin, getter, valueMapping](const ParameterState &state) {
        double val = getter(state);
        QSignalBlocker signalBlocker5(spin);
        spin->setValue(val);
        signalBlocker5.unblock();

        QSignalBlocker signalBlocker6(slider);
        slider->setValue(valueMapping.valueToSlider(val));
        signalBlocker6.unblock();
      });

  // A context-dependent editor changes logical target without being rebuilt.
  // Keep its metadata aligned with the key that will be used by the next edit.
  if (parameterKeyGetter) {
    m_widgetStateUpdaters.push_back(
        [container, slider, spin, resolvedParameterKey]() {
          const QString key = resolvedParameterKey();
          setParameterKey(container, key);
          setParameterKey(slider, key);
          setParameterKey(spin, key);
        });
  }

  // Enable Update
  if (enabledCheck) {
    m_widgetStateUpdaters.push_back(
        [this, slider, spin, container, enabledCheck]() {
          ParameterState state = m_stateGetter();
          bool en = enabledCheck(state);
          slider->setEnabled(en);
          spin->setEnabled(en);

          // The field container does not own its QFormLayout label, so keep
          // the label's enabled state synchronized explicitly.
          QWidget *labelWidget = m_form->labelForField(container);
          if (labelWidget)
            labelWidget->setEnabled(en);
        });
  }
  return {container, slider, spin};
}

QWidget *ParameterPanel::addSliderParameter(
    const QString &label, double min, double max, double scale, int decimals,
    const QString &suffix, const QString &specialValueText,
    std::function<double(const ParameterState &)> getter,
    std::function<void(ParameterState &, double)> setter, double gamma,
    std::function<bool(const ParameterState &)> enabledCheck,
    bool logarithmic, const QString &tooltip,
    const QString &parameterKey, bool showDefaultReset,
    std::optional<double> specialMinimumValue,
    ParameterKeyGetter parameterKeyGetter) {
  return addSliderParameterControls(
             label, min, max, scale, decimals, suffix, specialValueText,
             std::move(getter), std::move(setter), gamma,
             std::move(enabledCheck), logarithmic, tooltip, parameterKey,
             showDefaultReset, specialMinimumValue,
             std::move(parameterKeyGetter))
      .container;
}

ParameterPanel::SliderWidgets ParameterPanel::addSliderControls(
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

  const SliderValueMapping valueMapping(min, max, scale, gamma, logarithmic);
  slider->setRange(valueMapping.sliderMinimum(), valueMapping.sliderMaximum());

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

  // Synchronization
  connect(slider, &QSlider::valueChanged, this,
          [spin, valueMapping, onChanged](int val) {
            double dVal = valueMapping.sliderToValue(val);
            QSignalBlocker signalBlocker7(spin);
            spin->setValue(dVal);
            signalBlocker7.unblock();

            if (onChanged)
              onChanged(dVal);
          });

  connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
          [slider, valueMapping, onChanged](double val) {
            QSignalBlocker signalBlocker8(slider);
            slider->setValue(valueMapping.valueToSlider(val));
            signalBlocker8.unblock();

            if (onChanged)
              onChanged(val);
          });

  // Initial Value
  spin->setValue(initialValue);
  slider->setValue(valueMapping.valueToSlider(initialValue));

  return {container, slider, spin};
}

QWidget *ParameterPanel::addSlider(
    const QString &label, double min, double max, double scale, int decimals,
    const QString &suffix, const QString &specialValueText,
    double initialValue, std::function<void(double)> onChanged, double gamma,
    bool logarithmic, const QString &tooltip) {
  const SliderWidgets widgets =
      addSliderControls(label, min, max, scale, decimals, suffix,
                        specialValueText, initialValue, onChanged, gamma,
                        logarithmic, tooltip);
  return widgets.container;
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
    QSignalBlocker signalBlocker9(combo);
    int idx = combo->findData(val);
    if (idx != -1)
      combo->setCurrentIndex(idx);
    signalBlocker9.unblock();
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
    QSignalBlocker signalBlocker10(checkbox);
    checkbox->setChecked(val);
    signalBlocker10.unblock();
  });

  // Enablement is independent of applicability/visibility. Disabling the
  // container keeps the inline label (and Reset control, when present) in the
  // same prerequisite state as the checkbox itself.
  if (enabledCheck) {
    m_widgetStateUpdaters.push_back([this, container, enabledCheck]() {
      const bool enabled = enabledCheck(m_stateGetter());
      container->setEnabled(enabled);
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
    QSignalBlocker signalBlocker11(checkbox);
    checkbox->setChecked(val);
    signalBlocker11.unblock();
  });

  // Enablement is independent of applicability/visibility. Disabling the
  // container keeps the inline label (and Reset control, when present) in the
  // same prerequisite state as the checkbox itself.
  if (enabledCheck) {
    m_widgetStateUpdaters.push_back([this, container, enabledCheck]() {
      const bool enabled = enabledCheck(m_stateGetter());
      container->setEnabled(enabled);
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
        QSignalBlocker signalBlocker12(button);
        button->setChecked(val);
        signalBlocker12.unblock();
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

  // 2. Three saved channels. One visible RGB editor represents three
  // independent saved values when linking is disabled, so merge identity
  // must follow the channel rather than the compound helper.
  struct Channel {
    QSlider *slider;
    QDoubleSpinBox *spin;
  };
  std::vector<Channel> channels;
  QStringList names = {"Red", "Green", "Blue"};
  std::vector<QString> channelParameterKeys;

  for (int i = 0; i < 3; ++i) {
    const QString channelParameterKey =
        parameterKey.isEmpty()
            ? QString()
            : parameterKey + QStringLiteral(".") + names[i].toLower();
    channelParameterKeys.push_back(channelParameterKey);
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

    setParameterKey(container, channelParameterKey);
    setParameterKey(slider, channelParameterKey);
    setParameterKey(spin, channelParameterKey);

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
      QSignalBlocker signalBlocker13(spin);
      spin->setValue((double)val / scale);
      signalBlocker13.unblock();
    });
    connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [slider, scale](double val) {
              QSignalBlocker signalBlocker14(slider);
              slider->setValue(qRound(val * scale));
              signalBlocker14.unblock();
            });
  }

  // Link Checkbox row
  if (m_currentGroupForm)
    m_currentGroupForm->addRow("", linkCheck);
  else
    m_form->addRow("", linkCheck);

  // Interaction Logic
  auto handleValueChange = [this, channels, channelParameterKeys, linkCheck,
                            getter, setter, scale, label](int changedIdx,
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
                label, channelParameterKeys[changedIdx]);

    // Optimistic UI update for linked sliders
    if (linkCheck->isChecked()) {
      for (int i = 0; i < 3; ++i) {
        if (i != changedIdx) {
          QSignalBlocker signalBlocker15(channels[i].spin);
          channels[i].spin->setValue(next[i]);
          signalBlocker15.unblock();

          QSignalBlocker signalBlocker16(channels[i].slider);
          channels[i].slider->setValue(qRound(next[i] * scale));
          signalBlocker16.unblock();
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
      QSignalBlocker signalBlocker17(channels[i].spin);
      channels[i].spin->setValue(v[i]);
      signalBlocker17.unblock();

      QSignalBlocker signalBlocker18(channels[i].slider);
      channels[i].slider->setValue(qRound(v[i] * scale));
      signalBlocker18.unblock();
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

/** Add a foldable section, optionally restoring an application preference.

    SECTIONKEY is an untranslated presentation identity, never an undo key.
    Only explicit button activation persists a choice; refresh, applicability,
    and programmatic folding must not overwrite the user's preference. */
QToolButton *ParameterPanel::addSeparator(const QString &title,
                                         const QString &sectionKey) {
  const QString settingsKey = sectionKey.isEmpty()
      ? QString()
      : QStringLiteral("inspector/sections/%1/expanded").arg(sectionKey);
  const bool expanded = settingsKey.isEmpty()
      || QSettings().value(settingsKey, true).toBool();

  QGroupBox *group = new QGroupBox();
  group->setFlat(true);
  group->setStyleSheet(
      "QGroupBox { border: none; margin: 0px; padding: 0px; }");

  // Create a custom title widget with arrow button.
  QWidget *titleWidget = new QWidget();
  QPalette pal = titleWidget->palette();
  pal.setColor(QPalette::Window, pal.color(QPalette::Mid));
  titleWidget->setAutoFillBackground(true);
  titleWidget->setPalette(pal);

  QHBoxLayout *titleLayout = new QHBoxLayout(titleWidget);
  titleLayout->setContentsMargins(4, 4, 4, 4);
  titleLayout->setSpacing(4);

  QToolButton *arrowBtn = new QToolButton();
  arrowBtn->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
  arrowBtn->setStyleSheet(
      "QToolButton { border: none; background: transparent; }");
  arrowBtn->setCheckable(true);
  arrowBtn->setChecked(expanded);
  arrowBtn->setAccessibleName(title);
  arrowBtn->setToolTip(expanded ? tr("Collapse %1").arg(title)
                                : tr("Expand %1").arg(title));
  if (!sectionKey.isEmpty()) {
    arrowBtn->setProperty("sectionKey", sectionKey);
    group->setProperty("sectionKey", sectionKey);
  }

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
  groupForm->setProperty(parameterSectionExpandedProperty, expanded);
  m_groupForms.push_back(groupForm);
  groupLayout->addLayout(groupForm);

  // Rows are added after the header. Reapply folding during updateUI(), so
  // restored and dynamically added rows cannot escape a collapsed section.
  // Guard the refresh callback because derived panels may rebuild their UI.
  const QPointer<QToolButton> sectionToggle = arrowBtn;
  const QPointer<QFormLayout> sectionForm = groupForm;
  auto updateVisibility = [sectionToggle, sectionForm, title]() {
    if (!sectionToggle || !sectionForm)
      return;
    const bool checked = sectionToggle->isChecked();
    sectionToggle->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
    sectionToggle->setToolTip(checked ? tr("Collapse %1").arg(title)
                                     : tr("Expand %1").arg(title));
    sectionForm->setProperty(parameterSectionExpandedProperty, checked);

    auto setVisibleRecursive = [](auto self, QLayoutItem *item,
                                  bool visible) -> void {
      if (!item)
        return;
      if (QWidget *widget = item->widget()) {
        widget->setVisible(visible && parameterWidgetApplicable(widget));
      } else if (QLayout *layout = item->layout()) {
        for (int i = 0; i < layout->count(); ++i)
          self(self, layout->itemAt(i), visible);
      }
    };
    for (int i = 0; i < sectionForm->count(); ++i)
      setVisibleRecursive(setVisibleRecursive, sectionForm->itemAt(i), checked);
  };
  connect(arrowBtn, &QToolButton::toggled, this,
          [updateVisibility](bool) { updateVisibility(); });
  if (!settingsKey.isEmpty()) {
    m_widgetStateUpdaters.push_back(updateVisibility);
    connect(arrowBtn, &QToolButton::clicked, this,
            [settingsKey](bool checked) {
              QSettings().setValue(settingsKey, checked);
            });
  }

  m_form->addRow(group);
  m_currentGroupForm = groupForm;
  return arrowBtn;
}

/** Set logical applicability for the complete form row containing WIDGET. */
void ParameterPanel::setParameterRowApplicable(QWidget *widget,
                                               bool applicable) {
  if (!widget)
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
}

/** Register state-derived logical applicability for the row containing WIDGET. */
void ParameterPanel::setParameterApplicability(
    QWidget *widget,
    std::function<bool(const ParameterState &)> applicableCheck) {
  if (!widget || !applicableCheck)
    return;

  const QPointer<QWidget> guardedWidget = widget;
  m_widgetStateUpdaters.push_back(
      [this, guardedWidget,
       applicableCheck = std::move(applicableCheck)]() {
        if (!guardedWidget)
          return;
        setParameterRowApplicable(
            guardedWidget, applicableCheck(m_stateGetter()));
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
