#ifndef COLOR_OPTIMIZER_WORKER_H
#define COLOR_OPTIMIZER_WORKER_H
#pragma once

#include "WorkerBase.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/progress-info.h"
#include <memory>
#include <vector>

class ColorOptimizerWorker : public WorkerBase {
  Q_OBJECT
public:
  ColorOptimizerWorker(std::shared_ptr<colorscreen::image_data> scan,
                       QObject *parent = nullptr);

  /** Optimize one immutable request snapshot on the worker thread. */
  void optimize(int reqId,
                std::shared_ptr<colorscreen::image_data> scan,
                colorscreen::scr_to_img_parameters scrParams,
                colorscreen::render_parameters rparams,
                std::vector<colorscreen::point_t> spots,
                std::shared_ptr<colorscreen::progress_info> progress);

signals:
  void finished(int reqId,
                colorscreen::render_parameters updatedRparams,
                std::vector<colorscreen::color_match> results,
                bool success,
                bool cancelled);
};

#endif // COLOR_OPTIMIZER_WORKER_H
