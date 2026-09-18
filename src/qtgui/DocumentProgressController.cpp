#include "DocumentProgressController.h"

#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QProgressBar>
#include <QPushButton>
#include <QSizePolicy>
#include <QStatusBar>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>

/** Build the document-local progress widget trees and refresh timer. */
void DocumentProgressController::initialize(QMainWindow *host,
                                            QStatusBar *statusBar,
                                            Labels labels,
                                            Callbacks callbacks) {
  Q_ASSERT(host);
  Q_ASSERT(statusBar);
  Q_ASSERT(!m_host);

  m_host = host;
  m_labels = std::move(labels);
  m_callbacks = std::move(callbacks);

  m_progressContainer = new QWidget(statusBar);
  m_progressContainer->setObjectName(
      QStringLiteral("DocumentProgressContainer"));
  auto *progressContainerLayout = new QVBoxLayout(m_progressContainer);
  progressContainerLayout->setContentsMargins(0, 0, 0, 0);
  progressContainerLayout->setSpacing(0);

  m_userVisibleProgressContainer = new QWidget();
  m_userVisibleProgressContainer->setObjectName(
      QStringLiteral("UserVisibleProgressContainer"));
  m_userVisibleProgressLayout =
      new QVBoxLayout(m_userVisibleProgressContainer);
  m_userVisibleProgressLayout->setContentsMargins(4, 2, 4, 2);
  m_userVisibleProgressLayout->setSpacing(2);
  m_userVisibleProgressContainer->hide();

  m_userVisibleProgressDock = new QDockWidget(host);
  m_userVisibleProgressDock->setObjectName(
      QStringLiteral("UserVisibleProgressDock"));
  m_userVisibleProgressDock->setAllowedAreas(Qt::BottomDockWidgetArea);
  m_userVisibleProgressDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
  auto *taskDockTitle = new QWidget(m_userVisibleProgressDock);
  taskDockTitle->setFixedHeight(0);
  m_userVisibleProgressDock->setTitleBarWidget(taskDockTitle);
  m_userVisibleProgressDock->setWidget(m_userVisibleProgressContainer);
  host->addDockWidget(Qt::BottomDockWidgetArea, m_userVisibleProgressDock);
  m_userVisibleProgressDock->hide();

  m_transientProgressRow = new QWidget(m_progressContainer);
  m_transientProgressRow->setObjectName(
      QStringLiteral("TransientProgressRow"));
  auto *progressLayout = new QHBoxLayout(m_transientProgressRow);
  progressLayout->setContentsMargins(0, 0, 0, 0);
  progressLayout->setSpacing(8);

  m_statusLabel = new QLabel(QString(), m_transientProgressRow);
  m_statusLabel->setMinimumWidth(150);
  progressLayout->addWidget(m_statusLabel);

  m_progressBar = new QProgressBar(m_transientProgressRow);
  m_progressBar->setRange(0, 100);
  m_progressBar->setTextVisible(false);
  m_progressBar->setMinimumWidth(200);
  progressLayout->addWidget(m_progressBar);

  m_progressCountLabel =
      new QLabel(QStringLiteral("1/1"), m_transientProgressRow);
  m_progressCountLabel->setMinimumWidth(40);
  progressLayout->addWidget(m_progressCountLabel);

  m_prevProgressButton =
      new QPushButton(QStringLiteral("<"), m_transientProgressRow);
  m_prevProgressButton->setMaximumWidth(30);
  m_prevProgressButton->setToolTip(QStringLiteral("Previous progress"));
  QObject::connect(m_prevProgressButton, &QPushButton::clicked, this,
                   [this]() { selectPrevious(); });
  progressLayout->addWidget(m_prevProgressButton);

  m_nextProgressButton =
      new QPushButton(QStringLiteral(">"), m_transientProgressRow);
  m_nextProgressButton->setMaximumWidth(30);
  m_nextProgressButton->setToolTip(QStringLiteral("Next progress"));
  QObject::connect(m_nextProgressButton, &QPushButton::clicked, this,
                   [this]() { selectNext(); });
  progressLayout->addWidget(m_nextProgressButton);

  m_cancelButton = new QPushButton(m_labels.cancel, m_transientProgressRow);
  QObject::connect(
      m_cancelButton, &QPushButton::clicked, this,
      [this]() {
        requestTermination(m_currentlyDisplayedProgress,
                           ProgressAction::Cancel);
      });
  progressLayout->addWidget(m_cancelButton);

  progressContainerLayout->addWidget(m_transientProgressRow);
  QSizePolicy sizePolicy = m_transientProgressRow->sizePolicy();
  sizePolicy.setRetainSizeWhenHidden(true);
  m_transientProgressRow->setSizePolicy(sizePolicy);
  m_progressContainer->setMinimumHeight(
      m_transientProgressRow->sizeHint().height());

  m_transientProgressRow->hide();
  m_progressContainer->hide();
  statusBar->addPermanentWidget(m_progressContainer, 1);

  m_timer = new QTimer(host);
  m_timer->setInterval(100);
  QObject::connect(m_timer, &QTimer::timeout, this,
                   [this]() { onTimer(); });
}

