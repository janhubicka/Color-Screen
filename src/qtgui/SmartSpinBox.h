#ifndef SMARTSPINBOX_H
#define SMARTSPINBOX_H

#include <QDoubleSpinBox>
#include <map>
#include <set>
#include <cmath>

class SmartSpinBox : public QDoubleSpinBox
{
    Q_OBJECT
public:
    SmartSpinBox(QWidget *parent = nullptr) : QDoubleSpinBox(parent) {}
    
    void setSpecialValues(const std::map<double, QString> &values) {
        m_specialValues = values;
    }
    
    void setSpecialValueEnabled(double val, bool enabled) {
        if (enabled) m_disabledValues.erase(val);
        else m_disabledValues.insert(val);
        // Force re-val
        setValue(value()); 
    }

    QString textFromValue(double val) const override {
        for (auto const& [key, text] : m_specialValues) {
             if (qAbs(val - key) < 0.0001) return text;
        }
        return QDoubleSpinBox::textFromValue(val);
    }
    
    double valueFromText(const QString &text) const override {
        for (auto const& [key, valText] : m_specialValues) {
             if (text == valText) return key;
        }
        return QDoubleSpinBox::valueFromText(text);
    }
    
    QValidator::State validate(QString &input, int &pos) const override {
        double val = valueFromText(input);
        if (m_disabledValues.count(val)) return QValidator::Invalid;
        
        return QDoubleSpinBox::validate(input, pos);
    }
    
    void stepBy(int steps) override {
        QDoubleSpinBox::stepBy(steps);
        double newVal = value();
        
        bool disabled = false;
        for (double d : m_disabledValues) {
            if (qAbs(newVal - d) < 0.0001) {
                disabled = true;
                break;
            }
        }
        
        if (disabled) {
            // Try stepping once more
            QDoubleSpinBox::stepBy(steps > 0 ? 1 : -1);
        }
    }

private:
    std::map<double, QString> m_specialValues;
    std::set<double> m_disabledValues;
};

#endif // SMARTSPINBOX_H
