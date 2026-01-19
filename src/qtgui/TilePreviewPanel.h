#ifndef TILE_PREVIEW_PANEL_H
#define TILE_PREVIEW_PANEL_H

#include "../libcolorscreen/include/colorscreen.h"
#include "ParameterPanel.h"
#include "RenderQueue.h"
#include <QFutureWatcher>
#include <QImage>
#include <QLabel>
#include <QTimer>
#include <memory>
#include <vector>

namespace colorscreen {
struct progress_info;
}

struct TileRenderResult {
  std::vector<QImage> tiles;
  int generation = 0;
  bool success = false;
};

Q_DECLARE_METATYPE(TileRenderResult)

class TilePreviewPanel : public ParameterPanel {
  Q_OBJECT
public:
  TilePreviewPanel(StateGetter stateGetter, StateSetter stateSetter,
                   ImageGetter imageGetter, QWidget *parent = nullptr,
                   bool useScrollArea = true);
  ~TilePreviewPanel() override;

  QWidget *getTilesWidget() const;
  void reattachTiles(QWidget *widget);

signals:
  void detachTilesRequested(QWidget *widget);

protected:
  void resizeEvent(QResizeEvent *event) override;
  void showEvent(QShowEvent *event) override;

  void onParametersRefreshed(const ParameterState &state) override;
  
  void scheduleTileUpdate();
  
  // Call this to initialize the tiles UI
  void setupTiles(const QString &title);

  // Set the debounce interval (default 30ms)
  void setDebounceInterval(int msec);
  
signals:
  void progressStarted(std::shared_ptr<colorscreen::progress_info> progress);
  void progressFinished(std::shared_ptr<colorscreen::progress_info> progress);

private slots:
  void onTriggerRender(int reqId, std::shared_ptr<colorscreen::progress_info> progress);

  // Must be implemented by subclasses
  virtual std::vector<std::pair<colorscreen::render_screen_tile_type, QString>>
  getTileTypes() const = 0;
  virtual bool shouldUpdateTiles(const ParameterState &state) = 0;
  virtual void onTileUpdateScheduled() {
  } // Called when update logic decides to proceed (e.g. to update cache)

  virtual bool isTileRenderingEnabled(const ParameterState &state) const;

  // Return false if tiles can be rendered without a scan (e.g. using synthetic
  // patterns) Default is true.
  virtual bool requiresScan() const { return true; }

  // Call this to initialize the tiles UI
  // Call this to trigger an update (debounced)
  void onTileRenderFinished();
  // void scheduleTileUpdate(); // Moved to protected

  // Internal render function
  void performTileRender();

private:
  void startNextRender();

  QWidget *m_tilesContainer = nullptr;
  QVBoxLayout *m_tilesLayoutContainer = nullptr;
  std::vector<QLabel *> m_tileLabels;

  QTimer *m_updateTimer = nullptr;
  QFutureWatcher<TileRenderResult> *m_tileWatcher = nullptr;
  std::shared_ptr<colorscreen::progress_info> m_tileProgress;
  int m_tileGenerationCounter = 0;

  struct RenderRequest {
    ParameterState state;
    int scanWidth;
    int scanHeight;
    int tileSize;
    double pixelSize;
    std::shared_ptr<colorscreen::image_data> scan;
    std::vector<colorscreen::render_screen_tile_type> tileTypes;
  };
  RenderRequest m_pendingRequest;
  bool m_hasPendingRequest = false;

  int m_lastRenderedTileSize = 0;
  
  RenderQueue m_renderQueue;
};

#endif // TILE_PREVIEW_PANEL_H
