#include "FileRenderController.h"

#include <QFile>
#include <QFuture>
#include <QFutureWatcher>
#include <QtConcurrent>

#include <algorithm>
#include <exception>
#include <string>
#include <utility>

/** Cancel and join any worker that outlives its owning document. */
FileRenderController::~FileRenderController() { shutdown(); }

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
  return std::any_of(
      m_activeJobs.begin(), m_activeJobs.end(),
      [&info](const ActiveJob &job) { return job.progress == info; });
}

/** Forget one completed render job. */
void FileRenderController::removeActiveJob(
    const std::shared_ptr<colorscreen::progress_info> &progress) {
  m_activeJobs.erase(
      std::remove_if(m_activeJobs.begin(), m_activeJobs.end(),
                     [&progress](const ActiveJob &job) {
                       return job.progress == progress;
                     }),
      m_activeJobs.end());
}

/** Request cooperative cancellation of every active file render. */
void FileRenderController::cancelAll() {
  for (const ActiveJob &job : m_activeJobs)
    if (job.progress)
      job.progress->cancel();
}

/** Cancel, join and clean every worker before the controller can disappear. */
void FileRenderController::shutdown() {
  if (m_shuttingDown)
    return;
  m_shuttingDown = true;

  cancelAll();

  // Disconnect completion callbacks before waiting: shutdown owns cleanup from
  // this point forward and must not race a queued finished() delivery.
  for (ActiveJob &job : m_activeJobs)
    if (job.watcher)
      QObject::disconnect(job.watcher, nullptr, this, nullptr);

  for (ActiveJob &job : m_activeJobs) {
    job.future.waitForFinished();

    if (!job.outputPath.isEmpty() && QFile::exists(job.outputPath))
      QFile::remove(job.outputPath);

    if (job.watcher) {
      delete job.watcher.data();
      job.watcher = nullptr;
    }
  }

  m_activeJobs.clear();
}

/** Start REQUEST in the global thread pool. */
void FileRenderController::start(Request request) {
  Q_ASSERT(m_configured);
  if (m_shuttingDown || isClosing() || !request.scan ||
      request.outputPath.isEmpty())
    return;

  auto progress = std::make_shared<colorscreen::progress_info>();
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
            removeActiveJob(progress);
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

  m_activeJobs.push_back(
      {progress, outputPath, QPointer<QFutureWatcher<bool>>(watcher), future});
  watcher->setFuture(future);
}
