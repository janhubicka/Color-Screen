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

/** Build a complete payload at a private temporary PATH, then atomically
    replace the user target.

    WRITER receives the temporary filename and must return true only after
    closing/finalizing the complete payload. This is useful for libraries such
    as libzip that require a filesystem path rather than a FILE* or QIODevice. */
bool savePathAtomically(
    const QString &path,
    const std::function<bool(const QString &, QString *)> &writer,
    QString *error = nullptr);

/** Atomically replace PATH with UTF-8 TEXT. */
bool saveTextAtomically(const QString &path, const QString &text,
                        QString *error = nullptr);

} // namespace qtgui_io

#endif // ATOMIC_FILE_SAVE_H
