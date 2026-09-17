#pragma once

#include "MainWindow.h"

#include <QDateTime>
#include <QUndoCommand>

/** Undoable full-document parameter change.

    Adjacent updates carrying the same stable parameter key may merge within a
    short gesture window. Human-visible Undo text remains independent of that
    identity; legacy callers without a key continue to fall back to the
    description, preserving the pre-migration behavior. */
class ChangeParametersCommand final : public QUndoCommand {
public:
  /** Capture OLDSTATE and NEWSTATE for one undoable document edit. */
  ChangeParametersCommand(MainWindow *window, const ParameterState &oldState,
                const ParameterState &newState,
                const QString &description = QString(),
                const QString &parameterKey = QString())
      : m_window(window), m_oldState(oldState), m_newState(newState),
        m_mergeKey(parameterKey.isEmpty() ? description : parameterKey),
        m_timestamp(QDateTime::currentMSecsSinceEpoch()) {
    setText(description.isEmpty() ? "Change Parameters" : description);
  }

  /** Return the common Qt merge ID, or disable merging for anonymous edits. */
  int id() const override { return m_mergeKey.isEmpty() ? -1 : 1; }

  /** Merge OTHER only when it extends the same recent user edit gesture. */
  bool mergeWith(const QUndoCommand *other) override {
    if (other->id() != id())
      return false;

    const auto *command = static_cast<const ChangeParametersCommand *>(other);
    if (command->m_mergeKey != m_mergeKey)
      return false;

    const qint64 timeDifference = command->m_timestamp - m_timestamp;
    if (timeDifference < 0 || timeDifference > 500)
      return false;

    m_newState = command->m_newState;
    m_timestamp = command->m_timestamp;
    return true;
  }

  /** Restore the parameter snapshot from before this edit. */
  void undo() override { m_window->applyState(m_oldState); }

  /** Apply the newest parameter snapshot represented by this edit. */
  void redo() override { m_window->applyState(m_newState); }

private:
  MainWindow *m_window;
  ParameterState m_oldState;
  ParameterState m_newState;
  QString m_mergeKey;
  qint64 m_timestamp;
};
