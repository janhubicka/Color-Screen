#pragma once

#include "../libcolorscreen/include/finetune.h"
#include "../libcolorscreen/include/focus-analysis.h"
#include "../libcolorscreen/include/progress-info.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
#include <memory>
#include <string>
#include <vector>

namespace colorscreen {
class image_data;
}

/** Final result of one point-based focus analysis. */
struct FocusAnalysisResult {
  bool success = false;
  bool cancelled = false;
  colorscreen::finetune_result finetune;
};

/** Final candidates returned by the cheap uniform-area discovery pass. */
struct FocusAreaFindResult {
  bool success = false;
  bool cancelled = false;
  std::vector<colorscreen::finetune_focus_area_candidate> candidates;
  std::string error;
};

/** Final candidates and diagnostics returned by individual/joint area fitting. */
struct FocusAreaAnalyzeResult {
  bool success = false;
  bool cancelled = false;
  std::vector<colorscreen::finetune_focus_area_candidate> candidates;
  colorscreen::finetune_focus_analysis_result analysis;
  std::string error;
};

/** Synchronous focus-analysis helpers intended to run on a background thread. */
class FocusAnalysisWorker final {
public:
  /** Analyze POINT using immutable input snapshots and cooperative PROGRESS. */
  static FocusAnalysisResult analyze(
      colorscreen::render_parameters rparams,
      colorscreen::scr_to_img_parameters scrToImg,
      std::shared_ptr<colorscreen::image_data> scan,
      colorscreen::point_t point, colorscreen::finetune_parameters fparam,
      colorscreen::progress_info *progress);

  /** Find uniform areas in the captured image without publishing GUI state. */
  static FocusAreaFindResult findAreas(
      colorscreen::render_parameters rparams,
      colorscreen::scr_to_img_parameters scrToImg,
      std::shared_ptr<colorscreen::image_data> scan,
      colorscreen::progress_info *progress);

  /** Verify CANDIDATES and fit shared focus FLAGS with the captured input model. */
  static FocusAreaAnalyzeResult analyzeAreas(
      colorscreen::render_parameters rparams,
      colorscreen::scr_to_img_parameters scrToImg,
      std::shared_ptr<colorscreen::image_data> scan,
      std::vector<colorscreen::finetune_focus_area_candidate> candidates,
      uint64_t flags, bool useMonochrome,
      colorscreen::progress_info *progress);
};
