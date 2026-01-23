#pragma once

#include "../libcolorscreen/include/scr-detect-parameters.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
#include "../libcolorscreen/include/solver-parameters.h"
#include "../libcolorscreen/include/detect-regular-screen-parameters.h"
#include "../libcolorscreen/include/progress-info.h"
#include <QObject>
#include <memory>

namespace colorscreen {
class image_data;
}

class DetectScreenWorker : public QObject {
  Q_OBJECT
public:
  DetectScreenWorker(colorscreen::scr_detect_parameters detectParams,
                     colorscreen::solver_parameters solverParams,
                     colorscreen::scr_to_img_parameters scrToImgParams,
                     std::shared_ptr<colorscreen::image_data> scan,
                     std::shared_ptr<colorscreen::progress_info> progress);

public slots:
  void detect();

signals:
  void finished(bool success, colorscreen::detected_screen result, colorscreen::solver_parameters solverParams);

private:
  colorscreen::scr_detect_parameters m_detectParams;
  colorscreen::solver_parameters m_solverParams;
  colorscreen::scr_to_img_parameters m_scrToImgParams;
  std::shared_ptr<colorscreen::image_data> m_scan;
  std::shared_ptr<colorscreen::progress_info> m_progress;
};
