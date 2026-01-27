#ifndef TILE_PREVIEW_PANEL_H
#define TILE_PREVIEW_PANEL_H

#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/colorscreen.h"
#include "ParameterPanel.h"
#include "TaskQueue.h"
#include <QFutureWatcher>
#include <QWidget>
#include <QLabel>
#include <QTimer>
#include <memory>
#include <vector>
#include <QMap> 

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
  struct RenderRequest {
    ParameterState state;
    int scanWidth;
    int scanHeight;
    int tileSize;
    double pixelSize;
    std::shared_ptr<colorscreen::image_data> scan;
    std::vector<colorscreen::render_screen_tile_type> tileTypes;
  };

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
  
  void setupTiles(const QString &title);

  void setDebounceInterval(int msec);
  
signals:
  void progressStarted(std::shared_ptr<colorscreen::progress_info> progress);
  void progressFinished(std::shared_ptr<colorscreen::progress_info> progress);

private slots:
  void onTriggerRender(int reqId, std::shared_ptr<colorscreen::progress_info> progress, const QVariant &userData);

  virtual std::vector<std::pair<colorscreen::render_screen_tile_type, QString>>
  getTileTypes() const = 0;
  virtual bool shouldUpdateTiles(const ParameterState &state) = 0;
  virtual void onTileUpdateScheduled() {}

  virtual bool isTileRenderingEnabled(const ParameterState &state) const;

  virtual bool requiresScan() const { return true; }

  void onTileRenderFinished();
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

  int m_lastRenderedTileSize = 0;
  TaskQueue m_renderQueue;
};

#endif // TILE_PREVIEW_PANEL_H
