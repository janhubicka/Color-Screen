#pragma once

#include "../libcolorscreen/include/finetune.h"
#include "../libcolorscreen/include/progress-info.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
#include "../libcolorscreen/include/solver-parameters.h"
#include <memory>
#include <vector>

namespace colorscreen {
class image_data;
}

/** Final result of one single-area registration-point finetune. */
struct FinetuneAreaResult {
  bool success = false;
  bool cancelled = false;
  std::vector<colorscreen::solver_parameters::solver_point_t> points;
};

/** Synchronous single-area finetune helper intended for a background thread. */
class FinetuneWorker final {
public:
  /** Find registration points in AREA from immutable input snapshots. */
  static FinetuneAreaResult findPoints(
      colorscreen::solver_parameters solverParams,
      colorscreen::render_parameters rparams,
      colorscreen::scr_to_img_parameters scrToImg,
      std::shared_ptr<colorscreen::image_data> scan,
      colorscreen::int_image_area area,
      colorscreen::finetune_area_parameters fparams,
      colorscreen::progress_info *progress);
};
