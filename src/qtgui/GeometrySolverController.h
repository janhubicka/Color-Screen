#pragma once

#include "TaskQueue.h"

#include "../libcolorscreen/include/imagedata.h"
#include "../libcolorscreen/include/progress-info.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
#include "../libcolorscreen/include/solver-parameters.h"

#include <QPointer>
#include <QThread>

#include <functional>
#include <memory>

class GeometrySolverWorker;
class QObject;

/** Immutable request passed to the persistent geometry solver worker. */
struct GeometrySolverRequest {
  colorscreen::scr_to_img_parameters scrToImg;
  colorscreen::solver_parameters solver;
  bool computeMesh = false;
};

/** Own the persistent geometry-solver thread, worker and replaceable queue.

    MainWindow keeps geometry-fit provenance, document-level stale checks, undo
    publication and user-facing failure handling. This controller owns only
    worker/thread transport and TaskQueue publication authority. */
class GeometrySolverController final {
public:
  struct Callbacks {
    std::function<void(std::shared_ptr<colorscreen::progress_info>)>
        progressStarted;
    std::function<void(std::shared_ptr<colorscreen::progress_info>)>
        progressFinished;
    std::function<void(int, colorscreen::scr_to_img_parameters, bool, bool,
                       bool)>
        finished;
  };

  GeometrySolverController() = default;
  ~GeometrySolverController();

  /** Create the persistent worker and start its dedicated thread. */
  void initialize(QObject *context,
                  std::shared_ptr<colorscreen::image_data> scan,
                  Callbacks callbacks);

  /** Replace the worker's scan using WorkerBase's serialized handoff. */
  void setScan(std::shared_ptr<colorscreen::image_data> scan);

  /** Submit REQUEST and return its TaskQueue identity, or zero when unavailable. */
  int request(GeometrySolverRequest request);

  /** Cancel every pending/active solve without destroying the worker. */
  void cancelAll();

  /** Cancel work, disconnect result delivery and join the worker thread. */
  void shutdown();

  /** Return whether new requests can be accepted. */
  bool available() const {
    return m_initialized && !m_shuttingDown && !m_worker.isNull() &&
           m_thread.isRunning();
  }

private:
  /** Dispatch REQUEST to the worker once TaskQueue starts REQID. */
  void startRequest(
      int reqId, std::shared_ptr<colorscreen::progress_info> progress,
      GeometrySolverRequest request);

  /** Forward a worker completion together with TaskQueue's publishability. */
  void finishRequest(int reqId, colorscreen::scr_to_img_parameters result,
                     bool success, bool cancelled);

  QPointer<QObject> m_context;
  QThread m_thread;
  QPointer<GeometrySolverWorker> m_worker;
  TaskQueue m_queue;
  Callbacks m_callbacks;
  bool m_initialized = false;
  bool m_shuttingDown = false;
};
