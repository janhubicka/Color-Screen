#include "TilePreviewPanel.h"
#include "../libcolorscreen/include/scr-to-img.h"
#include <QDebug>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QScrollArea>
#include <QtConcurrent>

using namespace colorscreen;

namespace {
TileRenderResult
renderTilesGeneric(ParameterState state, int scanWidth, int scanHeight,
                   int generation, int tileSize, coord_t pixel_size,
                   std::vector<render_screen_tile_type> tileTypes,
                   std::shared_ptr<colorscreen::progress_info> progress) {
  TileRenderResult result;
  result.generation = generation;
  result.success = false;

  // Create tile parameters
  tile_parameters tile;
  tile.width = tileSize;
  tile.height = tileSize;
  tile.pixelbytes = 3;
  tile.rowstride = tileSize * 3; // 3 bytes per pixel (RGB)

  // Compute pixel size - passed in argument
  // scr_to_img scrToImgObj;
  // scrToImgObj.set_parameters(state.scrToImg, scanWidth, scanHeight);
  // coord_t pixel_size = scrToImgObj.pixel_size(scanWidth, scanHeight);

  result.tiles.resize(tileTypes.size());

  bool allSuccess = true;
  for (size_t i = 0; i < tileTypes.size(); ++i) {
    if (progress && progress->cancelled()) {
      allSuccess = false;
      break;
    }

    std::vector<uint8_t> pixels(tileSize * tileSize * 3);
    tile.pixels = pixels.data();

    colorscreen::scr_type type = state.scrToImg.type;
    // If panel does not require scan (e.g. ColorPanel), and type is Random
    // (default), force a valid screen type (e.g. Paget) so that
    // render_screen_tile does not abort. This allows visualizing dyes even
    // without a loaded image/screen detection.
    if (type == colorscreen::Random) {
      // We can't use requiresScan() here easily because this function is
      // static/free helper. But we pass 'state'. Wait, renderTilesGeneric is
      // free function. We need to pass requiresScan flag to it or handle it in
      // performTileRender.
    }

    if (render_screen_tile(tile, type, state.rparams, pixel_size, tileTypes[i],
                           progress.get())) {
      result.tiles[i] = QImage(pixels.data(), tileSize, tileSize,
                               tile.rowstride, QImage::Format_RGB888)
                            .copy();
    } else {
      allSuccess = false;
    }
  }

  result.success = allSuccess;
  return result;
}
} // namespace

TilePreviewPanel::TilePreviewPanel(StateGetter stateGetter,
                                   StateSetter stateSetter,
                                   ImageGetter imageGetter, QWidget *parent,
                                   bool useScrollArea)
    : ParameterPanel(stateGetter, stateSetter, imageGetter, parent,
                     useScrollArea) {

  // Initialize debounce timer
  m_updateTimer = new QTimer(this);
  m_updateTimer->setSingleShot(true);
  m_updateTimer->setInterval(30);
  connect(m_updateTimer, &QTimer::timeout, this,
          &TilePreviewPanel::performTileRender);

  // Initialize watcher
  m_tileWatcher = new QFutureWatcher<TileRenderResult>(this);
  connect(m_tileWatcher, &QFutureWatcher<TileRenderResult>::finished, this,
          [this]() {
            TileRenderResult result = m_tileWatcher->result();

            if (result.generation == m_tileGenerationCounter) {
              if (result.success) {
                if (result.tiles.size() == m_tileLabels.size()) {
                  for (size_t i = 0; i < m_tileLabels.size(); ++i) {
                    m_tileLabels[i]->setPixmap(
                        QPixmap::fromImage(result.tiles[i]));
                  }
                }
              } else {
                // If failed, reset last rendered size so we try again next time
                // (e.g. on resize or param change)
                m_lastRenderedTileSize = 0;
              }
            }

            if (m_tileProgress) {
              m_tileProgress.reset();
            }

            startNextRender();
          });
}

TilePreviewPanel::~TilePreviewPanel() = default;

