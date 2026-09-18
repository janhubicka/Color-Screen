#pragma once

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <optional>

/** Convert between a numeric value and its slider position.

    Callers can share identical linear, gamma, and logarithmic geometry while
    choosing their UI resolution. A separated stored sentinel reserves one
    slider position below the regular range without changing the mapping. */
class SliderValueMapping {
public:
  static constexpr int defaultNonlinearSliderMaximum = 65535;

  SliderValueMapping(
      double minimum, double maximum, double scale, double gamma,
      bool logarithmic,
      std::optional<double> specialMinimumValue = std::nullopt,
      int nonlinearSliderMaximum = defaultNonlinearSliderMaximum)
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
                                            : m_regularLinearSliderMin - 1),
        m_nonlinearSliderMaximum(nonlinearSliderMaximum) {
    Q_ASSERT(scale > 0);
    Q_ASSERT(!specialMinimumValue.has_value() ||
             *specialMinimumValue <= minimum);
    Q_ASSERT(!m_nonlinear ||
             m_nonlinearSliderMaximum > m_regularSliderMin);
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
    return m_nonlinear ? m_nonlinearSliderMaximum
                       : m_regularLinearSliderMax;
  }

  double sliderToValue(int sliderValue) const {
    if (m_hasSeparatedSpecialMinimum &&
        sliderValue == m_specialSliderPosition)
      return m_specialStateValue;
    if (!m_nonlinear)
      return static_cast<double>(sliderValue) / m_scale;

    const double t = static_cast<double>(sliderValue - m_regularSliderMin) /
                     (m_nonlinearSliderMaximum - m_regularSliderMin);
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
        qRound(t * (m_nonlinearSliderMaximum - m_regularSliderMin) +
               m_regularSliderMin),
        m_regularSliderMin, m_nonlinearSliderMaximum);
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
  int m_nonlinearSliderMaximum;
};
