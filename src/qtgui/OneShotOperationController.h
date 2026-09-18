#pragma once

#include "TaskQueue.h"

#include <QString>
#include <functional>
#include <memory>

class QObject;

class OneShotOperationController final {
public:
  struct Operation {
    QString description;
    QString progressTitle;
    std::function<bool()> prerequisites;
    std::function<void(std::shared_ptr<colorscreen::progress_info>)> onStart;
    std::function<bool()> resultValid;
    std::function<void()> applyResult;
    std::function<void()> onDone;
  };

  struct Callbacks {
    std::function<bool()> isClosing;
    std::function<void()> dismissPendingPrompts;
    std::function<void(std::shared_ptr<colorscreen::progress_info>)> addProgress;
    std::function<void(std::shared_ptr<colorscreen::progress_info>)> removeProgress;
    std::function<void(std::shared_ptr<colorscreen::progress_info>,
                       const QString &)> promoteProgress;
  };

  OneShotOperationController() = default;

  void configure(QObject *context, Callbacks callbacks);
  void run(Operation operation,
           std::function<void(colorscreen::progress_info *)> worker);
  void cancelAll() { m_queue.cancelAll(); }
  bool hasActiveTasks() const { return m_queue.hasActiveTasks(); }

private:
  bool isClosing() const;

  Callbacks m_callbacks;
  TaskQueue m_queue;
  bool m_configured = false;
};