void TilePreviewPanel::setupTiles(const QString &title) {
  auto types = getTileTypes();

  m_tilesContainer = new QWidget();
  QHBoxLayout *tilesLayout = new QHBoxLayout(m_tilesContainer);
  tilesLayout->setContentsMargins(0, 0, 0, 0);
  tilesLayout->setSpacing(5);

  // Install resize event filter
  class TileResizeEventFilter : public QObject {
    TilePreviewPanel *m_panel;

  public:
    TileResizeEventFilter(TilePreviewPanel *panel)
        : QObject(panel), m_panel(panel) {}

  protected:
    bool eventFilter(QObject *obj, QEvent *event) override {
      if (event->type() == QEvent::Resize) {
        m_panel->scheduleTileUpdate();
      }
      return QObject::eventFilter(obj, event);
    }
  };
  m_tilesContainer->installEventFilter(new TileResizeEventFilter(this));

  m_tileLabels.clear();
  for (const auto &pair : types) {
    QLabel *label = new QLabel();
    label->setScaledContents(false);
    label->setAlignment(Qt::AlignCenter);
    label->setMinimumSize(100, 100);
    label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_tileLabels.push_back(label);

    // Helper to create captioned tile
    auto createCaptionedTile = [](QLabel *imageLabel, const QString &caption) {
      QWidget *container = new QWidget();
      QVBoxLayout *layout = new QVBoxLayout(container);
      layout->setContentsMargins(0, 0, 0, 0);
      layout->setSpacing(2);

      layout->addWidget(imageLabel, 0, Qt::AlignHCenter);

      QLabel *captionLabel = new QLabel(caption);
      captionLabel->setAlignment(Qt::AlignCenter);

      layout->addWidget(captionLabel, 0, Qt::AlignHCenter);
      layout->addStretch(1);
      return container;
    };

    tilesLayout->addWidget(createCaptionedTile(label, pair.second), 1);
  }

  // Create layout container and detachable section
  m_tilesLayoutContainer = new QVBoxLayout();
  m_tilesLayoutContainer->setContentsMargins(0, 0, 0, 0);

  QWidget *detachableTiles =
      createDetachableSection(title, m_tilesContainer, [this]() {
        emit detachTilesRequested(m_tilesContainer);
      });
  detachableTiles->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

  // Hack: Rename title based on subclass?
  // Actually createDetachableSection uses "Tile Preview" here, but Sharpness
  // used "Sharpness Preview". We can allow overriding title later if needed, or
  // loop it into setupTiles("Title")? User requested: "On the top there add
  // three tiles in a dockable widget analogous to ones in Sharpness" "Sharpness
  // Preview" is the title in SharpnessPanel. "Tile Preview" is generic. We
  // might want to pass it.

  m_tilesLayoutContainer->addWidget(detachableTiles);

  QWidget *wrapper = new QWidget();
  wrapper->setLayout(m_tilesLayoutContainer);

  if (m_currentGroupForm)
    m_currentGroupForm->addRow(wrapper);
  else
    m_form->addRow(wrapper);

  // Update logic for visibility
  m_widgetStateUpdaters.push_back([this]() {
    ParameterState s = m_stateGetter();
    bool visible = isTileRenderingEnabled(s);

    for (int i = 0; i < m_tilesLayoutContainer->count(); ++i) {
      QWidget *w = m_tilesLayoutContainer->itemAt(i)->widget();
      if (w)
        w->setVisible(visible);
    }
  });
}

void TilePreviewPanel::onParametersRefreshed(const ParameterState &state) {
  scheduleTileUpdate();
}

void TilePreviewPanel::scheduleTileUpdate() { m_updateTimer->start(); }

void TilePreviewPanel::setDebounceInterval(int msec) {
  m_updateTimer->setInterval(msec);
}

