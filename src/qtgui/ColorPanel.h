#ifndef COLOR_PANEL_H
#define COLOR_PANEL_H

#include "../libcolorscreen/include/render-parameters.h"
#include "TilePreviewPanel.h"
#include <QCheckBox>
#include <QComboBox>
#include <QWidget>
#include <QPushButton>
#include <vector>

class CIEChartWidget; // Forward declaration
class SpectraChartWidget;
class ToneCurveWidget;

class ColorPanel : public TilePreviewPanel {
  Q_OBJECT
public:
  explicit ColorPanel(StateGetter stateGetter, StateSetter stateSetter,
                      ImageGetter imageGetter, QWidget *parent = nullptr);
  ~ColorPanel() override;

  void setNeutralAreaChecked(bool checked);
  void setNeutralAreaEnabled(bool enabled);

  void setAutoLevelsChecked(bool checked);
  void setAutoLevelsEnabled(bool enabled);

signals:
  void neutralAreaRequested();
  void autoLevelsRequested();

protected:
  // TilePreviewPanel overrides
  std::vector<std::pair<colorscreen::render_screen_tile_type, QString>>
  getTileTypes() const override;
  bool shouldUpdateTiles(const ParameterState &state) override;
  void onTileUpdateScheduled() override;
  bool isTileRenderingEnabled(const ParameterState &state) const override;
  bool requiresScan() const override { return false; }
  void resizeEvent(QResizeEvent* event) override;

private:
  void setupUi();
  void updateSpectraChart();
  void applyChange(std::function<void(ParameterState &)> modifier,
                   const QString &description = QString(),
                   const QString &parameterKey = QString()) override;

  // Cached parameters for change detection
  colorscreen::render_parameters m_lastRParams;
  int m_lastScrType = -1;

  SpectraChartWidget *m_spectraChart = nullptr;
  QComboBox *m_spectraMode = nullptr;

  QPushButton *m_setNeutralAreaBtn = nullptr;
  QPushButton *m_setAutoLevelsBtn = nullptr;

  // Historical color-screen-only groups. Generic black/backlight/final
  // appearance controls remain visible for ordinary captures.
  QWidget *m_screenDyesGroup = nullptr;
  QWidget *m_viewingCorrectionGroup = nullptr;

  struct GamutChartGroup {
    CIEChartWidget *chart = nullptr;
    QComboBox *referenceCombo = nullptr;
    QWidget *section = nullptr;
    QVBoxLayout *container = nullptr;
    bool corrected;
  };

  void initGamutGroup(GamutChartGroup &group, const QString &name,
                      bool corrected);
  void updateGamutGroup(GamutChartGroup &group);
  void updateGamutReference(GamutChartGroup &group);

  GamutChartGroup m_gamutGroup;
  GamutChartGroup m_correctedGamutGroup;

  ToneCurveWidget *m_toneCurveWidget = nullptr;
  QComboBox *m_toneCurveCoordCombo = nullptr;
};

#endif // COLOR_PANEL_H
