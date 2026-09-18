#include "OneShotOperationController.h"

#include <QByteArray>
#include <QObject>

#include <string>
#include <utility>

void OneShotOperationController::configure(QObject *context,
                                           Callbacks callbacks) {
  Q_ASSERT(context);
  Q_ASSERT(!m_configured);
  m_callbacks = std::move(callbacks);
  m_configured = true;

  QObject::connect(
      &m_queue, &TaskQueue::progressStarted, context,
      [this](std::shared_ptr<colorscreen::progress_info> progress) {
        if (m_callbacks.addProgress)
          m_callbacks.addProgress(std::move(progress));
      });
  QObject::connect(
      &m_queue, &TaskQueue::progressFinished, context,
      [this](std::shared_ptr<colorscreen::progress_info> progress) {
        if (m_callbacks.removeProgress)
          m_callbacks.removeProgress(std::move(progress));
      });
}

bool OneShotOperationController::isClosing() const {
  return m_callbacks.isClosing && m_callbacks.isClosing();
}

void OneShotOperationController::run(
    Operation operation,
    std::function<void(colorscreen::progress_info *)> worker) {
  Q_ASSERT(m_configured);
  if (isClosing() || !worker)
    return;
  if (operation.prerequisites && !operation.prerequisites())
    return;

  if (m_callbacks.dismissPendingPrompts)
    m_callbacks.dismissPendingPrompts();

  auto lifecycle = std::make_shared<Operation>(std::move(operation));
  const QString description = lifecycle->description;

  m_queue.cancelAll();
  m_queue.runAsync(
      [description, worker = std::move(worker)](
          colorscreen::progress_info *progress) mutable {
        if (progress) {
          const QByteArray taskName = description.toUtf8();
          progress->set_task(
              std::string(taskName.constData(),
                          static_cast<std::size_t>(taskName.size())),
              1);
        }
        worker(progress);
      },
      [this, lifecycle](bool publishResult) {
        const bool valid =
            publishResult && !isClosing() &&
            (!lifecycle->resultValid || lifecycle->resultValid());
        if (valid && lifecycle->applyResult)
          lifecycle->applyResult();
        if (!isClosing() && lifecycle->onDone)
          lifecycle->onDone();
      },
      QVariant(),
      [this, lifecycle](std::shared_ptr<colorscreen::progress_info> progress) {
        if (!lifecycle->progressTitle.isEmpty() &&
            m_callbacks.promoteProgress)
          m_callbacks.promoteProgress(progress, lifecycle->progressTitle);
        if (lifecycle->onStart)
          lifecycle->onStart(std::move(progress));
      });
}
