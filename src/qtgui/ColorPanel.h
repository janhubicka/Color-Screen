#ifndef COLOR_PANEL_H
#define COLOR_PANEL_H

#include "../libcolorscreen/include/render-parameters.h"
#include "TilePreviewPanel.h"
#include <QCheckBox>
#include <QComboBox>
#include <QWidget>
#include <vector>

class CIEChartWidget; // Forward declaration
class SpectraChartWidget;

class ColorPanel : public TilePreviewPanel {
  Q_OBJECT
public:
  explicit ColorPanel(StateGetter stateGetter, StateSetter stateSetter,
                      ImageGetter imageGetter, QWidget *parent = nullptr);
  ~ColorPanel() override;

  // Accessors for Dock Widgets
  QWidget *getSpectraChartWidget() const;
  void reattachSpectraChart(QWidget *widget);
  void reattachCorrectedTiles(QWidget *widget);

  QWidget *getGamutChartWidget() const;
  void reattachGamutChart(QWidget *widget);
  QWidget *getCorrectedGamutChartWidget() const;
  void reattachCorrectedGamutChart(QWidget *widget);

signals:
  void detachSpectraChartRequested(QWidget *widget);
  void detachCorrectedTilesRequested(QWidget *widget);
  void detachGamutChartRequested(QWidget *widget);
  void detachCorrectedGamutChartRequested(QWidget *widget);

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
  void updateGamutChart();
  void updateCorrectedGamutChart();
  void applyChange(std::function<void(ParameterState &)> modifier, const QString &description = QString()) override;

  // Cached parameters for change detection
  colorscreen::render_parameters m_lastRParams;
  int m_lastScrType = -1;

  QWidget *m_spectraSection = nullptr;
  SpectraChartWidget *m_spectraChart = nullptr;
  QVBoxLayout *m_spectraContainer = nullptr;
  QComboBox *m_spectraMode = nullptr;
  TilePreviewPanel *m_correctedPreview = nullptr;
  
  QWidget *m_gamutSection = nullptr;
  CIEChartWidget *m_gamutChart = nullptr;
  QVBoxLayout *m_gamutContainer = nullptr;
  QComboBox *m_gamutReferenceCombo = nullptr;
  void updateGamutReference();

  QWidget *m_correctedGamutSection = nullptr;
  CIEChartWidget *m_correctedGamutChart = nullptr;
  QVBoxLayout *m_correctedGamutContainer = nullptr;
  QComboBox *m_correctedGamutReferenceCombo = nullptr;
  void updateCorrectedGamutReference();
};

#endif // COLOR_PANEL_H
