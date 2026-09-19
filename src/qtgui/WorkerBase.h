#ifndef WORKER_BASE_H
#define WORKER_BASE_H

#include <QObject>
#include <memory>
#include "../libcolorscreen/include/imagedata.h"
#include "../libcolorscreen/include/progress-info.h"

class WorkerBase : public QObject {
  Q_OBJECT
public:
  explicit WorkerBase(std::shared_ptr<colorscreen::image_data> scan, QObject *parent = nullptr)
      : QObject(parent), m_scan(scan) {}
  virtual ~WorkerBase();

  /** Replace the worker's image on its own QObject thread.

      MainWindow and panels may call this directly from the GUI thread after the
      worker has moved to a dedicated QThread. Cross-thread calls are queued so
      m_scan is never assigned concurrently with a worker slot reading it. */
  void setScan(std::shared_ptr<colorscreen::image_data> scan);

protected:
  std::shared_ptr<colorscreen::image_data> m_scan;
};

#endif // WORKER_BASE_H
