#ifndef SHARPNESS_PANEL_H
#define SHARPNESS_PANEL_H

#include "ParameterPanel.h"

class MTFChartWidget;
class QLabel;
class QWidget;
class QImage;
class QTimer;
template <typename T> class QFutureWatcher;

namespace colorscreen {
struct progress_info;
struct sharpen_parameters;
} // namespace colorscreen

#include "TilePreviewPanel.h"

class SharpnessPanel : public TilePreviewPanel {
  Q_OBJECT
public:
  explicit SharpnessPanel(StateGetter stateGetter, StateSetter stateSetter,
                          ImageGetter imageGetter, QWidget *parent = nullptr);
  ~SharpnessPanel() override;

  // Accessors for Dock Widgets
  QWidget *getMTFChartWidget() const;

  // Methods to handle re-attaching
  void reattachMTFChart(QWidget *widget);

signals:
  void detachMTFChartRequested(QWidget *widget);
  void autodetectRequested();

protected:
  // TilePreviewPanel overrides
  std::vector<std::pair<colorscreen::render_screen_tile_type, QString>>
  getTileTypes() const override;
  bool shouldUpdateTiles(const ParameterState &state) override;
  void onTileUpdateScheduled() override;

private:
  void setupUi();
  void updateMTFChart();
  void updateScreenTiles(); // Wrapper to schedule
  void applyChange(std::function<void(ParameterState &)> modifier, const QString &description = QString()) override;
  void onParametersRefreshed(const ParameterState &state) override;

  MTFChartWidget *m_mtfChart = nullptr;
  QVBoxLayout *m_mtfContainer = nullptr; // Container Layout

  // Cached parameters for change detection (moved from private to be used in
  // shouldUpdateTiles)
  int m_lastTileSize = 0;
  double m_lastPixelSize = 0.0;
  int m_lastScrType = -1;
  colorscreen::sharpen_parameters m_lastSharpen;
  double m_lastRedStripWidth = 0.0;
  double m_lastGreenStripWidth = 0.0;
};

#endif // SHARPNESS_PANEL_H