/** Register ordinary transient background progress. */
void DocumentProgressController::addProgress(
    std::shared_ptr<colorscreen::progress_info> info) {
  registerProgress(std::move(info), false, QString(), ProgressAction::Cancel);
}

/** Register a long-running task with its own progress row. */
void DocumentProgressController::addUserVisibleProgress(
    std::shared_ptr<colorscreen::progress_info> info, const QString &title,
    ProgressAction action) {
  registerProgress(std::move(info), true, title, action);
}

/** Register INFO and create a dedicated row when USERVISIBLE is true. */
void DocumentProgressController::registerProgress(
    std::shared_ptr<colorscreen::progress_info> info, bool userVisible,
    const QString &title, ProgressAction action) {
  if (!info)
    return;

  ProgressEntry entry;
  entry.info = std::move(info);
  entry.userVisible = userVisible;
  entry.action = action;
  entry.title = title;
  entry.startTime.start();

  if (userVisible) {
    entry.row = new QWidget(m_userVisibleProgressContainer);
    entry.row->setObjectName(QStringLiteral("UserVisibleProgressRow"));
    entry.row->setProperty("progressTitle", title);
    auto *rowLayout = new QHBoxLayout(entry.row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(8);

    entry.rowLabel = new QLabel(title, entry.row);
    entry.rowLabel->setMinimumWidth(220);
    rowLayout->addWidget(entry.rowLabel, 1);

    entry.rowProgressBar = new QProgressBar(entry.row);
    entry.rowProgressBar->setRange(0, 0);
    entry.rowProgressBar->setTextVisible(false);
    entry.rowProgressBar->setMinimumWidth(200);
    rowLayout->addWidget(entry.rowProgressBar);

    entry.rowActionButton = new QPushButton(
        action == ProgressAction::Stop ? m_labels.stop : m_labels.cancel,
        entry.row);
    entry.rowActionButton->setFocusPolicy(Qt::TabFocus);
    entry.rowActionButton->setProperty(
        "progressAction",
        action == ProgressAction::Stop ? QStringLiteral("stop")
                                       : QStringLiteral("cancel"));
    const auto progress = entry.info;
    QObject::connect(
        entry.rowActionButton, &QPushButton::clicked, this,
        [this, progress, action]() { requestTermination(progress, action); });
    rowLayout->addWidget(entry.rowActionButton);

    m_userVisibleProgressLayout->addWidget(entry.row);
    entry.row->show();
  }

  m_activeProgresses.push_back(std::move(entry));
  if (!m_timer->isActive())
    m_timer->start();
  updateContainerVisibility();
}

/** Return active entries that share the transient status row. */
std::vector<ProgressEntry *> DocumentProgressController::transientProgresses() {
  std::vector<ProgressEntry *> result;
  result.reserve(m_activeProgresses.size());
  for (ProgressEntry &entry : m_activeProgresses)
    if (!entry.userVisible)
      result.push_back(&entry);
  return result;
}

/** Route focus restoration through the owning document. */
void DocumentProgressController::releaseFocus(QWidget *row) {
  if (m_callbacks.releaseFocus)
    m_callbacks.releaseFocus(row);
}

/** Remove a completed or terminated background task from progress tracking. */
void DocumentProgressController::removeProgress(
    std::shared_ptr<colorscreen::progress_info> info) {
  int removedTransientIndex = -1;
  int transientIndex = 0;

  for (auto it = m_activeProgresses.begin(); it != m_activeProgresses.end();
       ++it) {
    if (it->info == info) {
      if (!it->userVisible)
        removedTransientIndex = transientIndex;
      if (it->row) {
        releaseFocus(it->row);
        m_userVisibleProgressLayout->removeWidget(it->row);
        it->row->deleteLater();
      }
      m_activeProgresses.erase(it);
      break;
    }
    if (!it->userVisible)
      ++transientIndex;
  }

  if (m_currentlyDisplayedProgress == info)
    m_currentlyDisplayedProgress.reset();

  if (removedTransientIndex >= 0 && m_manuallySelectedProgressIndex >= 0) {
    if (m_manuallySelectedProgressIndex == removedTransientIndex)
      m_manuallySelectedProgressIndex = -1;
    else if (m_manuallySelectedProgressIndex > removedTransientIndex)
      --m_manuallySelectedProgressIndex;
  }

  const auto transient = transientProgresses();
  if (transient.empty()) {
    setTransientVisible(false);
    m_currentlyDisplayedProgress.reset();
    m_manuallySelectedProgressIndex = -1;
  } else if (m_manuallySelectedProgressIndex >=
             static_cast<int>(transient.size())) {
    m_manuallySelectedProgressIndex = -1;
  }

  if (m_activeProgresses.empty())
    m_timer->stop();
  updateContainerVisibility();
}

/** Cancel every task while leaving result/row teardown to its normal owner. */
void DocumentProgressController::cancelAll() {
  for (const ProgressEntry &entry : m_activeProgresses)
    if (entry.info)
      entry.info->cancel();
}

/** Return the most relevant transient task for automatic selection. */
ProgressEntry *DocumentProgressController::longestRunningTask() {
  ProgressEntry *oldestActive = nullptr;
  ProgressEntry *oldestAny = nullptr;
  qint64 maxActiveTime = -1;
  qint64 maxAnyTime = -1;

  for (ProgressEntry *entry : transientProgresses()) {
    const qint64 elapsed = entry->startTime.elapsed();
    float percent = 0;
    entry->info->get_status(&percent);
    if (percent > 0 && elapsed > maxActiveTime) {
      maxActiveTime = elapsed;
      oldestActive = entry;
    }
    if (elapsed > maxAnyTime) {
      maxAnyTime = elapsed;
      oldestAny = entry;
    }
  }
  return oldestActive ? oldestActive : oldestAny;
}

/** Format ENTRY's nested progress state into LABEL and BAR. */
void DocumentProgressController::updateProgressWidgets(
    const ProgressEntry &entry, QLabel *label, QProgressBar *bar,
    const QString &title) {
  if (!entry.info || !label || !bar)
    return;

  const auto statusStack = entry.info->get_status();
  QStringList tasks;
  float percent = -1;
  for (const auto &status : statusStack) {
    if (!status.task.empty()) {
      QString taskName = QString::fromUtf8(status.task.c_str());
      if (status.progress >= 0 && &status != &statusStack.back())
        taskName +=
            QStringLiteral(" (%1%)").arg(static_cast<int>(status.progress));
      tasks.append(taskName);
    }
    if (status.progress >= 0)
      percent = status.progress;
  }

  QString statusText = tasks.join(QStringLiteral(" > "));
  if (!title.isEmpty()) {
    if (statusText.isEmpty())
      statusText = title;
    else if (statusText.compare(title, Qt::CaseInsensitive) != 0)
      statusText = title + QStringLiteral(": ") + statusText;
  } else if (statusText.isEmpty()) {
    statusText = m_labels.working;
  }

  const qint64 elapsedMs = entry.startTime.elapsed();
  if (elapsedMs > 20000 && percent > 0.1f) {
    const double doneFraction = static_cast<double>(percent) / 100.0;
    const qint64 remainingMs =
        static_cast<qint64>(static_cast<double>(elapsedMs) / doneFraction) -
        elapsedMs;
    if (remainingMs > 0) {
      const int remainingSec = (remainingMs / 1000) % 60;
      const int remainingMin = remainingMs / 60000;
      statusText += QStringLiteral(" (ETR: %1:%2)")
                        .arg(remainingMin)
                        .arg(remainingSec, 2, 10, QChar('0'));
    }
  }

  label->setText(statusText);
  if (percent >= 0) {
    bar->setRange(0, 100);
    bar->setValue(static_cast<int>(percent));
  } else {
    bar->setRange(0, 0);
  }
}

/** Set delayed transient visibility and notify the owning document. */
void DocumentProgressController::setTransientVisible(bool visible) {
  if (m_transientProgressRow)
    m_transientProgressRow->setVisible(visible);
  if (m_progressContainer)
    m_progressContainer->setVisible(visible);
  if (m_transientProgressVisible == visible)
    return;

  m_transientProgressVisible = visible;
  if (m_callbacks.transientVisibilityChanged)
    m_callbacks.transientVisibilityChanged(visible);
}

/** Synchronize dedicated task container/dock visibility. */
void DocumentProgressController::updateContainerVisibility() {
  bool hasRows = false;
  for (const ProgressEntry &entry : m_activeProgresses) {
    if (entry.userVisible && entry.row && !entry.row->isHidden()) {
      hasRows = true;
      break;
    }
  }

  const bool changed = m_userVisibleProgressContainer->isHidden() == hasRows;
  m_userVisibleProgressContainer->setVisible(hasRows);
  if (m_userVisibleProgressDock &&
      m_userVisibleProgressDock->widget() == m_userVisibleProgressContainer)
    m_userVisibleProgressDock->setVisible(hasRows);
  if (changed && m_callbacks.userVisibleVisibilityChanged)
    m_callbacks.userVisibleVisibilityChanged(hasRows);
}

/** Periodically update transient progress and every dedicated row. */
void DocumentProgressController::onTimer() {
  if (m_activeProgresses.empty()) {
    setTransientVisible(false);
    m_currentlyDisplayedProgress.reset();
    m_manuallySelectedProgressIndex = -1;
    m_timer->stop();
    return;
  }

  for (ProgressEntry &entry : m_activeProgresses) {
    if (!entry.userVisible)
      continue;
    updateProgressWidgets(entry, entry.rowLabel, entry.rowProgressBar,
                          entry.title);
    if (entry.rowActionButton && entry.info->pool_cancel()) {
      releaseFocus(entry.row);
      entry.rowActionButton->setText(
          entry.action == ProgressAction::Stop ? m_labels.stopping
                                               : m_labels.cancelling);
      entry.rowActionButton->setEnabled(false);
    }
  }

  const auto transient = transientProgresses();
  if (transient.empty()) {
    setTransientVisible(false);
    m_currentlyDisplayedProgress.reset();
    m_manuallySelectedProgressIndex = -1;
    updateContainerVisibility();
    return;
  }

  ProgressEntry *task = nullptr;
  int currentIndex = 0;
  if (m_manuallySelectedProgressIndex >= 0 &&
      m_manuallySelectedProgressIndex < static_cast<int>(transient.size())) {
    currentIndex = m_manuallySelectedProgressIndex;
    task = transient[currentIndex];
  } else {
    task = longestRunningTask();
    for (size_t i = 0; task && i < transient.size(); ++i) {
      if (transient[i] == task) {
        currentIndex = static_cast<int>(i);
        break;
      }
    }
    m_manuallySelectedProgressIndex = -1;
  }

  if (!task) {
    setTransientVisible(false);
    updateContainerVisibility();
    return;
  }

  m_currentlyDisplayedProgress = task->info;
  m_progressCountLabel->setText(
      QStringLiteral("%1/%2").arg(currentIndex + 1).arg(transient.size()));
  const bool multiple = transient.size() > 1;
  m_prevProgressButton->setVisible(multiple);
  m_nextProgressButton->setVisible(multiple);
  m_progressCountLabel->setVisible(multiple);

  if (task->startTime.elapsed() > 300) {
    updateProgressWidgets(*task, m_statusLabel, m_progressBar, QString());
    setTransientVisible(true);
  } else {
    setTransientVisible(false);
  }
  updateContainerVisibility();
}

/** Request cooperative termination through document policy and progress_info. */
void DocumentProgressController::requestTermination(
    const std::shared_ptr<colorscreen::progress_info> &info,
    ProgressAction action) {
  if (!info)
    return;
  if (m_callbacks.confirmTermination &&
      !m_callbacks.confirmTermination(info, action))
    return;

  for (ProgressEntry &entry : m_activeProgresses)
    if (entry.info == info && entry.row)
      releaseFocus(entry.row);

  info->cancel();
  for (ProgressEntry &entry : m_activeProgresses) {
    if (entry.info != info || !entry.rowActionButton)
      continue;
    entry.rowActionButton->setText(
        action == ProgressAction::Stop ? m_labels.stopping
                                       : m_labels.cancelling);
    entry.rowActionButton->setEnabled(false);
  }
}

/** Select the previous transient progress entry. */
void DocumentProgressController::selectPrevious() {
  const auto transient = transientProgresses();
  if (transient.size() <= 1)
    return;

  if (m_manuallySelectedProgressIndex < 0) {
    ProgressEntry *currentTask = longestRunningTask();
    for (size_t i = 0; i < transient.size(); ++i) {
      if (transient[i] == currentTask) {
        m_manuallySelectedProgressIndex = static_cast<int>(i);
        break;
      }
    }
  }

  m_manuallySelectedProgressIndex =
      (m_manuallySelectedProgressIndex - 1 +
       static_cast<int>(transient.size())) %
      static_cast<int>(transient.size());
}

/** Select the next transient progress entry. */
void DocumentProgressController::selectNext() {
  const auto transient = transientProgresses();
  if (transient.size() <= 1)
    return;

  if (m_manuallySelectedProgressIndex < 0) {
    ProgressEntry *currentTask = longestRunningTask();
    for (size_t i = 0; i < transient.size(); ++i) {
      if (transient[i] == currentTask) {
        m_manuallySelectedProgressIndex = static_cast<int>(i);
        break;
      }
    }
  }

  m_manuallySelectedProgressIndex =
      (m_manuallySelectedProgressIndex + 1) %
      static_cast<int>(transient.size());
}

/** Remove transient progress from STATUSBAR for workspace hosting. */
QWidget *DocumentProgressController::takeTransientWidget(QStatusBar *statusBar) {
  if (!m_progressContainer)
    return nullptr;
  if (statusBar)
    statusBar->removeWidget(m_progressContainer);
  m_progressContainer->setParent(nullptr);
  return m_progressContainer;
}

/** Return transient progress to STATUSBAR after leaving the workspace. */
void DocumentProgressController::restoreTransientWidget(QStatusBar *statusBar) {
  if (!m_progressContainer || !statusBar)
    return;
  if (m_progressContainer->parentWidget() != statusBar) {
    m_progressContainer->setParent(statusBar);
    statusBar->addPermanentWidget(m_progressContainer, 1);
  }
  m_progressContainer->setVisible(m_transientProgressVisible);
}

/** Remove persistent progress rows from the document-local task dock. */
QWidget *DocumentProgressController::takeUserVisibleWidget() {
  if (!m_userVisibleProgressContainer || !m_userVisibleProgressDock)
    return m_userVisibleProgressContainer;
  if (m_userVisibleProgressDock->widget() == m_userVisibleProgressContainer) {
    m_userVisibleProgressDock->setWidget(nullptr);
    m_userVisibleProgressContainer->setParent(nullptr);
    m_userVisibleProgressDock->hide();
  }
  return m_userVisibleProgressContainer;
}

/** Return persistent progress rows to the document-local task dock. */
void DocumentProgressController::restoreUserVisibleWidget() {
  if (!m_userVisibleProgressContainer || !m_userVisibleProgressDock)
    return;
  if (m_userVisibleProgressDock->widget() != m_userVisibleProgressContainer)
    m_userVisibleProgressDock->setWidget(m_userVisibleProgressContainer);
  updateContainerVisibility();
}
