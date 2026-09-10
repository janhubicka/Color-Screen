#pragma once

#include "../libcolorscreen/include/scr-detect-parameters.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
#include "../libcolorscreen/include/solver-parameters.h"
#include "../libcolorscreen/include/detect-regular-screen-parameters.h"
#include "../libcolorscreen/include/progress-info.h"
#include "../libcolorscreen/include/render-parameters.h"
#include <memory>

namespace colorscreen {
class image_data;
class screen_map;
}

/** Final result of one automatic regular-screen detection. */
struct DetectScreenAnalysisResult {
  bool success = false;
  bool cancelled = false;
  colorscreen::detected_screen detected{};
  colorscreen::solver_parameters solver;
  std::shared_ptr<const colorscreen::screen_map> screenMap;
};

/** Synchronous screen-detection helper intended for a background thread. */
class DetectScreenWorker final {
public:
  /** Detect a regular screen using immutable document input snapshots. */
  static DetectScreenAnalysisResult analyze(
      colorscreen::scr_detect_parameters detectParams,
      colorscreen::solver_parameters solverParams,
      colorscreen::scr_to_img_parameters scrToImgParams,
      colorscreen::render_parameters renderParams,
      std::shared_ptr<colorscreen::image_data> scan,
      colorscreen::progress_info *progress);
};
