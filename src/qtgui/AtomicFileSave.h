#ifndef ATOMIC_FILE_SAVE_H
#define ATOMIC_FILE_SAVE_H

#include <QString>

#include <cstdio>
#include <functional>

namespace qtgui_io {

/** Stage a FILE*-based payload, then atomically replace PATH.

    WRITER must return true only after it has produced the complete payload.
    Direct-write fallback is disabled so a failed writer/write/commit leaves any
    existing target untouched. */
bool saveStdioAtomically(const QString &path,
                         const std::function<bool(FILE *)> &writer,
                         QString *error = nullptr);

/** Atomically replace PATH with UTF-8 TEXT. */
bool saveTextAtomically(const QString &path, const QString &text,
                        QString *error = nullptr);

} // namespace qtgui_io

#endif // ATOMIC_FILE_SAVE_H
