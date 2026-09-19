#pragma once

#include <QPointer>

#include <vector>

class QObject;
class QThread;

/** Track ad-hoc document worker threads until explicit shutdown.

    These workers may publish incremental results and may block in a
    BlockingQueuedConnection back to their owning document. shutdown() therefore
    services only MetaCall events for CALLBACKTARGET while joining them. */
class BackgroundThreadRegistry final {
public:
  /** Track THREAD, pruning guards for workers already deleted by Qt. */
  void track(QThread *thread);

  /** Request interruption/quit and join every tracked thread.

      CALLBACKTARGET receives queued MetaCall events while workers are joined,
      allowing a worker blocked on a BlockingQueuedConnection to return. */
  void shutdown(QObject *callbackTarget);

  /** Return true when no tracked worker guard remains. */
  bool empty() const { return m_threads.empty(); }

private:
  std::vector<QPointer<QThread>> m_threads;
};
