#pragma once

#include <QRunnable>
#include <QThreadPool>

#include <mutex>
#include <type_traits>
#include <utility>

/**
 * QRunnable wrapper with a sanitizer-visible publication edge.
 *
 * Ubuntu's distro Qt is not ThreadSanitizer-instrumented.  QThreadPool still
 * synchronizes publication of QRunnable objects correctly, but TSan cannot see
 * that internal Qt synchronization.  For templated QtConcurrent tasks this can
 * look like the worker races with construction of the task closure itself.
 *
 * Publish the fully constructed callable through a C++ mutex before handing it
 * to Qt.  run() acquires the same mutex before touching the callable.  This
 * preserves QThreadPool parallelism while giving TSan an explicit happens-before
 * edge; code executed by the callable remains fully instrumented.
 */
template <typename Function> class SynchronizedRunnable final : public QRunnable {
public:
  explicit SynchronizedRunnable(Function &&function)
      : m_function(std::move(function)) {
    setAutoDelete(true);
  }

  void publish() {
    std::lock_guard<std::mutex> locker(m_publicationMutex);
  }

  void run() override {
    {
      std::lock_guard<std::mutex> locker(m_publicationMutex);
    }
    m_function();
  }

private:
  std::mutex m_publicationMutex;
  Function m_function;
};

template <typename Function>
void runSynchronized(QThreadPool *pool, Function &&function) {
  using StoredFunction = std::decay_t<Function>;
  auto *task = new SynchronizedRunnable<StoredFunction>(
      StoredFunction(std::forward<Function>(function)));
  task->publish();
  (pool ? pool : QThreadPool::globalInstance())->start(task);
}

template <typename Function> void runSynchronized(Function &&function) {
  runSynchronized(QThreadPool::globalInstance(),
                  std::forward<Function>(function));
}
