#pragma once

#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
#include "../libcolorscreen/include/progress-info.h"
#include "../libcolorscreen/include/scanner-blur-correction-parameters.h"
#include <QObject>
#include <memory>
#include <vector>

namespace colorscreen {
class image_data;
}

class AdaptiveSharpeningWorker : public QObject {
  Q_OBJECT
public:
  AdaptiveSharpeningWorker(colorscreen::scr_to_img_parameters scrToImg,
                           colorscreen::render_parameters rparams,
                           std::shared_ptr<colorscreen::image_data> scan,
                           int xsteps,
                           std::shared_ptr<colorscreen::progress_info> progress);

public slots:
  void run();

signals:
  void finished(bool success, std::shared_ptr<colorscreen::scanner_blur_correction_parameters> result);

private:
  colorscreen::scr_to_img_parameters m_scrToImg;
  colorscreen::render_parameters m_rparams;
  std::shared_ptr<colorscreen::image_data> m_scan;
  int m_xsteps;
  std::shared_ptr<colorscreen::progress_info> m_progress;
};
