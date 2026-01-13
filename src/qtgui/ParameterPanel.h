#ifndef PARAMETER_PANEL_H
#define PARAMETER_PANEL_H

#include <QWidget>
#include <QToolButton>
#include <vector>
#include <functional>
#include <map>
#include <memory>
#include "ParameterState.h"

namespace colorscreen {
    class image_data;
}

class QVBoxLayout;
class QFormLayout;
class QGroupBox;

class ParameterPanel : public QWidget
{
    Q_OBJECT
public:
    using StateGetter = std::function<ParameterState()>;
    using StateSetter = std::function<void(const ParameterState&)>;
    using ImageGetter = std::function<std::shared_ptr<colorscreen::image_data>()>;

    explicit ParameterPanel(StateGetter stateGetter, StateSetter stateSetter, ImageGetter imageGetter, QWidget *parent = nullptr);
    ~ParameterPanel() override;

    // Called when the external state changes (Undo/Redo, Code Load)
    virtual void updateUI();

protected:
    /*
      Adds a double parameter row (SpinBox + Optional Combo).
    */
    void addDoubleParameter(const QString &label, double min, double max, 
                            std::function<double(const ParameterState&)> getter, 
                            std::function<void(ParameterState&, double)> setter,
                            const std::map<double, QString> &specialValues = {},
                            const std::map<double, QString> &quickSelects = {},
                            std::function<bool(double)> validator = nullptr);

    /*
      Adds a slider parameter row (Slider + SpinBox).
      scale: factor to map double value to integer slider range (e.g. 100 for 0.01 precision).
      decimals: precision for SpinBox.
    */
    void addSliderParameter(const QString &label, double min, double max, double scale, int decimals,
                            const QString &suffix, const QString &specialValueText,
                            std::function<double(const ParameterState&)> getter,
                            std::function<void(ParameterState&, double)> setter,
                            double gamma = 1.0,
                            std::function<bool(const ParameterState&)> enabledCheck = nullptr);

    void addEnumParameter(const QString &label, const std::map<int, QString> &options,
                          std::function<int(const ParameterState&)> getter,
                          std::function<void(ParameterState&, int)> setter,
                          std::function<bool(const ParameterState&)> enabledCheck = nullptr);
    
    QToolButton* addSeparator(const QString &title);

protected:
    virtual void applyChange(std::function<void(ParameterState&)> modifier);
    QFormLayout *m_currentGroupForm = nullptr;
    
    StateGetter m_stateGetter;
    StateSetter m_stateSetter;
    ImageGetter m_imageGetter;

    QVBoxLayout *m_layout;
    QFormLayout *m_form; // Helper to access form layout
    
    std::vector<std::function<void(const ParameterState&)>> m_paramUpdaters;
    std::vector<std::function<void()>> m_widgetStateUpdaters;
    
    virtual void onParametersRefreshed(const ParameterState &state) {}
};

#endif // PARAMETER_PANEL_H