void TilePreviewPanel::performTileRender() {
  if (m_tileLabels.empty())
    return;

  ParameterState state = m_stateGetter();
  std::shared_ptr<colorscreen::image_data> scan = m_imageGetter();

  if ((!scan && requiresScan()) || !isTileRenderingEnabled(state)) {
    for (auto l : m_tileLabels)
      l->clear();
    return;
  }

  // Calculate dynamic tile size
  int availableWidth = width();
  if (m_tilesContainer && m_tilesContainer->isVisible() &&
      m_tilesContainer->width() > 100) {
    availableWidth = m_tilesContainer->width() - 20;
  } else {
    QScrollArea *sa = findChild<QScrollArea *>();
    if (sa && sa->viewport()) {
      availableWidth = sa->viewport()->width();
    }
  }

  // Margins
  int margins = 0;
  if (m_form) {
    int l, t, r, b;
    m_form->getContentsMargins(&l, &t, &r, &b);
    margins = l + r;
  }
  int spacing = 10;
  int numTiles = m_tileLabels.size();
  if (numTiles == 0)
    numTiles = 1;

  int tileSize = qMax(64, (availableWidth - margins - spacing) / numTiles);

  for (auto l : m_tileLabels)
    l->setFixedSize(tileSize, tileSize);

  // Compute pixel size for check/render
  coord_t pixel_size = 1.0;
  if (scan) {
    scr_to_img scrToImgObj;
    scrToImgObj.set_parameters(state.scrToImg, scan->width, scan->height);
    pixel_size = scrToImgObj.pixel_size(scan->width, scan->height);
  }

  // Subclass check
  bool needsUpdate = shouldUpdateTiles(state);

  // Also check if tileSize changed significantly or first run
  // Also check if tileSize changed significantly or first run
  if (tileSize != m_lastRenderedTileSize) {
    needsUpdate = true;
    m_lastRenderedTileSize = tileSize;
  }

  // Force update if tiles are empty/not rendered yet
  if (!m_tileLabels.empty() && m_tileLabels[0]->pixmap().isNull()) {
    needsUpdate = true;
  }

  if (!needsUpdate)
    return;

  onTileUpdateScheduled();

  // Prepare types
  std::vector<render_screen_tile_type> types;
  for (const auto &p : getTileTypes())
    types.push_back(p.first);

  m_pendingRequest.state = state;
  if (scan) {
    m_pendingRequest.scanWidth = scan->width;
    m_pendingRequest.scanHeight = scan->height;
  } else {
    m_pendingRequest.scanWidth = 0;
    m_pendingRequest.scanHeight = 0;
  }
  m_pendingRequest.tileSize = tileSize;
  m_pendingRequest.pixelSize = pixel_size;
  m_pendingRequest.scan = scan;
  m_pendingRequest.tileTypes = types;
  m_hasPendingRequest = true;

  if (m_tileWatcher->isRunning()) {
    if (m_tileProgress)
      m_tileProgress->cancel();
    return;
  }

  startNextRender();
}

void TilePreviewPanel::startNextRender() {
  if (!m_hasPendingRequest)
    return;

  RenderRequest req = m_pendingRequest;
  m_hasPendingRequest = false;
  m_tileGenerationCounter++;
  int generation = m_tileGenerationCounter;

  m_tileProgress = std::make_shared<progress_info>();

  QFuture<TileRenderResult> future =
      QtConcurrent::run([req, generation, progress = m_tileProgress]() {
        return renderTilesGeneric(req.state, req.scanWidth, req.scanHeight,
                                  generation, req.tileSize, req.pixelSize,
                                  req.tileTypes, progress);
      });
  m_tileWatcher->setFuture(future);
}

void TilePreviewPanel::resizeEvent(QResizeEvent *event) {
  ParameterPanel::resizeEvent(event);
  if (m_updateTimer && event->size() != event->oldSize()) {
    scheduleTileUpdate();
  }
}

void TilePreviewPanel::showEvent(QShowEvent *event) {
  ParameterPanel::showEvent(event);
  scheduleTileUpdate();
}

QWidget *TilePreviewPanel::getTilesWidget() const { return m_tilesContainer; }

void TilePreviewPanel::reattachTiles(QWidget *widget) {
  if (widget != m_tilesContainer)
    return;

  if (m_tilesLayoutContainer && m_tilesLayoutContainer->count() > 0) {
    QWidget *section = m_tilesLayoutContainer->itemAt(0)->widget();
    if (section && section->layout()) {
      QLayoutItem *item =
          section->layout()->takeAt(section->layout()->count() - 1);
      if (item) {
        if (item->widget())
          delete item->widget();
        delete item;
      }
      section->layout()->addWidget(widget);
      widget->show();
      if (section->layout()->count() > 0) {
        QLayoutItem *headerItem = section->layout()->itemAt(0);
        if (headerItem && headerItem->widget())
          headerItem->widget()->show();
      }
    }
  }
}

bool TilePreviewPanel::isTileRenderingEnabled(
    const ParameterState &state) const {
  return state.scrToImg.type != scr_type::Random;
}
