#include "WorkerBase.h"

#include <QMetaObject>
#include <QThread>

#include <utility>

WorkerBase::~WorkerBase() = default;

/** Replace the scan without racing a worker slot that reads m_scan. */
void WorkerBase::setScan(std::shared_ptr<colorscreen::image_data> scan) {
  if (QThread::currentThread() == thread()) {
    m_scan = std::move(scan);
    return;
  }

  // QObject owns the queued functor's lifetime. If this worker is destroyed
  // before delivery, Qt drops the invocation rather than touching dead state.
  QMetaObject::invokeMethod(
      this,
      [this, scan = std::move(scan)]() mutable {
        m_scan = std::move(scan);
      },
      Qt::QueuedConnection);
}
