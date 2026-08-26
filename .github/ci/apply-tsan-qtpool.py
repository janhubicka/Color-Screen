#!/usr/bin/env python3
"""Replace QtConcurrent smoke-path handoffs with TSan-visible QThreadPool tasks."""

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
    "src/qtgui/Renderer.h",
    '''#include <atomic>\n#include <memory>\n#include <mutex>\n#include <unordered_map>\n#include <utility>\n#include <QFuture>\n#include <QList>\n''',
    '''#include <condition_variable>\n#include <cstddef>\n#include <memory>\n#include <mutex>\n#include <unordered_map>\n#include <utility>\n''',
)

replace_once(
    "src/qtgui/Renderer.h",
    '''    std::unordered_map<int, RenderRequest> m_pendingRenders;\n\n    // Futures of active rendering tasks; only touched by the renderer thread.\n    QList<QFuture<void>> m_activeFutures;\n''',
    '''    std::unordered_map<int, RenderRequest> m_pendingRenders;\n\n    void finishRenderTask();\n    std::mutex m_activeMutex;\n    std::condition_variable m_activeCondition;\n    std::size_t m_activeTasks = 0;\n''',
)

replace_once(
    "src/qtgui/Renderer.cpp",
    '''#include <QImage>\n#include <QtConcurrent>\n#include "Logging.h"\n''',
    '''#include <QImage>\n#include "Logging.h"\n#include "SynchronizedRunnable.h"\n''',
)

replace_once(
    "src/qtgui/Renderer.cpp",
    '''Renderer::~Renderer()\n{\n    // Wait for all running tasks to complete before destruction.\n    for (const auto &future : m_activeFutures) {\n        if (!future.isFinished()) {\n            const_cast<QFuture<void> &>(future).waitForFinished();\n        }\n    }\n}\n''',
    '''Renderer::~Renderer()\n{\n    // Render tasks use this object while emitting their completion signal.\n    // Keep it alive until every project-published QThreadPool task has left.\n    std::unique_lock<std::mutex> locker(m_activeMutex);\n    m_activeCondition.wait(locker, [this]() { return m_activeTasks == 0; });\n}\n''',
)

replace_once(
    "src/qtgui/Renderer.cpp",
    '''void Renderer::render(int reqId)\n{\n''',
    '''void Renderer::finishRenderTask()\n{\n    std::lock_guard<std::mutex> locker(m_activeMutex);\n    if (m_activeTasks > 0)\n        --m_activeTasks;\n    m_activeCondition.notify_all();\n}\n\nvoid Renderer::render(int reqId)\n{\n''',
)

replace_once(
    "src/qtgui/Renderer.cpp",
    '''    // Clean up finished futures to prevent unbounded growth.\n    m_activeFutures.removeIf(\n        [](const QFuture<void> &future) { return future.isFinished(); });\n\n    QFuture<void> future = QtConcurrent::run(\n        [this, reqId, xOffset, yOffset, scale, width, height, coordinateSpace,\n''',
    '''    {\n        std::lock_guard<std::mutex> locker(m_activeMutex);\n        ++m_activeTasks;\n    }\n\n    runSynchronized(\n        [this, reqId, xOffset, yOffset, scale, width, height, coordinateSpace,\n''',
)

replace_once(
    "src/qtgui/Renderer.cpp",
    '''         frameParams = std::move(frameParams), progress = std::move(progress),\n         taskName, scrToImg, scrDetect, renderType]() mutable {\n            if (progress && progress->cancel_requested()) {\n''',
    '''         frameParams = std::move(frameParams), progress = std::move(progress),\n         taskName, scrToImg, scrDetect, renderType]() mutable {\n            struct CompletionGuard {\n                Renderer *renderer;\n                ~CompletionGuard() { renderer->finishRenderTask(); }\n            } completion{this};\n\n            if (progress && progress->cancel_requested()) {\n''',
)

replace_once(
    "src/qtgui/Renderer.cpp",
    '''        });\n\n    m_activeFutures.append(future);\n}\n''',
    '''        });\n}\n''',
)

replace_once(
    "src/qtgui/ImageViewWindow.h",
    '''#include <QString>\n#include <memory>\n#include <vector>\n''',
    '''#include <QString>\n#include <condition_variable>\n#include <memory>\n#include <mutex>\n#include <vector>\n''',
)

