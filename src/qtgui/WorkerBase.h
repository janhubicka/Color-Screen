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

  void setScan(std::shared_ptr<colorscreen::image_data> scan) { m_scan = scan; }

protected:
  std::shared_ptr<colorscreen::image_data> m_scan;
};

#endif // WORKER_BASE_H
