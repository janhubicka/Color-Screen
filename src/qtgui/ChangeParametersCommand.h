#pragma once

#include "MainWindow.h"
#include "../libcolorscreen/parameter-archive.h"

#include <string>
#include <utility>
#include <vector>

#include <QDateTime>
#include <QUndoCommand>

/** Undoable full-document parameter change.

    Adjacent updates carrying the same explicit stable parameter key may merge
    within a short gesture window. Human-visible Undo text is presentation only:
    unkeyed edits are atomic commands and never merge merely because their
    descriptions happen to match. */
class ChangeParametersCommand final : public QUndoCommand {
public:
  /** Capture OLDSTATE and NEWSTATE for one undoable document edit. */
  ChangeParametersCommand(MainWindow *window, const ParameterState &oldState,
                          const ParameterState &newState,
                          const QString &description = QString(),
                          const QString &parameterKey = QString())
      : m_window(window), m_oldState(oldState), m_newState(newState),
        m_mergeKey(parameterKey),
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

  /* Relative measured-MTF sources in undo/redo snapshots must retain
     their physical meaning when Save As changes the parameter-file directory.
     Stage only path spellings, not full image/calibration copies, so a failed
     Save or a later invalid command cannot partially rewrite the Undo stack. */
  struct RebasedMtfPaths {
    std::vector<std::string> before;
    std::vector<std::string> after;
  };

  bool prepareMtfPathRebase(const std::string &oldDocument,
                            const std::string &newDocument,
                            RebasedMtfPaths *paths) const {
    if (!paths)
      return false;
    RebasedMtfPaths proposed;
    auto prepare = [&oldDocument, &newDocument](
                       const colorscreen::mtf_parameters &source,
                       std::vector<std::string> *names) {
      colorscreen::mtf_parameters temporary = source;
      std::string error;
      if (!colorscreen::rebase_parameter_mtf_source_paths(
              &temporary, oldDocument, newDocument, &error))
        return false;
      names->reserve(temporary.measurements.size());
      for (const auto &record : temporary.measurements)
        names->push_back(record.source_filename);
      return true;
    };
    if (!prepare(m_oldState.rparams.sharpen.scanner_mtf, &proposed.before) ||
        !prepare(m_newState.rparams.sharpen.scanner_mtf, &proposed.after))
      return false;
    *paths = std::move(proposed);
    return true;
  }

  bool applyPreparedMtfPaths(const RebasedMtfPaths &paths) {
    auto &before = m_oldState.rparams.sharpen.scanner_mtf.measurements;
    auto &after = m_newState.rparams.sharpen.scanner_mtf.measurements;
    if (before.size() != paths.before.size() ||
        after.size() != paths.after.size())
      return false;
    for (size_t i = 0; i < before.size(); ++i)
      before[i].source_filename = paths.before[i];
    for (size_t i = 0; i < after.size(); ++i)
      after[i].source_filename = paths.after[i];
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
