#ifndef COORDINATE_OPTIMIZATION_WORKER_H
#define COORDINATE_OPTIMIZATION_WORKER_H

#include "WorkerBase.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/finetune.h"
#include <QVariant>

class CoordinateOptimizationWorker : public WorkerBase {
  Q_OBJECT
public:
  explicit CoordinateOptimizationWorker(std::shared_ptr<colorscreen::image_data> scan, QObject *parent = nullptr);

public slots:
  void autodetect(int reqId, colorscreen::scr_to_img_parameters params,
                  colorscreen::render_parameters rparams,
                  std::shared_ptr<colorscreen::progress_info> progress);

  void optimize(int reqId, colorscreen::scr_to_img_parameters params,
                colorscreen::render_parameters rparams,
                std::shared_ptr<colorscreen::progress_info> progress);

signals:
  void autodetectFinished(int reqId, colorscreen::scr_to_img_parameters params,
                          std::shared_ptr<colorscreen::progress_info> progress,
                          bool success, bool cancelled);
  void optimizeFinished(int reqId, colorscreen::finetune_result result,
                        std::shared_ptr<colorscreen::progress_info> progress,
                        bool success, bool cancelled);

};

#endif // COORDINATE_OPTIMIZATION_WORKER_H
