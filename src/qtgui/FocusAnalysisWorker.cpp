#include "FocusAnalysisWorker.h"
#include "../libcolorscreen/include/finetune.h"

FocusAnalysisWorker::FocusAnalysisWorker(colorscreen::render_parameters rparams,
                                         colorscreen::scr_to_img_parameters scrToImg,
                                         std::shared_ptr<colorscreen::image_data> scan,
                                         colorscreen::point_t point,
                                         colorscreen::finetune_parameters fparam,
                                         std::shared_ptr<colorscreen::progress_info> progress)
    : m_rparams(rparams), m_scrToImg(scrToImg), m_scan(scan),
      m_point(point), m_fparam(fparam), m_progress(progress) {
}

void FocusAnalysisWorker::run() {
  std::vector<colorscreen::point_t> points = {m_point};
  
  colorscreen::finetune_result res = colorscreen::finetune(
      m_rparams, m_scrToImg, *m_scan, points, nullptr, m_fparam, m_progress.get());
  
  if (m_progress && m_progress->cancelled()) {
    emit finished(false, res);
    return;
  }
  
  emit finished(res.success, res);
}
