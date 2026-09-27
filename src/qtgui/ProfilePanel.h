#ifndef PROFILE_PANEL_H
#define PROFILE_PANEL_H

#include "ParameterPanel.h"
#include "../libcolorscreen/include/colorscreen.h"
#include <vector>

class QCheckBox;
class QLabel;
class QPushButton;

class ProfilePanel : public ParameterPanel {
  Q_OBJECT
public:
  ProfilePanel(StateGetter stateGetter, StateSetter stateSetter,
               ImageGetter imageGetter, QWidget *parent = nullptr);
  ~ProfilePanel() override;

  // Called by MainWindow after the optimizer finishes.
  void setSpotResults(const std::vector<colorscreen::color_match> &results);
  /** Synchronize the temporary Add spot toggle without emitting a request. */
  void setAddSpotChecked(bool checked);
  /** Mirror the active ordinary view's view-local spot visibility. */
  void setShowProfileSpots(bool show);
  /** Show document-owned profile calibration readiness/freshness. */
  void setCalibrationStatus(const QString &status);

signals:
  void optimizeColorRequested(bool autoMode);
  void addSpotModeRequested(bool active);
  void showProfileSpotsChanged(bool show);

protected:
  void onParametersRefreshed(const ParameterState &state) override;

private:
  void setupUi();
  bool isAutoEnabled() const;

  QCheckBox *m_showProfileSpotsCheck = nullptr;
  QLabel    *m_spotCountLabel        = nullptr;
  QPushButton *m_addSpotBtn          = nullptr;  // toggleable
  QPushButton *m_clearSpotsBtn        = nullptr;
  QCheckBox *m_autoCheck             = nullptr;
  QPushButton *m_optimizeBtn         = nullptr;
  QLabel    *m_prerequisiteLabel      = nullptr;
  QLabel    *m_calibrationStatusLabel = nullptr;
  QLabel    *m_resultLabel           = nullptr;
  std::vector<colorscreen::point_t> m_lastAutoSpots;
};

#endif // PROFILE_PANEL_H
