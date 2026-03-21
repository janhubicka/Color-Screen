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

  // Called by MainWindow after the optimizer finishes
  void setSpotResults(const std::vector<colorscreen::color_match> &results);

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
  QCheckBox *m_autoCheck             = nullptr;
  QLabel    *m_resultLabel           = nullptr;

  bool m_addSpotActive = false;
};

#endif // PROFILE_PANEL_H
