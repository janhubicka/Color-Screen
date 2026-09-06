#ifndef PARAMETER_PANEL_H
#define PARAMETER_PANEL_H

#include "ParameterState.h"
#include <QComboBox>
#include <QString>
#include <QToolButton>
#include <QWidget>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace colorscreen {
class image_data;
}

class QVBoxLayout;
class QHBoxLayout;
class QFormLayout;
class QGroupBox;
class QCheckBox;
class QPushButton;

class ParameterPanel : public QWidget {
  Q_OBJECT
public:
  using StateGetter = std::function<ParameterState()>;
  using StateSetter = std::function<void(const ParameterState &, const QString &,
                                         const QString &)>;
  using ImageGetter = std::function<std::shared_ptr<colorscreen::image_data>()>;

  explicit ParameterPanel(StateGetter stateGetter, StateSetter stateSetter,
                          ImageGetter imageGetter, QWidget *parent = nullptr,
                          bool useScrollArea = true);
  ~ParameterPanel() override;

  // Called when the external state changes (Undo/Redo, Code Load)
  virtual void updateUI();

protected:
  /*
    Adds a double parameter row (SpinBox + Optional Combo).

    PARAMETERKEY is a stable untranslated identifier for the logical
    parameter. Stateful helpers store it as widget metadata and use it as
    undo merge identity. Leave it empty while incrementally migrating old
    call sites; those retain the historical label-based merge behavior.
  */
  void addDoubleParameter(const QString &label, double min, double max,
                          std::function<double(const ParameterState &)> getter,
                          std::function<void(ParameterState &, double)> setter,
                          const std::map<double, QString> &specialValues = {},
                          const std::map<double, QString> &quickSelects = {},
                          std::function<bool(double)> validator = nullptr,
                          const QString &tooltip = QString(),
                          const QString &parameterKey = QString(),
                          bool showDefaultReset = false);

  /*
    Adds a slider parameter row (Slider + SpinBox).
    scale: factor to map double value to integer slider range (e.g. 100 for 0.01
    precision). decimals: precision for SpinBox.
  */
  QWidget *addSliderParameter(
      const QString &label, double min, double max, double scale, int decimals,
      const QString &suffix, const QString &specialValueText,
      std::function<double(const ParameterState &)> getter,
      std::function<void(ParameterState &, double)> setter, double gamma = 1.0,
      std::function<bool(const ParameterState &)> enabledCheck = nullptr,
      bool logarithmic = false, const QString &tooltip = QString(),
      const QString &parameterKey = QString(),
      bool showDefaultReset = false);

  /*
    Adds a slider parameter row (Slider + SpinBox) that does not participate in state.
  */
  QWidget* addSlider(
      const QString &label, double min, double max, double scale, int decimals,
      const QString &suffix, const QString &specialValueText,
      double initialValue,
      std::function<void(double)> onChanged, double gamma = 1.0,
      bool logarithmic = false, const QString &tooltip = QString());

  QComboBox *addEnumParameter(
      const QString &label, const std::map<int, QString> &options,
      std::function<int(const ParameterState &)> getter,
      std::function<void(ParameterState &, int)> setter,
      std::function<bool(const ParameterState &)> enabledCheck = nullptr,
      const QString &tooltip = QString(),
      const QString &parameterKey = QString());

