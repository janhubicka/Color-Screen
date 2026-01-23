#include "DetectScreenWorker.h"
#include "../libcolorscreen/include/colorscreen.h"

DetectScreenWorker::DetectScreenWorker(
    colorscreen::scr_detect_parameters detectParams,
    colorscreen::solver_parameters solverParams,
    colorscreen::scr_to_img_parameters scrToImgParams,
    std::shared_ptr<colorscreen::image_data> scan,
    std::shared_ptr<colorscreen::progress_info> progress,
    colorscreen::luminosity_t gamma)
    : m_detectParams(detectParams), m_solverParams(solverParams),
      m_scrToImgParams(scrToImgParams), m_scan(scan), m_progress(progress), m_gamma(gamma) {
}

void DetectScreenWorker::detect() {
  // Create local copy of solver parameters to work with (avoid race conditions with UI)
  colorscreen::solver_parameters localSolver = m_solverParams;
  
  // Setup detection parameters (based on gtkgui.C:755)
  colorscreen::detect_regular_screen_params dsparams;
  dsparams.return_screen_map = true;
  dsparams.gamma = m_gamma;
  dsparams.scr_type = m_scrToImgParams.type;
  dsparams.scanner_type = m_scrToImgParams.scanner_type;
  
  // Run detection - this modifies localSolver in place
  colorscreen::detected_screen result = colorscreen::detect_regular_screen(
      *m_scan, m_detectParams, localSolver, &dsparams, m_progress.get());
  
  // Check if cancelled
  if (m_progress && m_progress->cancelled()) {
    emit finished(false, result, localSolver);
    return;
  }
  
  // Emit result with the modified solver parameters
  emit finished(result.success, result, localSolver);
}
