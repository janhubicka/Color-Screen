#pragma once

#include <QObject>
#include <memory>
#include <QString>
#include "../libcolorscreen/include/imagedata.h"
#include "../libcolorscreen/include/progress-info.h"
#include "../libcolorscreen/include/backlight-correction-parameters.h"

class FieldLevelingWorker : public QObject {
  Q_OBJECT
public:
  FieldLevelingWorker(QString whiteFile, QString blackFile,
                      colorscreen::luminosity_t gamma,
                      colorscreen::image_data::demosaicing_t demosaic,
                      std::shared_ptr<colorscreen::progress_info> progress);

public slots:
  void run();

signals:
  void finished(bool success, std::shared_ptr<colorscreen::backlight_correction_parameters> result);

private:
  QString m_whiteFile;
  QString m_blackFile;
  colorscreen::luminosity_t m_gamma;
  colorscreen::image_data::demosaicing_t m_demosaic;
  std::shared_ptr<colorscreen::progress_info> m_progress;
};