replace_once(
    "src/qtgui/ImageViewWindow.h",
    '''  /** Consume a selected rectangle when reference MTF measurement is active. */\n  void onReferenceAreaSelected(QRect area);\n\nprivate:\n''',
    '''  /** Consume a selected rectangle when reference MTF measurement is active. */\n  void onReferenceAreaSelected(QRect area);\n\n  /** Consume the mutex-published result of an asynchronous reference load. */\n  void finishReferenceLoad();\n\nprivate:\n''',
)

replace_once(
    "src/qtgui/ImageViewWindow.h",
    '''  bool m_referenceLoadPending = false;\n  QString m_referenceFile;\n\n  std::shared_ptr<colorscreen::image_data> m_scan;\n''',
    '''  bool m_referenceLoadPending = false;\n  QString m_referenceFile;\n  std::mutex m_referenceLoadMutex;\n  std::condition_variable m_referenceLoadCondition;\n  bool m_referenceWorkerActive = false;\n  bool m_referenceLoadOk = false;\n  QString m_referenceLoadError;\n  std::shared_ptr<colorscreen::image_data> m_pendingReferenceScan;\n\n  std::shared_ptr<colorscreen::image_data> m_scan;\n''',
)

replace_once(
    "src/qtgui/ImageViewWindow.cpp",
    '''#include "SlantedEdgeDialog.h"\n\n#include <QAction>\n''',
    '''#include "SlantedEdgeDialog.h"\n#include "SynchronizedRunnable.h"\n\n#include <QAction>\n''',
)

replace_once(
    "src/qtgui/ImageViewWindow.cpp",
    '''#include <QFileInfo>\n#include <QFutureWatcher>\n#include <QIcon>\n''',
    '''#include <QFileInfo>\n#include <QIcon>\n''',
)

replace_once(
    "src/qtgui/ImageViewWindow.cpp",
    '''#include <QVariant>\n#include <QtConcurrent>\n\n#include <algorithm>\n''',
    '''#include <QVariant>\n\n#include <algorithm>\n''',
)

replace_once(
    "src/qtgui/ImageViewWindow.cpp",
    '''/** Destroy a secondary view without taking the document-owned inspector with it. */\nImageViewWindow::~ImageViewWindow() { releaseDocumentInspector(); }\n''',
    '''/** Destroy a secondary view without taking the document-owned inspector with it. */\nImageViewWindow::~ImageViewWindow() {\n  {\n    std::unique_lock<std::mutex> locker(m_referenceLoadMutex);\n    m_referenceLoadCondition.wait(\n        locker, [this]() { return !m_referenceWorkerActive; });\n  }\n  releaseDocumentInspector();\n}\n''',
)

old_load = '''/** Load the reference scan asynchronously without touching document filenames. */\nvoid ImageViewWindow::loadReferenceImage(const QString &fileName) {\n  if (!m_slantedEdgeReference || fileName.isEmpty() || m_referenceLoadPending)\n    return;\n\n  m_referenceLoadPending = true;\n  m_referenceFile = QFileInfo(fileName).absoluteFilePath();\n  statusBar()->showMessage(tr("Opening slanted-edge reference…"));\n\n  const colorscreen::image_data::demosaicing_t demosaic =\n      m_document ? m_document->documentStateSnapshot().rparams.demosaic\n                 : colorscreen::image_data::demosaic_none;\n  auto scan = std::make_shared<colorscreen::image_data>();\n  auto progress = std::make_shared<colorscreen::progress_info>();\n  progress->set_task("Opening slanted edge reference", 0);\n  auto *watcher = new QFutureWatcher<std::pair<bool, QString>>(this);\n  connect(watcher, &QFutureWatcher<std::pair<bool, QString>>::finished, this,\n          [this, watcher, scan]() {\n            const auto result = watcher->result();\n            watcher->deleteLater();\n            m_referenceLoadPending = false;\n            if (!result.first) {\n              QMessageBox::critical(\n                  this, tr("Error Loading Slanted Edge Reference"),\n                  result.second.isEmpty() ? tr("Failed to load image.")\n                                          : result.second);\n              close();\n              return;\n            }\n\n            m_scan = scan;\n            rebuildModeList();\n            updateViewControls();\n            updateImageParameters(true);\n            if (m_sharpnessPanel)\n              m_sharpnessPanel->updateUI();\n            setWindowTitle(tr("%1 — Slanted edge reference")\n                               .arg(QFileInfo(m_referenceFile).fileName()));\n            statusBar()->showMessage(\n                tr("Slanted-edge reference — sharpness parameters are shared"));\n          });\n\n  const QString path = m_referenceFile;\n  QFuture<std::pair<bool, QString>> future = QtConcurrent::run(\n      [scan, path, progress, demosaic]() {\n        const char *error = nullptr;\n        colorscreen::sub_task task(progress.get());\n        const bool ok = scan->load(path.toUtf8().constData(), true, &error,\n                                   progress.get(), demosaic);\n        return std::make_pair(ok,\n                              !ok && error ? QString::fromUtf8(error)\n                                           : QString());\n      });\n  watcher->setFuture(future);\n}\n'''

