#pragma once

#include "../libcolorscreen/include/progress-info.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
#include "../libcolorscreen/include/solver-parameters.h"
#include "../libcolorscreen/include/finetune.h"
#include <QObject>
#include <memory>
#include <vector>

namespace colorscreen {
class image_data;
}

class FinetuneMisregisteredWorker : public QObject {
  Q_OBJECT
public:
  FinetuneMisregisteredWorker(colorscreen::solver_parameters solverParams,
                              colorscreen::render_parameters rparams,
                              colorscreen::scr_to_img_parameters scrToImg,
                              std::shared_ptr<colorscreen::image_data> scan,
                              colorscreen::int_image_area area,
                              std::shared_ptr<colorscreen::progress_info> progress,
                              colorscreen::finetune_area_parameters fparams,
                              bool computeMesh = false);

public slots:
  void run();

signals:
  void finished(bool success);
  void pointsReady(
      std::vector<colorscreen::solver_parameters::solver_point_t> points);
  void geometryReady(colorscreen::scr_to_img_parameters result);
  void requestCurrentPoints(std::vector<colorscreen::solver_parameters::solver_point_t> *points);

private:
  colorscreen::solver_parameters m_solverParams;
  colorscreen::render_parameters m_rparams;
  colorscreen::scr_to_img_parameters m_scrToImg;
  std::shared_ptr<colorscreen::image_data> m_scan;
  colorscreen::int_image_area m_area;
  std::shared_ptr<colorscreen::progress_info> m_progress;
  colorscreen::finetune_area_parameters m_fparams;
  bool m_computeMesh;
};
