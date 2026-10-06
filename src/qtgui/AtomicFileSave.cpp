#include "AtomicFileSave.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QSaveFile>
#include <QTemporaryFile>

namespace qtgui_io {

bool saveStdioAtomically(const QString &path,
                         const std::function<bool(FILE *)> &writer,
                         QString *error) {
  FILE *staged = std::tmpfile();
  if (!staged) {
    if (error)
      *error = QStringLiteral("Could not create a temporary payload.");
    return false;
  }

  bool serialized = writer && writer(staged);
  if (serialized && std::fflush(staged) != 0)
    serialized = false;
  if (!serialized || std::fseek(staged, 0, SEEK_SET) != 0) {
    std::fclose(staged);
    if (error)
      *error = QStringLiteral("Could not serialize the complete payload.");
    return false;
  }

  QSaveFile output(path);
  output.setDirectWriteFallback(false);
  if (!output.open(QIODevice::WriteOnly)) {
    std::fclose(staged);
    if (error)
      *error = output.errorString();
    return false;
  }

  char buffer[64 * 1024];
  bool copied = true;
  while (copied) {
    const size_t count = std::fread(buffer, 1, sizeof(buffer), staged);
    if (count > 0 &&
        output.write(buffer, static_cast<qint64>(count)) !=
            static_cast<qint64>(count)) {
      copied = false;
      break;
    }
    if (count < sizeof(buffer)) {
      if (std::ferror(staged))
        copied = false;
      break;
    }
  }
  std::fclose(staged);

  if (!copied) {
    if (error)
      *error = output.errorString().isEmpty()
                   ? QStringLiteral("Could not write the complete payload.")
                   : output.errorString();
    output.cancelWriting();
    return false;
  }

  if (!output.commit()) {
    if (error)
      *error = output.errorString();
    return false;
  }
  return true;
}

bool savePathAtomically(
    const QString &path,
    const std::function<bool(const QString &, QString *)> &writer,
    QString *error) {
  const QFileInfo targetInfo(path);
  QTemporaryFile staged(
      targetInfo.absoluteDir().filePath(QStringLiteral(".colorscreen-save-XXXXXX")));
  staged.setAutoRemove(true);
  if (!staged.open()) {
    if (error)
      *error = staged.errorString();
    return false;
  }
  const QString stagedPath = staged.fileName();
  staged.close();

  QString writerError;
  if (!writer || !writer(stagedPath, &writerError)) {
    if (error)
      *error = writerError.isEmpty()
                   ? QStringLiteral("Could not serialize the complete payload.")
                   : writerError;
    return false;
  }

  QFile input(stagedPath);
  if (!input.open(QIODevice::ReadOnly)) {
    if (error)
      *error = input.errorString();
    return false;
  }

  QSaveFile output(path);
  output.setDirectWriteFallback(false);
  if (!output.open(QIODevice::WriteOnly)) {
    if (error)
      *error = output.errorString();
    return false;
  }

  char buffer[64 * 1024];
  while (true) {
    const qint64 count = input.read(buffer, sizeof(buffer));
    if (count < 0) {
      if (error)
        *error = input.errorString();
      output.cancelWriting();
      return false;
    }
    if (count == 0)
      break;
    if (output.write(buffer, count) != count) {
      if (error)
        *error = output.errorString().isEmpty()
                     ? QStringLiteral("Could not write the complete payload.")
                     : output.errorString();
      output.cancelWriting();
      return false;
    }
  }

  if (!output.commit()) {
    if (error)
      *error = output.errorString();
    return false;
  }
  if (error)
    error->clear();
  return true;
}

bool saveTextAtomically(const QString &path, const QString &text,
                        QString *error) {
  QSaveFile file(path);
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    if (error)
      *error = file.errorString();
    return false;
  }

  const QByteArray bytes = text.toUtf8();
  if (file.write(bytes) != bytes.size()) {
    if (error)
      *error = file.errorString().isEmpty()
                   ? QStringLiteral("Could not write the complete payload.")
                   : file.errorString();
    file.cancelWriting();
    return false;
  }
  if (!file.commit()) {
    if (error)
      *error = file.errorString();
    return false;
  }
  return true;
}

} // namespace qtgui_io
