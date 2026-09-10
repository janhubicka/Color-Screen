#include "DetectScreenWorker.h"

#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/screen-map.h"

/** Run one automatic regular-screen detection and own its diagnostic map. */
DetectScreenAnalysisResult DetectScreenWorker::analyze(
    colorscreen::scr_detect_parameters detectParams,
    colorscreen::solver_parameters solverParams,
    colorscreen::scr_to_img_parameters scrToImgParams,
    colorscreen::render_parameters renderParams,
    std::shared_ptr<colorscreen::image_data> scan,
    colorscreen::progress_info *progress) {
  DetectScreenAnalysisResult analysis;
  if (!scan)
    return analysis;
  if (progress && progress->pool_cancel()) {
    analysis.cancelled = true;
    return analysis;
  }

  colorscreen::detect_regular_screen_params detectorParams;
  detectorParams.return_screen_map = true;
  detectorParams.gamma = renderParams.gamma;
  // Preserve the historical fallback for scans without a linearization table.
  if (!detectorParams.gamma && !scan->to_linear[0].size())
    detectorParams.gamma = -1;
  detectorParams.scr_type = scrToImgParams.type;
  detectorParams.scanner_type = scrToImgParams.scanner_type;

  analysis.detected = colorscreen::detect_regular_screen(
      *scan, detectParams, solverParams, &detectorParams, progress, nullptr,
      &renderParams);
  analysis.solver = std::move(solverParams);

  // detect_regular_screen() transfers this optional raw map to its caller.
  // Adopt it immediately so every cancellation/staleness path is leak-safe.
  analysis.screenMap.reset(analysis.detected.smap);
  analysis.detected.smap = nullptr;

  analysis.cancelled = progress &&
      (progress->pool_cancel() || progress->cancelled());
  analysis.success = !analysis.cancelled && analysis.detected.success;
  return analysis;
}
