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

/** Incrementally discover registration points and keep geometry synchronized.

    Ordinary runs publish accepted point/geometry batches progressively. For a
    full-image sweep, stalled non-Dufay registration may force a conservative
    low-order lens fit before the usual global-coverage heuristic is satisfied,
    then continue discovery from that global mapping. Dufay is different: its
    screen is integrated into flexible film, so a stalled sweep promotes the
    nonlinear mesh to persistent document geometry and continues with it. */
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
                              bool computeMesh = false,
                              bool allowStallRecovery = false);

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
  bool m_allowStallRecovery;
};
