#include "FileRenderController.h"

#include <QFile>
#include <QFuture>
#include <QFutureWatcher>
#include <QtConcurrent>

#include <algorithm>
#include <exception>
#include <string>
#include <utility>

/** Bind document presentation/lifetime callbacks once. */
void FileRenderController::configure(Callbacks callbacks) {
  Q_ASSERT(!m_configured);
  m_callbacks = std::move(callbacks);
  m_configured = true;
}

/** Return whether the owning document is closing. */
bool FileRenderController::isClosing() const {
  return m_callbacks.isClosing && m_callbacks.isClosing();
}

/** Return whether INFO belongs to an active file-render job. */
bool FileRenderController::ownsProgress(
    const std::shared_ptr<colorscreen::progress_info> &info) const {
  if (!info)
    return false;
  return std::find(m_activeProgresses.begin(), m_activeProgresses.end(), info) !=
         m_activeProgresses.end();
}

/** Forget one completed progress identity. */
void FileRenderController::removeActiveProgress(
    const std::shared_ptr<colorscreen::progress_info> &progress) {
  m_activeProgresses.erase(
      std::remove(m_activeProgresses.begin(), m_activeProgresses.end(),
                  progress),
      m_activeProgresses.end());
}

/** Request cooperative cancellation of every active file render. */
void FileRenderController::cancelAll() {
  for (const auto &progress : m_activeProgresses)
    if (progress)
      progress->cancel();
}

/** Start REQUEST in the global thread pool. */
void FileRenderController::start(Request request) {
  Q_ASSERT(m_configured);
  if (isClosing() || !request.scan || request.outputPath.isEmpty())
    return;

  auto progress = std::make_shared<colorscreen::progress_info>();
  m_activeProgresses.push_back(progress);
  if (m_callbacks.addProgress)
    m_callbacks.addProgress(progress, request.progressTitle);

  const QString outputPath = request.outputPath;
  const std::string outputPathStd = outputPath.toStdString();
  auto *watcher = new QFutureWatcher<bool>(this);
  connect(watcher, &QFutureWatcher<bool>::finished, this,
          [this, watcher, progress, outputPath]() {
            bool success = false;
            try {
              success = watcher->result();
            } catch (const std::exception &) {
              success = false;
            } catch (...) {
              success = false;
            }

            const bool cancelled = progress->pool_cancel();
            removeActiveProgress(progress);
            watcher->deleteLater();

            if (cancelled || !success) {
              if (QFile::exists(outputPath))
                QFile::remove(outputPath);
            }

            if (isClosing())
              return;
            if (m_callbacks.removeProgress)
              m_callbacks.removeProgress(progress);
            if (m_callbacks.finished)
              m_callbacks.finished(outputPath, success, cancelled);
          });

  QFuture<bool> future = QtConcurrent::run(
      [request = std::move(request), outputPathStd, progress]() mutable {
        colorscreen::render_to_file_params params;
        params.filename = outputPathStd.c_str();
        params.verbose = false;
        params.dng = request.dng;
        params.hdr = request.hdr;
        params.depth = request.depth;
        params.geometry = request.geometry;
        params.antialias = request.antialias;
        params.scale = request.scale;
        params.screen_scale = request.screenScale;
        params.width = request.width;
        params.height = request.height;

        const char *error = nullptr;
        return colorscreen::render_to_file(
            *request.scan, request.scrParams, request.detectParams,
            request.renderParams, params, request.renderType, progress.get(),
            &error);
      });

  watcher->setFuture(future);
}
