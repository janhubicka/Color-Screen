#pragma once

#include "../libcolorscreen/include/finetune.h"
#include "../libcolorscreen/include/progress-info.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
#include <memory>

namespace colorscreen {
class image_data;
}

/** Final result of one point-based focus analysis. */
struct FocusAnalysisResult {
  bool success = false;
  bool cancelled = false;
  colorscreen::finetune_result finetune;
};

/** Synchronous focus-analysis helper intended to run on a background thread. */
class FocusAnalysisWorker final {
public:
  /** Analyze POINT using immutable input snapshots and cooperative PROGRESS. */
  static FocusAnalysisResult analyze(
      colorscreen::render_parameters rparams,
      colorscreen::scr_to_img_parameters scrToImg,
      std::shared_ptr<colorscreen::image_data> scan,
      colorscreen::point_t point, colorscreen::finetune_parameters fparam,
      colorscreen::progress_info *progress);
};
