#!/usr/bin/env python3
"""Replace TilePreviewPanel's QtConcurrent handoff with project synchronization."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def replace_once(relative, old, new):
    path = ROOT / relative
    text = path.read_text()
    if new in text:
        print(f"{relative}: already patched")
        return
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{relative}: expected one match, found {count}")
    path.write_text(text.replace(old, new))
    print(f"{relative}: patched")


replace_once(
    "src/qtgui/TilePreviewPanel.h",
    '''#include <QTimer>\n#include <memory>\n#include <vector>\n#include <QMap> \n''',
    '''#include <QTimer>\n#include <condition_variable>\n#include <cstddef>\n#include <memory>\n#include <mutex>\n#include <unordered_map>\n#include <vector>\n#include <QMap> \n''',
)

replace_once(
    "src/qtgui/TilePreviewPanel.h",
    '''private slots:\n  void onTriggerRender(int reqId, std::shared_ptr<colorscreen::progress_info> progress, const QVariant &userData);\n\n  virtual std::vector<std::pair<colorscreen::render_screen_tile_type, QString>>\n''',
    '''private slots:\n  void onTriggerRender(int reqId, std::shared_ptr<colorscreen::progress_info> progress, const QVariant &userData);\n  void finishTileRender(int reqId);\n\n  virtual std::vector<std::pair<colorscreen::render_screen_tile_type, QString>>\n''',
)

replace_once(
    "src/qtgui/TilePreviewPanel.h",
    '''  int m_lastRenderedTileSize = 0;\n  TaskQueue m_renderQueue;\n''',
    '''  int m_lastRenderedTileSize = 0;\n  TaskQueue m_renderQueue;\n\n  std::mutex m_workerMutex;\n  std::condition_variable m_workerCondition;\n  std::unordered_map<int, TileRenderResult> m_completedRenders;\n  std::size_t m_activeRenderWorkers = 0;\n  bool m_destroying = false;\n''',
)

replace_once(
    "src/qtgui/TilePreviewPanel.cpp",
    '''#include "TilePreviewPanel.h"\n#include "../libcolorscreen/include/scr-to-img.h"\n''',
    '''#include "TilePreviewPanel.h"\n#include "SynchronizedRunnable.h"\n#include "../libcolorscreen/include/scr-to-img.h"\n''',
)

replace_once(
    "src/qtgui/TilePreviewPanel.cpp",
    '''#include <QScrollArea>\n#include <QtConcurrent>\n#include <QColorSpace>\n''',
    '''#include <QScrollArea>\n#include <QColorSpace>\n''',
)

replace_once(
    "src/qtgui/TilePreviewPanel.cpp",
    '''TilePreviewPanel::~TilePreviewPanel() {\n  // Stop any pending update timer and disconnect it\n''',
    '''TilePreviewPanel::~TilePreviewPanel() {\n  {\n    std::lock_guard<std::mutex> locker(m_workerMutex);\n    m_destroying = true;\n  }\n\n  // Stop any pending update timer and disconnect it\n''',
)

replace_once(
    "src/qtgui/TilePreviewPanel.cpp",
    '''  // Cancel any running or pending render tasks\n  m_renderQueue.cancelAll();\n  \n  // Explicitly delete UI children created by this class\n''',
    '''  // Cancel any running or pending render tasks, then keep this QObject\n  // alive until every project-published QThreadPool worker has returned.\n  m_renderQueue.cancelAll();\n  {\n    std::unique_lock<std::mutex> locker(m_workerMutex);\n    m_workerCondition.wait(\n        locker, [this]() { return m_activeRenderWorkers == 0; });\n    m_completedRenders.clear();\n  }\n  \n  // Explicitly delete UI children created by this class\n''',
)

old_launch = '''  // Start background render\n  QFuture<TileRenderResult> future = QtConcurrent::run(\n        renderTilesGeneric,\n        req.state,\n        req.scanWidth,\n        req.scanHeight,\n        reqId, // Pass reqId as generation\n        req.tileSize,\n        req.pixelSize,\n        req.tileTypes,\n        progress\n  );\n\n  // Monitor it\n  // We create a new watcher for each job to support concurrency handled by queue\n  QFutureWatcher<TileRenderResult> *watcher = new QFutureWatcher<TileRenderResult>(this);\n  connect(watcher, &QFutureWatcher<TileRenderResult>::finished, this, [this, watcher, reqId](){\n      TileRenderResult result = watcher->result();\n\n      // Update UI if successful\n      if (result.success) {\n          if (result.tiles.size() == m_tileLabels.size()) {\n              for (size_t i = 0; i < m_tileLabels.size(); ++i) {\n                  const QImage& img = result.tiles[i];\n                  if (!img.isNull())\n                      m_tileLabels[i]->setPixmap(QPixmap::fromImage(img));\n              }\n          }\n      } else {\n          // Failed or cancelled\n          m_lastRenderedTileSize = 0; // Force retry\n      }\n\n      m_renderQueue.reportFinished(reqId, result.success);\n\n      watcher->deleteLater();\n  });\n\n  watcher->setFuture(future);\n}\n'''

new_launch = '''  {\n    std::lock_guard<std::mutex> locker(m_workerMutex);\n    ++m_activeRenderWorkers;\n  }\n\n  runSynchronized(\n      [this, state = std::move(req.state), scanWidth = req.scanWidth,\n       scanHeight = req.scanHeight, reqId, tileSize = req.tileSize,\n       pixelSize = req.pixelSize, tileTypes = std::move(req.tileTypes),\n       progress = std::move(progress)]() mutable {\n        TileRenderResult result = renderTilesGeneric(\n            std::move(state), scanWidth, scanHeight, reqId, tileSize, pixelSize,\n            std::move(tileTypes), std::move(progress));\n\n        std::lock_guard<std::mutex> locker(m_workerMutex);\n        m_completedRenders.insert_or_assign(reqId, std::move(result));\n        if (!m_destroying) {\n          // Publish only the integer request id through uninstrumented Qt.\n          QMetaObject::invokeMethod(this, "finishTileRender",\n                                    Qt::QueuedConnection, Q_ARG(int, reqId));\n        }\n        if (m_activeRenderWorkers > 0)\n          --m_activeRenderWorkers;\n        m_workerCondition.notify_all();\n      });\n}\n\nvoid TilePreviewPanel::finishTileRender(int reqId) {\n  TileRenderResult result;\n  {\n    std::lock_guard<std::mutex> locker(m_workerMutex);\n    auto it = m_completedRenders.find(reqId);\n    if (it == m_completedRenders.end())\n      return;\n    result = std::move(it->second);\n    m_completedRenders.erase(it);\n  }\n\n  if (result.success) {\n    if (result.tiles.size() == m_tileLabels.size()) {\n      for (size_t i = 0; i < m_tileLabels.size(); ++i) {\n        const QImage &img = result.tiles[i];\n        if (!img.isNull())\n          m_tileLabels[i]->setPixmap(QPixmap::fromImage(img));\n      }\n    }\n  } else {\n    // Failed or cancelled: force a retry on the next update.\n    m_lastRenderedTileSize = 0;\n  }\n\n  m_renderQueue.reportFinished(reqId, result.success);\n}\n'''

replace_once("src/qtgui/TilePreviewPanel.cpp", old_launch, new_launch)
