#ifndef COORDINATE_OPTIMIZATION_WORKER_H
#define COORDINATE_OPTIMIZATION_WORKER_H

#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/finetune.h"
#include "../libcolorscreen/include/progress-info.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
#include <memory>

namespace colorscreen {
class image_data;
}

/** Final result of detecting a screen's initial coordinate system. */
struct CoordinateAutodetectionResult {
  bool success = false;
  bool cancelled = false;
  colorscreen::scr_to_img_parameters coordinates;
};

/** Final result of refining an existing screen coordinate system. */
struct CoordinateOptimizationResult {
  bool success = false;
  bool cancelled = false;
  colorscreen::finetune_result finetune;
};

/** Synchronous coordinate helpers, run with immutable background inputs. */
class CoordinateOptimizationWorker final {
public:
  static CoordinateAutodetectionResult autodetect(
      colorscreen::scr_to_img_parameters params,
      colorscreen::render_parameters rparams,
      std::shared_ptr<colorscreen::image_data> scan,
      colorscreen::progress_info *progress);

  static CoordinateOptimizationResult optimize(
      colorscreen::scr_to_img_parameters params,
      colorscreen::render_parameters rparams,
      std::shared_ptr<colorscreen::image_data> scan,
      colorscreen::progress_info *progress);
};

#endif // COORDINATE_OPTIMIZATION_WORKER_H
