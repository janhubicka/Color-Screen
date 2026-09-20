#include "GeometrySolverController.h"

#include "GeometrySolverWorker.h"

#include <QMetaObject>
#include <QObject>

#include <utility>

GeometrySolverController::~GeometrySolverController() { shutdown(); }

/** Create the persistent geometry worker and its replaceable request queue. */
void GeometrySolverController::initialize(
    QObject *context, std::shared_ptr<colorscreen::image_data> scan,
    Callbacks callbacks) {
  Q_ASSERT(context);
  Q_ASSERT(!m_initialized);

  qRegisterMetaType<colorscreen::scr_to_img_parameters>();
  qRegisterMetaType<colorscreen::solver_parameters>();
  qRegisterMetaType<std::shared_ptr<colorscreen::progress_info>>();

  m_context = context;
  m_callbacks = std::move(callbacks);
  m_shuttingDown = false;

  m_worker = new GeometrySolverWorker(std::move(scan));
  m_worker->moveToThread(&m_thread);

  QObject::connect(&m_thread, &QThread::finished, m_worker.data(),
                   &QObject::deleteLater);
  QObject::connect(
      m_worker.data(), &GeometrySolverWorker::finished, context,
      [this](int reqId, colorscreen::scr_to_img_parameters result,
             bool success, bool cancelled) {
        finishRequest(reqId, std::move(result), success, cancelled);
      });
  QObject::connect(
      &m_queue, &TaskQueue::progressStarted, context,
      [this](std::shared_ptr<colorscreen::progress_info> progress) {
        if (m_callbacks.progressStarted)
          m_callbacks.progressStarted(std::move(progress));
      });
  QObject::connect(
      &m_queue, &TaskQueue::progressFinished, context,
      [this](std::shared_ptr<colorscreen::progress_info> progress) {
        if (m_callbacks.progressFinished)
          m_callbacks.progressFinished(std::move(progress));
      });

  m_thread.start();
  m_initialized = true;
}

/** Replace the scan without racing the worker's solve() slot. */
void GeometrySolverController::setScan(
    std::shared_ptr<colorscreen::image_data> scan) {
  if (m_worker)
    m_worker->setScan(std::move(scan));
}

/** Submit one typed request and keep its snapshot in TaskQueue's start closure. */
int GeometrySolverController::request(GeometrySolverRequest request) {
  if (!available())
    return 0;

  return m_queue.requestRender(
      QVariant(),
      [this, request = std::move(request)](
          int reqId,
          std::shared_ptr<colorscreen::progress_info> progress) mutable {
        startRequest(reqId, std::move(progress), std::move(request));
      });
}

/** Dispatch one started request to the worker thread. */
void GeometrySolverController::startRequest(
    int reqId, std::shared_ptr<colorscreen::progress_info> progress,
    GeometrySolverRequest request) {
  if (m_shuttingDown || !m_worker) {
    const bool publishable = m_queue.reportFinished(reqId, false);
    if (m_callbacks.finished)
      m_callbacks.finished(reqId, colorscreen::scr_to_img_parameters(), false,
                           true, publishable);
    return;
  }

  if (progress)
    progress->set_task("Optimizing geometry", 1);

  QMetaObject::invokeMethod(
      m_worker.data(), "solve", Qt::QueuedConnection, Q_ARG(int, reqId),
      Q_ARG(colorscreen::scr_to_img_parameters, request.scrToImg),
      Q_ARG(colorscreen::solver_parameters, request.solver),
      Q_ARG(std::shared_ptr<colorscreen::progress_info>, progress),
      Q_ARG(bool, request.computeMesh));
}

/** Resolve TaskQueue ownership before returning the result to MainWindow. */
void GeometrySolverController::finishRequest(
    int reqId, colorscreen::scr_to_img_parameters result, bool success,
    bool cancelled) {
  const bool publishable = m_queue.reportFinished(reqId, success);
  if (m_callbacks.finished)
    m_callbacks.finished(reqId, std::move(result), success, cancelled,
                         publishable);
}

/** Cancel all queued/active geometry work. */
void GeometrySolverController::cancelAll() { m_queue.cancelAll(); }

/** Stop result delivery and join the persistent worker thread. */
void GeometrySolverController::shutdown() {
  if (!m_initialized)
    return;

  m_shuttingDown = true;
  m_queue.cancelAll();

  if (m_worker && m_context)
    QObject::disconnect(m_worker.data(), nullptr, m_context.data(), nullptr);
  if (m_context)
    QObject::disconnect(&m_queue, nullptr, m_context.data(), nullptr);

  if (m_thread.isRunning()) {
    m_thread.requestInterruption();
    m_thread.quit();
    m_thread.wait();
  }

  m_worker.clear();
  m_context.clear();
  m_callbacks = {};
  m_initialized = false;
}
