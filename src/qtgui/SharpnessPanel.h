#ifndef SHARPNESS_PANEL_H
#define SHARPNESS_PANEL_H

#include "ParameterPanel.h"

class MTFChartWidget;
class QLabel;
class QWidget;
class QImage;
class QTimer;
class QVBoxLayout;
class FinetuneImagesPanel;
template <typename T> class QFutureWatcher;

namespace colorscreen {
struct progress_info;
struct sharpen_parameters;
struct finetune_result;
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
  void updateFinetuneImages(const colorscreen::finetune_result& result);
  void reattachFinetuneImages(QWidget *widget);
  void setFocusAnalysisChecked(bool checked);

signals:
  void detachMTFChartRequested(QWidget *widget);
  void detachFinetuneImagesRequested(QWidget *widget);
  void autodetectRequested();
  void focusAnalysisRequested(bool checked, uint64_t flags);

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
  void loadMTF();
  void updateMeasurementList();
  void onParametersRefreshed(const ParameterState &state) override;

  MTFChartWidget *m_mtfChart = nullptr;
  class QLabel *m_diffractionNotice = nullptr;
  QVBoxLayout *m_mtfContainer = nullptr; // Container Layout
  QVBoxLayout *m_measurementsLayout = nullptr;
  FinetuneImagesPanel *m_finetuneImagesPanel = nullptr;
  QVBoxLayout *m_finetuneImagesContainer = nullptr;
  std::vector<colorscreen::mtf_measurement> m_lastMeasurements;

  // Cached parameters for change detection (moved from private to be used in
  // shouldUpdateTiles)
  int m_lastTileSize = 0;
  double m_lastPixelSize = 0.0;
  int m_lastScrType = -1;
  colorscreen::sharpen_parameters m_lastSharpen;
  double m_lastRedStripWidth = 0.0;
  double m_lastGreenStripWidth = 0.0;
  class QPushButton *m_analyzeAreaBtn = nullptr;
  uint64_t m_finetuneFlags = 0;
};

#endif // SHARPNESS_PANEL_H
