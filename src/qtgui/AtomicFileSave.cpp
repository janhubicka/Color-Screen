#include "AtomicFileSave.h"

#include <QByteArray>
#include <QIODevice>
#include <QSaveFile>

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