new_load = '''/** Load the reference scan asynchronously without touching document filenames. */\nvoid ImageViewWindow::loadReferenceImage(const QString &fileName) {\n  if (!m_slantedEdgeReference || fileName.isEmpty() || m_referenceLoadPending)\n    return;\n\n  m_referenceLoadPending = true;\n  m_referenceFile = QFileInfo(fileName).absoluteFilePath();\n  statusBar()->showMessage(tr("Opening slanted-edge reference…"));\n\n  const colorscreen::image_data::demosaicing_t demosaic =\n      m_document ? m_document->documentStateSnapshot().rparams.demosaic\n                 : colorscreen::image_data::demosaic_none;\n  auto scan = std::make_shared<colorscreen::image_data>();\n  auto progress = std::make_shared<colorscreen::progress_info>();\n  progress->set_task("Opening slanted edge reference", 0);\n  const QString path = m_referenceFile;\n\n  {\n    std::lock_guard<std::mutex> locker(m_referenceLoadMutex);\n    m_referenceWorkerActive = true;\n    m_referenceLoadOk = false;\n    m_referenceLoadError.clear();\n    m_pendingReferenceScan.reset();\n  }\n\n  runSynchronized([this, scan, path, progress, demosaic]() {\n    const char *error = nullptr;\n    colorscreen::sub_task task(progress.get());\n    const bool ok = scan->load(path.toUtf8().constData(), true, &error,\n                               progress.get(), demosaic);\n    const QString message =\n        !ok && error ? QString::fromUtf8(error) : QString();\n\n    {\n      std::lock_guard<std::mutex> locker(m_referenceLoadMutex);\n      m_referenceLoadOk = ok;\n      m_referenceLoadError = message;\n      m_pendingReferenceScan = scan;\n    }\n\n    // The queued Qt call carries no non-trivial payload.  The result itself is\n    // published through m_referenceLoadMutex, which TSan can observe.\n    QMetaObject::invokeMethod(this, "finishReferenceLoad",\n                              Qt::QueuedConnection);\n\n    {\n      std::lock_guard<std::mutex> locker(m_referenceLoadMutex);\n      m_referenceWorkerActive = false;\n    }\n    m_referenceLoadCondition.notify_all();\n  });\n}\n\nvoid ImageViewWindow::finishReferenceLoad() {\n  std::shared_ptr<colorscreen::image_data> scan;\n  bool ok = false;\n  QString error;\n  {\n    std::lock_guard<std::mutex> locker(m_referenceLoadMutex);\n    ok = m_referenceLoadOk;\n    error = m_referenceLoadError;\n    scan = std::move(m_pendingReferenceScan);\n  }\n\n  m_referenceLoadPending = false;\n  if (!ok) {\n    QMessageBox::critical(this, tr("Error Loading Slanted Edge Reference"),\n                          error.isEmpty() ? tr("Failed to load image.")\n                                          : error);\n    close();\n    return;\n  }\n\n  m_scan = std::move(scan);\n  rebuildModeList();\n  updateViewControls();\n  updateImageParameters(true);\n  if (m_sharpnessPanel)\n    m_sharpnessPanel->updateUI();\n  setWindowTitle(tr("%1 — Slanted edge reference")\n                     .arg(QFileInfo(m_referenceFile).fileName()));\n  statusBar()->showMessage(\n      tr("Slanted-edge reference — sharpness parameters are shared"));\n}\n'''

replace_once("src/qtgui/ImageViewWindow.cpp", old_load, new_load)
