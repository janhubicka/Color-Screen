#ifndef SCREEN_PANEL_H
#define SCREEN_PANEL_H

#include "ParameterPanel.h"

class QComboBox;
class TilePreviewPanel;
namespace colorscreen { struct progress_info; } // namespace colorscreen

/** Create the icon-populated screen selector shared by the Screen panel and
    initial setup. REGULARONLY filters stochastic/no-screen entries.
    PLACEHOLDER, when non-empty, is inserted first with NoScreen data. */
QComboBox *createScreenTypeComboBox(QWidget *parent = nullptr,
                                    bool regularOnly = false,
                                    const QString &placeholder = QString());

class ScreenPanel : public ParameterPanel {
  Q_OBJECT
public:
  ScreenPanel(StateGetter stateGetter, StateSetter stateSetter,
              ImageGetter imageGetter, QWidget *parent = nullptr);
  ~ScreenPanel() override;

signals:
  void progressStarted(std::shared_ptr<colorscreen::progress_info> progress);
  void progressFinished(std::shared_ptr<colorscreen::progress_info> progress);
  // Request discovery/refinement of a regular screen lattice. Stochastic
  // screen-colour detection is handled by the screen-detection render modes.
  void autodetectRequested();
  // Request the alternate symmetric colour assignment for a configured lattice.
  void alternateColorsRequested();

private:
  void setupUi();
};

#endif // SCREEN_PANEL_H