  template <typename T>
  void addEnumTooltips(QComboBox *combo, const T *names, int max) {
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

  template <typename T>
  QComboBox *addEnumParameter(
      const QString &label, const T *names, int max,
      std::function<int(const ParameterState &)> getter,
      std::function<void(ParameterState &, int)> setter,
      std::function<bool(const ParameterState &)> enabledCheck = nullptr,
      const QString &tooltip = QString(),
      const QString &parameterKey = QString()) {
    std::map<int, QString> options;
    for (int i = 0; i < max; ++i) {
      if (names[i].pretty_name && names[i].pretty_name[0]) {
        options[i] = QString::fromUtf8(names[i].pretty_name);
      } else if (names[i].name && names[i].name[0]) {
        options[i] = QString::fromUtf8(names[i].name);
      }
    }

    QComboBox *combo = addEnumParameter(label, options, getter, setter,
                                        enabledCheck, tooltip, parameterKey);
    addEnumTooltips(combo, names, max);
    return combo;
  }

  template <typename EnumType, auto Names, int Max>
  QComboBox *addEnumParameter(
      const QString &label, std::function<int(const ParameterState &)> getter,
      std::function<void(ParameterState &, int)> setter,
      std::function<bool(const ParameterState &)> enabledCheck = nullptr,
      const QString &tooltip = QString(),
      const QString &parameterKey = QString()) {
    return addEnumParameter(label, Names, Max, getter, setter, enabledCheck,
                            tooltip, parameterKey);
  }

  QCheckBox *addCheckboxParameter(
      const QString &label, std::function<bool(const ParameterState &)> getter,
      std::function<void(ParameterState &, bool)> setter,
      std::function<bool(const ParameterState &)> enabledCheck = nullptr,
      const QString &tooltip = QString(),
      const QString &parameterKey = QString());

  QCheckBox *addCheckboxWithReset(
      const QString &label, std::function<bool(const ParameterState &)> getter,
      std::function<void(ParameterState &, bool)> setter,
      std::function<void(ParameterState &)> resetAction,
      std::function<bool(const ParameterState &)> enabledCheck = nullptr,
      const QString &tooltip = QString(),
      const QString &parameterKey = QString());

  QPushButton *addButtonParameter(
      const QString &label, const QString &text, std::function<void()> onClicked,
      std::function<bool(const ParameterState &)> enabledCheck = nullptr,
      const QString &tooltip = QString());

  QPushButton *addToggleButtonParameter(
      const QString &label, const QString &text,
      std::function<void(bool)> onToggled,
      std::function<bool(const ParameterState &)> getter = nullptr,
      std::function<bool(const ParameterState &)> enabledCheck = nullptr,
      const QString &tooltip = QString());

  void addCorrelatedRGBParameter(
      const QString &label, double min, double max, double scale, int decimals,
      const QString &suffix,
      std::function<colorscreen::rgbdata(const ParameterState &)> getter,
      std::function<void(ParameterState &, const colorscreen::rgbdata &)> setter,
      std::function<bool(const ParameterState &)> enabledCheck = nullptr,
      const QString &tooltip = QString(),
      const QString &parameterKey = QString());

  QToolButton *addSeparator(const QString &title);

  /** Hide the complete form row containing WIDGET whenever APPLICABLECHECK
      returns false.

      Applicability is deliberately independent of enablement: use this for a
      control whose concept does not exist for the current process/image, while
      enabledCheck remains appropriate when a visible control should explain a
      missing prerequisite. Section collapse/expand state is combined with the
      applicability predicate, so expanding a section never resurrects an
      inapplicable row. */
  void setParameterApplicability(
      QWidget *widget,
      std::function<bool(const ParameterState &)> applicableCheck);

  /** Add opt-in default/modified/reset presentation to a keyed numeric
      FIELD. The reset target comes from a fresh ParameterState so there is
      no second table of defaults. Reset is intentionally a separate undo
      gesture rather than another update carrying PARAMETERKEY. */
  void addNumericDefaultPresentation(
      QWidget *field, QHBoxLayout *layout, const QString &label,
      const QString &parameterKey, double defaultValue,
      std::function<double(const ParameterState &)> getter,
      std::function<void(ParameterState &, double)> setter,
      double tolerance);

  /** Wrap CONTENT in the standard detachable-panel presentation.
      The returned section owns the floating QDockWidget lifecycle and
      always reattaches CONTENT when the dock closes or its host changes.
      BEFOREDETACH is retained for panel-specific sizing adjustments; the
      section snapshots and restores widget constraints automatically. */
  QWidget *createDetachableSection(
      const QString &title, QWidget *content,
      std::function<void()> beforeDetach = {});

  // Ends the current group (if any) so subsequent items are added to the main form
  void endGroup();

protected:
  virtual void applyChange(std::function<void(ParameterState &)> modifier,
                           const QString &description = QString(),
                           const QString &parameterKey = QString());

  StateGetter m_stateGetter;
  StateSetter m_stateSetter;
  ImageGetter m_imageGetter;
  QFormLayout *m_currentGroupForm = nullptr;

  QVBoxLayout *m_layout;
  QFormLayout *m_form; // Helper to access form layout
  std::vector<QFormLayout *> m_groupForms;

  std::vector<std::function<void(const ParameterState &)>> m_paramUpdaters;
  std::vector<std::function<void()>> m_widgetStateUpdaters;

  virtual void onParametersRefreshed(const ParameterState &state) {}
};

#endif // PARAMETER_PANEL_H
