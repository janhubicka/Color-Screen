#pragma once

#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
#include "../libcolorscreen/include/finetune.h"
#include "../libcolorscreen/include/progress-info.h"
#include <QObject>
#include <memory>
#include <vector>

namespace colorscreen {
class image_data;
}

class FocusAnalysisWorker : public QObject {
  Q_OBJECT
public:
  FocusAnalysisWorker(colorscreen::render_parameters rparams,
                      colorscreen::scr_to_img_parameters scrToImg,
                      std::shared_ptr<colorscreen::image_data> scan,
                      colorscreen::point_t point,
                      colorscreen::finetune_parameters fparam,
                      std::shared_ptr<colorscreen::progress_info> progress);

public slots:
  void run();

signals:
  void finished(bool success, colorscreen::finetune_result result);

private:
  colorscreen::render_parameters m_rparams;
  colorscreen::scr_to_img_parameters m_scrToImg;
  std::shared_ptr<colorscreen::image_data> m_scan;
  colorscreen::point_t m_point;
  colorscreen::finetune_parameters m_fparam;
  std::shared_ptr<colorscreen::progress_info> m_progress;
};
