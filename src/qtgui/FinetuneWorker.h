#pragma once

#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
#include "../libcolorscreen/include/solver-parameters.h"
#include "../libcolorscreen/include/progress-info.h"
#include <QObject>
#include <memory>
#include <vector>

namespace colorscreen {
class image_data;
}

class FinetuneWorker : public QObject {
  Q_OBJECT
public:
  FinetuneWorker(colorscreen::solver_parameters solverParams,
                 colorscreen::render_parameters rparams,
                 colorscreen::scr_to_img_parameters scrToImg,
                 std::shared_ptr<colorscreen::image_data> scan,
                 int xmin, int ymin, int xmax, int ymax,
                 std::shared_ptr<colorscreen::progress_info> progress);

public slots:
  void run();

signals:
  void finished(bool success);
  void pointsReady(std::vector<colorscreen::solver_parameters::solver_point_t> points);

private:
  colorscreen::solver_parameters m_solverParams;
  colorscreen::render_parameters m_rparams;
  colorscreen::scr_to_img_parameters m_scrToImg;
  std::shared_ptr<colorscreen::image_data> m_scan;
  int m_xmin, m_ymin, m_xmax, m_ymax;
  std::shared_ptr<colorscreen::progress_info> m_progress;
};
