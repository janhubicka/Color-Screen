#ifndef SHARPNESS_PANEL_H
#define SHARPNESS_PANEL_H

#include "ParameterPanel.h"
#include "TaskQueue.h"
#include "AdaptiveSharpeningParameters.h"
#include "AdaptiveSharpeningChart.h"
#include <QPointer>
#include <functional>

class MTFChartWidget;
class QLabel;
class QCheckBox;
class QComboBox;
class QToolButton;
class QWidget;
class QImage;
class QVBoxLayout;
class FinetuneImagesPanel;

namespace colorscreen {
struct progress_info;
struct sharpen_parameters;
struct finetune_result;
} // namespace colorscreen

#include "TilePreviewPanel.h"

/** Document-owned MTF fit provenance hooks shared by primary and reference
    Sharpness panels. The panel owns the background operation; the document
    owns the meaning of Current/Stale across all views. */
struct MtfCalibrationCallbacks {
  std::function<QString()> summary;
  std::function<bool()> fitAvailable;
  std::function<bool(const colorscreen::mtf_parameters &)> fitStarted;
  std::function<void(const colorscreen::mtf_parameters &)> fitFailed;
  std::function<void(const colorscreen::mtf_parameters &, double)> fitAccepted;
  std::function<void()> fitFinishedWithoutResult;
};

class SharpnessPanel : public TilePreviewPanel {
  Q_OBJECT
public:
  explicit SharpnessPanel(StateGetter stateGetter, StateSetter stateSetter,
                          ImageGetter imageGetter,
                          MtfCalibrationCallbacks mtfCalibration = {},
                          QWidget *parent = nullptr);
  ~SharpnessPanel() override;

  // Accessors for Dock Widgets
  QWidget *getMTFChartWidget() const;

  // Methods to handle re-attaching
  void reattachMTFChart(QWidget *widget);
  void updateFinetuneImages(const colorscreen::finetune_result& result);
  void reattachFinetuneImages(QWidget *widget);
  void setFocusAnalysisChecked(bool checked);
  /** Update availability and summary text for automatic multi-area focus
      analysis owned by the current document. */
  void setFocusAreaAnalysisState(int candidateCount, bool running,
                                 const QString &summary = QString());
  void setMeasureMtfChecked(bool checked);
  void setMeasureMtfEnabled(bool enabled);
  /** Concise shared calibration state for this panel. */
  QString mtfCalibrationSummary() const;
  /** Refresh the panel-local status label from document-owned provenance. */
  void refreshMtfCalibrationStatus();
  /** Return whether this panel currently owns the document's active MTF fit. */
  bool mtfFitRunning() const { return m_mtfFitRunning; }

  void reattachDotSpread(QWidget *widget);
  void reattachAdaptiveChart(QWidget *widget);
  AdaptiveSharpeningChart *getAdaptiveChart() const;
  void showAdaptiveChart();

public slots:
  void onAnalyzeDisplacements();

signals:
  void adaptiveSharpeningRequested(const AdaptiveSharpeningParameters &parameters);
  void detachMTFChartRequested(QWidget *widget);
  void detachAdaptiveChartRequested(QWidget *widget);
  void detachDotSpreadRequested(QWidget *widget);
  void detachFinetuneImagesRequested(QWidget *widget);
  void autodetectRequested();
  void focusAnalysisRequested(bool checked, uint64_t flags);
  void findFocusAreasRequested();
  void analyzeFocusAreasRequested(uint64_t flags);
  /** Ask the application to open another scan used only as a slanted-edge
      sharpness reference while keeping the current document parameters. */
  void openSlantedEdgeReferenceRequested();
  void measureMtfRequested(bool checked);
  /** Select a stored measurement for provenance display in the owning image view. */
  void mtfMeasurementSelected(int index);
  /** Center the owning image view on the selected measurement ROI. */
  void mtfMeasurementLocateRequested(int index);

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
  /** Open the explicit fit dialog and run the selected MTF fit off the GUI
      thread.  */
  void fitMeasuredMtf();
  void updateMeasurementList();
  void updateMtfCalibrationStatus();
  void selectMtfMeasurement(int index);
  void updateSelectedMeasurementDetails();
  void onParametersRefreshed(const ParameterState &state) override;

  MTFChartWidget *m_mtfChart = nullptr;
  QCheckBox *m_showSignedOtfCheck = nullptr;
  class QLabel *m_diffractionNotice = nullptr;
  QVBoxLayout *m_mtfContainer = nullptr; // Container Layout
  QVBoxLayout *m_measurementsLayout = nullptr;
  QToolButton *m_scannerCameraSeparatorToggle = nullptr;
  QComboBox *m_measurementSelector = nullptr;
  QLabel *m_measurementDetailLabel = nullptr;
  class QPushButton *m_locateMeasurementBtn = nullptr;
  int m_selectedMtfMeasurement = -1;
  bool m_measurementUiInitialized = false;
  FinetuneImagesPanel *m_finetuneImagesPanel = nullptr;
  QWidget *m_finetuneImagesWrapper = nullptr;
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
  class QPushButton *m_findFocusAreasBtn = nullptr;
  class QPushButton *m_analyzeFocusAreasBtn = nullptr;
  class QLabel *m_focusAreaStatusLabel = nullptr;
  class QPushButton *m_measureMtfBtn = nullptr;
  class QPushButton *m_fitMtfBtn = nullptr;
  /** Queue dedicated to one-shot MTF fits so tile rendering stays responsive.  */
  TaskQueue m_mtfFitQueue;
  /** True while M_MTF_FIT_QUEUE is processing a submitted fit.  */
  bool m_mtfFitRunning = false;
  MtfCalibrationCallbacks m_mtfCalibration;
  QLabel *m_mtfFitStatusLabel = nullptr;
  uint64_t m_finetuneFlags = 0;
  AdaptiveSharpeningParameters m_adaptiveSharpeningParameters;
  bool m_adaptiveSharpeningParametersInitialized = false;
  class TilePreviewPanel *m_dotSpreadPanel = nullptr;
  QPointer<AdaptiveSharpeningChart> m_adaptiveChart;
  QWidget *m_adaptiveChartWrapper = nullptr;
  QVBoxLayout *m_adaptiveChartContainer = nullptr;
};

#endif // SHARPNESS_PANEL_H
