#include "BackgroundThreadRegistry.h"

#include <QCoreApplication>
#include <QEvent>
#include <QThread>

#include <algorithm>

/** Track THREAD, dropping null guards left by deleteLater(). */
void BackgroundThreadRegistry::track(QThread *thread) {
  if (!thread)
    return;

  m_threads.erase(
      std::remove_if(m_threads.begin(), m_threads.end(),
                     [](const QPointer<QThread> &candidate) {
                       return candidate.isNull();
                     }),
      m_threads.end());
  m_threads.emplace_back(thread);
}

/** Cancel and join every tracked thread without starving blocking callbacks. */
void BackgroundThreadRegistry::shutdown(QObject *callbackTarget) {
  for (const QPointer<QThread> &guard : m_threads) {
    if (QThread *thread = guard.data(); thread && thread->isRunning()) {
      thread->requestInterruption();
      thread->quit();
    }
  }

  bool running = true;
  while (running) {
    running = false;
    for (const QPointer<QThread> &guard : m_threads) {
      if (QThread *thread = guard.data(); thread && thread->isRunning()) {
        running = true;
        thread->wait(10);
      }
    }

    // FinetuneMisregisteredWorker can be blocked waiting for the document's
    // current point set through BlockingQueuedConnection. Service only queued
    // method calls for the owner so shutdown cannot deadlock or dispatch
    // unrelated user/input events.
    if (running && callbackTarget)
      QCoreApplication::sendPostedEvents(callbackTarget, QEvent::MetaCall);
  }

  m_threads.clear();
}
